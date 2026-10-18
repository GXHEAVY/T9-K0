/* T9-K0: event-sequence tests for the composition engine against the
 * fixture dictionary and a fake host text buffer.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "t9_dict_simple.h"
#include "t9_test.h"

static const t9_simple_entry_t fixture_words[] = {
#include "t9_fixture_table.h"
};

static void use_fixture(void) {
    t9_dict_simple_set(fixture_words, sizeof(fixture_words) / sizeof(fixture_words[0]));
}

static t9_rig_t rig;

/* completions on, as most tests expect `base[suffix]` (t9_init leaves them off) */
static t9_rig_t *fresh_rig(void) {
    use_fixture();
    rig_init(&rig, t9_dict_simple_lookup);
    t9_set_completions(&rig.engine, true);
    return &rig;
}

static const char *host(const t9_rig_t *r) {
    return r->host.text;
}

static void test_word_shows_top_candidate(void) {
    t9_rig_t *r = fresh_rig();
    rig_type(r, "4");
    CHECK_STR(host(r), "i[n]");
    CHECK(t9_is_composing(&r->engine));
    CHECK(t9_has_completion(&r->engine));
    rig_type(r, "663");
    CHECK_STR(host(r), "good");
    CHECK_STR(t9_preview(&r->engine), "good");
    CHECK(!t9_has_completion(&r->engine));
    CHECK_INT(r->engine.compose, T9_COMPOSE_T9);
    CHECK_INT(r->engine.digit_len, 4);
    CHECK_INT(r->engine.cands.count, 7);
    CHECK_INT(r->engine.cand_index, 0);
}

static void test_longer_words_replace_exact_recommendations(void) {
    static const t9_simple_entry_t words[] = {
        {"good", 63}, {"home", 62}, {"gone", 61}, {"goodbye", 40}, {"homes", 30},
    };
    for (int queued = 0; queued < 2; queued++) {
        t9_rig_t *r = fresh_rig();
        t9_dict_simple_set(words, sizeof(words) / sizeof(words[0]));
        r->auto_drain = !queued;
        rig_type(r, "4663");
        CHECK_STR(t9_preview(&r->engine), "good[bye]");
        CHECK(t9_has_completion(&r->engine));
        CHECK_INT(r->engine.cands.count, 3);
        CHECK_INT(r->engine.cands.total_exact, 1);
        rig_type(r, "n");
        CHECK_STR(t9_preview(&r->engine), "home[s]");
        rig_type(r, "n");
        CHECK_STR(t9_preview(&r->engine), "gone");
        CHECK(!t9_has_completion(&r->engine));
        rig_type(r, "n");
        CHECK_STR(t9_preview(&r->engine), "good[bye]");
        rig_type(r, "pp");
        CHECK_STR(t9_preview(&r->engine), "home[s]");
        rig_type(r, "0 4663 e");
        CHECK_STR(rig_drain(r), "home goodbye ");
        CHECK(!t9_is_composing(&r->engine));

        rig_type(r, "4663 c");
        CHECK_STR(t9_preview(&r->engine), "good");
        rig_type(r, "nn");
        CHECK_STR(t9_preview(&r->engine), "gone");
        rig_type(r, "c");
        CHECK_STR(t9_preview(&r->engine), "gone"); /* toggling preserves exact-only choices */
        rig_type(r, "n");
        CHECK_STR(t9_preview(&r->engine), "good[bye]");
        rig_type(r, "^ 2");
        CHECK_STR(t9_preview(&r->engine), "Goodb[ye]");
        rig_type(r, "<");
        CHECK_STR(t9_preview(&r->engine), "Good[bye]");
        rig_type(r, "293");
        CHECK_STR(t9_preview(&r->engine), "Goodbye");
        CHECK(!t9_has_completion(&r->engine));
        CHECK_INT(r->engine.cands.total_exact, 1);
        rig_type(r, "e"); /* no longer candidate: ordinary Enter */
        CHECK_STR(rig_drain(r), "home goodbye Goodbye<ENTER>");
    }
}

static void test_exact_only_and_empty_candidates(void) {
    static const t9_simple_entry_t words[] = {{"good", 63}, {"home", 62}};
    t9_rig_t *r                            = fresh_rig();
    t9_dict_simple_set(words, sizeof(words) / sizeof(words[0]));
    rig_type(r, "2");
    CHECK_STR(t9_preview(&r->engine), "");
    CHECK(!t9_is_composing(&r->engine));
    CHECK(!t9_has_completion(&r->engine));
    CHECK_INT(t9_take_events(&r->engine), T9_EVENT_MISS);
    rig_type(r, "4663");
    CHECK_STR(host(r), "good");
    CHECK_INT(r->engine.cands.count, 2);
    CHECK_INT(r->engine.cands.total_exact, 2);
    CHECK(!t9_has_completion(&r->engine));
    rig_type(r, "n");
    CHECK_STR(host(r), "home");
    rig_type(r, "n");
    CHECK_STR(host(r), "good");
    rig_type(r, "0");
    CHECK_STR(host(r), "good ");
}

static void test_reaching_word_end_prefers_its_extension(void) {
    static const t9_simple_entry_t words[] = {
        {"good", 63},
        {"immediately", 60},
        {"goodbye", 40},
    };
    t9_rig_t *r = fresh_rig();
    t9_dict_simple_set(words, sizeof(words) / sizeof(words[0]));
    rig_type(r, "466");
    CHECK_STR(host(r), "goo[d]");
    rig_type(r, "3");
    CHECK_STR(host(r), "good[bye]");
    rig_type(r, "n");
    CHECK_STR(host(r), "imme[diately]"); /* another base remains reachable */
    rig_type(r, "n0");
    CHECK_STR(host(r), "good ");
}

static void test_cycle_wraps(void) {
    t9_rig_t *r = fresh_rig();
    rig_type(r, "2 0 4663");
    rig_type(r, "n");
    CHECK_STR(host(r), "a home");
    rig_type(r, "nnnnn");
    CHECK_STR(host(r), "a hoof");
    rig_type(r, "n");
    CHECK_STR(host(r), "a good");
    rig_type(r, "p");
    CHECK_STR(host(r), "a hoof");
    CHECK_INT(r->engine.cand_index, 6);
    rig_type(r, "pp");
    CHECK_STR(host(r), "a goof");
}

/* With completions on, the list skips exact words with a listed extension:
 * i[n] i[s] i[t] h[ave] ... i[ts] */
static void test_cycle_shows_completions(void) {
    t9_rig_t *r = fresh_rig();
    rig_type(r, "4");
    CHECK_STR(host(r), "i[n]");
    CHECK_INT(r->engine.cands.count, 16);
    CHECK_INT(r->engine.cands.total_exact, 0);
    rig_type(r, "n");
    CHECK_STR(host(r), "i[s]");
    rig_type(r, "n");
    CHECK_STR(host(r), "i[t]");
    CHECK(t9_has_completion(&r->engine));
    rig_type(r, "p");
    CHECK_STR(host(r), "i[s]");
    CHECK(t9_has_completion(&r->engine));
    rig_type(r, "pp"); /* wraps to the last entry */
    CHECK_STR(host(r), "i[ts]");
    CHECK_INT(r->engine.cand_index, 15);
    rig_type(r, "n");
    CHECK_STR(host(r), "i[n]");
}

static void test_brackets_follow_digits_delete_and_caps(void) {
    t9_rig_t *r = fresh_rig();
    rig_type(r, "2");
    CHECK_STR(host(r), "a[nd]");
    rig_type(r, "6");
    CHECK_STR(host(r), "an[d]"); /* the same whole word stays selected */
    rig_type(r, "3");
    CHECK_STR(host(r), "and");
    rig_type(r, "<");
    CHECK_STR(host(r), "an[d]");
    rig_type(r, "<");
    CHECK_STR(host(r), "a[nd]");
    rig_type(r, "n");
    CHECK_STR(host(r), "a[re]");
    rig_type(r, "^");
    CHECK_STR(host(r), "A[re]");
    rig_type(r, "n");
    CHECK_STR(host(r), "A[n]"); /* caps once: the suffix stays lowercase */
    rig_type(r, "^");
    CHECK_STR(host(r), "A[N]"); /* caps lock: both */
    rig_type(r, "^");
    CHECK_STR(host(r), "a[n]");
    rig_type(r, "x");
    CHECK_STR(host(r), "");
    CHECK(!t9_is_composing(&r->engine));
}

/* extending keeps the same whole word, else the top entry; after an explicit
 * choice it prefers the same base; deleting keeps the word, else the base */
static void test_selection_keeps_word_then_base(void) {
    t9_rig_t *r = fresh_rig();
    rig_type(r, "4nnn"); /* fourth completion of 4: h[ave] */
    CHECK_STR(host(r), "h[ave]");
    rig_type(r, "6");
    CHECK_STR(host(r), "ho[w]"); /* no "have"; first longer entry with base "h" */
    rig_type(r, "6");
    CHECK_STR(host(r), "hom[e]");
    rig_type(r, "3");
    CHECK_STR(host(r), "home");
    rig_type(r, "<");
    CHECK_STR(host(r), "hom[e]"); /* the whole word "home" is in the list for 466 */
    rig_type(r, "3 0 46");
    CHECK_STR(host(r), "home in[to]"); /* reaching in prefers its extension into */
    rig_type(r, "0 84");
    CHECK_STR(host(r), "home in th[e]");
    rig_type(r, "4");
    CHECK_STR(host(r), "home in thi[s]"); /* no "the": the top entry */

    r = fresh_rig();
    rig_type(r, "36");
    CHECK_STR(host(r), "fo[r]");
    rig_type(r, "6");
    CHECK_STR(host(r), "don"); /* not the explicit choice: the base "fo" (foo) does not stick */
    rig_type(r, "<");
    CHECK_STR(host(r), "do[n]"); /* deleting keeps the word */
    rig_type(r, "0 36n");
    CHECK_STR(host(r), "do do[wn]");
    rig_type(r, "6");
    CHECK_STR(host(r), "do don"); /* cycled: "do" sticks over "for"; "don" has that base */
}

static void test_complete_key(void) {
    t9_rig_t *r = fresh_rig();
    rig_type(r, "84");
    CHECK_STR(host(r), "th[e]");
    rig_type(r, "e");
    CHECK_STR(host(r), "the ");
    CHECK(!t9_is_composing(&r->engine));
    rig_type(r, "843");
    CHECK_STR(host(r), "the the[n]");
    rig_type(r, "e"); /* the exact word no longer hides a longer completion */
    CHECK_STR(host(r), "the then ");
    CHECK(!rig_press(r, T9_KEY_COMPLETE)); /* idle: the host gets Enter */
    rig_release(r, T9_KEY_COMPLETE);
    CHECK_STR(host(r), "the then <ENTER>");

    r = fresh_rig();
    rig_type(r, "^ 84 e");
    CHECK_STR(host(r), "The ");
    CHECK_INT(r->engine.caps, T9_CAPS_OFF);
    rig_type(r, "^^ 84 e");
    CHECK_STR(host(r), "The THE ");

    r = fresh_rig();
    rig_type(r, "m 44 e"); /* multitap has no completions */
    CHECK_STR(host(r), "h<ENTER>");
}

