/* T9-K0: terminal simulator. Runs the firmware's engine, output queue
 * and dictionaries against a simulated host text buffer, either interactively
 * in a raw terminal or by replaying a timestamped script (--script FILE).
 * Usage notes: DEVELOPING.md.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _POSIX_C_SOURCE 200809L
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>
#include "t9_engine.h"
#include "t9_dict_packed.h"
#include "t9_dict_simple.h"
#include "config.h"

#define HOST_TEXT_MAX 4096
#define SCREEN_MAX 8192
#define HOST_SHOWN_CHARS 72
#define POLL_MS 5
#define ESCAPE_WAIT_MS 25
#define ESC '\033'
#define EVENT_RESET (1 << 7)

/* host key codes passed to the engine as passthrough codes (T9_OUT_KEY values) */
enum { HOST_ENTER = 1, HOST_TAB, HOST_ESCAPE, HOST_NUMLOCK, HOST_LEFT, HOST_RIGHT, HOST_WORD_LEFT, HOST_WORD_RIGHT };

typedef struct {
    const char *name;
    t9_key_t key;
    uint16_t code;
    const char *host; /* host effect when the engine does not take the key; "\b" = backspace */
} key_info_t;

/* Same phone-order bindings as keymap.c: in numpad mode phone 1 2 3 are the
 * pad's numpad 7 8 9, `-` and Backspace send Backspace, the knob sends volume. */
static const key_info_t keys[] = {
    {"0", T9_KEY_0, 0, "0"},
    {"1", T9_KEY_1, 0, "7"},
    {"2", T9_KEY_2, 0, "8"},
    {"3", T9_KEY_3, 0, "9"},
    {"4", T9_KEY_4, 0, "4"},
    {"5", T9_KEY_5, 0, "5"},
    {"6", T9_KEY_6, 0, "6"},
    {"7", T9_KEY_7, 0, "1"},
    {"8", T9_KEY_8, 0, "2"},
    {"9", T9_KEY_9, 0, "3"},
    {"NEXT", T9_KEY_NEXT, 0, "*"},
    {"PREV", T9_KEY_PREV, 0, ""},
    {"DOT", T9_KEY_DOT, 0, "."},
    {"DELETE", T9_KEY_DELETE, 0, "\b"},
    {"CANCEL", T9_KEY_CANCEL, HOST_ESCAPE, ""},
    {"SPELL", T9_KEY_SPELL, 0, "+"},
    {"CAPS", T9_KEY_CAPS, 0, "/"},
    {"MODE", T9_KEY_MODE, HOST_NUMLOCK, ""},
    {"ENTER", T9_KEY_COMPLETE, HOST_ENTER, "<ENTER>"},
    {"TAB", T9_KEY_OTHER, HOST_TAB, "<TAB>"},
    {"FN_ENTER", T9_KEY_COUNT, 0, ""},  /* completions toggle, not an engine key */
    {"DIAL_LEFT", T9_KEY_COUNT, 0, ""}, /* numpad volume has no text effect */
    {"DIAL_RIGHT", T9_KEY_COUNT, 0, ""},
};
#define KEY_COUNT (sizeof(keys) / sizeof(keys[0]))

static const t9_simple_entry_t fixture_words[] = {
#include "t9_fixture_table.h"
};

static t9_engine_t engine;
static t9_lookup_fn dict_lookup;
static bool packed_dict = true;
static char host_text[HOST_TEXT_MAX + 1];
static size_t host_len;
static size_t host_cursor;
static uint32_t pace_ms, last_output_ms;
static bool passed_to_host[KEY_COUNT];
static uint8_t recent_events; /* since the last key press */
static struct {
    uint32_t count, entries, blocks;
} lookups; /* count since the last key press, counters of the last lookup */

/* --- dictionary ------------------------------------------------------------- */

static void counted_lookup(const uint8_t *digits, uint8_t len, t9_candidates_t *out) {
    t9_dict_packed_reset_counters();
    dict_lookup(digits, len, out);
    lookups.count++;
    lookups.entries = t9_dict_packed_entries_decoded();
    lookups.blocks  = t9_dict_packed_blocks_skipped();
}

/* --- host model ------------------------------------------------------------- */

static const key_info_t *find_key(const char *name) {
    for (size_t i = 0; i < KEY_COUNT; i++) {
        if (strcmp(keys[i].name, name) == 0)
            return &keys[i];
    }
    return NULL;
}

