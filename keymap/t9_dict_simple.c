/* T9-K0: reference dictionary, linear scan. Deliberately simple so it can
 * serve as the oracle for the packed decoder.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <string.h>
#include "t9_dict_simple.h"

static const t9_simple_entry_t *table = 0;
static uint16_t table_count           = 0;

void t9_dict_simple_set(const t9_simple_entry_t *entries, uint16_t count) {
    table       = entries;
    table_count = count;
}

/* 0 when word's digit sequence is shorter than len or differs; otherwise the
 * word length (>= len). */
static uint8_t prefix_match(const char *word, const uint8_t *digits, uint8_t len) {
    uint8_t i = 0;
    for (; i < len; i++) {
        if (word[i] == 0 || t9_letter_digit(word[i]) != digits[i])
            return 0;
    }
    while (word[i])
        i++;
    return i;
}

void t9_dict_simple_lookup(const uint8_t *digits, uint8_t len, t9_candidates_t *out) {
    memset(out, 0, sizeof(*out));
    if (len == 0 || len > T9_MAX_WORD_LEN)
        return;

    t9_ranked_t exact[T9_MAX_EXACT], longer[T9_MAX_COMPLETIONS];
    uint8_t exact_count = 0, longer_count = 0;
    for (uint16_t i = 0; i < table_count; i++) {
        uint8_t word_len = prefix_match(table[i].word, digits, len);
        if (word_len == 0)
            continue;
        bool is_exact     = word_len == len;
        t9_ranked_t entry = {.score = (uint8_t)(table[i].score + (is_exact ? T9_EXACT_BONUS : 0)), .len = word_len};
        strcpy(entry.text, table[i].word);
        if (is_exact)
            t9_ranked_insert(exact, &exact_count, T9_MAX_EXACT, &entry);
        else
            t9_ranked_insert(longer, &longer_count, T9_MAX_COMPLETIONS, &entry);
    }
    t9_ranked_merge(exact, exact_count, longer, longer_count, out);
}
