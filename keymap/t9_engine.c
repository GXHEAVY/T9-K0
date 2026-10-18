/* T9-K0: T9 / multitap composition engine, see t9_engine.h for the
 * behaviour contract. Every operation first builds the text it wants after the
 * last untouched host character, then emits the edit only when the whole edit
 * fits the output queue, and only then updates the composition state.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <string.h>
#include "t9_engine.h"
#include "t9_contractions.h"

/* committed base + one extra character + new preview with its suffix */
#define TAIL_MAX (T9_MAX_WORD_LEN + 2 + T9_SHOWN_MAX)

static const t9_config_t default_config = {.multitap_timeout_ms = 800, .long_press_ms = 500};

static bool time_reached(uint32_t now, uint32_t at) {
    return (int32_t)(now - at) >= 0;
}

static bool is_word(t9_compose_t compose) {
    return compose == T9_COMPOSE_T9 || compose == T9_COMPOSE_MULTITAP || compose == T9_COMPOSE_JOIN;
}

static char to_upper(char c) {
    return (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
}

static uint8_t render_word(t9_caps_t caps, const char *lower, uint8_t len, char *out) {
    for (uint8_t i = 0; i < len; i++)
        out[i] = (caps == T9_CAPS_LOCK || (caps == T9_CAPS_ONCE && i == 0)) ? to_upper(lower[i]) : lower[i];
    out[len] = 0;
    return len;
}

/* `word` split at `base`: the rest in brackets (suggestions) or after > (one
 * word or a chain of word prefixes). Caps once uppercases the first letter. */
static uint8_t render_preview(t9_caps_t caps, const char *word, uint8_t base, bool brackets, char *out) {
    uint8_t len = (uint8_t)strlen(word);
    uint8_t n   = render_word(caps, word, base, out);
    if (len == base)
        return n;
    out[n++] = brackets ? T9_MARKERS[0] : '>';
    for (uint8_t i = base; i < len; i++)
        out[n++] = caps == T9_CAPS_LOCK ? to_upper(word[i]) : word[i];
    if (brackets)
        out[n++] = T9_MARKERS[1];
    out[n] = 0;
    return n;
}

static const char *current_candidate(const t9_engine_t *e) {
    return e->cands.words[e->cand_index];
}

/* leading characters of `shown` that accepting keeps */
static uint8_t base_len(const t9_engine_t *e) {
    switch (e->compose) {
        case T9_COMPOSE_T9:
            return e->digit_len;
        case T9_COMPOSE_MULTITAP:
            return e->letter_len;
        case T9_COMPOSE_PUNCT:
            return 1;
        case T9_COMPOSE_JOIN:
            return (uint8_t)(e->digit_len + 1);
        default:
            return 0;
    }
}

static bool has_completion(const t9_engine_t *e) {
    return e->compose == T9_COMPOSE_T9 && (e->completions || e->cands.prefix_family) &&
           current_candidate(e)[e->digit_len] != 0;
}

/* With completions off, > offers the next complete word in a prefix family. */
static uint8_t render_t9(const t9_engine_t *e, t9_caps_t caps, const t9_candidates_t *c, uint8_t index, uint8_t base,
                         char *out) {
    const char *word = c->words[index];
    return e->completions || c->prefix_family ? render_preview(caps, word, base, e->completions, out)
                                              : render_word(caps, word, base, out);
}

static void set_shown(t9_engine_t *e, const char *text, uint8_t len) {
    memmove(e->shown, text, len);
    e->shown[len] = 0;
    e->shown_len  = len;
}

/* Replace `old` (host text after the last character the edit keeps) with
 * `new`, then optionally tap a key. Queued trailing characters are taken back
 * instead of backspaced. Refused without any output when it does not fit. */
static bool emit_edit(t9_engine_t *e, const char *old, uint8_t old_len, const char *new, uint8_t new_len, bool with_key,
                      uint16_t key) {
    uint8_t common = 0;
    while (common < old_len && common < new_len && old[common] == new[common])
        common++;
    uint8_t erase       = (uint8_t)(old_len - common);
    uint8_t retractable = t9_output_retractable(&e->out);
    uint8_t retract     = erase < retractable ? erase : retractable;
    unsigned need       = (unsigned)(erase - retract) + (unsigned)(new_len - common) + (with_key ? 1u : 0u);
    if (need > (unsigned)t9_output_free(&e->out) + retract) {
        e->events |= T9_EVENT_BUSY;
        return false;
    }
    for (uint8_t i = 0; i < retract; i++)
        t9_output_retract(&e->out);
    for (uint8_t i = retract; i < erase; i++)
        t9_output_push(&e->out, T9_OUT_BACKSPACE, 0);
    for (uint8_t i = common; i < new_len; i++)
        t9_output_push(&e->out, T9_OUT_CHAR, (uint8_t)new[i]);
    if (with_key)
        t9_output_push(&e->out, T9_OUT_KEY, key);
    return true;
}

static bool replace_preview(t9_engine_t *e, const char *text, uint8_t len) {
    return emit_edit(e, e->shown, e->shown_len, text, len, false, 0);
}

static bool miss(t9_engine_t *e) {
    e->events |= T9_EVENT_MISS;
    return true;
}

static void end_composition(t9_engine_t *e) {
    e->compose       = T9_COMPOSE_NONE;
    e->digit_len     = 0;
    e->letter_len    = 0;
    e->join_pos      = 0;
    e->punct_group   = T9_PUNCT_NORMAL;
    e->mt_pending    = false;
    e->cand_index    = 0;
    e->user_selected = false;
    set_shown(e, "", 0);
}

static void consume_caps_once(t9_engine_t *e) {
    if (e->caps != T9_CAPS_ONCE)
        return;
    e->caps = T9_CAPS_OFF;
    e->events |= T9_EVENT_CAPS_CHANGED;
}

/* The base of the preview stays on the host as committed text. Callers with a
 * suffix shown must have replaced the preview with the base first. */
static void accept_composition(t9_engine_t *e) {
    if (is_word(e->compose))
        consume_caps_once(e);
    e->held_committed_len = base_len(e);
    e->spelling           = false;
    end_composition(e);
}

/* Replace the preview with its base plus `extra` (0: none), optionally tap
 * `key`, and accept. Refused as a whole when the edit does not fit. */
static bool commit_base(t9_engine_t *e, char extra, bool with_key, uint16_t key) {
    char tail[T9_MAX_WORD_LEN + 2];
    uint8_t len = base_len(e);
    memcpy(tail, e->shown, len);
    if (extra)
        tail[len++] = extra;
    if (!emit_edit(e, e->shown, e->shown_len, tail, len, with_key, key))
        return false;
    accept_composition(e);
    return true;
}

static bool erase_preview(t9_engine_t *e) {
    if (!replace_preview(e, "", 0))
        return false;
    end_composition(e);
    return true;
}

/* Keeps the first (preferred) entry of each group sharing the first `base`
 * letters, so cycling with completions off shows distinct bases. */
static void collapse_bases(t9_candidates_t *c, uint8_t base) {
    uint8_t kept = 0;
    for (uint8_t i = 0; i < c->count; i++) {
        bool seen = false;
        for (uint8_t j = 0; j < kept && !seen; j++)
            seen = strncmp(c->words[j], c->words[i], base) == 0;
        if (!seen)
            memmove(c->words[kept++], c->words[i], sizeof(c->words[i]));
        else if (c->words[i][base] == 0)
            c->total_exact--;
    }
    c->count = kept;
}

/* All words must be prefixes of the longest, not just share the typed base.
 * Return the shortest word beyond the base, or the longest if already complete.
 * A full lookup table may hide alternatives, so conservatively reject it. */
static uint8_t family_candidate(const t9_candidates_t *c, uint8_t base) {
    if (!c->count || c->count - c->total_exact >= T9_MAX_COMPLETIONS || c->count >= T9_MAX_CANDIDATES)
        return c->count;
    uint8_t longest = 0, next = c->count;
    uint8_t longest_len = 0, next_len = T9_MAX_WORD_LEN + 1;
    for (uint8_t i = 0; i < c->count; i++) {
        uint8_t len = (uint8_t)strlen(c->words[i]);
        if (len > longest_len) {
            longest     = i;
            longest_len = len;
        }
        if (len > base && len < next_len) {
            next     = i;
            next_len = len;
        }
    }
    for (uint8_t i = 0; i < c->count; i++) {
        if (strncmp(c->words[longest], c->words[i], strlen(c->words[i])) != 0)
            return c->count;
    }
    return next < c->count ? next : longest;
}

/* Base T9 prefers exact words; suggestions prefer longer words. In suggestion
 * mode, an exact word is redundant only when a listed
 * completion starts with the same letters: 0 accepts that completion's base.
 * Keep other exact words after the completions so every spelling is reachable. */
static void prepare_list(const t9_engine_t *e, t9_candidates_t *c, uint8_t base) {
    /* Check every returned word before collapsing bases or filtering exacts. */
    c->prefix_family = false;
    if (!e->completions) {
        uint8_t next = family_candidate(c, base);
        if (next < c->count) {
            memmove(c->words[0], c->words[next], sizeof(c->words[0]));
            c->count         = 1;
            c->total_exact   = c->words[0][base] == '\0';
            c->prefix_family = true;
            return;
        }
    }
    /* Stable partition in place, using just one word of scratch space. */
    uint8_t preferred = 0;
    for (uint8_t i = 0; i < c->count; i++) {
        bool longer = c->words[i][base] != '\0';
        if (longer != e->completions)
            continue;
        char word[T9_MAX_WORD_LEN + 1];
        memcpy(word, c->words[i], sizeof(word));
        memmove(c->words[preferred + 1], c->words[preferred], (size_t)(i - preferred) * sizeof(word));
        memcpy(c->words[preferred++], word, sizeof(word));
    }
    if (!e->completions) {
        /* Choose exact representatives before collapsing shared prefixes. */
        collapse_bases(c, base);
        return;
    }
    uint8_t longer = preferred;
    uint8_t kept   = longer;
    for (uint8_t i = longer; i < c->count; i++) {
        bool represented = false;
        for (uint8_t j = 0; j < longer && !represented; j++)
            represented = strncmp(c->words[j], c->words[i], base) == 0;
        if (!represented)
            memmove(c->words[kept++], c->words[i], sizeof(c->words[i]));
    }
    c->count       = kept;
    c->total_exact = (uint8_t)(kept - longer);
}

static void load_list(const t9_engine_t *e, const uint8_t *digits, uint8_t len, t9_candidates_t *c) {
    if (e->join_pos && len > e->join_pos && digits[e->join_pos] == 1) {
        t9_contractions_lookup(digits, len, e->user_selected ? e->join_base : NULL, c);
    } else {
        e->lookup(digits, len, c);
    }
    prepare_list(e, c, len);
}

/* Base T9 automatically selects the first exact word on extension, unless the
 * user cycled a candidate. Otherwise load the list and return `word` itself,
 * else (when `keep_base`) of the first entry whose first `base` letters equal
 * `word`'s, else 0. */
static uint8_t find_candidate(const t9_engine_t *e, const uint8_t *digits, uint8_t len, const char *word, uint8_t base,
                              bool keep_base, t9_candidates_t *c) {
    load_list(e, digits, len, c);
    /* Deletion, toggles and explicit choices pass keep_base to preserve them. */
    if (!e->completions && !keep_base && c->total_exact)
        return 0;
    /* When typing reaches the end of the previous recommendation, prefer
     * its best extension before changing the letters already shown. */
    bool extend_word  = e->completions && strlen(word) == len;
    uint8_t same_base = c->count;
    for (uint8_t i = 0; i < c->count; i++) {
        if (strcmp(c->words[i], word) == 0)
            return i;
        if (extend_word && strncmp(c->words[i], word, len) == 0)
            return i;
        if (keep_base && same_base == c->count && strncmp(c->words[i], word, base) == 0)
            same_base = i;
    }
    return same_base < c->count ? same_base : 0;
}

static void set_t9(t9_engine_t *e, const uint8_t *digits, uint8_t len, const t9_candidates_t *c, uint8_t index) {
    memmove(e->digits, digits, len);
    e->digit_len  = len;
    e->cands      = *c;
    e->cand_index = index;
    e->compose    = T9_COMPOSE_T9;
    if (len <= e->join_pos)
        e->join_pos = 0;
    if (e->join_pos) {
        memcpy(e->join_base, c->words[index], e->join_pos);
        e->join_base[e->join_pos] = 0;
    }
}

static bool t9_digit(t9_engine_t *e, uint8_t digit) {
    bool joining = e->compose == T9_COMPOSE_JOIN;
    bool extend  = e->compose == T9_COMPOSE_T9;
    uint8_t len  = extend || joining ? e->digit_len : 0;
    if (len >= T9_MAX_WORD_LEN)
        return miss(e);
    uint8_t digits[T9_MAX_WORD_LEN];
    memcpy(digits, e->digits, len);
    if (joining)
        digits[len++] = 1;
    if (len >= T9_MAX_WORD_LEN)
        return miss(e);
    digits[len++] = digit;

    t9_candidates_t *c = &e->next;
    uint8_t index      = 0;
    if (extend)
        index = find_candidate(e, digits, len, current_candidate(e), e->digit_len, e->user_selected, c);
    else
        load_list(e, digits, len, c);
    bool contraction = joining && c->count > 0;
    if (joining && !contraction) {
        /* No allowed suffix: accept the base and period, then start the next
         * word. Lookup and output must both succeed before accepting anything. */
        digits[0] = digit;
        len       = 1;
        e->lookup(digits, len, c);
        prepare_list(e, c, len);
    }
    if (c->count == 0)
        return miss(e);

    char tail[TAIL_MAX];
    uint8_t keep = extend || contraction ? 0 : e->shown_len;
    memcpy(tail, e->shown, keep);
    t9_caps_t caps    = joining && !contraction && e->caps == T9_CAPS_ONCE ? T9_CAPS_OFF : e->caps;
    uint8_t shown_len = render_t9(e, caps, c, index, len, tail + keep);
    if (!replace_preview(e, tail, (uint8_t)(keep + shown_len)))
        return false;
    if (e->compose == T9_COMPOSE_PUNCT || (joining && !contraction))
        accept_composition(e);
    set_t9(e, digits, len, c, index);
    set_shown(e, tail + keep, shown_len);
    return true;
}

static bool multitap_digit(t9_engine_t *e, uint8_t digit, uint32_t now) {
    const char *group = t9_digit_letters[digit];
    bool extend       = e->compose == T9_COMPOSE_MULTITAP;
    bool cycle        = extend && e->mt_pending && e->mt_key == digit;
    uint8_t len       = extend ? e->letter_len : 0;
    uint8_t letter    = cycle ? (uint8_t)((e->mt_cycle + 1) % strlen(group)) : 0;
    if (cycle)
        len--;
    else if (len >= T9_MAX_WORD_LEN)
        return miss(e);
    char letters[T9_MAX_WORD_LEN];
    memcpy(letters, e->letters, len);
    letters[len++] = group[letter];

    char tail[TAIL_MAX];
    uint8_t keep = extend ? 0 : e->shown_len;
    memcpy(tail, e->shown, keep);
    render_word(e->caps, letters, len, tail + keep);
    if (!replace_preview(e, tail, (uint8_t)(keep + len)))
        return false;
    if (e->compose == T9_COMPOSE_PUNCT)
        accept_composition(e);
    memcpy(e->letters, letters, len);
    e->letter_len  = len;
    e->compose     = T9_COMPOSE_MULTITAP;
    e->mt_pending  = true;
    e->mt_key      = digit;
    e->mt_cycle    = letter;
    e->mt_deadline = now + e->cfg.multitap_timeout_ms;
    set_shown(e, tail + keep, len);
    return true;
}

static const char *punct_chars(t9_punct_group_t group) {
    switch (group) {
        case T9_PUNCT_OPEN:
            return T9_WRAPPERS_OPEN;
        case T9_PUNCT_CLOSE:
            return T9_WRAPPERS_CLOSE;
        case T9_PUNCT_SYMBOL:
            return T9_SYMBOLS;
        case T9_PUNCT_EXTRA:
            return T9_SYMBOLS_EXTRA;
        default:
            return T9_PUNCTUATION;
    }
}

static bool cycle_punct(t9_engine_t *e, int8_t step) {
    const char *chars = punct_chars(e->punct_group);
    uint8_t count     = (uint8_t)strlen(chars);
    uint8_t index     = (uint8_t)((e->punct_index + count + step) % count);
    if (e->compose == T9_COMPOSE_JOIN) {
        char text[T9_MAX_WORD_LEN + 1];
        uint8_t base = e->digit_len;
        memcpy(text, e->shown, base);
        text[base] = chars[index];
        if (!replace_preview(e, text, (uint8_t)(base + 1)))
            return false;
        accept_composition(e);
        /* Only the word was committed, not the previous provisional period.
         * A long press of this second 1 must restore that period plus '1'. */
        e->held_committed_len = base;
        e->compose            = T9_COMPOSE_PUNCT;
    } else if (!replace_preview(e, &chars[index], 1))
        return false;
    e->punct_index = index;
    set_shown(e, &chars[index], 1);
    return true;
}

static bool cycle_candidate(t9_engine_t *e, int8_t step) {
    if (e->compose == T9_COMPOSE_PUNCT || e->compose == T9_COMPOSE_JOIN)
        return cycle_punct(e, step);
    if (e->compose != T9_COMPOSE_T9 || e->cands.count < 2)
        return true;
    uint8_t index = (uint8_t)((e->cand_index + e->cands.count + step) % e->cands.count);
    char text[T9_SHOWN_MAX + 1];
    uint8_t len = render_t9(e, e->caps, &e->cands, index, e->digit_len, text);
    if (!replace_preview(e, text, len))
        return false;
    e->cand_index    = index;
    e->user_selected = true;
    if (e->join_pos)
        memcpy(e->join_base, e->cands.words[index], e->join_pos);
    set_shown(e, text, len);
    return true;
}

static bool punct_key(t9_engine_t *e) {
    if ((e->compose == T9_COMPOSE_PUNCT && e->punct_group == T9_PUNCT_NORMAL) || e->compose == T9_COMPOSE_JOIN)
        return cycle_punct(e, 1);
    if (e->compose == T9_COMPOSE_T9 && !e->join_pos && e->digit_len < T9_MAX_WORD_LEN) {
        uint8_t digits[T9_MAX_WORD_LEN];
        memcpy(digits, e->digits, e->digit_len);
        digits[e->digit_len] = 1;
        t9_contractions_lookup(digits, (uint8_t)(e->digit_len + 1), e->user_selected ? current_candidate(e) : NULL,
                               &e->next);
        if (e->next.count) {
            char text[T9_MAX_WORD_LEN + 1];
            uint8_t len = render_word(e->caps, current_candidate(e), e->digit_len, text);
            text[len++] = T9_PUNCTUATION[0];
            if (!replace_preview(e, text, len))
                return false;
            memcpy(e->join_base, current_candidate(e), e->digit_len);
            e->join_base[e->digit_len] = 0;
            e->join_pos                = e->digit_len;
            e->compose                 = T9_COMPOSE_JOIN;
            e->punct_index             = 0;
            set_shown(e, text, len);
            return true;
        }
    }
    if (!commit_base(e, T9_PUNCTUATION[0], false, 0))
        return false;
    e->compose     = T9_COMPOSE_PUNCT;
    e->punct_index = 0;
    set_shown(e, T9_PUNCTUATION, 1);
    return true;
}

static bool zero_key(t9_engine_t *e) {
    return commit_base(e, ' ', false, 0);
}

/* Enter fills the offered word. In base T9, another family member keeps the
 * composition active at the new typed boundary; the last member adds a space. */
static bool complete_key(t9_engine_t *e) {
    const char *word = current_candidate(e);
    char text[T9_SHOWN_MAX + 1];
    uint8_t word_len = (uint8_t)strlen(word);
    if (!e->completions && e->cands.prefix_family) {
        uint8_t digits[T9_MAX_WORD_LEN];
        for (uint8_t i = 0; i < word_len; i++)
            digits[i] = word[i] == '\'' ? 1 : t9_letter_digit(word[i]);
        t9_candidates_t *c = &e->next;
        load_list(e, digits, word_len, c);
        if (c->prefix_family && c->words[0][word_len]) {
            uint8_t len = render_t9(e, e->caps, c, 0, word_len, text);
            if (!replace_preview(e, text, len))
                return false;
            set_t9(e, digits, word_len, c, 0);
            set_shown(e, text, len);
            return true;
        }
    }
    uint8_t len = render_word(e->caps, word, word_len, text);
    text[len++] = ' ';
    if (!replace_preview(e, text, len))
        return false;
    accept_composition(e);
    return true;
}

/* Removing a T9 digit keeps the same whole word, else one with the same base,
 * so deletion usually only erases one character. */
static bool delete_unit(t9_engine_t *e) {
    if (e->compose == T9_COMPOSE_PUNCT || base_len(e) <= 1)
        return erase_preview(e);
    char text[T9_SHOWN_MAX + 1];
    uint8_t len = (uint8_t)(base_len(e) - 1);
    if (e->compose == T9_COMPOSE_JOIN) {
        uint8_t shown_len = render_t9(e, e->caps, &e->cands, e->cand_index, len, text);
        if (!replace_preview(e, text, shown_len))
            return false;
        e->compose  = T9_COMPOSE_T9;
        e->join_pos = 0;
        set_shown(e, text, shown_len);
        return true;
    }
    if (e->compose == T9_COMPOSE_MULTITAP) {
        render_word(e->caps, e->letters, len, text);
        if (!replace_preview(e, text, len))
            return false;
        e->letter_len = len;
        e->mt_pending = false;
        set_shown(e, text, len);
        return true;
    }
    t9_candidates_t *c = &e->next;
    if (e->join_pos && len == e->join_pos + 1) {
        /* Deleting the final suffix digit restores the provisional first 1.
         * Recover a base candidate so the next delete can remove that 1 too. */
        uint8_t base  = e->join_pos;
        uint8_t index = find_candidate(e, e->digits, base, e->join_base, base, true, c);
        render_word(e->caps, e->join_base, base, text);
        text[base] = T9_PUNCTUATION[0];
        if (!replace_preview(e, text, (uint8_t)(base + 1)))
            return false;
        set_t9(e, e->digits, base, c, index);
        e->join_pos    = base;
        e->compose     = T9_COMPOSE_JOIN;
        e->punct_index = 0;
        set_shown(e, text, (uint8_t)(base + 1));
        return true;
    }
    uint8_t index = find_candidate(e, e->digits, len, current_candidate(e), len, true, c);
    if (c->count == 0)
        return erase_preview(e);
    uint8_t shown_len = render_t9(e, e->caps, c, index, len, text);
    if (!replace_preview(e, text, shown_len))
        return false;
    set_t9(e, e->digits, len, c, index);
    set_shown(e, text, shown_len);
    return true;
}

static bool spell_key(t9_engine_t *e) {
    /* press_typing already finished the pending letter; the word stays */
    if (e->compose == T9_COMPOSE_MULTITAP)
        return true;
    if (e->compose == T9_COMPOSE_PUNCT) {
        accept_composition(e);
    } else if (e->compose != T9_COMPOSE_NONE && !erase_preview(e)) {
        return false;
    }
    e->spelling = e->mode == T9_MODE_T9;
    return true;
}

static bool caps_key(t9_engine_t *e) {
    t9_caps_t caps = (t9_caps_t)((e->caps + 1) % 3);
    if (is_word(e->compose)) {
        char text[T9_SHOWN_MAX + 1];
        uint8_t len;
        if (e->compose == T9_COMPOSE_JOIN) {
            len         = render_word(caps, e->join_base, e->join_pos, text);
            text[len++] = T9_PUNCTUATION[0];
        } else {
            len = e->compose == T9_COMPOSE_T9 ? render_t9(e, caps, &e->cands, e->cand_index, e->digit_len, text)
                                              : render_word(caps, e->letters, e->letter_len, text);
        }
        if (!replace_preview(e, text, len))
            return false;
        set_shown(e, text, len);
    }
    e->caps = caps;
    e->events |= T9_EVENT_CAPS_CHANGED;
    return true;
}

/* DELETE / CANCEL / OTHER with nothing composing: the host handles the key
 * unless queued output is still pending, then it is queued behind it. */
static bool pass_through(t9_engine_t *e, t9_key_t key, uint16_t code) {
    if (t9_output_count(&e->out) == 0)
        return false;
    if (key == T9_KEY_DELETE)
        emit_edit(e, " ", 1, "", 0, false, 0);
    else
        emit_edit(e, "", 0, "", 0, true, code);
    return true;
}

static void track_hold(t9_engine_t *e, t9_key_t key, uint16_t code, uint32_t now) {
    e->held       = true;
    e->held_fired = false;
    e->held_key   = key;
    e->held_code  = code;
    e->held_since = now;
}

static bool press_typing(t9_engine_t *e, t9_key_t key, uint16_t code, uint32_t now) {
    if (key != (t9_key_t)e->mt_key)
        e->mt_pending = false;
    if (key <= T9_KEY_9) {
        uint8_t prev_len = base_len(e);
        memcpy(e->held_prev, e->shown, prev_len);
        e->held_prev[prev_len] = 0;
        e->held_prev_compose   = e->compose;
        e->held_committed_len  = 0;
        bool applied;
        if (key == T9_KEY_0) {
            applied = zero_key(e);
        } else if (key == T9_KEY_1) {
            applied = punct_key(e);
        } else if (e->mode == T9_MODE_T9 && !e->spelling) {
            applied = t9_digit(e, (uint8_t)key);
        } else {
            applied = multitap_digit(e, (uint8_t)key, now);
        }
        if (applied)
            track_hold(e, key, code, now);
        return true;
    }
    bool composing = e->compose != T9_COMPOSE_NONE;
    switch (key) {
        case T9_KEY_NEXT:
        case T9_KEY_PREV:
            cycle_candidate(e, key == T9_KEY_NEXT ? 1 : -1);
            return true;
        case T9_KEY_DOT:
            commit_base(e, 0, false, 0);
            return true;
        case T9_KEY_DELETE:
            if (!composing)
                return pass_through(e, key, code);
            delete_unit(e);
            return true;
        case T9_KEY_CANCEL:
            if (composing) {
                if (erase_preview(e))
                    e->spelling = false;
                return true;
            }
            if (e->spelling) {
                e->spelling = false;
                return true;
            }
            return pass_through(e, key, code);
        case T9_KEY_SPELL:
            /* starting a spelling from T9 needs a hold, so a stray tap keeps the word */
            if (e->mode == T9_MODE_T9 && !e->spelling)
                track_hold(e, key, code, now);
            else
                spell_key(e);
            return true;
        case T9_KEY_CAPS:
            caps_key(e);
            return true;
        case T9_KEY_COMPLETE:
            if (has_completion(e)) {
                complete_key(e);
                return true;
            }
            /* fall through: without a completion Enter is any other key */
        case T9_KEY_OTHER:
            if (!composing && t9_output_count(&e->out) == 0) {
                e->spelling = false;
                return false;
            }
            commit_base(e, 0, true, code);
            return true;
        default:
            return true;
    }
}

static bool mode_key(t9_engine_t *e, uint16_t code, uint32_t now) {
    if (!commit_base(e, 0, false, 0))
        return true;
    e->mode_before_hold = e->mode;
    e->mode             = (t9_mode_t)((e->mode + 1) % T9_MODE_COUNT);
    e->events |= T9_EVENT_MODE_CHANGED;
    track_hold(e, T9_KEY_MODE, code, now);
    return true;
}

/* Undo the held digit's tap, accept the preceding base and append a literal.
 * Both long presses and the first typing chord use the pre-press snapshot. */
static bool replace_held_tap(t9_engine_t *e, char literal) {
    uint8_t prev_len = (uint8_t)strlen(e->held_prev);
    char old[TAIL_MAX], new[T9_MAX_WORD_LEN + 1];
    uint8_t old_len = e->held_committed_len;
    memcpy(old, e->held_prev, old_len);
    if (e->held_key == T9_KEY_0)
        old[old_len++] = ' ';
    memcpy(old + old_len, e->shown, e->shown_len);
    old_len = (uint8_t)(old_len + e->shown_len);
    memcpy(new, e->held_prev, prev_len);
    new[prev_len] = literal;
    if (!emit_edit(e, old, old_len, new, (uint8_t)(prev_len + 1), false, 0))
        return false;
    if (!e->held_committed_len && is_word(e->held_prev_compose))
        consume_caps_once(e);
    e->spelling = false;
    end_composition(e);
    return true;
}

static bool fire_long_press(t9_engine_t *e) {
    if (e->held_key == T9_KEY_SPELL)
        return spell_key(e);
    if (e->held_key == T9_KEY_MODE) {
        if (!emit_edit(e, "", 0, "", 0, true, e->held_code))
            return false;
        e->mode = e->mode_before_hold;
        e->events |= T9_EVENT_MODE_CHANGED;
        return true;
    }
    return replace_held_tap(e, (char)('0' + e->held_key));
}

/* 1 stays the held key while 2-5 select and cycle a single symbol. Switching
 * groups replaces the symbol and restarts at the new group's first character. */
static bool symbol_chord_key(t9_engine_t *e, t9_key_t key) {
    t9_punct_group_t group;
    switch (key) {
        case T9_KEY_2:
            group = T9_PUNCT_OPEN;
            break;
        case T9_KEY_3:
            group = T9_PUNCT_CLOSE;
            break;
        case T9_KEY_4:
            group = T9_PUNCT_SYMBOL;
            break;
        default:
            group = T9_PUNCT_EXTRA;
            break; /* 5 */
    }
    const char *chars = punct_chars(group);
    if (e->compose == T9_COMPOSE_PUNCT && e->punct_group != T9_PUNCT_NORMAL) {
        if (e->punct_group == group)
            return cycle_punct(e, 1);
        if (!replace_preview(e, chars, 1))
            return false;
    } else if (e->held_fired) {
        /* The long hold already accepted the preceding word and typed 1.
         * Retract it from the queue, or backspace it if it reached the host. */
        if (!emit_edit(e, "1", 1, chars, 1, false, 0))
            return false;
    } else if (!replace_held_tap(e, chars[0])) {
        return false;
    }
    e->compose     = T9_COMPOSE_PUNCT;
    e->punct_group = group;
    e->punct_index = 0;
    e->held_fired  = true; /* further waiting must never turn the symbol into 1 */
    set_shown(e, chars, 1);
    return true;
}

void t9_init(t9_engine_t *e, t9_lookup_fn lookup, const t9_config_t *cfg) {
    memset(e, 0, sizeof(*e));
    e->lookup = lookup;
    e->cfg    = cfg ? *cfg : default_config;
    t9_output_init(&e->out);
}

void t9_reset(t9_engine_t *e) {
    end_composition(e);
    e->spelling = false;
    e->held     = false;
    t9_output_clear(&e->out);
}

bool t9_key_press(t9_engine_t *e, t9_key_t key, uint16_t passthrough_code, uint32_t now_ms) {
    t9_tick(e, now_ms);
    if (e->mode != T9_MODE_NUMPAD && e->held && e->held_key == T9_KEY_1 && key >= T9_KEY_2 && key <= T9_KEY_5) {
        symbol_chord_key(e, key);
        return true;
    }
    e->held = false;
    if (key >= T9_KEY_COUNT)
        return false;
    if (key == T9_KEY_MODE)
        return mode_key(e, passthrough_code, now_ms);
    if (e->mode == T9_MODE_NUMPAD)
        return false;
    return press_typing(e, key, passthrough_code, now_ms);
}

void t9_key_release(t9_engine_t *e, t9_key_t key, uint32_t now_ms) {
    t9_tick(e, now_ms);
    if (e->held && e->held_key == key) {
        if (key == T9_KEY_1 && e->compose == T9_COMPOSE_PUNCT && e->punct_group != T9_PUNCT_NORMAL)
            accept_composition(e);
        e->held = false;
    }
}

void t9_tick(t9_engine_t *e, uint32_t now_ms) {
    if (e->mt_pending && time_reached(now_ms, e->mt_deadline))
        e->mt_pending = false;
    if (e->held && !e->held_fired && time_reached(now_ms, e->held_since + e->cfg.long_press_ms))
        e->held_fired = fire_long_press(e);
}

bool t9_dial_turn(t9_engine_t *e, bool clockwise, uint16_t character_code, uint16_t word_code, uint32_t now_ms) {
    t9_tick(e, now_ms);
    e->held = false;
    if (e->mode == T9_MODE_NUMPAD)
        return false;
    if (e->compose == T9_COMPOSE_T9 || e->compose == T9_COMPOSE_PUNCT || e->compose == T9_COMPOSE_JOIN) {
        cycle_candidate(e, clockwise ? 1 : -1);
    } else {
        commit_base(e, 0, true, e->mode == T9_MODE_T9 ? word_code : character_code);
    }
    return true;
}

bool t9_accept(t9_engine_t *e) {
    e->held = false;
    return e->compose == T9_COMPOSE_NONE || commit_base(e, 0, false, 0);
}

bool t9_send_shortcut(t9_engine_t *e, uint16_t keycode) {
    e->held = false;
    return commit_base(e, 0, true, keycode);
}

bool t9_previous_mode(t9_engine_t *e) {
    e->held = false;
    if (!commit_base(e, 0, false, 0))
        return false;
    e->mode = (t9_mode_t)((e->mode + T9_MODE_COUNT - 1) % T9_MODE_COUNT);
    e->events |= T9_EVENT_MODE_CHANGED;
    return true;
}

/* Re-renders a T9 preview in place from the list with the new setting: the
 * same whole word when still listed, else the entry with the same base. */
bool t9_set_completions(t9_engine_t *e, bool on) {
    bool was       = e->completions;
    e->completions = on;
    if (e->compose == T9_COMPOSE_JOIN && on != was) {
        /* The provisional period stays visible, but deleting it must restore
         * a base list collapsed (or expanded) for the new setting. */
        uint8_t index = find_candidate(e, e->digits, e->digit_len, current_candidate(e), e->digit_len, true, &e->next);
        e->cands      = e->next;
        e->cand_index = index;
        return true;
    }
    if (e->compose != T9_COMPOSE_T9 || on == was)
        return true;
    t9_candidates_t *c = &e->next;
    char text[T9_SHOWN_MAX + 1];
    uint8_t index = find_candidate(e, e->digits, e->digit_len, current_candidate(e), e->digit_len, true, c);
    uint8_t len   = render_t9(e, e->caps, c, index, e->digit_len, text);
    if (!replace_preview(e, text, len)) {
        e->completions = was;
        return false;
    }
    set_t9(e, e->digits, e->digit_len, c, index);
    set_shown(e, text, len);
    return true;
}

uint8_t t9_take_events(t9_engine_t *e) {
    uint8_t events = e->events;
    e->events      = 0;
    return events;
}

bool t9_is_composing(const t9_engine_t *e) {
    return e->compose != T9_COMPOSE_NONE;
}

bool t9_has_completion(const t9_engine_t *e) {
    return has_completion(e);
}

const char *t9_preview(const t9_engine_t *e) {
    return e->shown;
}
