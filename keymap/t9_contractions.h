/* Common contractions, separate from the packed a-z dictionary.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#pragma once

#include "t9_dictionary.h"

/* Match digits containing one separator (1) and 0..2 suffix digits. Returns
 * whole words with an apostrophe. An optional lowercase selected_base pins the
 * letters before 1 after explicit cycling; otherwise suffix input can resolve
 * the base's ambiguity too (966: wom[en], 96618: won't). */
void t9_contractions_lookup(const uint8_t *digits, uint8_t len, const char *selected_base, t9_candidates_t *out);