/* every accepting key keeps the base and erases the suffix first */
static void test_accept_keeps_base(void) {
    t9_rig_t *r = fresh_rig();
    rig_type(r, "84 0");
    CHECK_STR(host(r), "th ");
    rig_type(r, "84 .");
    CHECK_STR(host(r), "th th");
    rig_type(r, "84 1");
    CHECK_STR(host(r), "th thth.");
    CHECK_INT(r->engine.compose, T9_COMPOSE_PUNCT);
    rig_type(r, "x 84 t");
    CHECK_STR(host(r), "th ththth<TAB>");
    rig_type(r, "84 m");
    CHECK_STR(host(r), "th ththth<TAB>th");
    CHECK_INT(r->engine.mode, T9_MODE_MULTITAP);
    rig_type(r, "mm 84");
    CHECK_STR(host(r), "th ththth<TAB>thth[e]");
    CHECK(t9_accept(&r->engine));
    CHECK_STR(rig_drain(r), "th ththth<TAB>thth");
    CHECK(!t9_is_composing(&r->engine));
    rig_type(r, "84");
    rig_hold(r, T9_KEY_3, 500); /* the tap showed "the"; the base before it was "th" */
    CHECK_STR(host(r), "th ththth<TAB>ththth3");
}

static void test_delete_to_empty(void) {
    t9_rig_t *r = fresh_rig();
    rig_type(r, "2 0 4663");
    rig_type(r, "<");
    CHECK_STR(host(r), "a goo[d]");
    rig_type(r, "<");
    CHECK_STR(host(r), "a go[od]"); /* the same whole word, not the top entry "in" */
    rig_type(r, "<<");
    CHECK_STR(host(r), "a ");
    CHECK(!t9_is_composing(&r->engine));
    CHECK_INT(t9_output_count(&r->engine.out), 0);
    uint32_t items = r->host.items;
    rig_wait(r, 2000);
    CHECK_INT(r->host.items, items);

    CHECK(!rig_press(r, T9_KEY_DELETE)); /* host backspace */
    rig_release(r, T9_KEY_DELETE);
    CHECK_STR(host(r), "a");
    CHECK_INT(r->host.items, items);
}

static void test_delete_without_composition_queues_behind_output(void) {
    t9_rig_t *r   = fresh_rig();
    r->auto_drain = false;
    rig_type(r, "2 0");
    CHECK(rig_press(r, T9_KEY_DELETE)); /* the space is still queued: take it back */
    rig_release(r, T9_KEY_DELETE);
    CHECK_STR(rig_drain(r), "a");
    CHECK_INT(r->host.backspaces, 0);
}

static void test_delete_multitap_and_punct(void) {
    t9_rig_t *r = fresh_rig();
    rig_type(r, "m 44 33");
    CHECK_STR(host(r), "he");
    rig_type(r, "<");
    CHECK_STR(host(r), "h");
    CHECK(!r->engine.mt_pending);
    rig_type(r, "4"); /* new letter, not a cycle of the deleted one */
    CHECK_STR(host(r), "hg");
    rig_type(r, "1<");
    CHECK_STR(host(r), "hg");
    CHECK(!t9_is_composing(&r->engine));
}

static void test_cancel_erases_only_preview(void) {
    t9_rig_t *r = fresh_rig();
    rig_type(r, "2 0 4663");
    CHECK_STR(host(r), "a good");
    rig_type(r, "x");
    CHECK_STR(host(r), "a ");
    CHECK(!t9_is_composing(&r->engine));
    CHECK(!rig_press(r, T9_KEY_CANCEL));
    rig_release(r, T9_KEY_CANCEL);
    CHECK_STR(host(r), "a <ESC>");
}

static void test_accept_zero_and_dot(void) {
    t9_rig_t *r = fresh_rig();
    rig_type(r, "4663 0");
    CHECK_STR(host(r), "good ");
    CHECK(!t9_is_composing(&r->engine));
    rig_type(r, "0");
    CHECK_STR(host(r), "good  ");
    rig_type(r, "4663.");
    CHECK_STR(host(r), "good  good");
    CHECK(!t9_is_composing(&r->engine));
    CHECK(rig_press(r, T9_KEY_DOT));
    rig_release(r, T9_KEY_DOT);
    CHECK_STR(host(r), "good  good");
    CHECK_INT(t9_output_count(&r->engine.out), 0);
}

static void test_punctuation(void) {
    t9_rig_t *r = fresh_rig();
    rig_type(r, "4663 1");
    CHECK_STR(host(r), "good.");
    CHECK_INT(r->engine.compose, T9_COMPOSE_PUNCT);
    const char *expected[] = {"good,", "good?", "good!", "good'", "good-", "good."};
    for (int i = 0; i < 6; i++) {
        rig_type(r, "1");
        CHECK_STR(host(r), expected[i]);
    }
    rig_type(r, "p");
    CHECK_STR(host(r), "good-");
    rig_type(r, "n");
    CHECK_STR(host(r), "good.");
    rig_type(r, "n0");
    CHECK_STR(host(r), "good, ");
    rig_type(r, "1 2");
    CHECK_STR(host(r), "good, .a[nd]");
    CHECK_INT(r->engine.compose, T9_COMPOSE_T9);
    rig_type(r, "1 .");
    CHECK_STR(host(r), "good, .a.");
    CHECK(!t9_is_composing(&r->engine));
}

static void test_contraction(void) {
    t9_rig_t *r = fresh_rig();
    rig_type(r, "366 1 nnnn");
    CHECK_STR(host(r), "don'");
    rig_type(r, "8 0");
    CHECK_STR(host(r), "don't ");
    rig_type(r, "48 11111 7 0");
    CHECK_STR(host(r), "don't it's ");
}

/* Exercise the same keystrokes with completions on/off and with output still
 * queued. The separator is an input unit, not a dictionary-wide new alphabet. */
static void test_inline_contractions(void) {
    static const struct {
        const char *keys, *text;
    } cases[] = {
        {"4160", "i'm "},     {"41830", "i've "},   {"41550", "i'll "},     {"4130", "i'd "},
        {"966180", "won't "}, {"366180", "don't "}, {"226180", "can't "},   {"9681730", "you're "},
        {"931830", "we've "}, {"743170", "she's "}, {"9681550", "you'll "}, {"968130", "you'd "},
        {"48170", "it's "},   {"96811", "you,"},    {"4118", "i,t"},        {"41nnnn8", "i't"},
        {"41pppppp8", "i.t"}, /* wrapping to '.' does not re-enable joining */
    };
    for (int completions = 0; completions < 2; completions++) {
        for (int queued = 0; queued < 2; queued++) {
            for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
                t9_rig_t *r = fresh_rig();
                t9_set_completions(&r->engine, completions);
                r->auto_drain = !queued;
                rig_type(r, cases[i].keys);
                rig_type(r, "."); /* drop an ordinary word's completion */
                CHECK_STR(rig_drain(r), cases[i].text);
                CHECK(!t9_is_composing(&r->engine));
                CHECK_INT(t9_take_events(&r->engine) & (T9_EVENT_MISS | T9_EVENT_BUSY), 0);
            }
        }
    }
}

static void test_contraction_preview_and_delete(void) {
    t9_rig_t *r = fresh_rig();
    rig_type(r, "20 41");
    CHECK_STR(host(r), "a i.");
    CHECK_STR(t9_preview(&r->engine), "i.");
    CHECK_INT(rig_committed_len(r), 2);
    CHECK(!t9_has_completion(&r->engine));
    rig_type(r, "<");
    CHECK_STR(host(r), "a i[n]");
    rig_type(r, "18");
    CHECK_STR(host(r), "a i'v[e]");
    CHECK(t9_has_completion(&r->engine));
    rig_type(r, "3");
    CHECK_STR(host(r), "a i've");
    CHECK_INT(r->engine.digit_len, 4);
    rig_type(r, "<");
    CHECK_STR(host(r), "a i'v[e]");
    rig_type(r, "<");
    CHECK_STR(host(r), "a i.");
    rig_type(r, "6");
    CHECK_STR(host(r), "a i'm");
    rig_type(r, "<<<");
    CHECK_STR(host(r), "a ");
    CHECK(!t9_is_composing(&r->engine));
    rig_type(r, "416x");
    CHECK_STR(host(r), "a ");
    rig_type(r, "41x");
    CHECK_STR(host(r), "a ");
    rig_type(r, "411x"); /* explicit punctuation accepts i */
    CHECK_STR(host(r), "a i");
}

static void test_contraction_resolves_base_ambiguity(void) {
    static const t9_simple_entry_t words[] = {{"women", 60}, {"woman", 50}, {"won", 40}, {"the", 62}};
    t9_rig_t *r                            = fresh_rig();
    t9_dict_simple_set(words, sizeof(words) / sizeof(words[0]));
    rig_type(r, "9661");
    CHECK_STR(host(r), "wom.");
    rig_type(r, "8");
    CHECK_STR(host(r), "won't");
    rig_type(r, "<");
    CHECK_STR(host(r), "won.");
    rig_type(r, "<");
    CHECK_STR(host(r), "won"); /* exact base survives alongside women/woman */
    rig_type(r, "x966nnn18."); /* explicitly cycle back to wom[en] */
    CHECK_STR(host(r), "wom.t");
}

static void test_contraction_caps_and_fallback(void) {
    t9_rig_t *r = fresh_rig();
    rig_type(r, "^4160");
    CHECK_STR(host(r), "I'm ");
    CHECK_INT(r->engine.caps, T9_CAPS_OFF);
    rig_type(r, "^^41830");
    CHECK_STR(host(r), "I'm I'VE ");
    rig_type(r, "^41^"); /* change caps while the period is provisional */
    CHECK_STR(host(r), "I'm I'VE I.");
    rig_type(r, "2"); /* no contraction ending on 2: normal punctuation + word */
    CHECK_STR(host(r), "I'm I'VE I.a[nd]");
    CHECK_INT(r->engine.caps, T9_CAPS_OFF);
    rig_type(r, "x");
    CHECK_STR(host(r), "I'm I'VE I.");
    rig_type(r, "416");
    t9_take_events(&r->engine);
    rig_type(r, "9"); /* an invalid continuation of an established contraction */
    CHECK_STR(t9_preview(&r->engine), "i'm");
    CHECK_INT(t9_take_events(&r->engine), T9_EVENT_MISS);
    rig_type(r, "x4");
    while (t9_preview(&r->engine)[0] != 'h')
        rig_type(r, "n");
    rig_type(r, "16."); /* selected h must not be reinterpreted as i */
    CHECK_STR(host(r), "I'm I'VE I.h.o");
}