static bool host_ends_with(const char *suffix) {
    size_t n = strlen(suffix);
    return host_cursor >= n && memcmp(host_text + host_cursor - n, suffix, n) == 0;
}

/* A marker such as <ENTER> stands for one host character. */
static void host_backspace(void) {
    size_t erase = host_ends_with("<ENTER>") ? 7 : host_ends_with("<TAB>") ? 5 : host_cursor ? 1 : 0;
    memmove(host_text + host_cursor - erase, host_text + host_cursor, host_len - host_cursor + 1);
    host_cursor -= erase;
    host_len -= erase;
}

static void host_type(const char *text) {
    size_t n = strlen(text);
    if (strcmp(text, "\b") == 0) {
        host_backspace();
    } else if (host_len + n <= HOST_TEXT_MAX) {
        memmove(host_text + host_cursor + n, host_text + host_cursor, host_len - host_cursor + 1);
        memcpy(host_text + host_cursor, text, n);
        host_cursor += n;
        host_len += n;
    }
}

static const char *host_effect_of_code(uint16_t code) {
    for (size_t i = 0; i < KEY_COUNT; i++) {
        if (keys[i].code == code)
            return keys[i].host;
    }
    return "";
}

static bool word_char(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '\'';
}

/* The simulator uses start/end-of-word boundaries; actual shortcuts follow the
 * host OS/application. ENTER/TAB markers count as single characters. */
static void move_cursor(bool right) {
    if (right && host_cursor < host_len) {
        host_cursor += strncmp(host_text + host_cursor, "<ENTER>", 7) == 0 ? 7
                       : strncmp(host_text + host_cursor, "<TAB>", 5) == 0 ? 5
                                                                           : 1;
    } else if (!right && host_cursor) {
        host_cursor -= host_ends_with("<ENTER>") ? 7 : host_ends_with("<TAB>") ? 5 : 1;
    }
}

static bool cursor_at_word(bool right) {
    if (right)
        return host_cursor < host_len && host_text[host_cursor] != '<' && word_char(host_text[host_cursor]);
    return host_cursor && !host_ends_with("<ENTER>") && !host_ends_with("<TAB>") &&
           word_char(host_text[host_cursor - 1]);
}

static void host_code(uint16_t code) {
    switch (code) {
        case HOST_LEFT:
            move_cursor(false);
            break;
        case HOST_RIGHT:
            move_cursor(true);
            break;
        case HOST_WORD_LEFT:
        case HOST_WORD_RIGHT: {
            bool right = code == HOST_WORD_RIGHT;
            while ((right ? host_cursor < host_len : host_cursor > 0) && !cursor_at_word(right))
                move_cursor(right);
            while (cursor_at_word(right))
                move_cursor(right);
            break;
        }
        default:
            host_type(host_effect_of_code(code));
            break;
    }
}

static void apply_item(const t9_out_item_t *item) {
    const char typed[2] = {(char)item->value, 0};
    if (item->kind == T9_OUT_CHAR)
        host_type(typed);
    if (item->kind == T9_OUT_BACKSPACE)
        host_backspace();
    if (item->kind == T9_OUT_KEY)
        host_code(item->value);
}

/* Paced like keymap.c output_task: one item per pace interval, all at once at 0. */
static void output_step(uint32_t now) {
    t9_out_item_t item;
    while ((uint32_t)(now - last_output_ms) >= pace_ms && t9_output_pop(&engine.out, &item)) {
        apply_item(&item);
        last_output_ms = now;
    }
}

static void drain_output(uint32_t now) {
    t9_out_item_t item;
    while (t9_output_pop(&engine.out, &item)) {
        apply_item(&item);
        last_output_ms = now;
    }
}

/* --- engine driving (mirrors keymap.c) -------------------------------------- */

static void housekeeping(uint32_t now) {
    t9_tick(&engine, now);
    output_step(now);
    recent_events |= t9_take_events(&engine);
}

