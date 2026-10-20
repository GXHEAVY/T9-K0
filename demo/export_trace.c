/* Export deterministic presentation frames from the production engine.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "t9_engine.h"
#include "t9_dict_packed.h"
#include "t9_shortcuts.h"

static t9_engine_t engine;
static char host_text[256];
static size_t host_len;
static uint32_t now;
static unsigned frame_count;
static const char *layer = "t9", *held = "", *action = "";
static t9_shortcuts_t shortcuts;

static void json_string(const char *s) {
    putchar('"');
    for (; *s; s++) {
        if (*s == '"' || *s == '\\')
            putchar('\\');
        if ((unsigned char)*s < 32)
            printf("\\u%04x", (unsigned char)*s);
        else
            putchar(*s);
    }
    putchar('"');
}

static void require(bool condition, const char *message) {
    if (!condition) {
        fprintf(stderr, "Demo trace: %s\n", message);
        exit(1);
    }
}

static void frame(const char *key, const char *caption) {
    unsigned erased = 0;
    char inserted[128] = {0};
    size_t inserted_len = 0;
    t9_out_item_t item;
    while (t9_output_pop(&engine.out, &item)) {
        if (item.kind == T9_OUT_BACKSPACE) {
            require(host_len > 0, "backspace beyond host text");
            host_text[--host_len] = 0;
            erased++;
        } else if (item.kind == T9_OUT_CHAR) {
            require(host_len + 1 < sizeof(host_text), "host buffer full");
            require(inserted_len + 1 < sizeof(inserted), "edit buffer full");
            host_text[host_len++] = (char)item.value;
            host_text[host_len] = 0;
            inserted[inserted_len++] = (char)item.value;
        } else {
            require(false, "unexpected host passthrough in demo");
        }
    }
    require(!t9_take_events(&engine), "engine rejected an input");
    if (frame_count++)
        putchar(',');
    printf("{\"key\":");
    json_string(key);
    printf(",\"caption\":");
    json_string(caption);
    printf(",\"layer\":");
    json_string(layer);
    printf(",\"held\":");
    json_string(held);
    printf(",\"action\":");
    json_string(action);
    printf(",\"completions\":%s", engine.completions ? "true" : "false");
    printf(",\"text\":");
    json_string(host_text);
    printf(",\"preview\":");
    json_string(t9_preview(&engine));
    printf(",\"digits\":\"");
    for (uint8_t i = 0; i < engine.digit_len; i++)
        printf("%u", engine.digits[i]);
    printf("\",\"selected\":%u,\"family\":%s,\"erased\":%u,\"inserted\":", engine.cand_index,
           engine.cands.prefix_family ? "true" : "false", erased);
    json_string(inserted);
    printf(",\"candidates\":[");
    if (engine.compose == T9_COMPOSE_T9) {
        for (uint8_t i = 0; i < engine.cands.count; i++) {
            if (i)
                putchar(',');
            json_string(engine.cands.words[i]);
        }
    }
    printf("]}");
}

static void tap(t9_key_t key, const char *name, const char *caption) {
    now += 100;
    require(t9_key_press(&engine, key, 0, now), "unhandled key");
    t9_key_release(&engine, key, now + 40);
    frame(name, caption);
}

static void digits(const char *sequence) {
    for (const char *p = sequence; *p; p++) {
        char name[2] = {*p, 0};
        tap((t9_key_t)(*p - '0'), name, "Digit appended; candidate list and host preview updated.");
    }
}

static void begin_scene(const char *id) {
    t9_init(&engine, t9_dict_packed_lookup, NULL);
    host_len = 0;
    host_text[0] = 0;
    frame_count = 0;
    now = 0;
    layer = "t9";
    held = action = "";
    memset(&shortcuts, 0, sizeof(shortcuts));
    printf("{\"id\":");
    json_string(id);
    printf(",\"frames\":[");
    frame("", "Composition idle. Awaiting digit input.");
}

static void end_scene(const char *expected) {
    require(strcmp(host_text, expected) == 0, "unexpected final text");
    printf("],\"expected\":");
    json_string(expected);
    printf("}");
}

static void hold_fn(void) {
    layer = "fn";
    held = "FN";
    action = "";
    frame("HOLD_FN", "Hold Fn: lighting, media, connection and suggestion controls replace the typing legends.");
}

static void release_layer(const char *key, const char *caption) {
    layer = "t9";
    held = action = "";
    frame(key, caption);
}

static void toggle_suggestions(void) {
    require(t9_set_completions(&engine, !engine.completions), "suggestion toggle failed");
    action = engine.completions ? "Word suggestions on" : "Word suggestions off";
    frame("ENTER", "Fn + Enter: toggle word suggestions. Untyped letters appear in brackets.");
}

static void hold_symbol(void) {
    now += 100;
    require(t9_key_press(&engine, T9_KEY_1, 0, now), "symbol prefix rejected");
    layer = "symbols";
    held = "1";
    frame("HOLD_1", "Hold phone 1 (printed 7): keys 2–5 select symbol groups. Tap repeatedly to cycle.");
}

static void release_symbol(void) {
    t9_key_release(&engine, T9_KEY_1, now + 40);
    release_layer("RELEASE_1", "Release phone 1: accept the symbol and return to the typing legends.");
}

static void hold_shortcut(t9_shortcut_key_t prefix, const char *name, const char *map) {
    now += 100;
    require(t9_shortcuts_event(&shortcuts, 0, prefix, true, engine.mode, now, 200) == T9_SC_NONE,
            "shortcut prefix rejected");
    layer = map;
    held = name;
    action = "";
    frame(prefix == T9_SK_SLASH ? "HOLD_SLASH" : "HOLD_M1",
          prefix == T9_SK_SLASH ? "Hold /: editing and navigation shortcuts replace the typing legends."
                               : "Hold M1: Tab becomes terminal interrupt (Ctrl+C on every host).");
}

static void shortcut(t9_shortcut_key_t key, t9_shortcut_action_t expected, const char *name, const char *description) {
    now += 100;
    require(t9_shortcuts_event(&shortcuts, 1, key, true, engine.mode, now, 200) == expected,
            "shortcut mapping changed");
    require(t9_accept(&engine), "shortcut must accept the typed portion");
    require(t9_shortcuts_event(&shortcuts, 1, key, false, engine.mode, now + 40, 200) == T9_SC_NONE,
            "shortcut release rejected");
    action = description;
    frame(name, "The held prefix selects this shortcut. Pending text is accepted before the host action.");
}

static void release_shortcut(void) {
    now += 100;
    require(t9_shortcuts_event(&shortcuts, 0, shortcuts.prefix, false, engine.mode, now, 200) == T9_SC_NONE,
            "shortcut prefix release rejected");
    release_layer(shortcuts.prefix == T9_SK_SLASH ? "RELEASE_SLASH" : "RELEASE_M1",
                  "Release the prefix: return to typing. Releasing / also confirms any app selection.");
}

int main(void) {
    printf("{\"wordCount\":%u,\"scenes\":[", (unsigned)t9_dict_packed_stats()->word_count);
    begin_scene("predict");
    digits("43556");
    require(strcmp(t9_preview(&engine), "hello") == 0, "hello prediction changed");
    tap(T9_KEY_0, "0", "0: commit hello and append a space.");
    digits("96753");
    require(strcmp(t9_preview(&engine), "world") == 0, "world prediction changed");
    tap(T9_KEY_0, "0", "0: commit world and append a space.");
    end_scene("hello world ");
    putchar(',');

    begin_scene("choose");
    digits("4663");
    require(engine.cands.count > 1, "ambiguity demo needs multiple candidates");
    unsigned turns = 0;
    do {
        require(turns++ < T9_MAX_CANDIDATES, "home missing from candidate list");
        now += 100;
        require(t9_dial_turn(&engine, true, 0, 0, now), "dial was not handled");
        frame("DIAL", "Dial clockwise: select the next candidate. Digit sequence unchanged.");
    } while (strcmp(t9_preview(&engine), "home") != 0);
    tap(T9_KEY_0, "0", "0: commit home and append a space.");
    end_scene("home ");
    putchar(',');

    begin_scene("complete");
    digits("53926");
    require(strcmp(t9_preview(&engine), "keybo>ard") == 0, "keyboard family changed");
    tap(T9_KEY_COMPLETE, "ENTER", "Enter: extend to keyboard; offer suffix s. Composition remains active.");
    require(strcmp(t9_preview(&engine), "keyboard>s") == 0, "keyboard extension changed");
    tap(T9_KEY_COMPLETE, "ENTER", "Enter: commit keyboards and append a space.");
    end_scene("keyboards ");
    putchar(',');

    begin_scene("suggest");
    hold_fn();
    toggle_suggestions();
    release_layer("RELEASE_FN", "Release Fn: return to typing with word suggestions enabled.");
    digits("4663");
    require(strcmp(t9_preview(&engine), "good[s]") == 0, "goods suggestion changed");
    tap(T9_KEY_COMPLETE, "ENTER", "Enter: accept the entire suggested word, goods, and add a space.");
    digits("4663");
    tap(T9_KEY_0, "0", "0: accept only the typed part, good, and add a space. The suggested s is discarded.");
    end_scene("goods good ");
    putchar(',');

    begin_scene("symbols");
    hold_symbol();
    tap(T9_KEY_2, "2", "Hold 1 + tap 2: insert an opening parenthesis.");
    tap(T9_KEY_2, "2", "Keep 1 held + tap 2 again: replace ( with [.");
    release_symbol();
    digits("43556");
    tap(T9_KEY_DOT, "DOT", "Dot: accept hello without adding a space.");
    hold_symbol();
    tap(T9_KEY_3, "3", "Hold 1 + tap 3: insert a closing parenthesis.");
    tap(T9_KEY_3, "3", "Keep 1 held + tap 3 again: replace ) with ].");
    release_symbol();
    hold_symbol();
    tap(T9_KEY_4, "4", "Hold 1 + tap 4: insert a colon.");
    tap(T9_KEY_4, "4", "Keep 1 held + tap 4 again: replace : with ;.");
    release_symbol();
    hold_symbol();
    tap(T9_KEY_5, "5", "Hold 1 + tap 5: insert +.");
    tap(T9_KEY_5, "5", "Keep 1 held + tap 5 again: replace + with a backtick.");
    tap(T9_KEY_5, "5", "Tap 5 a third time: replace the backtick with @.");
    release_symbol();
    end_scene("[hello];@");
    putchar(',');

    begin_scene("shortcuts");
    digits("43556");
    hold_shortcut(T9_SK_SLASH, "/", "slash");
    shortcut(T9_SK_TAB, T9_SC_COPY, "TAB", "Copy · Command+C on Mac / Ctrl+C on Windows and Linux");
    layer = "slash-switch";
    shortcut(T9_SK_PLUS, T9_SC_ALT_TAB, "SPELL", "Next app · hold Command on Mac / Alt on Windows and Linux, then tap Tab");
    shortcut(T9_SK_STAR, T9_SC_BACKTAB, "*", "Previous app · Command+Shift+Tab / Alt+Shift+Tab");
    release_shortcut();
    hold_shortcut(T9_SK_M1, "M1", "m1");
    shortcut(T9_SK_TAB, T9_SC_INTERRUPT, "TAB", "Interrupt terminal process · Ctrl+C on every host");
    release_shortcut();
    end_scene("hello");
    putchar(',');

    begin_scene("fn");
    hold_fn();
    action = "Increase backlight brightness";
    frame("/", "Fn + /: increase brightness. The dial also adjusts brightness while Fn is held.");
    action = "Change backlight hue";
    frame("*", "Fn + *: increase the backlight hue.");
    action = "Play / pause media";
    frame("9", "Fn + printed 3: play or pause media.");
    toggle_suggestions();
    release_layer("RELEASE_FN", "Release Fn: return to typing. Word suggestions stay enabled.");
    end_scene("");
    printf("]}\n");
    return 0;
}