static void test_contraction_completion_and_toggle(void) {
    t9_rig_t *r = fresh_rig();
    rig_type(r, "418");
    CHECK_STR(host(r), "i'v[e]");
    rig_type(r, "c");
    CHECK_STR(host(r), "i'v>e");
    rig_type(r, "c e");
    CHECK_STR(host(r), "i've ");
    rig_type(r, "4180"); /* space still accepts only the typed part */
    CHECK_STR(host(r), "i've i'v ");
    rig_type(r, "41c<"); /* toggle while pending, then remove the separator */
    CHECK_STR(t9_preview(&r->engine), "i");
    rig_type(r, "n");
    CHECK_STR(t9_preview(&r->engine), "h"); /* no duplicate i bases */
    rig_type(r, "x416e");
    CHECK_STR(host(r), "i've i'v i'm<ENTER>");

    r = fresh_rig();
    rig_type(r, "942817");
    CHECK_STR(host(r), "what'r[e]");
    rig_type(r, "n");
    CHECK_STR(host(r), "what's"); /* different exact spelling remains selectable */
    CHECK(!t9_has_completion(&r->engine));
    rig_type(r, "n");
    CHECK_STR(host(r), "what'r[e]");
    rig_type(r, "3");
    CHECK_STR(host(r), "what're");
    rig_type(r, "<");
    CHECK_STR(host(r), "what'r[e]");
}

static void test_contraction_long_press_and_boundaries(void) {
    static const struct {
        const char *prefix;
        t9_key_t held;
        const char *text;
    } cases[] = {
        {"4", T9_KEY_1, "i1"},   {"41", T9_KEY_1, "i.1"},   {"41", T9_KEY_6, "i.6"},   {"41", T9_KEY_2, "i.2"},
        {"41", T9_KEY_0, "i.0"}, {"416", T9_KEY_0, "i'm0"}, {"418", T9_KEY_3, "i'v3"},
    };
    for (int queued = 0; queued < 2; queued++) {
        for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
            t9_rig_t *r   = fresh_rig();
            r->auto_drain = !queued;
            rig_type(r, cases[i].prefix);
            rig_hold(r, cases[i].held, 500);
            CHECK_STR(rig_drain(r), cases[i].text);
            CHECK(!t9_is_composing(&r->engine));
        }
    }
    t9_rig_t *r = fresh_rig();
    rig_type(r, "41t416t41m");
    CHECK_STR(host(r), "i.<TAB>i'm<TAB>i.");
    CHECK_INT(r->engine.mode, T9_MODE_MULTITAP);
    rig_type(r, "mm41S");
    CHECK_STR(host(r), "i.<TAB>i'm<TAB>i.");
    CHECK(r->engine.spelling);
    rig_type(r, "x41");
    t9_reset(&r->engine);
    CHECK_INT(r->engine.join_pos, 0);
    rig_type(r, "6.");
    CHECK_STR(host(r), "i.<TAB>i'm<TAB>i.i.o");
}

static void test_contraction_queue_refusal(void) {
    const char *prefixes[] = {"4", "41", "41", "41", "418", "41", "416"};
    const char *keys[]     = {"1", "6", "1", "2", "<", "x", "0"};
    for (size_t i = 0; i < sizeof(prefixes) / sizeof(prefixes[0]); i++) {
        t9_rig_t *r = fresh_rig();
        rig_type(r, prefixes[i]);
        t9_compose_t compose = r->engine.compose;
        uint8_t digit_len = r->engine.digit_len, join_pos = r->engine.join_pos;
        char preview[T9_SHOWN_MAX + 1];
        strcpy(preview, t9_preview(&r->engine));
        r->auto_drain = false;
        while (t9_output_free(&r->engine.out))
            t9_output_push(&r->engine.out, T9_OUT_KEY, T9_TEST_CODE_TAB);
        t9_take_events(&r->engine);
        rig_type(r, keys[i]);
        CHECK_INT(t9_take_events(&r->engine), T9_EVENT_BUSY);
        CHECK_STR(t9_preview(&r->engine), preview);
        CHECK_INT(r->engine.compose, compose);
        CHECK_INT(r->engine.digit_len, digit_len);
        CHECK_INT(r->engine.join_pos, join_pos);
        t9_output_clear(&r->engine.out); /* remove only the synthetic pressure */
        rig_type(r, keys[i]);
        CHECK_INT(t9_take_events(&r->engine) & T9_EVENT_BUSY, 0);
        rig_drain(r);
    }
}

static void test_caps_once(void) {
    t9_rig_t *r = fresh_rig();
    rig_type(r, "^");
    CHECK_INT(r->engine.caps, T9_CAPS_ONCE);
    CHECK(t9_take_events(&r->engine) & T9_EVENT_CAPS_CHANGED);
    rig_type(r, "4663");
    CHECK_STR(host(r), "Good");
    rig_type(r, "n");
    CHECK_STR(host(r), "Home");
    rig_type(r, "n");
    CHECK_STR(host(r), "Gone");
    rig_type(r, "<");
    CHECK_STR(host(r), "Gon[e]");
    rig_type(r, "3 1");
    CHECK_INT(r->engine.caps, T9_CAPS_OFF); /* consumed by accepting the word */
    rig_type(r, "0 4663");
    CHECK_STR(host(r), "Gone. good");
    rig_type(r, "^"); /* applies to the active word */
    CHECK_STR(host(r), "Gone. Good");
    rig_type(r, "1");
    CHECK_INT(r->engine.caps, T9_CAPS_OFF);
}

static void test_caps_once_survives_punctuation(void) {
    t9_rig_t *r = fresh_rig();
    rig_type(r, "^ 1 0 2");
    CHECK_STR(host(r), ". A[nd]");
    CHECK_INT(r->engine.caps, T9_CAPS_ONCE);
}

static void test_caps_lock(void) {
    t9_rig_t *r = fresh_rig();
    rig_type(r, "^^ 4663 0 46");
    CHECK_INT(r->engine.caps, T9_CAPS_LOCK);
    CHECK_STR(host(r), "GOOD IN[TO]");
    rig_type(r, "n");
    CHECK_STR(host(r), "GOOD HO[USE]");
    rig_type(r, "^");
    CHECK_STR(host(r), "GOOD ho[use]");
    CHECK_INT(r->engine.caps, T9_CAPS_OFF);
}

static void test_caps_cycle(void) {
    t9_rig_t *r             = fresh_rig();
    const t9_caps_t order[] = {T9_CAPS_ONCE, T9_CAPS_LOCK, T9_CAPS_OFF, T9_CAPS_ONCE};
    for (int i = 0; i < 4; i++) {
        rig_type(r, "^");
        CHECK_INT(r->engine.caps, order[i]);
        CHECK_INT(t9_take_events(&r->engine), T9_EVENT_CAPS_CHANGED);
    }
}

static void test_spell_from_t9(void) {
    t9_rig_t *r = fresh_rig();
    rig_type(r, "2 0 967");
    CHECK_STR(host(r), "a wor[ld]");
    rig_type(r, "2");
    CHECK_INT(t9_take_events(&r->engine), T9_EVENT_MISS);
    rig_type(r, "S");
    CHECK_STR(host(r), "a ");
    CHECK(r->engine.spelling);
    CHECK(!t9_is_composing(&r->engine));
    CHECK_INT(r->engine.mode, T9_MODE_T9);
    rig_type(r, "9999 666 777 22_2 55");
    CHECK_STR(host(r), "a zorbak");
    CHECK_INT(r->engine.compose, T9_COMPOSE_MULTITAP);
    rig_type(r, "0");
    CHECK_STR(host(r), "a zorbak ");
    CHECK(!r->engine.spelling);
    rig_type(r, "4663");
    CHECK_STR(host(r), "a zorbak good");
    CHECK_INT(r->engine.compose, T9_COMPOSE_T9);
}

static void test_spell_needs_hold_in_t9(void) {
    t9_rig_t *r = fresh_rig();
    rig_type(r, "4663 +");
    CHECK_STR(host(r), "good"); /* a tap keeps the word */
    CHECK(!r->engine.spelling);
    rig_hold(r, T9_KEY_SPELL, 499);
    CHECK_STR(host(r), "good");
    CHECK(!r->engine.spelling);

    rig_press(r, T9_KEY_SPELL);
    rig_wait(r, 500); /* fires while still held */
    CHECK_STR(host(r), "");
    CHECK(r->engine.spelling);
    rig_release(r, T9_KEY_SPELL);

    r = fresh_rig();
    rig_type(r, "4663");
    rig_press(r, T9_KEY_SPELL);
    rig_wait(r, 200);
    rig_type(r, "0"); /* another key cancels the pending hold */
    rig_wait(r, 1000);
    rig_release(r, T9_KEY_SPELL);
    CHECK_STR(host(r), "good ");
    CHECK(!r->engine.spelling);
}

static void test_spell_delete_and_cancel(void) {
    t9_rig_t *r = fresh_rig();
    rig_type(r, "4663 S 2<");
    CHECK_STR(host(r), "");
    CHECK(r->engine.spelling); /* still spelling the word */
    rig_type(r, "x");
    CHECK(!r->engine.spelling);
    rig_type(r, "2");
    CHECK_INT(r->engine.compose, T9_COMPOSE_T9);
}

static void test_multitap_cycling_and_timeout(void) {
    t9_rig_t *r = fresh_rig();
    rig_type(r, "m");
    CHECK_INT(r->engine.mode, T9_MODE_MULTITAP);
    rig_type(r, "22");
    CHECK_STR(host(r), "b");
    rig_type(r, "22");
    CHECK_STR(host(r), "a"); /* wraps within abc */

    /* same key: timeout - 1 still cycles, exactly timeout starts a new letter */
    uint32_t pressed = r->now;
    rig_press(r, T9_KEY_3);
    rig_release(r, T9_KEY_3);
    CHECK_STR(host(r), "ad");
    r->now = pressed + 799;
    t9_tick(&r->engine, r->now);
    CHECK(r->engine.mt_pending);
    rig_press(r, T9_KEY_3);
    rig_release(r, T9_KEY_3);
    CHECK_STR(host(r), "ae");
    pressed = r->now;
    r->now  = pressed + 800;
    t9_tick(&r->engine, r->now);
    CHECK(!r->engine.mt_pending);
    rig_press(r, T9_KEY_3);
    rig_release(r, T9_KEY_3);
    CHECK_STR(host(r), "aed");

    /* another key finalizes without a space; timers never type */
    rig_type(r, "4");
    CHECK_STR(host(r), "aedg");
    rig_wait(r, 5000);
    CHECK_STR(host(r), "aedg");
    CHECK(t9_is_composing(&r->engine));
}

