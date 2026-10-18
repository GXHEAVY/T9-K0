/* Held-prefix shortcut maps and tap detection, independent of QMK.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#pragma once

#include "t9_engine.h"

typedef enum {
    T9_SK_UNMAPPED,
    T9_SK_SLASH,
    T9_SK_ESC,
    T9_SK_DEL,
    T9_SK_TAB,
    T9_SK_BSPC,
    T9_SK_MINUS,
    T9_SK_STAR,
    T9_SK_PLUS,
    T9_SK_ENTER,
    T9_SK_NUM,
    T9_SK_DIAL_CW,
    T9_SK_DIAL_CCW,
    T9_SK_DOT,
    T9_SK_KNOB,
    T9_SK_M1,
} t9_shortcut_key_t;

typedef enum {
    T9_SC_PASS, /* ordinary key handling, including its release */
    T9_SC_NONE, /* consumed, no action */
    T9_SC_CAPS,
    T9_SC_DIVIDE, /* slash tapped alone */
    T9_SC_UNDO,
    T9_SC_CUT,
    T9_SC_COPY,
    T9_SC_PASTE,
    T9_SC_TAB,
    T9_SC_BACKTAB,
    T9_SC_ALT_TAB,
    T9_SC_TERMINAL,
    T9_SC_MODE_PREV,
    T9_SC_DOWN,
    T9_SC_UP,
    T9_SC_CLOSE,
    T9_SC_QUIT,
    T9_SC_INTERRUPT,
    T9_SC_MACRO1,
} t9_shortcut_action_t;

/* One slot per physical key, plus two for encoder directions. A release is
 * owned by whoever consumed its press, even after the prefix is released or the
 * host/mode changes. No host modifiers are held by this router. */
#define T9_SHORTCUT_SLOTS 32
typedef struct {
    uint32_t consumed;
    uint32_t pressed_at;
    uint8_t prefix_slot;
    t9_shortcut_key_t prefix;
    t9_mode_t tap_mode;
    bool active;
    bool used;
} t9_shortcuts_t;

t9_shortcut_action_t t9_shortcuts_event(t9_shortcuts_t *s, uint8_t slot, t9_shortcut_key_t key, bool pressed,
                                        t9_mode_t mode, uint32_t now, uint16_t tap_ms);
/* Cancels pending taps/holds, while swallowing releases of consumed presses. */
void t9_shortcuts_reset(t9_shortcuts_t *s);
