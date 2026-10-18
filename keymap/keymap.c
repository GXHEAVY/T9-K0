/* Copyright 2025 ~ 2026 @ Keychron (https://www.keychron.com)
 * T9 integration Copyright 2026 T9-K0 contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include QMK_KEYBOARD_H
#include "keychron_common.h"
#include "dynamic_keymap.h"
#include "t9_engine.h"
#include "t9_shortcuts.h"
#ifdef T9_DICT_EXPERIMENT
#    include "t9_experiment_words.h"
#else
#    include "t9_dict_packed.h"
#endif
#ifdef LK_WIRELESS_ENABLE
#    include "transport.h"
#    include "wireless.h"
#    include "report_buffer.h"
#endif

enum layers {
    BASE,
    FN,
};

/* QK_USER range: clear of Keychron's QK_KB keycodes and VIA's custom list. */
enum custom_keycodes {
    T9_COMPLETIONS = SAFE_RANGE, /* completions on / off */
    /* Private output-queue commands, never sent as host keycodes. END marks
     * the last queued Tab of a gesture, so release needs no extra queue slot. */
    T9_APP_MAC,
    T9_APP_PC,
    T9_APP_MAC_BACK,
    T9_APP_PC_BACK,
    T9_APP_MAC_END,
    T9_APP_PC_END,
    T9_APP_MAC_BACK_END,
    T9_APP_PC_BACK_END,
};

/* The matrix keeps Keychron's stock keycodes so VIA shows familiar keys and
 * numpad mode is plain passthrough. Typing modes intercept them by keycode in
 * process_record_user (phone order: the top digit row is 1 2 3). */
// clang-format off
const uint16_t PROGMEM keymaps[][MATRIX_ROWS][MATRIX_COLS] = {
    [BASE] = LAYOUT_tenkey_27(
        KC_MUTE, KC_ESC,  KC_DEL,  KC_TAB,  KC_BSPC,
        MC_1,    KC_NUM,  KC_PSLS, KC_PAST, KC_PMNS,
        MC_2,    KC_P7,   KC_P8,   KC_P9,   KC_PPLS,
        MC_3,    KC_P4,   KC_P5,   KC_P6,
        MC_4,    KC_P1,   KC_P2,   KC_P3,
        MO(FN),  KC_P0,            KC_PDOT, KC_PENT ),

    [FN] = LAYOUT_tenkey_27(
        UG_TOGG, BT_HST1, BT_HST2, BT_HST3, P2P4G,
        QK_BOOT, UG_NEXT, UG_VALU, UG_HUEU, _______,
        _______, UG_PREV, UG_VALD, UG_HUED, _______,
        _______, UG_SATU, UG_SPDU, KC_MPRV,
        _______, UG_SATD, UG_SPDD, KC_MPLY,
        _______, UG_TOGG,          KC_MNXT, T9_COMPLETIONS),
};

#if defined(ENCODER_MAP_ENABLE)
const uint16_t PROGMEM encoder_map[][NUM_ENCODERS][2] = {
    [BASE] = {ENCODER_CCW_CW(KC_VOLD, KC_VOLU)},
    [FN]   = {ENCODER_CCW_CW(UG_VALD, UG_VALU)},
};
#endif
// clang-format on

/* --- engine instance ---------------------------------------------------------- */

/* QMK's 32-bit user EEPROM word; the remaining bits are free for the mode. */
typedef union {
    uint32_t raw;
    struct {
        bool completions : 1;
    };
} user_config_t;

static t9_engine_t engine;
static t9_shortcuts_t shortcut_state;
static bool app_switch_pending; /* this slash gesture has accepted an app-switch action */
static uint8_t app_switch_mods; /* only modifier bits introduced by this gesture */
static uint32_t last_output_time;
static uint32_t flash_until;
static uint8_t flash_event;
static uint8_t passthrough_held[MATRIX_ROWS]; /* bit per column: release must pass through */
static uint16_t remapped_held[MATRIX_ROWS]
                             [MATRIX_COLS]; /* remember the code pressed, even if the host profile changes */

typedef struct {
    uint16_t keycode;
    t9_key_t key;
    uint16_t passthrough; /* host key sent when the engine does not handle it */
} t9_binding_t;