static void test_multitap_repeated_letters(void) {
    t9_rig_t *r = fresh_rig();
    rig_type(r, "m 44 33 555_555 666 0");
    CHECK_STR(host(r), "hello ");
}

static void test_spell_key_finishes_multitap_letter(void) {
    t9_rig_t *r = fresh_rig();
    rig_type(r, "m 66+666+66");
    CHECK_STR(host(r), "non");
    CHECK_INT(r->engine.compose, T9_COMPOSE_MULTITAP);
    rig_type(r, "+");
    CHECK_STR(host(r), "non"); /* no pending letter: nothing changes */
    rig_type(r, "0");
    CHECK_STR(host(r), "non ");

    r = fresh_rig();
    rig_type(r, "4663 S 66+66");
    CHECK_STR(host(r), "nn");
    CHECK(r->engine.spelling); /* still spelling the same word */
}

static void test_long_press_digit(void) {
    t9_rig_t *r = fresh_rig();
    rig_type(r, "466");
    rig_hold(r, T9_KEY_3, 500);
    CHECK_STR(host(r), "goo3");
    CHECK(!t9_is_composing(&r->engine));
    rig_wait(r, 1000);
    CHECK_STR(host(r), "goo3");

    rig_type(r, "0");
    rig_hold(r, T9_KEY_2, 600);
    CHECK_STR(host(r), "goo3 2");
    CHECK(!t9_is_composing(&r->engine));

    rig_type(r, "4663 1");
    rig_hold(r, T9_KEY_1, 500); /* cycling punctuation: revert to '.', then '1' */
    CHECK_STR(host(r), "goo3 2good.1");

    rig_type(r, "0 4663");
    rig_hold(r, T9_KEY_1, 500); /* the word stays accepted, the '.' is reverted */
    CHECK_STR(host(r), "goo3 2good.1 good1");
}

static void test_short_hold_is_tap(void) {
    t9_rig_t *r = fresh_rig();
    rig_hold(r, T9_KEY_2, 499);
    CHECK_STR(host(r), "a[nd]");
    CHECK(t9_is_composing(&r->engine));
}

static void test_long_press_multitap_and_caps(void) {
    t9_rig_t *r = fresh_rig();
    rig_type(r, "m 4");
    rig_hold(r, T9_KEY_4, 700); /* the tap cycled g -> h; reverted */
    CHECK_STR(host(r), "g4");
    CHECK(!t9_is_composing(&r->engine));

    rig_type(r, "m m ^ 466"); /* back to T9 */
    CHECK_STR(host(r), "g4Goo[d]");
    rig_hold(r, T9_KEY_3, 500);
    CHECK_STR(host(r), "g4Goo3");
    CHECK_INT(r->engine.caps, T9_CAPS_OFF);
}

static void test_second_key_cancels_long_press(void) {
    t9_rig_t *r = fresh_rig();
    rig_press(r, T9_KEY_2);
    rig_wait(r, 100);
    rig_press(r, T9_KEY_8);
    rig_wait(r, 30);
    rig_release(r, T9_KEY_8);
    rig_wait(r, 1000);
    rig_release(r, T9_KEY_2);
    CHECK_STR(host(r), "bu[t]");
}

static void test_long_press_zero(void) {
    t9_rig_t *r = fresh_rig();
    rig_type(r, "4663");
    rig_hold(r, T9_KEY_0, 500);
    CHECK_STR(host(r), "good0");
    rig_hold(r, T9_KEY_0, 500);
    CHECK_STR(host(r), "good00");
    CHECK(!t9_is_composing(&r->engine));

    r->auto_drain      = false; /* the space is taken back while still queued */
    r->host.backspaces = 0;
    rig_type(r, "2");
    rig_press(r, T9_KEY_0);
    rig_wait(r, 500);
    rig_release(r, T9_KEY_0);
    CHECK_STR(rig_drain(r), "good00a0");
    CHECK_INT(r->host.backspaces, 0);
}

static void test_mode_cycle(void) {
    t9_rig_t *r = fresh_rig();
    rig_type(r, "4663");
    rig_type(r, "m");
    CHECK_STR(host(r), "good");
    CHECK(!t9_is_composing(&r->engine));
    CHECK_INT(r->engine.mode, T9_MODE_MULTITAP);
    CHECK(t9_take_events(&r->engine) & T9_EVENT_MODE_CHANGED);
    rig_type(r, "m");
    CHECK_INT(r->engine.mode, T9_MODE_NUMPAD);
    rig_type(r, "m");
    CHECK_INT(r->engine.mode, T9_MODE_T9);
    CHECK_STR(host(r), "good");
}

static void test_long_press_mode(void) {
    t9_rig_t *r = fresh_rig();
    rig_type(r, "2");
    rig_hold(r, T9_KEY_MODE, 500);
    CHECK_INT(r->engine.mode, T9_MODE_T9);
    CHECK_STR(host(r), "a<NUM>");
    CHECK(!t9_is_composing(&r->engine));
    rig_type(r, "mm");
    rig_hold(r, T9_KEY_MODE, 800);
    CHECK_INT(r->engine.mode, T9_MODE_NUMPAD);
    CHECK_STR(host(r), "a<NUM><NUM>");
}

static void test_numpad_mode(void) {
    t9_rig_t *r = fresh_rig();
    rig_type(r, "mm");
    CHECK_INT(r->engine.mode, T9_MODE_NUMPAD);
    for (int key = 0; key < T9_KEY_COUNT; key++) {
        if (key == T9_KEY_MODE)
            continue;
        CHECK(!t9_key_press(&r->engine, (t9_key_t)key, 0, r->now));
        t9_key_release(&r->engine, (t9_key_t)key, r->now);
    }
    CHECK_INT(t9_output_count(&r->engine.out), 0);
    CHECK(rig_press(r, T9_KEY_MODE));
    CHECK_INT(r->engine.mode, T9_MODE_T9);
}

static void test_dial_navigation_by_mode(void) {
    t9_rig_t *r = fresh_rig();
    CHECK(rig_dial(r, false));
    CHECK(rig_dial(r, true));
    CHECK_STR(host(r), "<WORD_LEFT><WORD_RIGHT>");
    rig_type(r, "m");
    CHECK(rig_dial(r, false));
    CHECK(rig_dial(r, true));
    CHECK_STR(host(r), "<WORD_LEFT><WORD_RIGHT><LEFT><RIGHT>");
    rig_type(r, "m");
    CHECK(!rig_dial(r, false));
    CHECK(!rig_dial(r, true));
    CHECK_INT(t9_output_count(&r->engine.out), 0);
    CHECK_STR(host(r), "<WORD_LEFT><WORD_RIGHT><LEFT><RIGHT>");
    rig_type(r, "m");
    CHECK_INT(r->engine.mode, T9_MODE_T9);
}

static void test_dial_composition(void) {
    t9_rig_t *r = fresh_rig();
    rig_type(r, "4663");
    rig_dial(r, true);
    CHECK_STR(host(r), "home");
    CHECK(t9_is_composing(&r->engine));
    rig_dial(r, false);
    CHECK_STR(host(r), "good");
    rig_type(r, ".");
    rig_dial(r, false);
    CHECK_STR(host(r), "good<WORD_LEFT>");

    rig_type(r, "41");
    rig_dial(r, true);
    CHECK_STR(host(r), "good<WORD_LEFT>i,");
    rig_dial(r, false);
    CHECK_STR(host(r), "good<WORD_LEFT>i.");
    rig_type(r, "m 44");
    rig_dial(r, false); /* accept pending multitap h before moving */
    CHECK_STR(host(r), "good<WORD_LEFT>i.h<LEFT>");
    CHECK(!t9_is_composing(&r->engine));
    rig_type(r, "1");
    rig_dial(r, true); /* punctuation still cycles in multitap */
    CHECK_STR(t9_preview(&r->engine), ",");
    rig_type(r, ".mm S44"); /* temporary spelling in T9 uses T9 word navigation */
    rig_dial(r, true);
    CHECK_STR(host(r), "good<WORD_LEFT>i.h<LEFT>,h<WORD_RIGHT>");
    CHECK(!r->engine.spelling);
    CHECK(!t9_is_composing(&r->engine));
}

static void test_dial_output_order_and_refusal(void) {
    t9_rig_t *r   = fresh_rig();
    r->auto_drain = false;
    rig_type(r, "84m"); /* erase completion and accept th before changing mode */
    rig_dial(r, false);
    rig_type(r, "44");
    CHECK_STR(rig_drain(r), "th<LEFT>h");
    rig_dial(r, true);
    CHECK_STR(rig_drain(r), "th<LEFT>h<RIGHT>");
    CHECK(!t9_is_composing(&r->engine));

    r = fresh_rig();
    rig_type(r, "m44");
    r->auto_drain = false;
    while (t9_output_free(&r->engine.out))
        t9_output_push(&r->engine.out, T9_OUT_KEY, T9_TEST_CODE_TAB);
    t9_take_events(&r->engine);
    CHECK(rig_dial(r, false));
    CHECK_INT(t9_take_events(&r->engine), T9_EVENT_BUSY);
    CHECK_STR(t9_preview(&r->engine), "h");
    CHECK(t9_is_composing(&r->engine));
    t9_output_clear(&r->engine.out);
    rig_dial(r, false);
    CHECK_STR(rig_drain(r), "h<LEFT>");
    CHECK(!t9_is_composing(&r->engine));

    rig_dial(r, true);
    t9_reset(&r->engine); /* no queued navigation reaches a new host */
    CHECK_INT(t9_output_count(&r->engine.out), 0);
    CHECK_STR(rig_drain(r), "h<LEFT>");
}

static void test_other_key_ordering(void) {
    t9_rig_t *r   = fresh_rig();
    r->auto_drain = false;
    rig_type(r, "4663");
    CHECK(rig_tap_code(r, T9_KEY_OTHER, T9_TEST_CODE_ENTER));
    CHECK(!t9_is_composing(&r->engine));
    t9_out_item_t last = r->engine.out.items[(r->engine.out.tail + T9_OUTPUT_QUEUE_LEN - 1) % T9_OUTPUT_QUEUE_LEN];
    CHECK_INT(last.kind, T9_OUT_KEY);
    CHECK_INT(last.value, T9_TEST_CODE_ENTER);
    CHECK_STR(rig_drain(r), "good<ENTER>");

    CHECK(!rig_tap_code(r, T9_KEY_OTHER, T9_TEST_CODE_TAB)); /* nothing pending: host handles it */
    CHECK_STR(host(r), "good<ENTER><TAB>");

    rig_type(r, "0");
    CHECK(rig_tap_code(r, T9_KEY_OTHER, T9_TEST_CODE_TAB)); /* queued behind the space */
    CHECK_STR(rig_drain(r), "good<ENTER><TAB> <TAB>");
}

