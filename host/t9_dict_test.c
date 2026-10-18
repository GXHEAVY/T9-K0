/* T9-K0: packed decoder versus the simple reference over the fixture.
 *
 * Built against host/t9_fixture_packed.h (generator, small --block-size so the
 * fixture spans many restart blocks) and host/t9_fixture_table.h (awk rule).
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "t9_dict_packed.h"
#include "t9_test.h"
#include "t9_dict_simple.h"
#include "t9_fixture_packed.h"

static const t9_simple_entry_t fixture[] = {
#include "t9_fixture_table.h"
};
#define FIXTURE_COUNT (sizeof(fixture) / sizeof(fixture[0]))

/* CHECK with a printf-style explanation on failure */
#define CHECK_MSG(cond, ...)             \
    do {                                 \
        if (!CHECK(cond)) {              \
            printf("     " __VA_ARGS__); \
            putchar('\n');               \
        }                                \
    } while (0)

typedef struct {
    uint8_t digits[T9_MAX_WORD_LEN + 2];
    uint8_t len;
} sequence_t;

static sequence_t word_sequence(const char *word, uint8_t len) {
    sequence_t sequence = {.len = len};
    for (uint8_t i = 0; i < len; i++)
        sequence.digits[i] = t9_letter_digit(word[i]);
    return sequence;
}

static const char *sequence_text(const sequence_t *sequence) {
    static char text[T9_MAX_WORD_LEN + 3];
    for (uint8_t i = 0; i < sequence->len; i++)
        text[i] = (char)('0' + sequence->digits[i]);
    text[sequence->len] = 0;
    return text;
}

static int compare_sorted(const void *a, const void *b) {
    const t9_simple_entry_t *x = a, *y = b;
    for (const char *p = x->word, *q = y->word;; p++, q++) {
        if (!*p || !*q) {
            if (*p != *q)
                return *p ? 1 : -1;
            break;
        }
        int diff = t9_letter_digit(*p) - t9_letter_digit(*q);
        if (diff)
            return diff;
    }
    if (x->score != y->score)
        return y->score - x->score;
    return strcmp(x->word, y->word);
}

static bool same_sequence(const char *a, const char *b) {
    for (; *a && *b; a++, b++) {
        if (t9_letter_digit(*a) != t9_letter_digit(*b))
            return false;
    }
    return !*a && !*b;
}

static bool has_prefix(const char *word, const sequence_t *prefix) {
    for (uint8_t i = 0; i < prefix->len; i++) {
        if (!word[i] || t9_letter_digit(word[i]) != prefix->digits[i])
            return false;
    }
    return true;
}

static uint32_t worst_entries_decoded = 0;

/* one lookup from both implementations, compared field by field */
static bool compare_lookup(const sequence_t *sequence, t9_candidates_t *packed) {
    t9_candidates_t simple;
    t9_dict_simple_lookup(sequence->digits, sequence->len, &simple);
    t9_dict_packed_reset_counters();
    t9_dict_packed_lookup(sequence->digits, sequence->len, packed);
    uint32_t decoded = t9_dict_packed_entries_decoded();
    if (decoded > worst_entries_decoded)
        worst_entries_decoded = decoded;
    CHECK_MSG(decoded <= T9_DICT_WORD_COUNT, "%s: decoded %u entries of %u", sequence_text(sequence), (unsigned)decoded,
              (unsigned)T9_DICT_WORD_COUNT);

    bool same = packed->count == simple.count && packed->total_exact == simple.total_exact;
    for (uint8_t i = 0; same && i < packed->count; i++)
        same = strcmp(packed->words[i], simple.words[i]) == 0;
    CHECK_MSG(same, "%s: packed count %u/%u '%s' vs simple count %u/%u '%s'", sequence_text(sequence), packed->count,
              packed->total_exact, packed->count ? packed->words[0] : "", simple.count, simple.total_exact,
              simple.count ? simple.words[0] : "");
    CHECK_MSG(packed->total_exact <= T9_MAX_EXACT && packed->count <= T9_MAX_CANDIDATES, "%s: %u exact, %u entries",
              sequence_text(sequence), packed->total_exact, packed->count);
    return same;
}

static bool exact_candidate_present(const sequence_t *sequence, const char *word) {
    t9_candidates_t list;
    t9_dict_packed_lookup(sequence->digits, sequence->len, &list);
    for (uint8_t i = 0; i < list.count; i++) {
        if (strcmp(list.words[i], word) == 0)
            return true;
    }
    return false;
}

static size_t word_index(const char *word) {
    size_t w = 0;
    while (w < FIXTURE_COUNT && strcmp(fixture[w].word, word) != 0)
        w++;
    return w;
}

