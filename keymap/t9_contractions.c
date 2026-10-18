/* Common contractions, separate from the packed a-z dictionary.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "t9_contractions.h"

enum { S = 1 << 0, T = 1 << 1, M = 1 << 2, RE = 1 << 3, VE = 1 << 4, LL = 1 << 5, D = 1 << 6 };
static const char endings[][3] = {"s", "t", "m", "re", "ve", "ll", "d"};

/* Surface spellings: won + 't, not will + a spelling transformation. Fixed
 * arrays avoid pointer overhead. Only 's / 're share a first suffix digit;
 * prefer 's, then let normal candidate cycling reach 're. */
static const struct {
    char base[8];
    uint8_t endings;
} bases[] = {
    {"i", M | VE | LL | D},
    {"you", RE | VE | LL | D},
    {"he", S | LL | D},
    {"she", S | LL | D},
    {"it", S | LL | D},
    {"we", RE | VE | LL | D},
    {"they", RE | VE | LL | D},
    {"that", S | LL | D},
    {"there", S | RE | VE | LL | D},
    {"here", S},
    {"what", S | RE | LL | D},
    {"who", S | RE | VE | LL | D},
    {"where", S | RE | D},
    {"when", S | D},
    {"why", S | D},
    {"how", S | RE | D},
    {"let", S},
    {"don", T},
    {"doesn", T},
    {"didn", T},
    {"isn", T},
    {"aren", T},
    {"wasn", T},
    {"weren", T},
    {"haven", T},
    {"hasn", T},
    {"hadn", T},
    {"won", T},
    {"wouldn", T},
    {"shan", T},
    {"shouldn", T},
    {"can", T},
    {"couldn", T},
    {"mustn", T},
    {"needn", T},
    {"mightn", T},
    {"oughtn", T},
    {"daren", T},
    {"ain", T},
    {"could", VE},
    {"would", VE},
    {"should", VE},
    {"might", VE},
    {"must", VE},
};

void t9_contractions_lookup(const uint8_t *digits, uint8_t len, const char *selected_base, t9_candidates_t *out) {
    memset(out, 0, sizeof(*out));
    if (len == 0 || len > T9_MAX_WORD_LEN)
        return;
    uint8_t base_len = 0;
    while (base_len < len && digits[base_len] != 1)
        base_len++;
    if (base_len == 0 || base_len == len)
        return;
    uint8_t typed_suffix = (uint8_t)(len - base_len - 1);
    if (typed_suffix > 2)
        return;
    for (uint8_t i = 0; i < sizeof(bases) / sizeof(bases[0]); i++) {
        const char *base = bases[i].base;
        if (strlen(base) != base_len || (selected_base && strncmp(base, selected_base, base_len) != 0))
            continue;
        uint8_t matched = 0;
        while (matched < base_len && t9_letter_digit(base[matched]) == digits[matched])
            matched++;
        if (matched != base_len)
            continue;
        for (uint8_t j = 0; j < sizeof(endings) / sizeof(endings[0]) && out->count < T9_MAX_CANDIDATES; j++) {
            uint8_t suffix_len = (uint8_t)strlen(endings[j]);
            if (!(bases[i].endings & (1u << j)) || suffix_len < typed_suffix ||
                base_len + 1u + suffix_len > T9_MAX_WORD_LEN)
                continue;
            matched = 0;
            while (matched < typed_suffix && t9_letter_digit(endings[j][matched]) == digits[base_len + 1 + matched])
                matched++;
            if (matched != typed_suffix)
                continue;
            char *word = out->words[out->count++];
            memcpy(word, base, base_len);
            word[base_len] = '\'';
            strcpy(word + base_len + 1, endings[j]);
            if (suffix_len == typed_suffix)
                out->total_exact++;
        }
    }
}
