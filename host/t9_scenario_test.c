/* T9-K0: replays the paragraph scenario script (format documented at the
 * top of host/scenario_paragraph.t9) against the fixture dictionary. Checks
 * every expectation, the final text, and that corrections never touch
 * committed text. The replay runs twice: with an instant host and with a host
 * that applies one queued item per script line, so later input always arrives
 * before earlier output has been sent.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <stdlib.h>
#include "t9_dict_simple.h"
#include "t9_test.h"

static const t9_simple_entry_t fixture_words[] = {
#include "t9_fixture_table.h"
};

static const char *const final_text =
    "Hello Zorbak, I don't think it's a good idea to go home now. Call Anna at 4, she's well-known in the hood. GOOD "
    "luck! and<ENTER>good luck.";

typedef struct {
    const char *name;
    t9_key_t key;
    uint16_t code;
} key_name_t;

static const key_name_t key_names[] = {
    {"0", T9_KEY_0, 0},
    {"1", T9_KEY_1, 0},
    {"2", T9_KEY_2, 0},
    {"3", T9_KEY_3, 0},
    {"4", T9_KEY_4, 0},
    {"5", T9_KEY_5, 0},
    {"6", T9_KEY_6, 0},
    {"7", T9_KEY_7, 0},
    {"8", T9_KEY_8, 0},
    {"9", T9_KEY_9, 0},
    {"NEXT", T9_KEY_NEXT, 0},
    {"PREV", T9_KEY_PREV, 0},
    {"DOT", T9_KEY_DOT, 0},
    {"DELETE", T9_KEY_DELETE, T9_TEST_CODE_BSPC},
    {"CANCEL", T9_KEY_CANCEL, T9_TEST_CODE_ESC},
    {"SPELL", T9_KEY_SPELL, 0},
    {"CAPS", T9_KEY_CAPS, 0},
    {"MODE", T9_KEY_MODE, T9_TEST_CODE_NUM},
    {"ENTER", T9_KEY_COMPLETE, T9_TEST_CODE_ENTER},
    {"TAB", T9_KEY_OTHER, T9_TEST_CODE_TAB},
    {"FN_ENTER", T9_KEY_COUNT, 0}, /* completions toggle, not an engine key */
};

static const key_name_t *find_key(const char *name) {
    for (size_t i = 0; i < sizeof(key_names) / sizeof(key_names[0]); i++) {
        if (strcmp(key_names[i].name, name) == 0)
            return &key_names[i];
    }
    return NULL;
}

static bool is_correction(t9_key_t key) {
    return key == T9_KEY_NEXT || key == T9_KEY_PREV || key == T9_KEY_DELETE || key == T9_KEY_CANCEL ||
           key == T9_KEY_SPELL || key == T9_KEY_CAPS || key == T9_KEY_COUNT;
}

/* like the keymap: the toggle goes to t9_set_completions, unhandled keys to the host */
static void press_key(t9_rig_t *r, const key_name_t *key, uint32_t time) {
    if (key->key == T9_KEY_COUNT)
        t9_set_completions(&r->engine, !r->engine.completions);
    else if (!t9_key_press(&r->engine, key->key, key->code, time) && key->code)
        host_key(&r->host, key->code);
}

typedef struct {
    int expects;
    int corrections;
    int max_queued;
} replay_stats_t;

static void fail_line(const char *path, int line, const char *what) {
    t9_test_record(false, path, line, what);
}