/* rank of `word` among the fixture words with its digit sequence (score desc, word asc) */
static uint16_t exact_rank(const char *word) {
    uint16_t rank = 0;
    for (size_t w = 0; w < FIXTURE_COUNT; w++) {
        if (!same_sequence(fixture[w].word, word) || strcmp(fixture[w].word, word) == 0)
            continue;
        rank += fixture[w].score > fixture[word_index(word)].score ||
                (fixture[w].score == fixture[word_index(word)].score && strcmp(fixture[w].word, word) < 0);
    }
    return rank;
}

/* The generator drops words beyond T9_MAX_EXACT from the packed fixture; the
 * reference lookup drops them from the full table. Both must agree, and the
 * dropped words are the lowest ranked. */
static void test_exact_cap(void) {
    uint32_t dropped = 0;
    for (size_t w = 0; w < FIXTURE_COUNT; w++) {
        if (exact_rank(fixture[w].word) < T9_MAX_EXACT)
            continue;
        dropped++;
        sequence_t full = word_sequence(fixture[w].word, (uint8_t)strlen(fixture[w].word));
        t9_candidates_t simple;
        t9_dict_simple_lookup(full.digits, full.len, &simple);
        CHECK_MSG(simple.total_exact == T9_MAX_EXACT, "%s: reference keeps %u exact words", sequence_text(&full),
                  simple.total_exact);
        CHECK_MSG(!exact_candidate_present(&full, fixture[w].word),
                  "%s: beyond T9_MAX_EXACT but among the candidates of %s", fixture[w].word, sequence_text(&full));
        t9_candidates_t packed;
        compare_lookup(&full, &packed);
    }
    CHECK_MSG(dropped > 0, "no fixture word is beyond T9_MAX_EXACT; add a sequence with more than %u words",
              T9_MAX_EXACT);
    printf("  words beyond T9_MAX_EXACT in the fixture: %u\n", (unsigned)dropped);
}

static void test_every_word_and_prefix(void) {
    for (size_t w = 0; w < FIXTURE_COUNT; w++) {
        const char *word = fixture[w].word;
        uint8_t len      = (uint8_t)strlen(word);
        sequence_t full  = word_sequence(word, len);
        if (exact_rank(word) < T9_MAX_EXACT)
            CHECK_MSG(exact_candidate_present(&full, word), "%s: not among the candidates of %s", word,
                      sequence_text(&full));
        t9_candidates_t packed;
        compare_lookup(&full, &packed);
        for (uint8_t prefix = 1; prefix < len; prefix++) {
            sequence_t partial = word_sequence(word, prefix);
            compare_lookup(&partial, &packed);
        }
    }
}

static void test_all_short_sequences(void) {
    for (uint8_t len = 1; len <= 4; len++) {
        uint32_t combinations = 1;
        for (uint8_t i = 0; i < len; i++)
            combinations *= 8;
        for (uint32_t n = 0; n < combinations; n++) {
            sequence_t sequence = {.len = len};
            for (uint32_t i = 0, rest = n; i < len; i++, rest /= 8)
                sequence.digits[i] = (uint8_t)(2 + rest % 8);
            t9_candidates_t packed;
            compare_lookup(&sequence, &packed);
        }
    }
}

static void expect_missing(sequence_t sequence, const char *what) {
    t9_candidates_t packed;
    compare_lookup(&sequence, &packed);
    t9_candidates_t zero;
    memset(&zero, 0, sizeof(zero));
    CHECK_MSG(memcmp(&packed, &zero, sizeof(zero)) == 0, "%s: expected an all-zero result, count %u", what,
              packed.count);
}

static void test_missing(void) {
    sequence_t nines = {.len = T9_MAX_WORD_LEN};
    memset(nines.digits, 9, sizeof(nines.digits));
    expect_missing(nines, "max-length all-9 sequence");
    sequence_t too_long = word_sequence("abcdefghijklmnopqrstuvwxyz", T9_MAX_WORD_LEN + 1);
    expect_missing(too_long, "sequence longer than T9_MAX_WORD_LEN");
    expect_missing((sequence_t){.len = 0}, "empty sequence");
    expect_missing((sequence_t){.digits = {4, 1, 6}, .len = 3}, "digit 1");
    expect_missing((sequence_t){.digits = {0}, .len = 1}, "digit 0");

    /* a missing 1-digit sequence, if the fixture leaves one; else a longer extension */
    uint16_t missing_single = 0;
    for (uint8_t digit = 2; digit <= 9; digit++) {
        sequence_t single = {.digits = {digit}, .len = 1};
        t9_candidates_t packed;
        compare_lookup(&single, &packed);
        missing_single += packed.count == 0;
    }
    for (size_t w = 0; w < FIXTURE_COUNT; w++) {
        uint8_t len         = (uint8_t)strlen(fixture[w].word);
        sequence_t extended = word_sequence(fixture[w].word, len);
        if (len == T9_MAX_WORD_LEN)
            continue;
        extended.digits[len] = 9;
        extended.len++;
        t9_candidates_t packed;
        compare_lookup(&extended, &packed);
    }
    printf("  missing 1-digit sequences in fixture: %u\n", missing_single);
}

