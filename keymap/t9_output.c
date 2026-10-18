/* T9-K0: ordered host output queue, see t9_output.h.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <string.h>
#include "t9_output.h"

static uint8_t ring_prev(uint8_t index) {
    return (uint8_t)((index + T9_OUTPUT_QUEUE_LEN - 1) % T9_OUTPUT_QUEUE_LEN);
}
static uint8_t ring_next(uint8_t index) {
    return (uint8_t)((index + 1) % T9_OUTPUT_QUEUE_LEN);
}

void t9_output_init(t9_output_t *q) {
    memset(q, 0, sizeof(*q));
}

void t9_output_clear(t9_output_t *q) {
    q->head = q->tail = q->count = 0;
}

uint8_t t9_output_count(const t9_output_t *q) {
    return q->count;
}

uint8_t t9_output_free(const t9_output_t *q) {
    return (uint8_t)(T9_OUTPUT_QUEUE_LEN - q->count);
}

bool t9_output_push(t9_output_t *q, t9_out_kind_t kind, uint16_t value) {
    if (q->count >= T9_OUTPUT_QUEUE_LEN)
        return false;
    q->items[q->tail] = (t9_out_item_t){.kind = (uint8_t)kind, .value = value};
    q->tail           = ring_next(q->tail);
    q->count++;
    return true;
}

bool t9_output_pop(t9_output_t *q, t9_out_item_t *item) {
    if (!t9_output_peek(q, item))
        return false;
    q->head = ring_next(q->head);
    q->count--;
    return true;
}

bool t9_output_peek(const t9_output_t *q, t9_out_item_t *item) {
    if (q->count == 0)
        return false;
    if (item)
        *item = q->items[q->head];
    return true;
}

uint8_t t9_output_retractable(const t9_output_t *q) {
    uint8_t chars = 0;
    for (uint8_t index = ring_prev(q->tail); chars + 1 < q->count && q->items[index].kind == T9_OUT_CHAR;
         index         = ring_prev(index))
        chars++;
    return chars;
}

bool t9_output_retract(t9_output_t *q) {
    if (q->count < 2 || q->items[ring_prev(q->tail)].kind != T9_OUT_CHAR)
        return false;
    q->tail = ring_prev(q->tail);
    q->count--;
    return true;
}
