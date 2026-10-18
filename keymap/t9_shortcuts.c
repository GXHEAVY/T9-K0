/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "t9_shortcuts.h"

/* Mode-specific entries take precedence; absent entries inherit the T9 map.
 * An explicit T9_SC_NONE entry can disable an inherited shortcut. These maps
 * contain shortcut actions only: a missing chord never falls through to text. */
static const struct {
    t9_mode_t mode;
    t9_shortcut_key_t prefix;
    t9_shortcut_key_t key;
    t9_shortcut_action_t action;
} shortcuts[] = {
    {T9_MODE_T9, T9_SK_SLASH, T9_SK_ESC, T9_SC_UNDO},      {T9_MODE_T9, T9_SK_SLASH, T9_SK_DEL, T9_SC_CUT},
    {T9_MODE_T9, T9_SK_SLASH, T9_SK_TAB, T9_SC_COPY},      {T9_MODE_T9, T9_SK_SLASH, T9_SK_BSPC, T9_SC_PASTE},
    {T9_MODE_T9, T9_SK_SLASH, T9_SK_MINUS, T9_SC_TAB},     {T9_MODE_T9, T9_SK_SLASH, T9_SK_STAR, T9_SC_BACKTAB},
    {T9_MODE_T9, T9_SK_SLASH, T9_SK_PLUS, T9_SC_ALT_TAB},  {T9_MODE_T9, T9_SK_SLASH, T9_SK_ENTER, T9_SC_TERMINAL},
    {T9_MODE_T9, T9_SK_SLASH, T9_SK_NUM, T9_SC_MODE_PREV}, {T9_MODE_T9, T9_SK_SLASH, T9_SK_DIAL_CW, T9_SC_DOWN},
    {T9_MODE_T9, T9_SK_SLASH, T9_SK_DIAL_CCW, T9_SC_UP},   {T9_MODE_T9, T9_SK_SLASH, T9_SK_DOT, T9_SC_CLOSE},
    {T9_MODE_T9, T9_SK_SLASH, T9_SK_KNOB, T9_SC_QUIT},     {T9_MODE_T9, T9_SK_M1, T9_SK_TAB, T9_SC_INTERRUPT},
};

static t9_shortcut_action_t lookup(t9_mode_t mode, t9_shortcut_key_t prefix, t9_shortcut_key_t key) {
    for (uint8_t pass = 0; pass < 2; pass++) {
        t9_mode_t map = pass == 0 ? mode : T9_MODE_T9;
        for (uint8_t i = 0; i < sizeof(shortcuts) / sizeof(shortcuts[0]); i++) {
            if (shortcuts[i].mode == map && shortcuts[i].prefix == prefix && shortcuts[i].key == key)
                return shortcuts[i].action;
        }
        if (mode == T9_MODE_T9)
            break;
    }
    return T9_SC_NONE;
}

t9_shortcut_action_t t9_shortcuts_event(t9_shortcuts_t *s, uint8_t slot, t9_shortcut_key_t key, bool pressed,
                                        t9_mode_t mode, uint32_t now, uint16_t tap_ms) {
    if (slot >= T9_SHORTCUT_SLOTS)
        return T9_SC_PASS;
    uint32_t bit = (uint32_t)1 << slot;
    if (!pressed) {
        if (!(s->consumed & bit))
            return T9_SC_PASS;
        s->consumed &= ~bit;
        if (s->active && slot == s->prefix_slot) {
            s->active = false;
            if (!s->used && (uint32_t)(now - s->pressed_at) < tap_ms) {
                if (s->prefix == T9_SK_M1)
                    return T9_SC_MACRO1;
                return s->tap_mode == T9_MODE_NUMPAD ? T9_SC_DIVIDE : T9_SC_CAPS;
            }
        }
        return T9_SC_NONE;
    }
    if (s->consumed & bit)
        return T9_SC_NONE;
    if (s->active) {
        s->used = true;
        s->consumed |= bit;
        return lookup(mode, s->prefix, key);
    }
    if (key != T9_SK_SLASH && key != T9_SK_M1)
        return T9_SC_PASS;
    s->active      = true;
    s->used        = false;
    s->prefix_slot = slot;
    s->prefix      = key;
    s->pressed_at  = now;
    s->tap_mode    = mode;
    s->consumed |= bit;
    return T9_SC_NONE;
}

void t9_shortcuts_reset(t9_shortcuts_t *s) {
    s->active = false;
    s->used   = true;
}