static void press_key(const key_info_t *k, uint32_t now) {
    recent_events = 0;
    lookups.count = 0;
    if (strcmp(k->name, "DIAL_LEFT") == 0 || strcmp(k->name, "DIAL_RIGHT") == 0) {
        bool right = strcmp(k->name, "DIAL_RIGHT") == 0;
        t9_dial_turn(&engine, right, right ? HOST_RIGHT : HOST_LEFT, right ? HOST_WORD_RIGHT : HOST_WORD_LEFT, now);
        housekeeping(now);
        return;
    }
    if (k->key == T9_KEY_COUNT) {
        t9_set_completions(&engine, !engine.completions);
        housekeeping(now);
        return;
    }
    bool handled             = t9_key_press(&engine, k->key, k->code, now);
    passed_to_host[k - keys] = !handled;
    if (!handled)
        host_type(k->host);
    housekeeping(now);
}

static void release_key(const key_info_t *k, uint32_t now) {
    if (passed_to_host[k - keys]) {
        passed_to_host[k - keys] = false;
    } else if (k->key != T9_KEY_COUNT) {
        t9_key_release(&engine, k->key, now);
    }
    housekeeping(now);
}

static void reset_host_link(void) {
    t9_reset(&engine);
    recent_events = EVENT_RESET;
}

/* --- status display ---------------------------------------------------------- */

typedef struct {
    char *buf;
    size_t size, len;
} text_t;

__attribute__((format(printf, 2, 3))) static void put(text_t *t, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(t->buf + t->len, t->size - t->len, fmt, args);
    va_end(args);
    if (n > 0)
        t->len = t->len + (size_t)n < t->size ? t->len + (size_t)n : t->size - 1;
}

/* Whole words for suggestions or a prefix family; otherwise their typed bases. */
static void put_candidates(text_t *t) {
    const t9_candidates_t *c = &engine.cands;
    int shown                = engine.completions || c->prefix_family ? T9_MAX_WORD_LEN : engine.digit_len;
    put(t, "cands    %u/%u (%u exact)", engine.cand_index + 1u, c->count, c->total_exact);
    for (uint8_t i = 0; i < c->count; i++)
        put(t, i == engine.cand_index ? " <%.*s>" : " %.*s", shown, c->words[i]);
    put(t, "\n");
}

static void render_status(text_t *t, const char *cursor) {
    static const char *const modes[]    = {"T9", "multitap", "numpad"};
    static const char *const caps[]     = {"off", "once", "lock"};
    static const char *const composes[] = {"none", "t9", "multitap", "punct", "join"};
    size_t from                         = host_cursor > HOST_SHOWN_CHARS ? host_cursor - HOST_SHOWN_CHARS : 0;
    put(t, "host     %s%.*s%s%s\n", from ? "..." : "", (int)(host_cursor - from), host_text + from, cursor,
        host_text + host_cursor);
    put(t, "preview  \"%s\"  compose %s", t9_preview(&engine), composes[engine.compose]);
    if (engine.compose == T9_COMPOSE_T9) {
        put(t, "  digits ");
        for (uint8_t i = 0; i < engine.digit_len; i++)
            put(t, "%u", engine.digits[i]);
    }
    put(t, "%s\n", engine.mt_pending ? "  letter pending" : "");
    put(t, "state    mode %s%s  caps %s  completions %s  queue %u  pace %u ms\n", modes[engine.mode],
        engine.spelling ? " +spelling" : "", caps[engine.caps], engine.completions ? "on" : "off",
        t9_output_count(&engine.out), (unsigned)pace_ms);
    if (engine.compose == T9_COMPOSE_T9) {
        put_candidates(t);
    } else {
        put(t, "cands    -\n");
    }
    put(t, "events  %s%s%s%s%s%s", recent_events & T9_EVENT_MISS ? " MISS" : "",
        recent_events & T9_EVENT_BUSY ? " BUSY" : "", recent_events & T9_EVENT_MODE_CHANGED ? " mode" : "",
        recent_events & T9_EVENT_CAPS_CHANGED ? " caps" : "", recent_events & EVENT_RESET ? " reset" : "",
        recent_events ? "" : " -");
    if (packed_dict) {
        put(t, "  | lookups %u, last: %u entries decoded, %u blocks skipped\n", (unsigned)lookups.count,
            (unsigned)lookups.entries, (unsigned)lookups.blocks);
    } else {
        put(t, "  | lookups %u (fixture table, no counters)\n", (unsigned)lookups.count);
    }
}

/* --- scripted mode ---------------------------------------------------------------- */