/* Phone order: numpad 7 8 9 are phone 1 2 3. Adjust here, nowhere else. */
static const t9_binding_t bindings[] = {
    {KC_P7, T9_KEY_1, KC_P7},
    {KC_P8, T9_KEY_2, KC_P8},
    {KC_P9, T9_KEY_3, KC_P9},
    {KC_P4, T9_KEY_4, KC_P4},
    {KC_P5, T9_KEY_5, KC_P5},
    {KC_P6, T9_KEY_6, KC_P6},
    {KC_P1, T9_KEY_7, KC_P1},
    {KC_P2, T9_KEY_8, KC_P2},
    {KC_P3, T9_KEY_9, KC_P3},
    {KC_P0, T9_KEY_0, KC_P0},
    {KC_PAST, T9_KEY_NEXT, KC_PAST},
    {KC_PDOT, T9_KEY_DOT, KC_PDOT},
    {KC_PMNS, T9_KEY_DELETE, KC_BSPC},
    {KC_BSPC, T9_KEY_CANCEL, KC_BSPC},
    {KC_ESC, T9_KEY_CANCEL, KC_ESC},
    {KC_PPLS, T9_KEY_SPELL, KC_PPLS},
    {KC_PSLS, T9_KEY_CAPS, KC_PSLS},
    {KC_NUM, T9_KEY_MODE, KC_NUM},
    {KC_PENT, T9_KEY_COMPLETE, KC_PENT},
    {KC_TAB, T9_KEY_OTHER, KC_TAB},
    {KC_DEL, T9_KEY_OTHER, KC_DEL},
};

static const t9_binding_t *binding_for(uint16_t keycode) {
    for (uint8_t i = 0; i < sizeof(bindings) / sizeof(bindings[0]); i++) {
        if (bindings[i].keycode == keycode)
            return &bindings[i];
    }
    return NULL;
}

static bool typing_mode(void) {
    return engine.mode == T9_MODE_T9 || engine.mode == T9_MODE_MULTITAP;
}

/* --- host output ---------------------------------------------------------------- */

static uint16_t output_interval_ms(void) {
#ifdef LK_WIRELESS_ENABLE
    if (get_transport() & TRANSPORT_WIRELESS)
        return T9_OUTPUT_INTERVAL_WIRELESS_MS;
#endif
    return T9_OUTPUT_INTERVAL_USB_MS;
}

static bool host_ready_for_output(void) {
#ifdef LK_WIRELESS_ENABLE
    if (get_transport() & TRANSPORT_WIRELESS) {
        if (wireless_get_state() != WT_CONNECTED)
            return false;
        extern uint16_t report_buffer_queue_head, report_buffer_queue_tail;
        uint16_t backlog = (uint16_t)(report_buffer_queue_head - report_buffer_queue_tail) % REPORT_BUFFER_QUEUE_SIZE;
        if (backlog > T9_WIRELESS_BACKLOG_LIMIT)
            return false;
    }
#endif
    return true;
}

static void release_app_switch_mods(void) {
    if (!app_switch_mods)
        return;
    del_mods(app_switch_mods);
    app_switch_mods = 0;
    send_keyboard_report();
}

/* Finish after all Tabs already queued, even if the output queue is full.
 * Later text/actions will follow that final Tab and its modifier release. */
static void end_app_switch(void) {
    if (!app_switch_pending)
        return;
    app_switch_pending = false;
    uint8_t index      = engine.out.tail;
    for (uint8_t n = 0; n < t9_output_count(&engine.out); n++) {
        index               = (uint8_t)((index + T9_OUTPUT_QUEUE_LEN - 1) % T9_OUTPUT_QUEUE_LEN);
        t9_out_item_t *item = &engine.out.items[index];
        if (item->kind == T9_OUT_KEY && item->value >= T9_APP_MAC && item->value <= T9_APP_PC_BACK) {
            item->value += T9_APP_MAC_END - T9_APP_MAC;
            return;
        }
    }
    release_app_switch_mods();
}