static void test_miss(void) {
    t9_rig_t *r = fresh_rig();
    rig_type(r, "669");
    t9_take_events(&r->engine);
    rig_type(r, "3");
    CHECK_STR(host(r), "now");
    CHECK_INT(r->engine.digit_len, 3);
    CHECK_INT(t9_take_events(&r->engine), T9_EVENT_MISS);
    CHECK_INT(t9_take_events(&r->engine), 0);
    rig_type(r, "0");
    CHECK_STR(host(r), "now ");
}

static void test_max_word_length(void) {
    t9_rig_t *r = fresh_rig();
    rig_type(r, "353287636237425647274437"); /* electroencephalographies */
    CHECK_INT(r->engine.digit_len, T9_MAX_WORD_LEN);
    CHECK_STR(host(r), "electroencephalographies");
    t9_take_events(&r->engine);
    rig_type(r, "2");
    CHECK_INT(t9_take_events(&r->engine), T9_EVENT_MISS);
    CHECK_INT(r->engine.digit_len, T9_MAX_WORD_LEN);
    CHECK_STR(host(r), "electroencephalographies");

    rig_type(r, "0 m");
    for (int i = 0; i < T9_MAX_WORD_LEN; i++)
        rig_type(r, "2_");
    t9_take_events(&r->engine);
    rig_type(r, "3");
    CHECK_INT(t9_take_events(&r->engine), T9_EVENT_MISS);
    CHECK_INT(r->engine.letter_len, T9_MAX_WORD_LEN);
}

static void test_queue_pressure(void) {
    t9_rig_t *r   = fresh_rig();
    r->auto_drain = false;
    for (int i = 0; i < T9_OUTPUT_QUEUE_LEN - 4; i++)
        rig_type(r, "0");
    CHECK_INT(t9_output_free(&r->engine.out), 4);
    rig_type(r, "4");
    CHECK_STR(t9_preview(&r->engine), "i[n]");
    CHECK_INT(t9_output_free(&r->engine.out), 0);
    t9_take_events(&r->engine);
    rig_type(r, "n"); /* the queued suffix is taken back, so a same-size preview fits */
    CHECK_STR(t9_preview(&r->engine), "i[s]");
    rig_type(r, "6"); /* in[to] needs more room than the queued i[s] */
    CHECK_INT(t9_take_events(&r->engine), T9_EVENT_BUSY);
    CHECK_STR(t9_preview(&r->engine), "i[s]");
    CHECK_INT(r->engine.digit_len, 1);
    CHECK(t9_is_composing(&r->engine));

    rig_drain(r);
    CHECK_INT(r->host.len, T9_OUTPUT_QUEUE_LEN);
    CHECK_STR(r->host.text + T9_OUTPUT_QUEUE_LEN - 4, "i[s]");
    rig_type(r, "6");
    CHECK_STR(rig_drain(r) + T9_OUTPUT_QUEUE_LEN - 4, "in[to]");
}

/* Output queued behind a shown suffix cannot happen on the pad (passthrough
 * keys accept first), so the queue is filled by hand to reach the refusals. */
static void test_full_queue_refuses_accept(void) {
    t9_rig_t *r = fresh_rig();
    rig_type(r, "4663");
    CHECK_STR(host(r), "good");
    r->auto_drain = false;
    while (t9_output_free(&r->engine.out) > 0)
        t9_output_push(&r->engine.out, T9_OUT_KEY, T9_TEST_CODE_TAB);
    rig_type(r, "0"); /* the space does not fit */
    CHECK_INT(t9_take_events(&r->engine), T9_EVENT_BUSY);
    CHECK(t9_is_composing(&r->engine));
    rig_type(r, "."); /* nothing to erase or type: accepted */
    CHECK(!t9_is_composing(&r->engine));

    r = fresh_rig();
    rig_type(r, "84");
    CHECK_STR(host(r), "th[e]");
    r->auto_drain = false;
    while (t9_output_free(&r->engine.out) > 0)
        t9_output_push(&r->engine.out, T9_OUT_KEY, T9_TEST_CODE_TAB);
    rig_type(r, ".");
    CHECK_INT(t9_take_events(&r->engine), T9_EVENT_BUSY);
    rig_type(r, "m");
    CHECK_INT(t9_take_events(&r->engine), T9_EVENT_BUSY);
    CHECK_INT(r->engine.mode, T9_MODE_T9);
    rig_type(r, "e");
    CHECK_INT(t9_take_events(&r->engine), T9_EVENT_BUSY);
    CHECK(!t9_accept(&r->engine));
    CHECK(rig_tap_code(r, T9_KEY_OTHER, T9_TEST_CODE_TAB));
    CHECK_INT(t9_take_events(&r->engine), T9_EVENT_BUSY);
    CHECK(t9_is_composing(&r->engine));
    CHECK_STR(t9_preview(&r->engine), "th[e]");
    CHECK_INT(r->engine.digit_len, 2);
}

static void test_reset(void) {
    t9_rig_t *r   = fresh_rig();
    r->auto_drain = false;
    rig_type(r, "^^ m m m 4663");                 /* caps lock, full mode cycle back to T9 */
    host_drain_some(&r->host, &r->engine.out, 4); /* I, backspace, G, O */
    CHECK_STR(host(r), "GO");
    t9_reset(&r->engine);
    CHECK(!t9_is_composing(&r->engine));
    CHECK_INT(t9_output_count(&r->engine.out), 0);
    CHECK_STR(t9_preview(&r->engine), "");
    CHECK_INT(r->engine.caps, T9_CAPS_LOCK);
    CHECK_INT(r->engine.mode, T9_MODE_T9);

    /* new host: no stale backspaces or characters */
    memset(&r->host, 0, sizeof(r->host));
    rig_type(r, "2");
    CHECK_STR(rig_drain(r), "A[ND]");
    CHECK_INT(r->host.backspaces, 0);

    rig_type(r, "mm");
    CHECK_INT(r->engine.mode, T9_MODE_NUMPAD);
    t9_reset(&r->engine);
    CHECK_INT(r->engine.mode, T9_MODE_NUMPAD);
}

static void test_reset_cancels_long_press(void) {
    t9_rig_t *r = fresh_rig();
    rig_press(r, T9_KEY_4);
    t9_reset(&r->engine);
    rig_wait(r, 600);
    rig_release(r, T9_KEY_4);
    CHECK_STR(host(r), "i[n]");
    CHECK_INT(t9_output_count(&r->engine.out), 0);
}

static void test_output_diffs_against_desired_state(void) {
    t9_rig_t *r   = fresh_rig();
    r->auto_drain = false;
    rig_type(r, "4663");
    /* Intermediate suffixes retract; only the first i needs a backspace. */
    CHECK_INT(t9_output_count(&r->engine.out), 6);
    CHECK_STR(rig_drain(r), "good");
    CHECK_INT(r->host.backspaces, 1);
    rig_type(r, "n");
    CHECK_STR(rig_drain(r), "home");

    rig_type(r, "0 4");
    rig_drain(r);
    rig_type(r, "6pp63"); /* into, home, good, goo, good without draining */
    CHECK_STR(rig_drain(r), "home good");
}

static void test_public_accept(void) {
    t9_rig_t *r = fresh_rig();
    rig_type(r, "^ 4663");
    CHECK(t9_accept(&r->engine));
    CHECK(!t9_is_composing(&r->engine));
    CHECK_INT(r->engine.caps, T9_CAPS_OFF);
    rig_type(r, "4663");
    CHECK_STR(host(r), "Goodgood");
    CHECK(t9_accept(&r->engine));
    CHECK(t9_accept(&r->engine));
    CHECK_STR(rig_drain(r), "Goodgood");
}

/* --- completions off (Fn+Enter) --------------------------------------------- */

/* toggling mid-word re-renders the preview in place and never accepts */
static void test_toggle_completions_mid_word(void) {
    t9_rig_t *r = fresh_rig();
    rig_type(r, "843 0 84");
    CHECK_STR(host(r), "the th[e]");
    rig_type(r, "c");
    CHECK(!r->engine.completions);
    CHECK_STR(host(r), "the th");
    CHECK_STR(t9_preview(&r->engine), "th");
    CHECK(!t9_has_completion(&r->engine));
    CHECK(t9_is_composing(&r->engine));
    CHECK_INT(r->engine.digit_len, 2);
    rig_type(r, "c");
    CHECK(r->engine.completions);
    CHECK_STR(host(r), "the th[e]");
    CHECK(t9_has_completion(&r->engine));

    r = fresh_rig();
    rig_type(r, "4nnnn c"); /* "have" heads the base "h": the same whole word */
    CHECK_STR(host(r), "h");
    rig_type(r, "c");
    CHECK_STR(host(r), "h[ave]");
    rig_type(r, "6nnnn");
    CHECK_STR(host(r), "ho[use]");
    rig_type(r, "c"); /* "house" is not listed, "how" heads the base "ho" */
    CHECK_STR(host(r), "ho");
    rig_type(r, "c");
    CHECK_STR(host(r), "ho[w]");
}

/* Off: exact words first, then distinct remaining bases. */
static void test_completions_off_collapses_bases(void) {
    t9_rig_t *r = fresh_rig();
    rig_type(r, "c 4");
    CHECK_STR(host(r), "i");
    CHECK_INT(r->engine.cands.count, 3); /* i, have, go */
    CHECK_STR(r->engine.cands.words[0], "i");
    rig_type(r, "n");
    CHECK_STR(host(r), "h");
    rig_type(r, "n");
    CHECK_STR(host(r), "g");
    rig_type(r, "n");
    CHECK_STR(host(r), "i");
    rig_type(r, "p");
    CHECK_STR(host(r), "g");
    rig_type(r, "x 46");
    CHECK_INT(r->engine.cands.count, 4); /* in go ho im */
    rig_type(r, "nnn");
    CHECK_STR(host(r), "im");
    rig_type(r, "n");
    CHECK_STR(host(r), "in");
    rig_type(r, "x 843");
    CHECK_INT(r->engine.cands.count, 3); /* the tie vie */
    CHECK_INT(r->engine.cands.total_exact, 3);
    rig_type(r, "n");
    CHECK_STR(host(r), "tie");
}

