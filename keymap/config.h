/* T9-K0 keymap configuration. Timing, host profiles and
 * hardware settings; adjust these for the intended pad and hosts.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#pragma once

/* --- timing (ms) ---------------------------------------------------------- */
#define T9_MULTITAP_TIMEOUT_MS 800
#define T9_LONG_PRESS_MS 500
/* Slash tapped alone cycles caps (numpad: divide); M1 tapped alone runs its
 * VIA macro. A second key activates shortcuts immediately; an unused hold
 * at/over this duration has no tap. */
#define T9_SHORTCUT_TAP_MS 200

/* --- output pacing ---------------------------------------------------------
 * One queue item (a character, a backspace or a passthrough key) is emitted at
 * most every N ms. Over wireless, emission also pauses while Keychron's report
 * buffer holds more than T9_WIRELESS_BACKLOG_LIMIT reports (each item is two
 * reports; the buffer drains one report per 1-3 ms depending on transport). */
#define T9_OUTPUT_INTERVAL_USB_MS 0
#define T9_OUTPUT_INTERVAL_WIRELESS_MS 6
#define T9_WIRELESS_BACKLOG_LIMIT 24
/* Abort a blocking flush if radio backpressure makes no progress for this long. */
#define T9_OUTPUT_STALL_TIMEOUT_MS 1000

/* Starting mode after power-up: T9_MODE_T9, T9_MODE_MULTITAP or T9_MODE_NUMPAD
 * (stock behaviour). Not yet persisted. */
#define T9_DEFAULT_MODE T9_MODE_T9

/* T9 completions (`cand[idate]`): 0 off, 1 on. Fn+Enter toggles them and the
 * choice is kept in QMK's user EEPROM word across power cycles; this default
 * applies after an EEPROM reset (Esc bootmagic, EE_CLR). */
#define T9_DEFAULT_COMPLETIONS 0

/* Host shortcut Bksp sends in T9 / multitap with nothing composing: delete the
 * previous word. macOS: LALT(KC_BSPC); Windows and Linux: LCTL(KC_BSPC). */
#define T9_WORD_DELETE_APPLE_KEY LALT(KC_BSPC)
#define T9_WORD_DELETE_PC_KEY LCTL(KC_BSPC)

/* Shared host profile: 1 is Apple (Mac/iPhone), 0 is Windows/Linux. Dial word
 * navigation uses Option/Ctrl accordingly; shortcut maps use Command/Ctrl
 * editing keys and Command+Tab/Alt+Tab, Command+Q/Alt+F4. The explicit terminal
 * bindings stay Ctrl on every host. These match the current pairings; change
 * them if hosts move slots. Fn+encoder brightness remains stock. */
#define T9_WORD_NAV_APPLE_USB 1
#define T9_WORD_NAV_APPLE_BT1 0
#define T9_WORD_NAV_APPLE_BT2 1
#define T9_WORD_NAV_APPLE_BT3 1
#define T9_WORD_NAV_APPLE_P2P4 0

/* --- RGB indications ---------------------------------------------------------
 * LED indices from led_config.c. 0-3 (Esc Del Tab Bksp) are Keychron's
 * Bluetooth/2.4 GHz indicators and 23 (0 key) is the low-battery indicator;
 * do not use them. */
#define T9_LED_MODE 5     /* Num key */
#define T9_LED_CAPS 6     /* slash key */
#define T9_LED_SPELL 13   /* plus key */
#define T9_LED_ACCEPT 24  /* dot key: dim while composing */
#define T9_LED_COMPLETE 7 /* * key: Enter's LED is hidden by its tall keycap; bright when completion is available */
#define T9_LED_DIGITS {10, 11, 12, 15, 16, 17, 19, 20, 21} /* phone 1-9 */
#define T9_FLASH_MS 160

#define T9_RGB_MODE_T9 0, 160, 40
#define T9_RGB_MODE_MULTITAP 0, 120, 200
#define T9_RGB_SPELL 200, 0, 160
#define T9_RGB_CAPS_ONCE 90, 90, 90
#define T9_RGB_CAPS_LOCK 255, 255, 255
#define T9_RGB_ACCEPT 40, 40, 40
#define T9_RGB_COMPLETIONS_ON 0, 40, 16
#define T9_RGB_COMPLETE 0, 200, 80
#define T9_RGB_MISS 255, 0, 0
#define T9_RGB_BUSY 255, 90, 0
#define T9_RGB_BOOTLOADER 255, 0, 0 /* Num key, rest dark: waiting for a flash */

/* The pad's flash size register reports 128 KB (factory EEPROM was found at
 * 0x0801F000), but the image with the full dictionary needs about 232 KB.
 * The upper 128 KB passed a write/read-back test (README.md, Flash layout), so the
 * wear-leveling EEPROM moves to the end of 256 KB, clear of the image.
 * FIRST_SECTOR is required: the backwards sector search in QMK's
 * wear_leveling_efl.c skips sectors and, against ChibiOS's 256-sector L4
 * descriptor, picks sectors 119-135; erasing 128 wiped page 0 on the pad.
 * Sectors 126-127 are 0x0803F000-0x0803FFFF. */
#define WEAR_LEVELING_EFL_FLASH_SIZE 262144
#define WEAR_LEVELING_EFL_FIRST_SECTOR 126

/* Mirrors Keychron's default so the backlog check in keymap.c matches the real
 * report buffer size (report_buffer.c only defines it when undefined). */
#define REPORT_BUFFER_QUEUE_SIZE 256