static void emit(const t9_out_item_t *item) {
    if (item->kind == T9_OUT_KEY && item->value >= T9_APP_MAC && item->value <= T9_APP_PC_BACK_END) {
        bool end        = item->value >= T9_APP_MAC_END;
        uint16_t action = item->value - (end ? T9_APP_MAC_END - T9_APP_MAC : 0);
        bool apple      = action == T9_APP_MAC || action == T9_APP_MAC_BACK;
        uint8_t mod     = MOD_BIT(apple ? KC_LGUI : KC_LALT);
        if (!app_switch_mods) {
            /* Real mods survive QMK clearing weak mods on the next keypress.
             * Preserve any modifier already held independently on this pad. */
            app_switch_mods = mod & ~get_mods();
            add_mods(mod);
            send_keyboard_report();
        }
        tap_code16(action >= T9_APP_MAC_BACK ? LSFT(KC_TAB) : KC_TAB);
        if (end)
            release_app_switch_mods();
        return;
    }
    /* Never let the app-switch modifier affect queued text or another action. */
    release_app_switch_mods();
    switch (item->kind) {
        case T9_OUT_CHAR:
            send_char((char)item->value);
            break;
        case T9_OUT_BACKSPACE:
            tap_code(KC_BSPC);
            break;
        case T9_OUT_KEY:
            tap_code16(item->value);
            break;
    }
}

static void output_task(void) {
    t9_out_item_t item;
    if (!t9_output_count(&engine.out))
        return;
    if (timer_elapsed32(last_output_time) < output_interval_ms())
        return;
    if (!host_ready_for_output())
        return;
    if (t9_output_pop(&engine.out, &item)) {
        emit(&item);
        last_output_time = timer_read32();
    }
}

/* --- connection and host changes --------------------------------------------- */

static void drop_composition(void) {
    t9_reset(&engine);
    t9_shortcuts_reset(&shortcut_state);
    app_switch_pending = false;
    release_app_switch_mods();
}

#ifdef LK_WIRELESS_ENABLE
static uint8_t last_transport;
static wt_state_t last_wireless_state;
static uint8_t last_host_index;

void wireless_enter_connected_kb(uint8_t host_idx) {
    if (host_idx != last_host_index)
        drop_composition();
    last_host_index = host_idx;
}

static void connection_watch(void) {
    uint8_t transport = get_transport();
    wt_state_t state  = wireless_get_state();
    if (transport != last_transport || state != last_wireless_state)
        drop_composition();
    last_transport      = transport;
    last_wireless_state = state;
}
#else
static void connection_watch(void) {}
#endif

/* Used before actions the keymap cannot order through the queue (VIA macros).
 * A disconnected/stalled radio must not trap the scan loop or send the action
 * to a different host. Abandon the composition and its dependent action. */
static bool flush_output_blocking(void) {
    uint32_t progress = timer_read32();
#ifdef LK_WIRELESS_ENABLE
    uint8_t transport  = get_transport();
    uint8_t host_index = last_host_index;
#endif
    for (;;) {
#ifdef LK_WIRELESS_ENABLE
        if (get_transport() != transport || last_host_index != host_index ||
            ((transport & TRANSPORT_WIRELESS) && wireless_get_state() != WT_CONNECTED)) {
            drop_composition();
            return false;
        }
#endif
        uint8_t count = t9_output_count(&engine.out);
        if (!count)
            return true;
        output_task();
#ifdef LK_WIRELESS_ENABLE
        send_string_task();
#endif
        if (t9_output_count(&engine.out) < count)
            progress = timer_read32();
        else if (timer_elapsed32(progress) >= T9_OUTPUT_STALL_TIMEOUT_MS) {
            drop_composition();
            return false;
        }
        wait_ms(1);
    }
}

void suspend_power_down_user(void) {
    drop_composition();
}

void suspend_wakeup_init_user(void) {
    drop_composition();
}

/* --- QMK hooks ------------------------------------------------------------------- */

void keyboard_post_init_user(void) {
    const t9_config_t cfg = {.multitap_timeout_ms = T9_MULTITAP_TIMEOUT_MS, .long_press_ms = T9_LONG_PRESS_MS};
#ifdef T9_DICT_EXPERIMENT
    t9_dict_simple_set(t9_experiment_words, sizeof(t9_experiment_words) / sizeof(t9_experiment_words[0]));
    t9_init(&engine, t9_dict_simple_lookup, &cfg);
#else
    t9_init(&engine, t9_dict_packed_lookup, &cfg);
#endif
    engine.mode        = T9_DEFAULT_MODE;
    engine.completions = ((user_config_t){.raw = eeconfig_read_user()}).completions;
#ifdef LK_WIRELESS_ENABLE
    last_transport      = get_transport();
    last_wireless_state = wireless_get_state();
#endif
}