static t9_rig_t *exact_first_rig(bool queued) {
    /* A popular extension must not boost "medic" ahead of "media". */
    static const t9_simple_entry_t words[] = {
        {"office", 63}, {"official", 62}, {"medication", 60}, {"of", 55}, {"media", 50}, {"medic", 40}, {"off", 30},
    };
    t9_rig_t *r = fresh_rig();
    t9_dict_simple_set(words, sizeof(words) / sizeof(words[0]));
    CHECK(t9_set_completions(&r->engine, false));
    r->auto_drain = !queued;
    return r;
}

static void test_completions_off_exact_priority(void) {
    for (int queued = 0; queued < 2; queued++) {
        t9_rig_t *r = exact_first_rig(queued);
        rig_type(r, "6334");
        CHECK_STR(t9_preview(&r->engine), "offi");
        rig_type(r, "2"); /* an exact match supersedes the previous "office" */
        CHECK_STR(t9_preview(&r->engine), "media");
        CHECK_INT(r->engine.cands.count, 3);
        CHECK_INT(r->engine.cands.total_exact, 2);
        CHECK(!r->engine.cands.prefix_family);
        CHECK(!t9_has_completion(&r->engine));
        CHECK_STR(r->engine.cands.words[0], "media");
        CHECK_STR(r->engine.cands.words[1], "medic");
        CHECK_STR(r->engine.cands.words[2], "office");
        rig_type(r, "n");
        CHECK_STR(t9_preview(&r->engine), "medic");
        rig_type(r, "n");
        CHECK_STR(t9_preview(&r->engine), "offic");
        rig_type(r, "n");
        CHECK_STR(t9_preview(&r->engine), "media");
        rig_type(r, "p");
        CHECK_STR(rig_drain(r), "offic");

        r = exact_first_rig(queued);
        rig_type(r, "6334np2"); /* explicitly selected office stays selected */
        CHECK_STR(t9_preview(&r->engine), "offic");
        rig_type(r, "cc");
        CHECK_STR(t9_preview(&r->engine), "offic");
        rig_type(r, "<");
        CHECK_STR(t9_preview(&r->engine), "offi");
        rig_type(r, "0");
        CHECK_STR(rig_drain(r), "offi ");

        r = exact_first_rig(queued);
        rig_type(r, "63342<"); /* deletion preserves media's base */
        CHECK_STR(t9_preview(&r->engine), "medi");
        rig_type(r, "2");
        CHECK_STR(rig_drain(r), "media");

        r = exact_first_rig(queued);
        rig_type(r, "c63342"); /* suggestions retain their existing ranking */
        CHECK_STR(t9_preview(&r->engine), "offic[e]");
        rig_type(r, "c"); /* a toggle preserves the displayed base */
        CHECK_STR(rig_drain(r), "offic");

        static const struct {
            const char *key, *text;
        } accepts[] = {
            {"0", "Media "},
            {".", "Media"},
            {"e", "Media<ENTER>"},
        };
        for (size_t i = 0; i < sizeof(accepts) / sizeof(accepts[0]); i++) {
            r = exact_first_rig(queued);
            rig_type(r, "^63342");
            rig_type(r, accepts[i].key);
            CHECK_STR(rig_drain(r), accepts[i].text);
            CHECK(!t9_is_composing(&r->engine));
        }
    }
}

static void test_exact_priority_refused_when_queue_full(void) {
    t9_rig_t *r = exact_first_rig(false);
    rig_type(r, "6334");
    r->auto_drain = false;
    while (t9_output_free(&r->engine.out))
        t9_output_push(&r->engine.out, T9_OUT_KEY, T9_TEST_CODE_TAB);
    rig_type(r, "2");
    CHECK_INT(t9_take_events(&r->engine), T9_EVENT_BUSY);
    CHECK_INT(r->engine.digit_len, 4);
    CHECK_STR(t9_preview(&r->engine), "offi");
    CHECK_STR(r->engine.cands.words[r->engine.cand_index], "office");
    rig_drain(r);
    rig_type(r, "2");
    CHECK_INT(r->engine.digit_len, 5);
    CHECK_STR(t9_preview(&r->engine), "media");
}

/* off with ambiguous matches: Enter accepts the base, then the host Enter */
static void test_completions_off_enter_passes_through(void) {
    t9_rig_t *r = fresh_rig();
    rig_type(r, "c 84");
    r->auto_drain = false;
    CHECK(rig_press(r, T9_KEY_COMPLETE));
    t9_out_item_t item;
    CHECK_INT(t9_output_count(&r->engine.out), 1);
    CHECK(t9_output_peek(&r->engine.out, &item));
    CHECK_INT(item.kind, T9_OUT_KEY);
    CHECK_INT(item.value, T9_TEST_CODE_ENTER);
    CHECK_STR(rig_drain(r), "th<ENTER>");
    CHECK(!t9_is_composing(&r->engine));
    rig_release(r, T9_KEY_COMPLETE);
    CHECK(!rig_press(r, T9_KEY_COMPLETE)); /* idle: the host gets Enter */
    CHECK_STR(host(r), "th<ENTER><ENTER>");
}

static void test_completions_off_extend_and_delete(void) {
    t9_rig_t *r = fresh_rig();
    rig_type(r, "c 2");
    CHECK_STR(host(r), "a");
    rig_type(r, "6");
    CHECK_STR(host(r), "an");
    rig_type(r, "3");
    CHECK_STR(host(r), "and");
    rig_type(r, "<");
    CHECK_STR(host(r), "an");
    rig_type(r, "<");
    CHECK_STR(host(r), "a");
    rig_type(r, "x 4n");
    CHECK_STR(host(r), "h");
    rig_type(r, "6"); /* cycled: the base "h" sticks */
    CHECK_STR(host(r), "ho");
    rig_type(r, "6");
    CHECK_STR(host(r), "hom");
    rig_type(r, "<");
    CHECK_STR(host(r), "ho");
    rig_type(r, "0");
    CHECK_STR(host(r), "ho ");
}

static void test_toggle_completions_idle_emits_nothing(void) {
    t9_rig_t *r = fresh_rig();
    t9_init(&r->engine, t9_dict_simple_lookup, NULL);
    CHECK(!r->engine.completions); /* the default */
    rig_type(r, "84");
    CHECK_STR(host(r), "th");
    rig_type(r, "x c");
    rig_type(r, "843 0");
    CHECK(t9_set_completions(&r->engine, false));
    CHECK(!r->engine.completions);
    CHECK_INT(t9_output_count(&r->engine.out), 0);
    CHECK(t9_set_completions(&r->engine, true));
    CHECK_INT(t9_output_count(&r->engine.out), 0);
    rig_type(r, "m 44 c"); /* a multitap word has no completions to re-render */
    CHECK_INT(t9_output_count(&r->engine.out), 0);
    CHECK_STR(host(r), "the h");
    CHECK(!r->engine.completions);
    rig_type(r, "mm c");
    CHECK_STR(host(r), "the h");
    CHECK_INT(t9_take_events(&r->engine), T9_EVENT_MODE_CHANGED);
}

static void test_toggle_completions_refused_when_queue_full(void) {
    t9_rig_t *r = fresh_rig();
    rig_type(r, "84");
    r->auto_drain = false;
    while (t9_output_free(&r->engine.out) > 0)
        t9_output_push(&r->engine.out, T9_OUT_KEY, T9_TEST_CODE_TAB);
    CHECK(!t9_set_completions(&r->engine, false));
    CHECK_INT(t9_take_events(&r->engine), T9_EVENT_BUSY);
    CHECK(r->engine.completions);
    CHECK_STR(t9_preview(&r->engine), "th[e]");
    CHECK(t9_is_composing(&r->engine));
    rig_drain(r); /* with room again the suffix is erased */
    CHECK(t9_set_completions(&r->engine, false));
    CHECK_STR(t9_preview(&r->engine), "th");
    CHECK_INT(t9_output_count(&r->engine.out), 3);
}

static void test_completions_off_caps(void) {
    t9_rig_t *r = fresh_rig();
    rig_type(r, "c ^ 84");
    CHECK_STR(host(r), "Th");
    rig_type(r, "c");
    CHECK_STR(host(r), "Th[e]");
    rig_type(r, "^ c");
    CHECK_STR(host(r), "TH");
    rig_type(r, "c");
    CHECK_STR(host(r), "TH[E]");
    rig_type(r, "c 3 0");
    CHECK_STR(host(r), "THE ");
}

static t9_rig_t *unique_rig(bool queued) {
    static const t9_simple_entry_t words[] = {{"goodbye", 63}, {"good", 62}, {"book", 61}};
    t9_rig_t *r                            = fresh_rig();
    t9_dict_simple_set(words, sizeof(words) / sizeof(words[0]));
    CHECK(t9_set_completions(&r->engine, false));
    r->auto_drain = !queued;
    return r;
}

static void test_unique_completion_transitions(void) {
    for (int queued = 0; queued < 2; queued++) {
        t9_rig_t *r = unique_rig(queued);
        rig_type(r, "466");
        CHECK_STR(t9_preview(&r->engine), "goo>d");
        CHECK_INT(r->engine.cands.count, 1); /* good -> goodbye is one family */
        CHECK(t9_has_completion(&r->engine));
        rig_type(r, "3");
        CHECK_STR(t9_preview(&r->engine), "good>bye");
        CHECK(t9_has_completion(&r->engine));
        rig_type(r, "2");
        CHECK_STR(t9_preview(&r->engine), "goodb>ye");
        CHECK(t9_has_completion(&r->engine));
        rig_type(r, "np"); /* cycling a sole match leaves it alone */
        CHECK_STR(t9_preview(&r->engine), "goodb>ye");
        rig_type(r, "9");
        CHECK_STR(t9_preview(&r->engine), "goodby>e");
        rig_type(r, "<");
        CHECK_STR(t9_preview(&r->engine), "goodb>ye");
        rig_type(r, "<");
        CHECK_STR(t9_preview(&r->engine), "good>bye");
        CHECK(t9_has_completion(&r->engine));
        rig_type(r, "293");
        CHECK_STR(t9_preview(&r->engine), "goodbye");
        CHECK(!t9_has_completion(&r->engine));
        rig_type(r, "e");
        CHECK_STR(rig_drain(r), "goodbye<ENTER>");

        r = unique_rig(queued);
        rig_type(r, "46632 ^");
        CHECK_STR(t9_preview(&r->engine), "Goodb>ye");
        rig_type(r, "c");
        CHECK_STR(t9_preview(&r->engine), "Goodb[ye]");
        rig_type(r, "^ c");
        CHECK_STR(t9_preview(&r->engine), "GOODB>YE");
        rig_type(r, "e");
        CHECK_STR(rig_drain(r), "GOODBYE ");
        CHECK(!t9_is_composing(&r->engine));
    }
}