static void advance_to(uint32_t *now, uint32_t target) {
    for (uint32_t ms = *now + 1; ms <= target; ms++)
        housekeeping(ms);
    *now = target;
}

static int script_error(const char *path, unsigned line_no, const char *what) {
    fprintf(stderr, "%s:%u: %s\n", path, line_no, what);
    return 2;
}

static bool check(const char *path, unsigned line_no, const char *what, const char *want, const char *got) {
    if (strcmp(want, got) == 0)
        return true;
    printf("%s:%u: %s mismatch\n  want: \"%s\"\n  got:  \"%s\"\n", path, line_no, what, want, got);
    return false;
}

static int run_script(const char *path, bool verbose) {
    FILE *file = fopen(path, "r");
    if (!file) {
        perror(path);
        return 2;
    }
    static char line[HOST_TEXT_MAX + 64];
    unsigned line_no = 0, checks = 0, failures = 0;
    uint32_t now = 0;
    int status   = 0;
    while (status == 0 && fgets(line, sizeof(line), file)) {
        line_no++;
        line[strcspn(line, "\r\n")] = 0;
        const char *start           = line + strspn(line, " \t");
        if (*start == 0 || *start == '#')
            continue;
        char *end;
        unsigned long time = strtoul(start, &end, 10);
        char command[32];
        int used = 0;
        if (end == start || sscanf(end, " %31s%n", command, &used) != 1) {
            status = script_error(path, line_no, "expected <time_ms> <command>");
            break;
        }
        if (time < now || time > UINT32_MAX) {
            status = script_error(path, line_no, "time must be non-decreasing");
            break;
        }
        const char *arg = end + used + (end[used] == ' ');
        advance_to(&now, (uint32_t)time);

        const key_info_t *key = find_key(arg);
        if (strcmp(command, "press") == 0 && key) {
            press_key(key, now);
        } else if (strcmp(command, "release") == 0 && key) {
            release_key(key, now);
        } else if (strcmp(command, "expect") == 0) {
            drain_output(now);
            checks++;
            failures += !check(path, line_no, "host text", arg, host_text);
        } else if (strcmp(command, "expect-preview") == 0) {
            checks++;
            failures += !check(path, line_no, "preview", arg, t9_preview(&engine));
        } else if (strcmp(command, "expect-cursor") == 0) {
            drain_output(now);
            char position[32];
            snprintf(position, sizeof(position), "%zu", host_cursor);
            checks++;
            failures += !check(path, line_no, "cursor position", arg, position);
        } else if (strcmp(command, "reset") == 0) {
            reset_host_link();
        } else if (strcmp(command, "drain") == 0) {
            drain_output(now);
        } else {
            status = script_error(path, line_no, "unknown command or key");
        }
        if (verbose && status == 0) {
            printf("%6lu %-22s host \"%s\"  preview \"%s\"  queue %u%s%s\n", time, end + strspn(end, " "), host_text,
                   t9_preview(&engine), t9_output_count(&engine.out), recent_events & T9_EVENT_MISS ? "  MISS" : "",
                   recent_events & T9_EVENT_BUSY ? "  BUSY" : "");
        }
    }
    fclose(file);
    if (status)
        return status;
    printf("%s: %u expectations, %u failed\n", path, checks, failures);
    return failures ? 1 : 0;
}

/* --- interactive mode -------------------------------------------------------------- */

static struct termios saved_tty;

static void restore_tty(void) {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &saved_tty);
}

static void on_signal(int sig) {
    restore_tty();
    _exit(128 + sig);
}

static bool enter_raw_mode(void) {
    if (!isatty(STDIN_FILENO) || tcgetattr(STDIN_FILENO, &saved_tty) != 0)
        return false;
    struct termios raw = saved_tty;
    raw.c_lflag &= ~(tcflag_t)(ECHO | ICANON | ISIG | IEXTEN);
    raw.c_iflag &= ~(tcflag_t)(IXON | ICRNL | INLCR | ISTRIP);
    raw.c_cc[VMIN]  = 0;
    raw.c_cc[VTIME] = 0;
    atexit(restore_tty);
    signal(SIGTERM, on_signal);
    signal(SIGHUP, on_signal);
    return tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == 0;
}

static uint32_t wall_ms(void) {
    static struct timespec start;
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    if (start.tv_sec == 0 && start.tv_nsec == 0)
        start = now;
    return (uint32_t)((now.tv_sec - start.tv_sec) * 1000 + (now.tv_nsec - start.tv_nsec) / 1000000);
}

