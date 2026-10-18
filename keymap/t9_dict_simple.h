/* T9-K0: reference dictionary over a plain {word, score} table. Used by the
 * host tests as the oracle for the packed format and by the hardware
 * diagnostic build with a handful of hardcoded words.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#pragma once

#include "t9_dictionary.h"

typedef struct {
    const char *word; /* lowercase a-z, 1..T9_MAX_WORD_LEN letters */
    uint8_t score;    /* 0..T9_SCORE_MAX */
} t9_simple_entry_t;

void t9_dict_simple_set(const t9_simple_entry_t *entries, uint16_t count);
void t9_dict_simple_lookup(const uint8_t *digits, uint8_t len, t9_candidates_t *out);