static void test_unique_completion_acceptance(void) {
    static const struct {
        const char *keys, *text;
    } cases[] = {
        {"e", "goodbye "},   {"0", "goodb "}, {".", "goodb"}, {"1.", "goodb."},
        {"t", "goodb<TAB>"}, {"m", "goodb"},  {"x", ""},      {"S", ""},
    };
    for (int queued = 0; queued < 2; queued++) {
        for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
            t9_rig_t *r = unique_rig(queued);
            rig_type(r, "46632");
            CHECK_STR(t9_preview(&r->engine), "goodb>ye");
            rig_type(r, cases[i].keys);
            CHECK_STR(rig_drain(r), cases[i].text);
        }
        t9_rig_t *r = unique_rig(queued);
        rig_type(r, "46632");
        CHECK(t9_accept(&r->engine));
        CHECK_STR(rig_drain(r), "goodb");

        r = unique_rig(queued);
        rig_type(r, "46632");
        rig_hold(r, T9_KEY_9, 500);
        CHECK_STR(rig_drain(r), "goodb9");

        r             = fresh_rig();
        r->auto_drain = !queued;
        rig_type(r, "c418"); /* contractions also have a unique suffix */
        CHECK_STR(t9_preview(&r->engine), "i'v>e");
        rig_type(r, "<");
        CHECK_STR(t9_preview(&r->engine), "i.");
        rig_type(r, "8e");
        CHECK_STR(rig_drain(r), "i've ");
    }
}

static void test_unique_completion_queue_refusal(void) {
    t9_rig_t *r = unique_rig(false);
    rig_type(r, "4663");
    r->auto_drain = false;
    while (t9_output_free(&r->engine.out))
        t9_output_push(&r->engine.out, T9_OUT_KEY, T9_TEST_CODE_TAB);
    rig_type(r, "2");
    CHECK_INT(t9_take_events(&r->engine), T9_EVENT_BUSY);
    CHECK_STR(t9_preview(&r->engine), "good>bye");
    CHECK_INT(r->engine.digit_len, 4);
    CHECK(t9_has_completion(&r->engine));
    t9_output_clear(&r->engine.out); /* remove the test's queue filler */
    rig_type(r, "2");
    CHECK_STR(rig_drain(r), "goodb>ye");
    while (t9_output_free(&r->engine.out))
        t9_output_push(&r->engine.out, T9_OUT_KEY, T9_TEST_CODE_TAB);
    rig_type(r, "e0.<");
    CHECK(!t9_set_completions(&r->engine, true));
    CHECK_INT(t9_take_events(&r->engine), T9_EVENT_BUSY);
    CHECK_STR(t9_preview(&r->engine), "goodb>ye");
    CHECK_INT(r->engine.digit_len, 5);
    CHECK(!r->engine.completions);
    CHECK(t9_has_completion(&r->engine));
    t9_output_clear(&r->engine.out);
    rig_type(r, "e");
    CHECK_STR(rig_drain(r), "goodbye ");
}

static t9_rig_t *family_rig(bool queued) {
    /* Deliberately rank the longest highest: > must still offer shortest first. */
    static const t9_simple_entry_t words[] = {{"canes", 63}, {"cane", 40}, {"can", 20}};
    t9_rig_t *r                            = fresh_rig();
    t9_dict_simple_set(words, sizeof(words) / sizeof(words[0]));
    CHECK(t9_set_completions(&r->engine, false));
    r->auto_drain = !queued;
    return r;
}

static void test_family_completion_steps(void) {
    for (int queued = 0; queued < 2; queued++) {
        t9_rig_t *r = family_rig(queued);
        rig_type(r, "22");
        CHECK_STR(t9_preview(&r->engine), "ca>n");
        rig_type(r, "e");
        CHECK_STR(t9_preview(&r->engine), "can>e");
        CHECK_INT(r->engine.digit_len, 3);
        CHECK_INT(r->engine.digits[2], 6); /* Enter inserts real T9 digits */
        CHECK(t9_is_composing(&r->engine));
        rig_type(r, "e");
        CHECK_STR(t9_preview(&r->engine), "cane>s");
        CHECK_INT(r->engine.digit_len, 4);
        CHECK_INT(r->engine.digits[3], 3);
        CHECK(t9_has_completion(&r->engine));
        rig_type(r, "e");
        CHECK_STR(rig_drain(r), "canes ");
        CHECK(!t9_is_composing(&r->engine));

        static const struct {
            const char *keys, *text;
        } accepts[] = {
            {"0", "ca "},    {"e0", "can "},     {"ee0", "cane "}, {"e.", "can"}, {"ee.", "cane"},
            {"e1.", "can."}, {"et", "can<TAB>"}, {"em", "can"},    {"ex", ""},
        };
        for (size_t i = 0; i < sizeof(accepts) / sizeof(accepts[0]); i++) {
            r = family_rig(queued);
            rig_type(r, "22");
            rig_type(r, accepts[i].keys);
            CHECK_STR(rig_drain(r), accepts[i].text);
            CHECK(!t9_is_composing(&r->engine));
        }

        r = family_rig(queued);
        rig_type(r, "22ee<");
        CHECK_STR(t9_preview(&r->engine), "can>e");
        rig_type(r, "<");
        CHECK_STR(t9_preview(&r->engine), "ca>n");
        rig_type(r, "e3"); /* continue typing after Enter */
        CHECK_STR(t9_preview(&r->engine), "cane>s");
        rig_type(r, "70");
        CHECK_STR(rig_drain(r), "canes ");

        r = family_rig(queued);
        rig_type(r, "^22e");
        CHECK_STR(t9_preview(&r->engine), "Can>e");
        CHECK_INT(r->engine.caps, T9_CAPS_ONCE);
        rig_type(r, "c");
        CHECK_STR(t9_preview(&r->engine), "Can[e]"); /* toggling retains cane */
        rig_type(r, "c^e");
        CHECK_STR(t9_preview(&r->engine), "CANE>S");
        rig_type(r, "0");
        CHECK_STR(rig_drain(r), "CANE ");

        r = family_rig(queued);
        rig_type(r, "c22e"); /* suggestions still finish their whole word */
        CHECK_STR(rig_drain(r), "canes ");

        r = family_rig(queued);
        rig_type(r, "22e");
        CHECK(t9_accept(&r->engine));
        CHECK_STR(rig_drain(r), "can");

        r = family_rig(queued);
        rig_type(r, "22e");
        rig_hold(r, T9_KEY_3, 500);
        CHECK_STR(rig_drain(r), "can3");
    }
}

static void test_family_completion_requires_one_chain(void) {
    static const t9_simple_entry_t branches[] = {
        {"good", 63},
        {"goodbye", 60},
        {"goodness", 55},
    };
    t9_rig_t *r = fresh_rig();
    t9_dict_simple_set(branches, sizeof(branches) / sizeof(branches[0]));
    rig_type(r, "c466");
    CHECK_STR(t9_preview(&r->engine), "goo");
    CHECK_INT(r->engine.cands.count, 1); /* collapsing a shared base isn't enough */
    CHECK(!r->engine.cands.prefix_family);
    CHECK(!t9_has_completion(&r->engine));

    /* Sixteen matching extensions fill the lookup; a hidden seventeenth can
     * branch even if every returned word is a prefix of the longest. */
    static char text[T9_MAX_COMPLETIONS + 1][T9_MAX_WORD_LEN + 1];
    static t9_simple_entry_t words[T9_MAX_COMPLETIONS + 1];
    for (unsigned i = 0; i < T9_MAX_COMPLETIONS; i++) {
        memset(text[i], 'a', i + 2);
        text[i][i + 2] = 0;
        words[i]       = (t9_simple_entry_t){text[i], 60};
    }
    strcpy(text[T9_MAX_COMPLETIONS], "bbb");
    words[T9_MAX_COMPLETIONS] = (t9_simple_entry_t){text[T9_MAX_COMPLETIONS], 1};
    r                         = fresh_rig();
    t9_dict_simple_set(words, T9_MAX_COMPLETIONS + 1);
    rig_type(r, "c2");
    CHECK_STR(t9_preview(&r->engine), "a");
    CHECK_INT(r->engine.cands.count, 1);
    CHECK(!r->engine.cands.prefix_family);
    CHECK(!t9_has_completion(&r->engine));
}

static void test_family_completion_queue_refusal(void) {
    t9_rig_t *r = family_rig(false);
    rig_type(r, "22");
    r->auto_drain = false;
    while (t9_output_free(&r->engine.out))
        t9_output_push(&r->engine.out, T9_OUT_KEY, T9_TEST_CODE_TAB);
    rig_type(r, "e");
    CHECK_INT(t9_take_events(&r->engine), T9_EVENT_BUSY);
    CHECK_STR(t9_preview(&r->engine), "ca>n");
    CHECK_INT(r->engine.digit_len, 2);
    CHECK_STR(r->engine.cands.words[0], "can");
    t9_output_clear(&r->engine.out);
    rig_type(r, "e");
    CHECK_STR(rig_drain(r), "can>e");
    CHECK_INT(r->engine.digit_len, 3);
    rig_type(r, "0");
    CHECK_STR(rig_drain(r), "can ");
}

static void test_wrapper_chord_cycles(void) {
    const uint32_t delays[] = {0, 499, 500, 700};
    for (int mode = T9_MODE_T9; mode <= T9_MODE_MULTITAP; mode++) {
        for (int queued = 0; queued < 2; queued++) {
            for (size_t d = 0; d < sizeof(delays) / sizeof(delays[0]); d++) {
                t9_rig_t *r    = fresh_rig();
                r->engine.mode = (t9_mode_t)mode;
                r->auto_drain  = !queued;
                rig_press(r, T9_KEY_1);
                rig_wait(r, delays[d]);
                if (delays[d] >= 500)
                    CHECK_STR(rig_drain(r), "1");
                for (int side = 0; side < 2; side++) {
                    const char *cycle = side ? ")]}>)" : "([{<(";
                    for (int i = 0; i < 5; i++) {
                        rig_tap(r, side ? T9_KEY_3 : T9_KEY_2);
                        char expected[] = {cycle[i], 0};
                        CHECK_STR(t9_preview(&r->engine), expected);
                        CHECK(r->engine.held);
                        CHECK_INT(r->engine.held_key, T9_KEY_1);
                        CHECK_INT(r->engine.compose, T9_COMPOSE_PUNCT);
                    }
                }
                rig_wait(r, 1000);
                CHECK_STR(rig_drain(r), ")");
                rig_release(r, T9_KEY_1);
                CHECK(!t9_is_composing(&r->engine));
                CHECK(!r->engine.held);
                rig_wait(r, 1000);
                CHECK_STR(rig_drain(r), ")");
            }
        }
    }
}