/* lagging: apply one queued item per line instead of draining after each */
static replay_stats_t replay(const char *path, bool lagging) {
    replay_stats_t stats = {0, 0, 0};
    FILE *file           = fopen(path, "r");
    if (!CHECK(file != NULL))
        return stats;

    static t9_rig_t rig;
    t9_rig_t *r = &rig;
    rig_init(r, t9_dict_simple_lookup);
    r->auto_drain = false;

    char buf[1024];
    int line_no   = 0;
    uint32_t last = 0;
    while (fgets(buf, sizeof(buf), file)) {
        line_no++;
        buf[strcspn(buf, "\r\n")] = 0;
        if (buf[0] == 0 || buf[0] == '#')
            continue;
        char *rest;
        uint32_t time = (uint32_t)strtoul(buf, &rest, 10);
        if (rest == buf || *rest++ != ' ' || time < last) {
            fail_line(path, line_no, "malformed line or time going backwards");
            continue;
        }
        last   = time;
        r->now = time;
        t9_tick(&r->engine, time);
        if (lagging)
            host_drain_some(&r->host, &r->engine.out, 1);
        else
            rig_drain(r);

        if (strncmp(rest, "expect ", 7) == 0) {
            const char *text = rig_drain(r);
            stats.expects++;
            if (!t9_test_record(strcmp(text, rest + 7) == 0, path, line_no, rest))
                printf("     got \"%s\"\n", text);
            continue;
        }
        bool press = strncmp(rest, "press ", 6) == 0;
        if (!press && strncmp(rest, "release ", 8) != 0) {
            fail_line(path, line_no, "unknown action");
            continue;
        }
        const key_name_t *key = find_key(rest + (press ? 6 : 8));
        if (!key) {
            fail_line(path, line_no, "unknown key");
            continue;
        }
        if (!press) {
            if (key->key != T9_KEY_COUNT)
                t9_key_release(&r->engine, key->key, time);
            continue;
        }

        if (lagging) {
            press_key(r, key, time);
            if (t9_output_count(&r->engine.out) > stats.max_queued)
                stats.max_queued = t9_output_count(&r->engine.out);
            continue;
        }

        /* committed text = host text before the preview, with the queue drained */
        char committed[T9_HOST_TEXT_MAX];
        uint16_t committed_len = rig_committed_len(r);
        bool composing         = t9_is_composing(&r->engine);
        bool correction        = is_correction(key->key) && composing;
        /* Choosing punctuation resolves the provisional first 1, accepting
         * its base while keeping only the punctuation editable. */
        uint16_t correction_committed = committed_len;
        if (r->engine.compose == T9_COMPOSE_JOIN && (key->key == T9_KEY_NEXT || key->key == T9_KEY_PREV))
            correction_committed += r->engine.digit_len;
        bool host_delete = key->key == T9_KEY_DELETE && !composing;
        memcpy(committed, r->host.text, committed_len);

        press_key(r, key, time);
        rig_drain(r);
        if (!host_delete && (r->host.len < committed_len || memcmp(r->host.text, committed, committed_len) != 0))
            fail_line(path, line_no, "committed text changed");
        if (correction) {
            stats.corrections++;
            if (rig_committed_len(r) != correction_committed)
                fail_line(path, line_no, "unexpected committed length after correction");
        }
    }
    fclose(file);
    CHECK_STR(rig_drain(r), final_text);
    CHECK(!t9_is_composing(&r->engine));
    CHECK_INT(r->engine.caps, T9_CAPS_OFF);
    CHECK_INT(r->engine.mode, T9_MODE_T9);
    CHECK(r->engine.completions);
    CHECK_INT(t9_take_events(&r->engine) & T9_EVENT_BUSY, 0);
    return stats;
}

int main(int argc, char **argv) {
    const char *path = argc > 1 ? argv[1] : "host/scenario_paragraph.t9";
    t9_dict_simple_set(fixture_words, sizeof(fixture_words) / sizeof(fixture_words[0]));

    replay_stats_t instant = replay(path, false);
    CHECK(instant.expects >= 20);
    CHECK(instant.corrections >= 10);
    printf("instant host: %d expectations, %d corrections checked\n", instant.expects, instant.corrections);

    replay_stats_t lagging = replay(path, true);
    CHECK_INT(lagging.expects, instant.expects);
    CHECK(lagging.max_queued > 4);
    printf("lagging host: up to %d items queued\n", lagging.max_queued);
    return t9_test_summary("t9_scenario_test");
}