/* EEPROM reset (Esc bootmagic, EE_CLR): back to the defaults in config.h */
void eeconfig_init_user(void) {
    user_config_t config = {.raw = 0};
    config.completions   = T9_DEFAULT_COMPLETIONS;
    eeconfig_update_user(config.raw);
}

void housekeeping_task_user(void) {
    connection_watch();
    t9_tick(&engine, timer_read32());
    output_task();
    uint8_t events = t9_take_events(&engine);
    if (events & (T9_EVENT_MISS | T9_EVENT_BUSY)) {
        flash_event = events;
        flash_until = timer_read32() + T9_FLASH_MS;
    }
}

/* Shared connection profile for word navigation and Mac/PC shortcut variants. */
static bool apple_host(void) {
    bool apple = T9_WORD_NAV_APPLE_USB;
#ifdef LK_WIRELESS_ENABLE
    if (get_transport() & TRANSPORT_P2P4)
        apple = T9_WORD_NAV_APPLE_P2P4;
    else if (get_transport() & TRANSPORT_BLUETOOTH) {
        switch (last_host_index) {
            case 2:
                apple = T9_WORD_NAV_APPLE_BT2;
                break;
            case 3:
                apple = T9_WORD_NAV_APPLE_BT3;
                break;
            default:
                apple = T9_WORD_NAV_APPLE_BT1;
                break;
        }
    }
#endif
    return apple;
}

static bool handle_encoder(uint16_t keycode, keyrecord_t *record) {
    if (!typing_mode())
        return true;
    if (record->event.pressed) {
        bool clockwise = keycode == KC_VOLU;
        uint16_t arrow = clockwise ? KC_RGHT : KC_LEFT;
        t9_dial_turn(&engine, clockwise, arrow, apple_host() ? LALT(arrow) : LCTL(arrow), timer_read32());
    }
    return false;
}

/* Keep the existing VIA keycodes: slash/M1 select shortcut maps in every
 * mode, without requiring an EEPROM keymap reset or consuming QMK layers. */
static t9_shortcut_key_t shortcut_key(uint16_t keycode) {
    switch (keycode) {
        case KC_PSLS:
            return T9_SK_SLASH;
        case KC_ESC:
            return T9_SK_ESC;
        case KC_DEL:
            return T9_SK_DEL;
        case KC_TAB:
            return T9_SK_TAB;
        case KC_BSPC:
            return T9_SK_BSPC;
        case KC_PMNS:
            return T9_SK_MINUS;
        case KC_PAST:
            return T9_SK_STAR;
        case KC_PPLS:
            return T9_SK_PLUS;
        case KC_PENT:
            return T9_SK_ENTER;
        case KC_NUM:
            return T9_SK_NUM;
        case KC_PDOT:
            return T9_SK_DOT;
        case KC_MUTE:
            return T9_SK_KNOB;
        case MC_1:
            return T9_SK_M1;
        default:
            return T9_SK_UNMAPPED;
    }
}

static void run_shortcut(t9_shortcut_action_t action, uint32_t now) {
    uint16_t code;
    bool apple = apple_host();
    /* Once + starts the chooser, * reverses it without ending the held modifier.
     * Use the queued gesture state so this also works before its first Tab emits. */
    if (action == T9_SC_BACKTAB && app_switch_pending) {
        t9_send_shortcut(&engine, apple ? T9_APP_MAC_BACK : T9_APP_PC_BACK);
        return;
    }
    if (action != T9_SC_NONE && action != T9_SC_ALT_TAB)
        end_app_switch();
    switch (action) {
        case T9_SC_CAPS:
            t9_key_press(&engine, T9_KEY_CAPS, 0, now);
            t9_key_release(&engine, T9_KEY_CAPS, now);
            return;
        case T9_SC_MODE_PREV:
            t9_previous_mode(&engine);
            return;
        case T9_SC_MACRO1:
            if (!flush_output_blocking())
                return;
            t9_accept(&engine);
            if (!flush_output_blocking())
                return;
            dynamic_keymap_macro_send(MC_1 - QK_MACRO_0);
            return;
        case T9_SC_DIVIDE:
            code = KC_PSLS;
            break;
        case T9_SC_UNDO:
            code = apple ? LGUI(KC_Z) : LCTL(KC_Z);
            break;
        case T9_SC_CUT:
            code = apple ? LGUI(KC_X) : LCTL(KC_X);
            break;
        case T9_SC_COPY:
            code = apple ? LGUI(KC_C) : LCTL(KC_C);
            break;
        case T9_SC_PASTE:
            code = apple ? LGUI(KC_V) : LCTL(KC_V);
            break;
        case T9_SC_TAB:
            code = KC_TAB;
            break;
        case T9_SC_BACKTAB:
            code = LSFT(KC_TAB);
            break;
        case T9_SC_ALT_TAB:
            if (t9_send_shortcut(&engine, apple ? T9_APP_MAC : T9_APP_PC))
                app_switch_pending = true;
            return;
        case T9_SC_TERMINAL:
            code = LCTL(KC_GRV);
            break;
        case T9_SC_DOWN:
            code = KC_DOWN;
            break;
        case T9_SC_UP:
            code = KC_UP;
            break;
        case T9_SC_CLOSE:
            code = apple ? LGUI(KC_W) : LCTL(KC_W);
            break;
        case T9_SC_QUIT:
            code = apple ? LGUI(KC_Q) : LALT(KC_F4);
            break;
        case T9_SC_INTERRUPT:
            code = LCTL(KC_C);
            break;
        default:
            return;
    }
    t9_send_shortcut(&engine, code);
}

