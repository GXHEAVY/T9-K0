/* T9-K0: packed dictionary decoder, see dictionary/README.md#packed-dictionary-format.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <string.h>
#include "t9_dict_packed.h"

#ifndef T9_DICT_DATA_HEADER
#    define T9_DICT_DATA_HEADER "t9_dictionary_data.h"
#endif
#include T9_DICT_DATA_HEADER

#define SHARED_BITS 5
#define LEN_BITS 5
#define DIGIT_BITS 3
#define LETTER_BITS 2
#define SCORE_BITS 6

static const t9_dict_packed_stats_t stats = {T9_DICT_WORD_COUNT, T9_DICT_DATA_BYTES, T9_DICT_BLOCK_COUNT,
                                             T9_DICT_BLOCK_SIZE};

static uint32_t entries_decoded = 0;
static uint32_t blocks_skipped  = 0;

/* Ranking tables of the running lookup, about 700 B: kept out of the frame
 * because the MCU's process stack is small (see keymap/rules.mk). */
static t9_ranked_t exact[T9_MAX_EXACT], longer[T9_MAX_COMPLETIONS];

const t9_dict_packed_stats_t *t9_dict_packed_stats(void) {
    return &stats;
}

uint32_t t9_dict_packed_entries_decoded(void) {
    return entries_decoded;
}

uint32_t t9_dict_packed_blocks_skipped(void) {
    return blocks_skipped;
}

void t9_dict_packed_reset_counters(void) {
    entries_decoded = 0;
    blocks_skipped  = 0;
}

/* width <= 8; MSB-first */
static uint8_t read_bits(uint32_t pos, uint8_t width) {
    uint32_t byte   = pos >> 3;
    uint16_t window = (uint16_t)(t9_dict_bits[byte] << 8);
    if (byte + 1 < sizeof(t9_dict_bits))
        window |= t9_dict_bits[byte + 1];
    return (uint8_t)((window >> (16 - (pos & 7) - width)) & ((1u << width) - 1));
}

/* first sequence of `block` versus digits[0..len): <0 before, 0 equal, 1 a longer
 * sequence starting with digits, 2 after and not starting with digits */
static int compare_block_first(uint32_t block, const uint8_t *digits, uint8_t len) {
    uint32_t pos      = t9_dict_block_offsets[block] + SHARED_BITS;
    uint8_t entry_len = read_bits(pos, LEN_BITS);
    pos += LEN_BITS;
    for (uint8_t i = 0; i < entry_len && i < len; i++, pos += DIGIT_BITS) {
        int diff = (int)read_bits(pos, DIGIT_BITS) + 2 - (int)digits[i];
        if (diff)
            return diff < 0 ? -1 : 2;
    }
    return entry_len < len ? -1 : entry_len > len;
}

static void decode_letters(uint32_t letters_pos, const uint8_t *sequence, uint8_t len, char *text) {
    for (uint8_t i = 0; i < len; i++)
        text[i] = t9_digit_letters[sequence[i]][read_bits(letters_pos + LETTER_BITS * i, LETTER_BITS)];
    text[len] = 0;
}

static uint32_t first_block_to_scan(const uint8_t *digits, uint8_t len) {
    /* blocks whose first sequence is < D form a prefix; start in its last block,
     * because exact matches of D may begin before a block that starts with D */
    uint32_t low = 0, high = T9_DICT_BLOCK_COUNT;
    while (low < high) {
        uint32_t middle = low + (high - low) / 2;
        if (compare_block_first(middle, digits, len) < 0) {
            low = middle + 1;
        } else {
            high = middle;
        }
    }
    return low > 0 ? low - 1 : 0;
}

void t9_dict_packed_lookup(const uint8_t *digits, uint8_t len, t9_candidates_t *out) {
    memset(out, 0, sizeof(*out));
    if (len == 0 || len > T9_MAX_WORD_LEN)
        return;
    for (uint8_t i = 0; i < len; i++) {
        if (digits[i] < 2 || digits[i] > 9)
            return;
    }

    uint8_t exact_count = 0, longer_count = 0;
    uint8_t sequence[T9_MAX_WORD_LEN];
    uint8_t matched = 0; /* common prefix length of `sequence` and digits */
    bool past_exact = false;
    uint32_t block  = first_block_to_scan(digits, len);
    uint32_t index  = block * T9_DICT_BLOCK_SIZE;
    uint32_t pos    = t9_dict_block_offsets[block];

    while (index < T9_DICT_WORD_COUNT) {
        if (index % T9_DICT_BLOCK_SIZE == 0) {
            block = index / T9_DICT_BLOCK_SIZE;
            /* a full completion table: a block below its lowest score cannot change it */
            if (past_exact && longer_count == T9_MAX_COMPLETIONS &&
                t9_dict_block_max_score[block] < longer[longer_count - 1].score) {
                if (compare_block_first(block, digits, len) != 1)
                    break;
                blocks_skipped++;
                index += T9_DICT_BLOCK_SIZE;
                pos = t9_dict_block_offsets[block + 1];
                continue;
            }
        }
        uint8_t shared    = read_bits(pos, SHARED_BITS);
        uint8_t entry_len = read_bits(pos + SHARED_BITS, LEN_BITS);
        pos += SHARED_BITS + LEN_BITS;
        if (entry_len > T9_MAX_WORD_LEN || shared > entry_len)
            break; /* corrupt data */
        for (uint8_t i = shared; i < entry_len; i++, pos += DIGIT_BITS)
            sequence[i] = read_bits(pos, DIGIT_BITS) + 2;
        uint32_t letters_pos = pos;
        pos += LETTER_BITS * entry_len + SCORE_BITS;
        index++;
        entries_decoded++;

        if (shared < matched)
            matched = shared;
        while (matched < entry_len && matched < len && sequence[matched] == digits[matched])
            matched++;
        if (matched < len) {
            if (matched == entry_len || sequence[matched] < digits[matched])
                continue;
            break;
        }

        uint8_t score     = read_bits(letters_pos + LETTER_BITS * entry_len, SCORE_BITS);
        bool is_exact     = entry_len == len;
        t9_ranked_t entry = {.score = (uint8_t)(score + (is_exact ? T9_EXACT_BONUS : 0)), .len = entry_len};
        if (is_exact) {
            decode_letters(letters_pos, sequence, entry_len, entry.text);
            t9_ranked_insert(exact, &exact_count, T9_MAX_EXACT, &entry);
            continue;
        }
        past_exact = true;
        if (longer_count == T9_MAX_COMPLETIONS && score < longer[longer_count - 1].score)
            continue;
        decode_letters(letters_pos, sequence, entry_len, entry.text);
        t9_ranked_insert(longer, &longer_count, T9_MAX_COMPLETIONS, &entry);
    }
    t9_ranked_merge(exact, exact_count, longer, longer_count, out);
}