static void test_wrapper_chord_preserves_preceding_text(void) {
    static const struct {
        const char *keys, *text;
    } cases[] = {
        {"84", "th["},    {"4", "i["},  {"1", ".["},   {"41", "i.["},
        {"c418", "i'v["}, {"^4", "I["}, {"m44", "h["}, {"S44", "h["},
    };
    for (int queued = 0; queued < 2; queued++) {
        for (int fired = 0; fired < 2; fired++) {
            for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
                t9_rig_t *r   = fresh_rig();
                r->auto_drain = !queued;
                rig_type(r, cases[i].keys);
                rig_press(r, T9_KEY_1);
                if (fired)
                    rig_wait(r, 500);
                rig_type(r, "22");
                rig_release(r, T9_KEY_1);
                CHECK_STR(rig_drain(r), cases[i].text);
                CHECK(!r->engine.spelling);
                CHECK_INT(r->engine.caps, T9_CAPS_OFF);
            }
        }
    }
    t9_rig_t *r = fresh_rig();
    rig_type(r, "^");
    rig_press(r, T9_KEY_1);
    rig_type(r, "2");
    rig_release(r, T9_KEY_1);
    CHECK_INT(r->engine.caps, T9_CAPS_ONCE); /* punctuation does not use caps once */
    rig_type(r, "843.");
    rig_press(r, T9_KEY_1);
    rig_type(r, "3");
    rig_release(r, T9_KEY_1);
    CHECK_STR(host(r), "(The)");
}

static void test_wrapper_chord_interruptions(void) {
    static const struct {
        const char *keys, *text;
    } cases[] = {
        {"x", ""}, {"<", ""}, {"0", "( "}, {".", "("}, {"t", "(<TAB>"}, {"e", "(<ENTER>"}, {"m", "("}, {"843.", "(the"},
    };
    for (int queued = 0; queued < 2; queued++) {
        for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
            t9_rig_t *r   = fresh_rig();
            r->auto_drain = !queued;
            rig_press(r, T9_KEY_1);
            rig_type(r, "2");
            rig_type(r, cases[i].keys);
            rig_wait(r, 1000);
            rig_release(r, T9_KEY_1);
            CHECK_STR(rig_drain(r), cases[i].text);
        }
    }

    t9_rig_t *r = fresh_rig();
    rig_press(r, T9_KEY_1);
    rig_press(r, T9_KEY_2);
    rig_release(r, T9_KEY_1); /* prefix released before its chord key */
    CHECK(!t9_is_composing(&r->engine));
    rig_press(r, T9_KEY_1);
    rig_release(r, T9_KEY_2); /* late release must not cancel the new 1 hold */
    rig_wait(r, 500);
    rig_release(r, T9_KEY_1);
    CHECK_STR(host(r), "(1");

    r = fresh_rig();
    rig_press(r, T9_KEY_1);
    rig_type(r, "2");
    t9_reset(&r->engine);
    rig_release(r, T9_KEY_1);
    rig_wait(r, 1000);
    CHECK_INT(t9_output_count(&r->engine.out), 0);
    CHECK(!t9_is_composing(&r->engine));
    rig_type(r, "1");
    CHECK_STR(host(r), "(."); /* reset left the old host untouched */
}

static void test_wrapper_chord_queue_refusal(void) {
    for (int fired = 0; fired < 2; fired++) {
        t9_rig_t *r = fresh_rig();
        rig_type(r, "^4");
        rig_press(r, T9_KEY_1);
        if (fired)
            rig_wait(r, 500);
        char before[T9_SHOWN_MAX + 1];
        strcpy(before, t9_preview(&r->engine));
        r->auto_drain = false;
        while (t9_output_free(&r->engine.out))
            t9_output_push(&r->engine.out, T9_OUT_KEY, T9_TEST_CODE_TAB);
        rig_type(r, "2");
        CHECK_INT(t9_take_events(&r->engine) & T9_EVENT_BUSY, T9_EVENT_BUSY);
        CHECK_STR(t9_preview(&r->engine), before);
        CHECK_INT(r->engine.punct_group, T9_PUNCT_NORMAL);
        CHECK_INT(r->engine.held_fired, fired);
        t9_output_clear(&r->engine.out); /* remove only the test's queue filler */
        rig_type(r, "2");
        CHECK_STR(rig_drain(r), "I(");
        CHECK_INT(r->engine.caps, T9_CAPS_OFF);
        t9_take_events(&r->engine);
        while (t9_output_free(&r->engine.out))
            t9_output_push(&r->engine.out, T9_OUT_KEY, T9_TEST_CODE_TAB);
        rig_type(r, "23"); /* refusing a cycle or side switch keeps the wrapper */
        CHECK_INT(t9_take_events(&r->engine), T9_EVENT_BUSY);
        CHECK_STR(t9_preview(&r->engine), "(");
        CHECK_INT(r->engine.punct_group, T9_PUNCT_OPEN);
        t9_output_clear(&r->engine.out);
        rig_type(r, "3");
        rig_release(r, T9_KEY_1);
        CHECK_STR(rig_drain(r), "I)");
    }
}

static void test_symbol_typing_chords(void) {
    const char *cycles[] = {":;|~:", "+`@\\+"};
    for (int mode = T9_MODE_T9; mode <= T9_MODE_MULTITAP; mode++) {
        for (int queued = 0; queued < 2; queued++) {
            for (int fired = 0; fired < 2; fired++) {
                for (int group = 0; group < 2; group++) {
                    t9_rig_t *r    = fresh_rig();
                    r->engine.mode = (t9_mode_t)mode;
                    r->auto_drain  = !queued;
                    rig_press(r, T9_KEY_1);
                    if (fired)
                        rig_wait(r, 500);
                    for (int i = 0; i < 5; i++) {
                        rig_tap(r, group ? T9_KEY_5 : T9_KEY_4);
                        char expected[] = {cycles[group][i], 0};
                        CHECK_STR(t9_preview(&r->engine), expected);
                        CHECK(r->engine.held);
                        CHECK_INT(r->engine.held_key, T9_KEY_1);
                    }
                    rig_type(r, "245"); /* changing groups replaces the same symbol */
                    CHECK_STR(t9_preview(&r->engine), "+");
                    rig_release(r, T9_KEY_1);
                    rig_wait(r, 1000);
                    CHECK_STR(rig_drain(r), "+");
                    CHECK(!t9_is_composing(&r->engine));
                }
            }
        }
    }
}

int main(void) {
    RUN_TEST(test_symbol_typing_chords);
    RUN_TEST(test_wrapper_chord_cycles);
    RUN_TEST(test_wrapper_chord_preserves_preceding_text);
    RUN_TEST(test_wrapper_chord_interruptions);
    RUN_TEST(test_wrapper_chord_queue_refusal);
    RUN_TEST(test_unique_completion_transitions);
    RUN_TEST(test_unique_completion_acceptance);
    RUN_TEST(test_unique_completion_queue_refusal);
    RUN_TEST(test_family_completion_steps);
    RUN_TEST(test_family_completion_requires_one_chain);
    RUN_TEST(test_family_completion_queue_refusal);
    RUN_TEST(test_word_shows_top_candidate);
    RUN_TEST(test_longer_words_replace_exact_recommendations);
    RUN_TEST(test_exact_only_and_empty_candidates);
    RUN_TEST(test_reaching_word_end_prefers_its_extension);
    RUN_TEST(test_cycle_wraps);
    RUN_TEST(test_cycle_shows_completions);
    RUN_TEST(test_brackets_follow_digits_delete_and_caps);
    RUN_TEST(test_selection_keeps_word_then_base);
    RUN_TEST(test_complete_key);
    RUN_TEST(test_accept_keeps_base);
    RUN_TEST(test_delete_to_empty);
    RUN_TEST(test_delete_without_composition_queues_behind_output);
    RUN_TEST(test_delete_multitap_and_punct);
    RUN_TEST(test_cancel_erases_only_preview);
    RUN_TEST(test_accept_zero_and_dot);
    RUN_TEST(test_punctuation);
    RUN_TEST(test_contraction);
    RUN_TEST(test_inline_contractions);
    RUN_TEST(test_contraction_preview_and_delete);
    RUN_TEST(test_contraction_resolves_base_ambiguity);
    RUN_TEST(test_contraction_caps_and_fallback);
    RUN_TEST(test_contraction_completion_and_toggle);
    RUN_TEST(test_contraction_long_press_and_boundaries);
    RUN_TEST(test_contraction_queue_refusal);
    RUN_TEST(test_caps_once);
    RUN_TEST(test_caps_once_survives_punctuation);
    RUN_TEST(test_caps_lock);
    RUN_TEST(test_caps_cycle);
    RUN_TEST(test_spell_from_t9);
    RUN_TEST(test_spell_needs_hold_in_t9);
    RUN_TEST(test_spell_delete_and_cancel);
    RUN_TEST(test_spell_key_finishes_multitap_letter);
    RUN_TEST(test_multitap_cycling_and_timeout);
    RUN_TEST(test_multitap_repeated_letters);
    RUN_TEST(test_long_press_digit);
    RUN_TEST(test_short_hold_is_tap);
    RUN_TEST(test_long_press_multitap_and_caps);
    RUN_TEST(test_second_key_cancels_long_press);
    RUN_TEST(test_long_press_zero);
    RUN_TEST(test_mode_cycle);
    RUN_TEST(test_long_press_mode);
    RUN_TEST(test_numpad_mode);
    RUN_TEST(test_dial_navigation_by_mode);
    RUN_TEST(test_dial_composition);
    RUN_TEST(test_dial_output_order_and_refusal);
    RUN_TEST(test_other_key_ordering);
    RUN_TEST(test_miss);
    RUN_TEST(test_max_word_length);
    RUN_TEST(test_queue_pressure);
    RUN_TEST(test_full_queue_refuses_accept);
    RUN_TEST(test_reset);
    RUN_TEST(test_reset_cancels_long_press);
    RUN_TEST(test_output_diffs_against_desired_state);
    RUN_TEST(test_public_accept);
    RUN_TEST(test_toggle_completions_mid_word);
    RUN_TEST(test_completions_off_collapses_bases);
    RUN_TEST(test_completions_off_exact_priority);
    RUN_TEST(test_exact_priority_refused_when_queue_full);
    RUN_TEST(test_completions_off_enter_passes_through);
    RUN_TEST(test_completions_off_extend_and_delete);
    RUN_TEST(test_toggle_completions_idle_emits_nothing);
    RUN_TEST(test_toggle_completions_refused_when_queue_full);
    RUN_TEST(test_completions_off_caps);
    return t9_test_summary("t9_engine_test");
}
