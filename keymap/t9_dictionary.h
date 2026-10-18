/* T9-K0: dictionary lookup interface shared by the engine, the dictionary
 * implementations, the host tests and the simulator. Pure C, no QMK dependency.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/* Longest word the engine composes or a dictionary returns (bytes, no NUL;
 * contraction previews count the apostrophe as one input and one byte). */
#ifndef T9_MAX_WORD_LEN
#    define T9_MAX_WORD_LEN 24
#endif

/* Exact words a lookup returns for one digit sequence. The generator drops
 * lower-ranked words beyond this (--max-exact), so every exact word of the
 * production dictionary is reachable. */
#ifndef T9_MAX_EXACT
#    define T9_MAX_EXACT 10
#endif

/* Longer words (completions) a lookup returns for one digit sequence. */
#ifndef T9_MAX_COMPLETIONS
#    define T9_MAX_COMPLETIONS 16
#endif

/* The whole candidate list fits one lookup result. */
#define T9_MAX_CANDIDATES (T9_MAX_EXACT + T9_MAX_COMPLETIONS)

/* Added to the score of exact words when the list is merged. 0 ranks purely by
 * frequency, so `26` shows `an[d]` before `an`. Tune on the pad. */
#ifndef T9_EXACT_BONUS
#    define T9_EXACT_BONUS 0
#endif

/* Phone keypad letter groups. Index = digit value; 0 and 1 carry no letters. */
static const char *const t9_digit_letters[10] = {"", "", "abc", "def", "ghi", "jkl", "mno", "pqrs", "tuv", "wxyz"};

static inline uint8_t t9_letter_digit(char lower) {
    static const uint8_t map[26] = {2, 2, 2, 3, 3, 3, 4, 4, 4, 5, 5, 5, 6, 6, 6, 7, 7, 7, 7, 8, 8, 8, 9, 9, 9, 9};
    return (lower >= 'a' && lower <= 'z') ? map[lower - 'a'] : 0;
}

/* Index of `lower` within its digit group (0..3), or 0xff when not a letter. */
static inline uint8_t t9_letter_index(char lower) {
    const char *group = t9_digit_letters[t9_letter_digit(lower)];
    for (uint8_t i = 0; group[i]; i++) {
        if (group[i] == lower)
            return i;
    }
    return 0xff;
}

/* Candidate list of one lookup for a digit sequence D of length L: every word
 * whose digit sequence is exactly D (at most T9_MAX_EXACT, the best ranked)
 * and the T9_MAX_COMPLETIONS best ranked longer words whose digit sequence
 * starts with D, merged by rank with T9_EXACT_BONUS added to the exact words'
 * scores. Rank: score descending, shorter word first, then text ascending.
 * Each entry is a whole word; the engine shows it split at L. All text is
 * lowercase a-z, NUL-terminated. The engine's separate contraction lookup also
 * uses this buffer, with an apostrophe at the explicitly typed separator.
 */
typedef struct {
    uint8_t count;
    uint8_t total_exact; /* exact words among `count` */
    bool prefix_family;  /* engine verified all matches form one chain of word prefixes */
    char words[T9_MAX_CANDIDATES][T9_MAX_WORD_LEN + 1];
} t9_candidates_t;

/* digits[i] are numeric values 2..9. Implementations must zero `out` when
 * nothing matches (count == 0). */
typedef void (*t9_lookup_fn)(const uint8_t *digits, uint8_t len, t9_candidates_t *out);

/* Scores are quantized zipf frequencies: score = clamp(round(zipf * 8), 0, 63). */
#define T9_SCORE_MAX 63

/* --- ranking helpers shared by the dictionary implementations ---------------- */

typedef struct {
    char text[T9_MAX_WORD_LEN + 1];
    uint8_t score; /* exact words carry T9_EXACT_BONUS already */
    uint8_t len;
} t9_ranked_t;

static inline bool t9_ranks_before(const t9_ranked_t *a, const t9_ranked_t *b) {
    if (a->score != b->score)
        return a->score > b->score;
    if (a->len != b->len)
        return a->len < b->len;
    return strcmp(a->text, b->text) < 0;
}

/* Ordered insert into a table of at most `cap` entries; the worst falls off. */
static inline void t9_ranked_insert(t9_ranked_t *table, uint8_t *count, uint8_t cap, const t9_ranked_t *entry) {
    uint8_t pos = 0;
    while (pos < *count && t9_ranks_before(&table[pos], entry))
        pos++;
    if (pos >= cap)
        return;
    uint8_t last = *count < cap ? *count : (uint8_t)(cap - 1);
    memmove(&table[pos + 1], &table[pos], (size_t)(last - pos) * sizeof(*table));
    table[pos] = *entry;
    if (*count < cap)
        (*count)++;
}

/* Merges two tables in rank order into `out`. */
static inline void t9_ranked_merge(const t9_ranked_t *exact, uint8_t exact_count, const t9_ranked_t *longer,
                                   uint8_t longer_count, t9_candidates_t *out) {
    uint8_t i = 0, j = 0;
    out->count       = 0;
    out->total_exact = exact_count;
    while (i < exact_count || j < longer_count) {
        bool take_exact = i < exact_count && (j >= longer_count || t9_ranks_before(&exact[i], &longer[j]));
        strcpy(out->words[out->count++], take_exact ? exact[i++].text : longer[j++].text);
    }
}
