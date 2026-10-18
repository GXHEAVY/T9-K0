/* T9-K0: ordered host output queue. The engine appends edit operations;
 * the host integration (QMK keymap or simulator) drains them in order at its
 * own pace. Pure C, no QMK dependency.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifndef T9_OUTPUT_QUEUE_LEN
#    define T9_OUTPUT_QUEUE_LEN 128
#endif

typedef enum {
    T9_OUT_CHAR = 1,  /* value: printable ASCII character to type */
    T9_OUT_BACKSPACE, /* value unused: erase one character before the cursor */
    T9_OUT_KEY,       /* value: opaque host key code to tap after earlier items */
} t9_out_kind_t;

typedef struct {
    uint8_t kind;
    uint16_t value;
} t9_out_item_t;

typedef struct {
    t9_out_item_t items[T9_OUTPUT_QUEUE_LEN];
    uint8_t head; /* next slot to pop */
    uint8_t tail; /* next slot to push */
    uint8_t count;
} t9_output_t;

void t9_output_init(t9_output_t *q);
void t9_output_clear(t9_output_t *q);
uint8_t t9_output_count(const t9_output_t *q);
uint8_t t9_output_free(const t9_output_t *q);
bool t9_output_push(t9_output_t *q, t9_out_kind_t kind, uint16_t value);
bool t9_output_pop(t9_output_t *q, t9_out_item_t *item);
bool t9_output_peek(const t9_output_t *q, t9_out_item_t *item);

/* Taking back a queued character is equivalent to a later backspace. Only
 * trailing T9_OUT_CHAR items count, and never the oldest item, so a host may
 * peek an item, send it and pop it afterwards. */
uint8_t t9_output_retractable(const t9_output_t *q);
bool t9_output_retract(t9_output_t *q);