static bool process_shortcut(uint16_t keycode, keyrecord_t *record) {
    _Static_assert(MATRIX_ROWS * MATRIX_COLS + 2 <= T9_SHORTCUT_SLOTS, "shortcut slots must cover keys and encoder");
    uint8_t slot;
    t9_shortcut_key_t key;
    if (IS_ENCODEREVENT(record->event)) {
        bool clockwise = record->event.type == ENCODER_CW_EVENT;
        slot           = MATRIX_ROWS * MATRIX_COLS + (clockwise ? 0 : 1);
        key            = clockwise ? T9_SK_DIAL_CW : T9_SK_DIAL_CCW;
    } else {
        slot = record->event.key.row * MATRIX_COLS + record->event.key.col;
        key  = shortcut_key(keycode);
    }
    uint32_t now    = timer_read32();
    bool was_active = shortcut_state.active;
    t9_shortcut_action_t action =
        t9_shortcuts_event(&shortcut_state, slot, key, record->event.pressed, engine.mode, now, T9_SHORTCUT_TAP_MS);
    if (was_active && !shortcut_state.active)
        end_app_switch();
    if (action == T9_SC_PASS)
        return true;
    /* A shortcut gesture cancels an earlier digit/Num/spell long press, but
     * holding a prefix alone never accepts or changes the active composition. */
    if (record->event.pressed)
        engine.held = false;
    run_shortcut(action, now);
    return false;
}

bool process_record_user(uint16_t keycode, keyrecord_t *record) {
    if (!process_shortcut(keycode, record))
        return false;
    if (IS_ENCODEREVENT(record->event) && (keycode == KC_VOLU || keycode == KC_VOLD)) {
        return handle_encoder(keycode, record);
    }

    if (keycode == T9_COMPLETIONS) {
        /* re-renders the preview in place, never accepts; refused when the queue is full */
        if (record->event.pressed && t9_set_completions(&engine, !engine.completions)) {
            user_config_t config = {.raw = eeconfig_read_user()};
            config.completions   = engine.completions;
            eeconfig_update_user(config.raw);
        }
        return false;
    }

    if (IS_QK_MACRO(keycode)) {
        if (record->event.pressed && typing_mode()) {
            /* an empty queue cannot refuse the accept, so no suffix is left behind */
            if (!flush_output_blocking())
                return false;
            t9_accept(&engine);
            if (!flush_output_blocking())
                return false;
        }
        return true;
    }

    const t9_binding_t *binding = binding_for(keycode);
    if (!binding)
        return true;

    uint8_t row = record->event.key.row;
    uint8_t col = record->event.key.col;
    uint8_t bit = 1 << record->event.key.col;
    if (!record->event.pressed) {
        if (remapped_held[row][col]) {
            unregister_code16(remapped_held[row][col]);
            remapped_held[row][col] = 0;
            return false;
        }
        if (passthrough_held[row] & bit) {
            passthrough_held[row] &= ~bit;
            return true;
        }
        t9_key_release(&engine, binding->key, timer_read32());
        return false;
    }

    uint16_t passthrough = binding->passthrough;
    if (typing_mode() && keycode == KC_BSPC)
        passthrough = apple_host() ? T9_WORD_DELETE_APPLE_KEY : T9_WORD_DELETE_PC_KEY;
    if (t9_key_press(&engine, binding->key, passthrough, timer_read32()))
        return false;
    /* the host handles the key itself: keep it behind any output still queued
     * (numpad mode right after multitap, or a passthrough during a flush) */
    if (!flush_output_blocking())
        return false;
    if (typing_mode() && passthrough != keycode) {
        /* idle typing modes: '-' is Backspace, Bksp deletes a word; held for repeat */
        remapped_held[row][col] = passthrough;
        register_code16(passthrough);
        return false;
    }
    passthrough_held[row] |= bit;
    return true;
}