static const char help_text[] =
    "keys     0-9 phone digits   * next   # prev   [ / ] dial left/right   . accept   - or Backspace delete\n"
    "         Esc cancel   h+ spell   + next letter   / caps   n or m mode   Enter complete (else Enter)   Tab "
    "passthrough\n"
    "         c completions on/off (Fn+Enter)   h<key> hold the key for long_press_ms   r host/transport reset\n"
    "         single 1 joins contractions (416 i'm, 4183 i've); repeated 1 cycles punctuation\n"
    "         dial: active candidates/punctuation cycle; otherwise T9 word jumps, multitap arrows, numpad volume\n"
    "         ? help   q or Ctrl-C quit\n";

static bool help_visible = true;

static void redraw(void) {
    static char screen[SCREEN_MAX], shown[SCREEN_MAX];
    text_t t = {screen, sizeof(screen), 0};
    put(&t, "\033[H\033[J");
    render_status(&t, "\033[7m \033[0m");
    put(&t, "%s", help_visible ? help_text : "");
    if (strcmp(screen, shown) == 0)
        return;
    memcpy(shown, screen, t.len + 1);
    fputs(screen, stdout);
    fflush(stdout);
}

static const key_info_t *key_for_char(char c) {
    static const struct {
        const char *chars, *name;
    } bindings[] = {
        {"*", "NEXT"},         {"#", "PREV"},      {"[", "DIAL_LEFT"}, {"]", "DIAL_RIGHT"}, {".", "DOT"},
        {"-\x7f\b", "DELETE"}, {"\033", "CANCEL"}, {"+", "SPELL"},     {"/", "CAPS"},       {"nm", "MODE"},
        {"\r\n", "ENTER"},     {"\t", "TAB"},      {"c", "FN_ENTER"},
    };
    if (c >= '0' && c <= '9')
        return &keys[c - '0'];
    for (size_t i = 0; c && i < sizeof(bindings) / sizeof(bindings[0]); i++) {
        if (strchr(bindings[i].chars, c))
            return find_key(bindings[i].name);
    }
    return NULL;
}

/* A lone ESC is Escape. ESC O x (application keypad) maps to the keypad key;
 * other ESC O x and CSI sequences (arrows, function keys) are ignored. */
static size_t decode_input(const char *buf, size_t n, char *c) {
    static const char ss3_from[] = "pqrstuvwxyMjkmno", ss3_to[] = "0123456789\r*+-./";
    if (buf[0] != ESC || n == 1 || (buf[1] != '[' && buf[1] != 'O')) {
        *c = buf[0];
        return 1;
    }
    if (buf[1] == 'O') {
        const char *hit = n > 2 && buf[2] ? strchr(ss3_from, buf[2]) : NULL;
        *c              = hit ? ss3_to[hit - ss3_from] : 0;
        return n > 2 ? 3 : 2;
    }
    size_t end = 2;
    while (end < n && !(buf[end] >= 0x40 && buf[end] <= 0x7e))
        end++;
    *c = 0;
    return end < n ? end + 1 : n;
}

static size_t read_input(char *buf, size_t size) {
    struct pollfd in = {.fd = STDIN_FILENO, .events = POLLIN};
    ssize_t n        = poll(&in, 1, POLL_MS) > 0 ? read(STDIN_FILENO, buf, size) : 0;
    size_t len       = n > 0 ? (size_t)n : 0;
    while (len > 0 && len < size && buf[len - 1] == ESC && poll(&in, 1, ESCAPE_WAIT_MS) > 0) {
        n = read(STDIN_FILENO, buf + len, size - len);
        if (n <= 0)
            break;
        len += (size_t)n;
    }
    return len;
}

static void tap(const key_info_t *k) {
    uint32_t now = wall_ms();
    press_key(k, now);
    release_key(k, now);
}

/* Terminals deliver no key releases: hold the key in real time instead. */
static void hold(const key_info_t *k) {
    uint32_t pressed = wall_ms();
    press_key(k, pressed);
    while (wall_ms() - pressed < engine.cfg.long_press_ms) {
        poll(NULL, 0, POLL_MS);
        housekeeping(wall_ms());
        redraw();
    }
    release_key(k, wall_ms());
}

