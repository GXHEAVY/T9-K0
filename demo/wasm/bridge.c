/* Browser boundary for the unchanged production engine and shortcut router.
 * Positions follow LAYOUT_tenkey_27; host codes below belong only to the demo.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <stdint.h>
#include <string.h>
#include "t9_engine.h"
#include "t9_dict_packed.h"
#include "t9_shortcuts.h"
#define EXPORT(name) __attribute__((export_name(name)))
static t9_engine_t engine;
static t9_shortcuts_t shortcuts;
static uint32_t pressed;
static bool fn, app_switch;
static uint8_t route[27]; /* 1: engine, 2: Fn mapping, 3: shortcut, 4: Fn prefix */
static unsigned action;
enum { BACKSPACE = 1, WORD_BACKSPACE, DELETE, TAB, ENTER, ESCAPE, LEFT, RIGHT, WORD_LEFT, WORD_RIGHT, NUM_LOCK };
static const t9_key_t keys[27] = {
    T9_KEY_COUNT, T9_KEY_CANCEL, T9_KEY_OTHER, T9_KEY_OTHER, T9_KEY_CANCEL,
    T9_KEY_OTHER, T9_KEY_MODE, T9_KEY_CAPS, T9_KEY_NEXT, T9_KEY_DELETE,
    T9_KEY_OTHER, T9_KEY_1, T9_KEY_2, T9_KEY_3, T9_KEY_SPELL,
    T9_KEY_OTHER, T9_KEY_4, T9_KEY_5, T9_KEY_6,
    T9_KEY_OTHER, T9_KEY_7, T9_KEY_8, T9_KEY_9,
    T9_KEY_COUNT, T9_KEY_0, T9_KEY_DOT, T9_KEY_COMPLETE,
};
static const t9_shortcut_key_t shortcut_keys[27] = {
    T9_SK_KNOB, T9_SK_ESC, T9_SK_DEL, T9_SK_TAB, T9_SK_BSPC,
    T9_SK_M1, T9_SK_NUM, T9_SK_SLASH, T9_SK_STAR, T9_SK_MINUS,
    0, 0, 0, 0, T9_SK_PLUS, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, T9_SK_DOT, T9_SK_ENTER,
};
/* Transparent Fn positions inherit BASE; other positions are hardware previews. */
static bool fn_mapped(unsigned pos) {
    return pos != 9 && pos != 10 && pos != 14 && pos != 15 && pos != 19 && pos != 23;
}
static uint16_t host_code(unsigned pos) {
    static const char digits[27] = { [11]='7', [12]='8', [13]='9', [16]='4', [17]='5', [18]='6',
                                    [20]='1', [21]='2', [22]='3', [24]='0' };
    if (digits[pos]) return 256 + digits[pos];
    switch (pos) {
        case 1: return ESCAPE;
        case 2: return DELETE;
        case 3: return TAB;
        case 4: return engine.mode == T9_MODE_NUMPAD ? BACKSPACE : WORD_BACKSPACE;
        case 6: return NUM_LOCK;
        case 7: return 256 + '/';
        case 8: return 256 + '*';
        case 9: return engine.mode == T9_MODE_NUMPAD ? 256 + '-' : BACKSPACE;
        case 14: return 256 + '+';
        case 25: return 256 + '.';
        case 26: return ENTER;
        default: return 0;
    }
}
static void run_shortcut(t9_shortcut_action_t selected, uint32_t now) {
    if (selected == T9_SC_NONE || selected == T9_SC_PASS) return;
    action = selected;
    if (selected == T9_SC_CAPS) {
        t9_key_press(&engine, T9_KEY_CAPS, 0, now);
        t9_key_release(&engine, T9_KEY_CAPS, now);
    } else if (selected == T9_SC_MODE_PREV) {
        t9_previous_mode(&engine);
    } else if (selected == T9_SC_DIVIDE) {
        t9_send_shortcut(&engine, 256 + '/');
    } else {
        if (selected == T9_SC_BACKTAB && app_switch) action = 160;
        if (selected != T9_SC_ALT_TAB && selected != T9_SC_BACKTAB) app_switch = false;
        if (selected == T9_SC_ALT_TAB) app_switch = true;
        t9_send_shortcut(&engine, selected == T9_SC_TAB ? TAB : 1000 + action);
    }
}
EXPORT("init") void t9_demo_init(void) {
    t9_init(&engine, t9_dict_packed_lookup, 0);
    memset(&shortcuts, 0, sizeof(shortcuts));
    memset(route, 0, sizeof(route));
    pressed = 0;
    fn = app_switch = false;
    action = 0;
}
EXPORT("key") void t9_demo_key(unsigned pos, unsigned down, uint32_t now) {
    if (pos >= 27) return;
    uint32_t bit = (uint32_t)1 << pos;
    if (down) {
        if (pressed & bit) return;
        pressed |= bit;
        action = 0;
        if (fn && fn_mapped(pos)) {
            route[pos] = 2;
            action = 100 + pos;
            if (pos == 26) t9_set_completions(&engine, !engine.completions);
            return;
        }
        t9_shortcut_action_t selected = t9_shortcuts_event(&shortcuts, pos, shortcut_keys[pos], true,
                                                         engine.mode, now, 200);
        if (selected != T9_SC_PASS) {
            route[pos] = 3;
            engine.held = false;
            run_shortcut(selected, now);
            return;
        }
        if (pos == 23) { fn = true; route[pos] = 4; return; }
        if (pos == 0) { action = 140; route[pos] = 2; return; }
        if (pos == 10 || pos == 15 || pos == 19) {
            t9_accept(&engine);
            action = 200 + pos;
            route[pos] = 2;
            return;
        }
        route[pos] = 1;
        uint16_t code = host_code(pos);
        if (!t9_key_press(&engine, keys[pos], code, now) && code)
            t9_output_push(&engine.out, T9_OUT_KEY, code);
    } else {
        if (!(pressed & bit)) return;
        pressed &= ~bit;
        if (route[pos] == 1) t9_key_release(&engine, keys[pos], now);
        if (route[pos] == 4) fn = false;
        if (route[pos] == 3) {
            run_shortcut(t9_shortcuts_event(&shortcuts, pos, shortcut_keys[pos], false, engine.mode, now, 200), now);
            if (!shortcuts.active && app_switch) { app_switch = false; action = 161; }
        }
        route[pos] = 0;
    }
}
EXPORT("dial") void t9_demo_dial(unsigned clockwise, uint32_t now) {
    action = 0;
    if (fn) { action = clockwise ? 130 : 131; return; }
    t9_shortcut_action_t selected = t9_shortcuts_event(&shortcuts, 27, clockwise ? T9_SK_DIAL_CW : T9_SK_DIAL_CCW,
                                                     true, engine.mode, now, 200);
    if (selected != T9_SC_PASS) {
        engine.held = false;
        run_shortcut(selected, now);
        t9_shortcuts_event(&shortcuts, 27, 0, false, engine.mode, now, 200);
    } else if (!t9_dial_turn(&engine, clockwise, clockwise ? RIGHT : LEFT,
                            clockwise ? WORD_RIGHT : WORD_LEFT, now)) action = clockwise ? 142 : 141;
}
EXPORT("tick") void t9_demo_tick(uint32_t now) { t9_tick(&engine, now); }
EXPORT("release_all") void t9_demo_release_all(void) {
    engine.held = false;
    if (engine.compose == T9_COMPOSE_PUNCT && engine.punct_group != T9_PUNCT_NORMAL) t9_accept(&engine);
    memset(&shortcuts, 0, sizeof(shortcuts));
    memset(route, 0, sizeof(route));
    pressed = 0;
    fn = app_switch = false;
}
EXPORT("pop") uint32_t t9_demo_pop(void) {
    t9_out_item_t item;
    return t9_output_pop(&engine.out, &item) ? ((uint32_t)item.kind << 16) | item.value : 0;
}
/* Numeric fields and string pointers; keep in sync with live-engine.js. */
EXPORT("get") uint32_t t9_demo_get(unsigned field, unsigned index) {
    switch (field) {
        case 0: return engine.mode;
        case 1: return engine.spelling;
        case 2: return engine.caps;
        case 3: return engine.completions;
        case 4: return engine.compose;
        case 5: return engine.digit_len;
        case 6: return (uintptr_t)engine.digits;
        case 7: return engine.cand_index;
        case 8: return engine.cands.prefix_family;
        case 9: return engine.compose == T9_COMPOSE_T9 ? engine.cands.count : 0;
        case 10: return index < engine.cands.count ? (uintptr_t)engine.cands.words[index] : 0;
        case 11: return (uintptr_t)t9_preview(&engine);
        case 12: return fn ? 1 : shortcuts.active ? (shortcuts.prefix == T9_SK_M1 ? 3 : app_switch ? 5 : 2)
                             : engine.held && engine.held_key == T9_KEY_1 && engine.mode != T9_MODE_NUMPAD ? 4 : 0;
        case 13: return pressed;
        case 14: return t9_take_events(&engine);
        case 15: return action;
        default: return 0;
    }
}