/* restart-block coverage: computed from the fixture sorted in dictionary order */
static void test_block_boundaries(const t9_simple_entry_t *sorted) {
    CHECK_MSG(T9_DICT_BLOCK_COUNT >= 4, "fixture spans %u blocks; lower --block-size in the Makefile rule",
              (unsigned)T9_DICT_BLOCK_COUNT);
    uint32_t straddling = 0;
    for (uint32_t i = T9_DICT_BLOCK_SIZE; i < T9_DICT_WORD_COUNT; i += T9_DICT_BLOCK_SIZE) {
        if (!same_sequence(sorted[i - 1].word, sorted[i].word))
            continue;
        sequence_t sequence = word_sequence(sorted[i].word, (uint8_t)strlen(sorted[i].word));
        t9_candidates_t list;
        compare_lookup(&sequence, &list);
        CHECK_MSG(list.total_exact >= 2, "%s: straddling sequence has %u exact words", sequence_text(&sequence),
                  list.total_exact);
        straddling += list.count > 0;
    }
    CHECK_MSG(straddling > 0, "no exact collision straddles a block boundary; lower --block-size or add collisions");

    /* prefixes whose range covers several blocks */
    uint32_t multi_block = 0, most_blocks = 0, skipped = 0;
    for (uint8_t len = 1; len <= 3; len++) {
        for (uint32_t start = 0; start < T9_DICT_WORD_COUNT;) {
            if (strlen(sorted[start].word) < len) {
                start++;
                continue;
            }
            sequence_t prefix = word_sequence(sorted[start].word, len);
            uint32_t end      = start;
            while (end < T9_DICT_WORD_COUNT && has_prefix(sorted[end].word, &prefix))
                end++;
            uint32_t blocks = (end - 1) / T9_DICT_BLOCK_SIZE - start / T9_DICT_BLOCK_SIZE + 1;
            if (blocks >= 3) {
                multi_block++;
                t9_candidates_t list;
                compare_lookup(&prefix, &list);
                skipped += t9_dict_packed_blocks_skipped();
            }
            if (blocks > most_blocks)
                most_blocks = blocks;
            start = end;
        }
    }
    CHECK_MSG(multi_block > 0, "no prefix range covers 3 or more blocks");
    printf(
        "  blocks: %u of %u entries; straddling collisions: %u; prefixes over >= 3 blocks: %u (widest %u blocks), "
        "blocks skipped in them: %u\n",
        (unsigned)T9_DICT_BLOCK_COUNT, (unsigned)T9_DICT_BLOCK_SIZE, (unsigned)straddling, (unsigned)multi_block,
        (unsigned)most_blocks, (unsigned)skipped);
}

static void test_stats(void) {
    const t9_dict_packed_stats_t *stats = t9_dict_packed_stats();
    uint32_t beyond_cap                 = 0;
    for (size_t w = 0; w < FIXTURE_COUNT; w++)
        beyond_cap += exact_rank(fixture[w].word) >= T9_MAX_EXACT;
    CHECK_MSG(stats->word_count + beyond_cap == FIXTURE_COUNT,
              "packed word count %u + %u beyond T9_MAX_EXACT, fixture table %u", (unsigned)stats->word_count,
              (unsigned)beyond_cap, (unsigned)FIXTURE_COUNT);
    CHECK_MSG(
        stats->data_bytes == sizeof(t9_dict_bits) + sizeof(t9_dict_block_offsets) + sizeof(t9_dict_block_max_score),
        "T9_DICT_DATA_BYTES %u disagrees with the arrays", (unsigned)stats->data_bytes);
}

int main(void) {
    static t9_simple_entry_t sorted[FIXTURE_COUNT], kept[FIXTURE_COUNT];
    memcpy(sorted, fixture, sizeof(fixture));
    qsort(sorted, FIXTURE_COUNT, sizeof(sorted[0]), compare_sorted);
    t9_dict_simple_set(fixture, (uint16_t)FIXTURE_COUNT);

    printf("t9_dict_test: %u fixture words\n", (unsigned)FIXTURE_COUNT);
    test_stats();
    test_exact_cap();

    /* the packed fixture never holds the dropped words: compare against the same list */
    uint16_t kept_count = 0;
    for (size_t w = 0; w < FIXTURE_COUNT; w++) {
        if (exact_rank(fixture[w].word) < T9_MAX_EXACT)
            kept[kept_count++] = fixture[w];
    }
    t9_dict_simple_set(kept, kept_count);
    test_every_word_and_prefix();
    test_all_short_sequences();
    test_missing();
    test_block_boundaries(sorted);
    printf("  worst entries decoded in one lookup: %u of %u\n", (unsigned)worst_entries_decoded,
           (unsigned)T9_DICT_WORD_COUNT);
    return t9_test_summary("t9_dict_test");
}
