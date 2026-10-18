/* T9-K0: minimal host test framework, a fake host text buffer that applies
 * engine output, and a rig that scripts key events with explicit timestamps.
 * Everything is static inline so test programs only pay for what they use.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#pragma once

#include <stdio.h>
#include <string.h>
#include "t9_engine.h"

/* --- checks ------------------------------------------------------------------ */

static int t9_test_checks;
static int t9_test_failures;

static inline bool t9_test_record(bool ok, const char *file, int line, const char *what) {
    t9_test_checks++;
    if (!ok) {
        t9_test_failures++;
        printf("FAIL %s:%d: %s\n", file, line, what);
    }
    return ok;
}

#define CHECK(cond) t9_test_record((cond), __FILE__, __LINE__, #cond)

#define CHECK_INT(actual, expected)                                                            \
    do {                                                                                       \
        long check_a = (long)(actual), check_e = (long)(expected);                             \
        if (!t9_test_record(check_a == check_e, __FILE__, __LINE__, #actual " == " #expected)) \
            printf("     got %ld, expected %ld\n", check_a, check_e);                          \
    } while (0)

#define CHECK_STR(actual, expected)                                                                       \
    do {                                                                                                  \
        const char *check_a = (actual), *check_e = (expected);                                            \
        if (!t9_test_record(strcmp(check_a, check_e) == 0, __FILE__, __LINE__, #actual " == " #expected)) \
            printf("     got \"%s\"\n     exp \"%s\"\n", check_a, check_e);                               \
    } while (0)

#define RUN_TEST(fn)                             \
    do {                                         \
        int failures_before = t9_test_failures;  \
        fn();                                    \
        if (t9_test_failures != failures_before) \
            printf("  in %s\n", #fn);            \
    } while (0)

/* Prints the summary; use as the exit status of main. */
static inline int t9_test_summary(const char *name) {
    printf("%s: %d checks, %d failed\n", name, t9_test_checks, t9_test_failures);
    return t9_test_failures ? 1 : 0;
}

/* --- fake host ------------------------------------------------------------------ */

/* passthrough codes used by the tests; the marker is what the fake host types */
enum {
    T9_TEST_CODE_BSPC = 1,
    T9_TEST_CODE_ESC,
    T9_TEST_CODE_ENTER,
    T9_TEST_CODE_TAB,
    T9_TEST_CODE_NUM,
    T9_TEST_CODE_LEFT,
    T9_TEST_CODE_RIGHT,
    T9_TEST_CODE_WORD_LEFT,
    T9_TEST_CODE_WORD_RIGHT
};

static inline const char *t9_test_code_marker(uint16_t code) {
    static const char *const markers[] = {"<?>",   "<BSPC>", "<ESC>",   "<ENTER>",     "<TAB>",
                                          "<NUM>", "<LEFT>", "<RIGHT>", "<WORD_LEFT>", "<WORD_RIGHT>"};
    return code < sizeof(markers) / sizeof(markers[0]) ? markers[code] : markers[0];
}

#define T9_HOST_TEXT_MAX 4096

typedef struct {
    char text[T9_HOST_TEXT_MAX];
    uint16_t len;
    uint32_t items;      /* queue items applied */
    uint32_t backspaces; /* backspaces applied, including host passthrough */
} t9_fake_host_t;

static inline void host_append(t9_fake_host_t *h, const char *s) {
    while (*s && h->len + 1 < T9_HOST_TEXT_MAX)
        h->text[h->len++] = *s++;
    h->text[h->len] = 0;
}

static inline void host_backspace(t9_fake_host_t *h) {
    if (h->len > 0)
        h->text[--h->len] = 0;
    h->backspaces++;
}

/* Keys tap a marker such as "<ENTER>", except the host backspace code. */
static inline void host_key(t9_fake_host_t *h, uint16_t code) {
    if (code == T9_TEST_CODE_BSPC)
        host_backspace(h);
    else
        host_append(h, t9_test_code_marker(code));
}

static inline void host_apply(t9_fake_host_t *h, t9_out_item_t item) {
    char ch[2] = {(char)item.value, 0};
    h->items++;
    if (item.kind == T9_OUT_CHAR)
        host_append(h, ch);
    else if (item.kind == T9_OUT_BACKSPACE)
        host_backspace(h);
    else
        host_key(h, item.value);
}

/* Applies at most `limit` queued items in order; returns how many. */
static inline uint32_t host_drain_some(t9_fake_host_t *h, t9_output_t *q, uint32_t limit) {
    t9_out_item_t item;
    uint32_t applied = 0;
    while (applied < limit && t9_output_pop(q, &item)) {
        host_apply(h, item);
        applied++;
    }
    return applied;
}

static inline uint32_t host_drain(t9_fake_host_t *h, t9_output_t *q) {
    return host_drain_some(h, q, UINT32_MAX);
}

/* --- rig: engine + host + clock ------------------------------------------------- */

typedef struct {
    t9_engine_t engine;
    t9_fake_host_t host;
    uint32_t now;
    bool auto_drain; /* drain after every event, like an instant host */
} t9_rig_t;

static inline void rig_init(t9_rig_t *r, t9_lookup_fn lookup) {
    static const t9_config_t cfg = {.multitap_timeout_ms = 800, .long_press_ms = 500};
    memset(r, 0, sizeof(*r));
    t9_init(&r->engine, lookup, &cfg);
    r->now        = 1000;
    r->auto_drain = true;
}

static inline const char *rig_drain(t9_rig_t *r) {
    host_drain(&r->host, &r->engine.out);
    return r->host.text;
}

static inline void rig_settle(t9_rig_t *r) {
    if (r->auto_drain)
        rig_drain(r);
}

static inline uint16_t rig_default_code(t9_key_t key) {
    switch (key) {
        case T9_KEY_DELETE:
            return T9_TEST_CODE_BSPC;
        case T9_KEY_CANCEL:
            return T9_TEST_CODE_ESC;
        case T9_KEY_MODE:
            return T9_TEST_CODE_NUM;
        case T9_KEY_COMPLETE:
            return T9_TEST_CODE_ENTER;
        case T9_KEY_OTHER:
            return T9_TEST_CODE_ENTER;
        default:
            return 0;
    }
}

/* Press at the current time. Keys the engine does not handle reach the host
 * directly, as on the real keyboard. */
static inline bool rig_press_code(t9_rig_t *r, t9_key_t key, uint16_t code) {
    bool handled = t9_key_press(&r->engine, key, code, r->now);
    if (!handled && code)
        host_key(&r->host, code);
    rig_settle(r);
    return handled;
}

static inline bool rig_press(t9_rig_t *r, t9_key_t key) {
    return rig_press_code(r, key, rig_default_code(key));
}

static inline void rig_release(t9_rig_t *r, t9_key_t key) {
    t9_key_release(&r->engine, key, r->now);
    rig_settle(r);
}

static inline void rig_wait(t9_rig_t *r, uint32_t ms) {
    r->now += ms;
    t9_tick(&r->engine, r->now);
    rig_settle(r);
}

#define RIG_TAP_HOLD_MS 30
#define RIG_TAP_GAP_MS 70

/* Press, release after RIG_TAP_HOLD_MS, then idle RIG_TAP_GAP_MS. */
static inline bool rig_tap_code(t9_rig_t *r, t9_key_t key, uint16_t code) {
    bool handled = rig_press_code(r, key, code);
    rig_wait(r, RIG_TAP_HOLD_MS);
    rig_release(r, key);
    rig_wait(r, RIG_TAP_GAP_MS);
    return handled;
}

static inline bool rig_tap(t9_rig_t *r, t9_key_t key) {
    return rig_tap_code(r, key, rig_default_code(key));
}

static inline bool rig_dial(t9_rig_t *r, bool clockwise) {
    bool handled = t9_dial_turn(&r->engine, clockwise, clockwise ? T9_TEST_CODE_RIGHT : T9_TEST_CODE_LEFT,
                                clockwise ? T9_TEST_CODE_WORD_RIGHT : T9_TEST_CODE_WORD_LEFT, r->now);
    rig_settle(r);
    return handled;
}

static inline void rig_hold(t9_rig_t *r, t9_key_t key, uint32_t ms) {
    rig_press(r, key);
    rig_wait(r, ms);
    rig_release(r, key);
}

/* Taps one key per character: 0-9 digits, n NEXT, p PREV, . DOT, < DELETE,
 * x CANCEL, + SPELL, ^ CAPS, m MODE, e ENTER (COMPLETE), t TAB (OTHER);
 * c toggles completions (Fn+Enter); S holds SPELL for long_press_ms; _ waits
 * out the multitap timeout; spaces are ignored. */
static inline void rig_type(t9_rig_t *r, const char *keys) {
    for (; *keys; keys++) {
        char c = *keys;
        if (c >= '0' && c <= '9')
            rig_tap(r, (t9_key_t)(T9_KEY_0 + (c - '0')));
        else if (c == 'n')
            rig_tap(r, T9_KEY_NEXT);
        else if (c == 'p')
            rig_tap(r, T9_KEY_PREV);
        else if (c == '.')
            rig_tap(r, T9_KEY_DOT);
        else if (c == '<')
            rig_tap(r, T9_KEY_DELETE);
        else if (c == 'x')
            rig_tap(r, T9_KEY_CANCEL);
        else if (c == '+')
            rig_tap(r, T9_KEY_SPELL);
        else if (c == 'S') {
            rig_hold(r, T9_KEY_SPELL, r->engine.cfg.long_press_ms);
            rig_wait(r, RIG_TAP_GAP_MS);
        } else if (c == '^')
            rig_tap(r, T9_KEY_CAPS);
        else if (c == 'm')
            rig_tap(r, T9_KEY_MODE);
        else if (c == 'e')
            rig_tap(r, T9_KEY_COMPLETE);
        else if (c == 't')
            rig_tap_code(r, T9_KEY_OTHER, T9_TEST_CODE_TAB);
        else if (c == 'c') {
            t9_set_completions(&r->engine, !r->engine.completions);
            rig_settle(r);
        } else if (c == '_')
            rig_wait(r, r->engine.cfg.multitap_timeout_ms);
    }
}

/* Host text before the active preview (meaningful once the queue is drained). */
static inline uint16_t rig_committed_len(const t9_rig_t *r) {
    return (uint16_t)(r->host.len - r->engine.shown_len);
}