/* Returns false to quit. */
static bool handle_char(char c, bool *hold_pending) {
    const key_info_t *k = key_for_char(c);
    if (*hold_pending) {
        *hold_pending = false;
        if (k) {
            hold(k);
            return true;
        }
    }
    if (c == 'q' || c == 3)
        return false;
    if (c == 'r')
        reset_host_link();
    if (c == '?')
        help_visible = !help_visible;
    if (c == 'h')
        *hold_pending = true;
    if (k)
        tap(k);
    return true;
}

static int run_interactive(void) {
    if (!enter_raw_mode()) {
        fprintf(stderr, "interactive mode needs a terminal; use --script FILE otherwise\n");
        return 2;
    }
    bool running = true, hold_pending = false;
    char buf[64];
    while (running) {
        size_t n = read_input(buf, sizeof(buf));
        for (size_t i = 0; running && i < n;) {
            char c;
            i += decode_input(buf + i, n - i, &c);
            running = !c || handle_char(c, &hold_pending);
        }
        housekeeping(wall_ms());
        redraw();
    }
    drain_output(wall_ms());
    printf("\nhost text: %s\n", host_text);
    return 0;
}

/* --- main ----------------------------------------------------------------------------- */

static void usage(FILE *out) {
    fprintf(out,
            "usage: t9_simulator [--dict packed|fixture] [--pace MS] [--multitap MS] [--long-press MS]\n"
            "                    [--script FILE [--verbose]]\n"
            "  --dict        packed: production dictionary (default); fixture: host/t9_fixture_table.h\n"
            "  --pace        ms between host output items (default 0 = immediate)\n"
            "  --multitap    multitap letter timeout (default %u)\n"
            "  --long-press  long-press threshold (default %u)\n"
            "  --script      replay FILE and check its expectations; exit 1 on mismatch, 2 on error\n"
            "  --verbose     print every script step\n"
            "without --script: interactive raw-terminal mode, press ? for keys\n",
            T9_MULTITAP_TIMEOUT_MS, T9_LONG_PRESS_MS);
}

static bool parse_ms(const char *text, uint16_t *out) {
    char *end;
    unsigned long value = text ? strtoul(text, &end, 10) : 0;
    if (!text || *text == 0 || *end != 0 || value > UINT16_MAX)
        return false;
    *out = (uint16_t)value;
    return true;
}

int main(int argc, char **argv) {
    t9_config_t cfg    = {.multitap_timeout_ms = T9_MULTITAP_TIMEOUT_MS, .long_press_ms = T9_LONG_PRESS_MS};
    const char *script = NULL, *dict = "packed";
    bool verbose = false, ok = true;
    uint16_t pace = 0;
    for (int i = 1; ok && i < argc; i++) {
        const char *flag = argv[i], *value = i + 1 < argc ? argv[i + 1] : NULL;
        if (strcmp(flag, "--help") == 0) {
            usage(stdout);
            return 0;
        }
        if (strcmp(flag, "--verbose") == 0) {
            verbose = true;
            continue;
        }
        i++;
        if (strcmp(flag, "--dict") == 0 && value) {
            dict = value;
        } else if (strcmp(flag, "--script") == 0 && value) {
            script = value;
        } else if (strcmp(flag, "--pace") == 0) {
            ok = parse_ms(value, &pace);
        } else if (strcmp(flag, "--multitap") == 0) {
            ok = parse_ms(value, &cfg.multitap_timeout_ms);
        } else if (strcmp(flag, "--long-press") == 0) {
            ok = parse_ms(value, &cfg.long_press_ms);
        } else {
            ok = false;
        }
    }
    packed_dict = strcmp(dict, "packed") == 0;
    if (!ok || (!packed_dict && strcmp(dict, "fixture") != 0)) {
        usage(stderr);
        return 2;
    }
    if (packed_dict) {
        dict_lookup = t9_dict_packed_lookup;
    } else {
        t9_dict_simple_set(fixture_words, sizeof(fixture_words) / sizeof(fixture_words[0]));
        dict_lookup = t9_dict_simple_lookup;
    }
    pace_ms = pace;
    t9_init(&engine, counted_lookup, &cfg);
    engine.mode = T9_DEFAULT_MODE; /* completions start off, as after t9_init and an EEPROM reset */
    return script ? run_script(script, verbose) : run_interactive();
}
