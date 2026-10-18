/* Minimal QMK boundary for testing the real keymap event handlers.
 * Keycodes are opaque test values; the firmware build checks QMK's definitions.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "config.h"
#ifdef T9_TEST_PC
#    undef T9_WORD_NAV_APPLE_USB
#    define T9_WORD_NAV_APPLE_USB 0
#endif

#define PROGMEM
#define RGB_MATRIX_ENABLE
#define RGB_MATRIX_LED_COUNT 26
#define MATRIX_ROWS 6
#define MATRIX_COLS 5
/* Matrix layout itself is checked by the firmware compiler. */
#define LAYOUT_tenkey_27(...) {{0}}
#define MO(layer) (0x4000 | (layer))
#define LCTL(code) (0x100 | (code))
#define LSFT(code) (0x200 | (code))
#define LALT(code) (0x400 | (code))
#define LGUI(code) (0x800 | (code))
#define MOD_BIT(code) ((uint8_t)(1u << ((code) & 7)))
#define IS_QK_MACRO(code) ((code) >= MC_1 && (code) <= MC_4)

enum {
    KC_MUTE = 100,
    KC_ESC,
    KC_DEL,
    KC_TAB,
    KC_BSPC,
    KC_NUM,
    KC_PSLS,
    KC_PAST,
    KC_PMNS,
    KC_P7,
    KC_P8,
    KC_P9,
    KC_PPLS,
    KC_P4,
    KC_P5,
    KC_P6,
    KC_P1,
    KC_P2,
    KC_P3,
    KC_P0,
    KC_PDOT,
    KC_PENT,
    KC_VOLD,
    KC_VOLU,
    KC_RGHT,
    KC_LEFT,
    KC_DOWN,
    KC_UP,
    KC_Z,
    KC_X,
    KC_C,
    KC_V,
    KC_GRV,
    KC_W,
    KC_Q,
    KC_F4,
    UG_VALU,
    KC_LCTL = 0xe0,
    KC_LSFT,
    KC_LALT,
    KC_LGUI,
    QK_MACRO_0 = 0x5000,
    MC_1,
    MC_2,
    MC_3,
    MC_4,
    SAFE_RANGE = 0x6000,
};
enum { KEY_EVENT = 1, ENCODER_CW_EVENT, ENCODER_CCW_EVENT };
typedef struct {
    struct {
        struct {
            uint8_t row, col;
        } key;
        bool pressed;
        uint8_t type;
    } event;
} keyrecord_t;
#define IS_ENCODEREVENT(event) ((event).type == ENCODER_CW_EVENT || (event).type == ENCODER_CCW_EVENT)

uint32_t timer_read32(void);
uint32_t timer_elapsed32(uint32_t then);
void wait_ms(uint16_t ms);
void send_char(char c);
void tap_code(uint8_t code);
void tap_code16(uint16_t code);
void register_code16(uint16_t code);
void unregister_code16(uint16_t code);
uint32_t eeconfig_read_user(void);
void eeconfig_update_user(uint32_t value);
uint8_t get_mods(void);
void add_mods(uint8_t value);
void del_mods(uint8_t value);
void send_keyboard_report(void);
void rgb_matrix_set_color(int index, uint8_t r, uint8_t g, uint8_t b);
void rgb_matrix_set_color_all(uint8_t r, uint8_t g, uint8_t b);
void rgb_matrix_update_pwm_buffers(void);
