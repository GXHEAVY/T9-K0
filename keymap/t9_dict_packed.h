/* T9-K0: decoder for the packed dictionary (dictionary/README.md#packed-dictionary-format). Pure C,
 * no heap, no QMK dependency. Data comes from the header named by
 * T9_DICT_DATA_HEADER (default "t9_dictionary_data.h").
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#pragma once

#include "t9_dictionary.h"

typedef struct {
    uint32_t word_count;
    uint32_t data_bytes;
    uint32_t block_count;
    uint32_t block_size;
} t9_dict_packed_stats_t;

/* t9_lookup_fn over the packed data */
void t9_dict_packed_lookup(const uint8_t *digits, uint8_t len, t9_candidates_t *out);

const t9_dict_packed_stats_t *t9_dict_packed_stats(void);

/* Measurement counters, cumulative since the last reset. Entries decoded counts
 * entry headers walked by the scan (block-first probes of the binary search are
 * not included); blocks skipped counts blocks passed over by the max-score test. */
uint32_t t9_dict_packed_entries_decoded(void);
uint32_t t9_dict_packed_blocks_skipped(void);
void t9_dict_packed_reset_counters(void);