/* --- RGB indications -------------------------------------------------------------- */

#ifdef RGB_MATRIX_ENABLE
/* QMK calls this once the entire effect frame is rendered, before Keychron's
 * connection/battery indicators. Remove decorative effects in every mode,
 * including on their indicator keys, without erasing the hardware notices. */
bool rgb_matrix_indicators_user(void) {
    rgb_matrix_set_color_all(0, 0, 0);
    return true;
}

static void set_rgb(uint8_t index, uint8_t r, uint8_t g, uint8_t b, uint8_t led_min, uint8_t led_max) {
    if (index >= led_min && index < led_max)
        rgb_matrix_set_color(index, r, g, b);
}

bool rgb_matrix_indicators_advanced_user(uint8_t led_min, uint8_t led_max) {
    /* The basic hook clears the full frame before the final advanced batch.
     * Redraw all status LEDs, including those outside that batch. This also
     * keeps the T9 mode cue above Keychron's host Num Lock indicator. */
    led_min = 0;
    led_max = RGB_MATRIX_LED_COUNT;
    switch (engine.mode) {
        case T9_MODE_T9:
            if (engine.spelling) {
                set_rgb(T9_LED_MODE, T9_RGB_SPELL, led_min, led_max);
                set_rgb(T9_LED_SPELL, T9_RGB_SPELL, led_min, led_max);
            } else {
                set_rgb(T9_LED_MODE, T9_RGB_MODE_T9, led_min, led_max);
            }
            break;
        case T9_MODE_MULTITAP:
            set_rgb(T9_LED_MODE, T9_RGB_MODE_MULTITAP, led_min, led_max);
            break;
        default:
            break;
    }
    if (typing_mode()) {
        if (engine.caps == T9_CAPS_ONCE)
            set_rgb(T9_LED_CAPS, T9_RGB_CAPS_ONCE, led_min, led_max);
        if (engine.caps == T9_CAPS_LOCK)
            set_rgb(T9_LED_CAPS, T9_RGB_CAPS_LOCK, led_min, led_max);
        if (t9_is_composing(&engine))
            set_rgb(T9_LED_ACCEPT, T9_RGB_ACCEPT, led_min, led_max);
        if (t9_has_completion(&engine))
            set_rgb(T9_LED_COMPLETE, T9_RGB_COMPLETE, led_min, led_max);
        else if (engine.completions)
            set_rgb(T9_LED_COMPLETE, T9_RGB_COMPLETIONS_ON, led_min, led_max);
        else
            set_rgb(T9_LED_COMPLETE, 0, 0, 0, led_min, led_max);
    }
    if (flash_until && timer_read32() < flash_until) {
        const uint8_t digit_leds[] = T9_LED_DIGITS;
        for (uint8_t i = 0; i < sizeof(digit_leds); i++) {
            if (flash_event & T9_EVENT_BUSY) {
                set_rgb(digit_leds[i], T9_RGB_BUSY, led_min, led_max);
            } else {
                set_rgb(digit_leds[i], T9_RGB_MISS, led_min, led_max);
            }
        }
    } else {
        flash_until = 0;
    }
    return true;
}

/* The LED driver keeps its last frame while the MCU sits in the ROM bootloader,
 * so this stays visible until the new firmware starts. */
bool shutdown_user(bool jump_to_bootloader) {
    if (jump_to_bootloader) {
        rgb_matrix_set_color_all(0, 0, 0);
        rgb_matrix_set_color(T9_LED_MODE, T9_RGB_BOOTLOADER);
        rgb_matrix_update_pwm_buffers();
    }
    return true;
}
#endif
