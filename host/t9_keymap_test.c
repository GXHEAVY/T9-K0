/* Exercise the actual QMK integration with a stub host and clock.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define QMK_KEYBOARD_H "qmk_stub.h"
#define T9_DICT_EXPERIMENT
#define LK_WIRELESS_ENABLE
#include "../keymap/keymap.c"
#include "t9_test.h"
#include <stdlib.h>

static uint32_t now, eeprom;
static t9_fake_host_t host;
static unsigned taps, registrations, unregistrations;
static unsigned macros;
static uint8_t last_macro;
static uint16_t last_code;
static uint8_t mods, last_tap_mods;
static unsigned mod_presses, mod_releases;
static char text_at_tap[T9_HOST_TEXT_MAX];
static char text_at_mod_press[T9_HOST_TEXT_MAX];
static uint8_t rgb[RGB_MATRIX_LED_COUNT][3];
static transport_t transport;
static wt_state_t wireless_state;
static bool stall_radio;
static unsigned radio_change;
static uint32_t wait_deadline;
uint16_t report_buffer_queue_head, report_buffer_queue_tail;

transport_t get_transport(void) {
    return transport;
}
wt_state_t wireless_get_state(void) {
    return wireless_state;
}
void send_string_task(void) {
    if (!(transport & TRANSPORT_WIRELESS) || wireless_state != WT_CONNECTED)
        return;
    if (radio_change == 1)
        wireless_state = WT_DISCONNECTED;
    if (radio_change == 2)
        transport = TRANSPORT_USB;
    if (radio_change == 3)
        wireless_enter_connected_kb(2);
    radio_change = 0;
    if (!stall_radio && report_buffer_queue_head != report_buffer_queue_tail)
        report_buffer_queue_tail = (report_buffer_queue_tail + 1) % REPORT_BUFFER_QUEUE_SIZE;
}

void rgb_matrix_set_color(int index, uint8_t r, uint8_t g, uint8_t b) {
    if (!CHECK(index >= 0 && index < RGB_MATRIX_LED_COUNT))
        return;
    rgb[index][0] = r;
    rgb[index][1] = g;
    rgb[index][2] = b;
}
void rgb_matrix_set_color_all(uint8_t r, uint8_t g, uint8_t b) {
    for (int i = 0; i < RGB_MATRIX_LED_COUNT; i++)
        rgb_matrix_set_color(i, r, g, b);
}
void rgb_matrix_update_pwm_buffers(void) {}

uint32_t timer_read32(void) {
    return now;
}
uint32_t timer_elapsed32(uint32_t then) {
    return now - then;
}
void wait_ms(uint16_t ms) {
    now += ms;
    if (wait_deadline && now > wait_deadline) {
        fprintf(stderr, "blocking flush exceeded test deadline\n");
        exit(1);
    }
}
void send_char(char c) {
    host_apply(&host, (t9_out_item_t){T9_OUT_CHAR, (uint8_t)c});
}
void tap_code(uint8_t code) {
    CHECK_INT(code, KC_BSPC);
    host_apply(&host, (t9_out_item_t){T9_OUT_BACKSPACE, 0});
}
void tap_code16(uint16_t code) {
    taps++;
    last_code     = code;
    last_tap_mods = mods;
    strcpy(text_at_tap, host.text);
}
uint8_t get_mods(void) {
    return mods;
}
void add_mods(uint8_t value) {
    if (value & ~mods) {
        mod_presses++;
        strcpy(text_at_mod_press, host.text);
    }
    mods |= value;
}
void del_mods(uint8_t value) {
    if (value & mods)
        mod_releases++;
    mods &= ~value;
}
void send_keyboard_report(void) {}
void register_code16(uint16_t code) {
    registrations++;
    last_code = code;
}
void unregister_code16(uint16_t code) {
    unregistrations++;
    last_code = code;
}
uint32_t eeconfig_read_user(void) {
    return eeprom;
}
void eeconfig_update_user(uint32_t value) {
    eeprom = value;
}
void dynamic_keymap_macro_send(uint8_t id) {
    macros++;
    last_macro = id;
    strcpy(text_at_tap, host.text);
}

#if T9_WORD_NAV_APPLE_USB
#    define EDIT(code) LGUI(code)
#    define APP_MOD MOD_BIT(KC_LGUI)
#    define QUIT LGUI(KC_Q)
#else
#    define EDIT(code) LCTL(code)
#    define APP_MOD MOD_BIT(KC_LALT)
#    define QUIT LALT(KC_F4)
#endif

static void fresh(t9_mode_t mode) {
    now    = 1000;
    eeprom = 1; /* completions enabled */
    memset(&host, 0, sizeof(host));
    memset(&shortcut_state, 0, sizeof(shortcut_state));
    memset(passthrough_held, 0, sizeof(passthrough_held));
    memset(remapped_held, 0, sizeof(remapped_held));
    taps = registrations = unregistrations = 0;
    macros = last_macro = 0;
    mods = last_tap_mods = app_switch_mods = 0;
    app_switch_pending                     = false;
    mod_presses = mod_releases = 0;
    last_code                  = 0;
    text_at_tap[0]             = 0;
    text_at_mod_press[0]       = 0;
    flash_until                = 0;
    flash_event                = 0;
    transport                  = TRANSPORT_USB;
    wireless_state             = WT_CONNECTED;
    last_host_index            = 0;
    stall_radio                = false;
    radio_change               = 0;
    wait_deadline              = 0;
    report_buffer_queue_head = report_buffer_queue_tail = 0;
    keyboard_post_init_user();
    engine.mode = mode;
}

/* Slots model physical identity independently of the keycode on release. */
static bool event(uint16_t code, uint8_t slot, bool pressed) {
    keyrecord_t r = {.event = {.key     = {.row = slot / MATRIX_COLS, .col = slot % MATRIX_COLS},
                               .pressed = pressed,
                               .type    = KEY_EVENT}};
    return process_record_user(code, &r);
}
static void slash(bool pressed) {
    CHECK(!event(KC_PSLS, 7, pressed));
}
static void tap(uint16_t code, uint8_t slot) {
    event(code, slot, true);
    now += 30;
    event(code, slot, false);
    now += 70;
}
static void drain(void) {
    flush_output_blocking();
}
static void type_th(void) {
    tap(KC_P2, 26);
    tap(KC_P4, 16);
}

static void test_word_delete_profiles(void) {
    static const struct {
        transport_t transport;
        uint8_t host;
        bool apple;
    } profiles[] = {
        {TRANSPORT_USB, 0, T9_WORD_NAV_APPLE_USB},
        {TRANSPORT_BLUETOOTH, 1, false},
        {TRANSPORT_BLUETOOTH, 2, true},
        {TRANSPORT_BLUETOOTH, 3, true},
        {TRANSPORT_P2P4, 0, false},
    };
    for (int mode = T9_MODE_T9; mode <= T9_MODE_MULTITAP; mode++) {
        for (unsigned i = 0; i < sizeof(profiles) / sizeof(profiles[0]); i++) {
            fresh((t9_mode_t)mode);
            transport = profiles[i].transport;
            wireless_enter_connected_kb(profiles[i].host);
            uint16_t expected = profiles[i].apple ? LALT(KC_BSPC) : LCTL(KC_BSPC);
            CHECK(!event(KC_BSPC, 4, true));
            CHECK_INT(registrations, 1);
            CHECK_INT(last_code, expected);
            /* Release must use the original modifier even after switching hosts. */
            transport = TRANSPORT_BLUETOOTH;
            wireless_enter_connected_kb(profiles[i].apple ? 1 : 2);
            CHECK(!event(KC_BSPC, 4, false));
            CHECK_INT(unregistrations, 1);
            CHECK_INT(last_code, expected);
        }
    }
    fresh(T9_MODE_NUMPAD);
    CHECK(event(KC_BSPC, 4, true));
    CHECK(event(KC_BSPC, 4, false));
    CHECK_INT(registrations, 0);
    fresh(T9_MODE_T9);
    type_th();
    tap(KC_BSPC, 4);
    drain();
    CHECK_STR(host.text, "");
    CHECK_INT(registrations, 0);
}

static void test_wireless_flush_failures(void) {
    for (unsigned action = 0; action < 3; action++) {
        fresh(T9_MODE_T9);
        transport      = TRANSPORT_BLUETOOTH;
        wireless_state = WT_DISCONNECTED;
        housekeeping_task_user();
        type_th();
        CHECK(t9_output_count(&engine.out) > 0);
        wait_deadline = now + 2 * T9_OUTPUT_STALL_TIMEOUT_MS;
        if (action == 0)
            tap(MC_1, 5);
        if (action == 1)
            CHECK(!event(MC_2, 10, true));
        if (action == 2) {
            engine.mode = T9_MODE_NUMPAD;
            CHECK(!event(KC_P8, 12, true));
            CHECK(!event(KC_P8, 12, false));
        }
        CHECK_INT(macros, 0);
        CHECK_INT(registrations, 0);
        CHECK_INT(t9_output_count(&engine.out), 0);
        CHECK(!t9_is_composing(&engine));
        wireless_state = WT_CONNECTED;
        housekeeping_task_user();
        drain();
        CHECK_STR(host.text, "");
    }
    for (unsigned change = 0; change <= 3; change++) {
        fresh(T9_MODE_T9);
        transport = TRANSPORT_BLUETOOTH;
        housekeeping_task_user();
        type_th();
        report_buffer_queue_head = T9_WIRELESS_BACKLOG_LIMIT + 1;
        stall_radio              = true;
        radio_change             = change; /* 0: stalled; 1: disconnect; 2: transport; 3: host */
        wait_deadline            = now + 2 * T9_OUTPUT_STALL_TIMEOUT_MS;
        CHECK(!event(MC_2, 10, true));
        CHECK_INT(t9_output_count(&engine.out), 0);
        CHECK(!t9_is_composing(&engine));
        CHECK_STR(host.text, "");
    }
    /* A connected radio that drains its backlog still preserves macro ordering. */
    fresh(T9_MODE_T9);
    transport = TRANSPORT_BLUETOOTH;
    housekeeping_task_user();
    type_th();
    report_buffer_queue_head = T9_WIRELESS_BACKLOG_LIMIT + 4;
    wait_deadline            = now + 2 * T9_OUTPUT_STALL_TIMEOUT_MS;
    tap(MC_1, 5);
    CHECK_INT(macros, 1);
    CHECK_STR(text_at_tap, "th");
    CHECK_INT(t9_output_count(&engine.out), 0);
}

static void test_slash_tap_hold_and_unmapped(void) {
    for (int mode = 0; mode < T9_MODE_COUNT; mode++) {
        fresh((t9_mode_t)mode);
        slash(true);
        CHECK_INT(engine.caps, T9_CAPS_OFF);
        now += T9_SHORTCUT_TAP_MS - 1;
        slash(false);
        drain();
        CHECK_INT(engine.caps, mode == T9_MODE_NUMPAD ? T9_CAPS_OFF : T9_CAPS_ONCE);
        CHECK_INT(taps, mode == T9_MODE_NUMPAD ? 1 : 0);
        if (mode == T9_MODE_NUMPAD)
            CHECK_INT(last_code, KC_PSLS);

        fresh((t9_mode_t)mode);
        slash(true);
        now += T9_SHORTCUT_TAP_MS;
        slash(false);
        CHECK_INT(t9_output_count(&engine.out), 0);
        CHECK_INT(engine.caps, T9_CAPS_OFF);

        slash(true);
        CHECK(!event(KC_P8, 12, true)); /* unassigned chord */
        slash(false);                   /* release prefix first */
        CHECK(!event(KC_P8, 12, false));
        CHECK(!t9_is_composing(&engine));
        CHECK_INT(engine.caps, T9_CAPS_OFF);
        CHECK_INT(t9_output_count(&engine.out), 0);
    }
    fresh(T9_MODE_T9);
    tap(KC_PSLS, 7);
    tap(KC_PSLS, 7);
    tap(KC_PSLS, 7);
    CHECK_INT(engine.caps, T9_CAPS_OFF); /* once -> lock -> off */
    now = UINT32_MAX - 20;
    slash(true);
    now += 30;
    slash(false);
    CHECK_INT(engine.caps, T9_CAPS_ONCE); /* timer wrap */
}

static void test_inherited_shortcuts_and_release_orders(void) {
    static const struct {
        uint16_t key, output;
    } cases[] = {
        {KC_ESC, EDIT(KC_Z)},  {KC_DEL, EDIT(KC_X)},    {KC_TAB, EDIT(KC_C)}, {KC_BSPC, EDIT(KC_V)},
        {KC_PMNS, KC_TAB},     {KC_PAST, LSFT(KC_TAB)}, {KC_PPLS, KC_TAB},    {KC_PENT, LCTL(KC_GRV)},
        {KC_PDOT, EDIT(KC_W)}, {KC_MUTE, QUIT},
    };
    for (int mode = 0; mode < T9_MODE_COUNT; mode++) {
        for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
            for (int prefix_first = 0; prefix_first < 2; prefix_first++) {
                fresh((t9_mode_t)mode);
                slash(true);
                CHECK(!event(cases[i].key, 1, true)); /* no hold delay needed */
                if (prefix_first)
                    slash(false);
                CHECK(!event(cases[i].key, 1, false));
                if (!prefix_first)
                    slash(false);
                drain();
                CHECK_INT(taps, 1);
                CHECK_INT(last_code, cases[i].output);
                CHECK_INT(last_tap_mods, cases[i].key == KC_PPLS ? APP_MOD : 0);
                CHECK_INT(mods, 0);
                CHECK_INT(engine.caps, T9_CAPS_OFF);
                CHECK_INT(engine.mode, mode);
                CHECK_INT(registrations + unregistrations, 0);
                CHECK_INT(shortcut_state.consumed, 0);
            }
        }
    }
}

static void test_reverse_cycle_and_dial(void) {
    fresh(T9_MODE_T9);
    slash(true);
    const t9_mode_t modes[] = {T9_MODE_NUMPAD, T9_MODE_MULTITAP, T9_MODE_T9};
    for (unsigned i = 0; i < 3; i++) {
        tap(KC_NUM, 6);
        CHECK_INT(engine.mode, modes[i]);
        now += 600;
        housekeeping_task_user();
        CHECK_INT(engine.mode, modes[i]); /* no long-Num reversal or Num Lock */
    }
    slash(false);
    CHECK_INT(taps, 0);
    for (int mode = 0; mode < T9_MODE_COUNT; mode++) {
        for (int cw = 0; cw < 2; cw++) {
            fresh((t9_mode_t)mode);
            slash(true);
            keyrecord_t r = {.event = {.pressed = true, .type = cw ? ENCODER_CW_EVENT : ENCODER_CCW_EVENT}};
            CHECK(!process_record_user(cw ? KC_VOLU : KC_VOLD, &r));
            slash(false);
            r.event.pressed = false;
            CHECK(!process_record_user(cw ? KC_VOLU : KC_VOLD, &r));
            drain();
            CHECK_INT(taps, 1);
            CHECK_INT(last_code, cw ? KC_DOWN : KC_UP);
        }
    }
}

static void test_composition_order_refusal_and_reset(void) {
    for (int queued = 0; queued < 2; queued++) {
        fresh(T9_MODE_T9);
        type_th();
        CHECK_STR(t9_preview(&engine), "th[e]");
        if (!queued)
            drain();
        slash(true);
        tap(KC_P8, 12); /* unmapped: no acceptance or typing */
        CHECK_STR(t9_preview(&engine), "th[e]");
        tap(KC_TAB, 3);
        slash(false);
        CHECK(!t9_is_composing(&engine));
        CHECK_INT(taps, 0); /* shortcut waits behind preview edits */
        drain();
        CHECK_STR(text_at_tap, "th");
        CHECK_INT(last_code, EDIT(KC_C));
        CHECK_INT(taps, 1);
    }

    fresh(T9_MODE_T9);
    type_th();
    drain();
    while (t9_output_free(&engine.out))
        t9_output_push(&engine.out, T9_OUT_KEY, KC_TAB);
    slash(true);
    tap(KC_ESC, 1);
    tap(KC_NUM, 6);
    slash(false);
    CHECK_STR(t9_preview(&engine), "th[e]");
    CHECK_INT(engine.mode, T9_MODE_T9);
    CHECK(t9_take_events(&engine) & T9_EVENT_BUSY);
    CHECK_INT(t9_output_count(&engine.out), T9_OUTPUT_QUEUE_LEN);

    fresh(T9_MODE_T9);
    slash(true);
    event(KC_TAB, 3, true);
    suspend_power_down_user();
    CHECK_INT(t9_output_count(&engine.out), 0);
    slash(false);
    CHECK(!event(KC_TAB, 3, false));
    drain();
    CHECK_INT(taps, 0);
    CHECK_INT(engine.caps, T9_CAPS_OFF);
    CHECK_INT(shortcut_state.consumed, 0);
}

static void test_existing_release_and_fn_behavior(void) {
    fresh(T9_MODE_MULTITAP);
    CHECK(!event(KC_PMNS, 9, true)); /* ordinary held Backspace */
    CHECK_INT(registrations, 1);
    slash(true);
    CHECK(!event(KC_PMNS, 9, false)); /* still releases its original action */
    CHECK_INT(unregistrations, 1);
    now += T9_SHORTCUT_TAP_MS;
    slash(false);

    fresh(T9_MODE_T9);
    CHECK(event(MO(FN), 25, true));
    CHECK(event(UG_VALU, 7, true)); /* Fn remaps slash to brightness */
    CHECK(event(UG_VALU, 7, false));
    CHECK(event(MO(FN), 25, false));
    slash(true);
    CHECK(!event(MO(FN), 25, true)); /* unassigned slash+Fn is blocked */
    slash(false);
    CHECK(!event(MO(FN), 25, false));
    CHECK_INT(engine.caps, T9_CAPS_OFF);
}

static void test_m1_tap_hold_and_interrupt(void) {
    for (int mode = 0; mode < T9_MODE_COUNT; mode++) {
        fresh((t9_mode_t)mode);
        CHECK(!event(MC_1, 5, true));
        CHECK_INT(macros, 0);
        now += 30;
        CHECK(!event(MC_1, 5, false));
        CHECK_INT(macros, 1);
        CHECK_INT(last_macro, 1); /* preserve MC_1's existing VIA slot */

        for (int prefix_first = 0; prefix_first < 2; prefix_first++) {
            fresh((t9_mode_t)mode);
            CHECK(!event(MC_1, 5, true));
            CHECK(!event(KC_TAB, 3, true));
            if (prefix_first)
                CHECK(!event(MC_1, 5, false));
            CHECK(!event(KC_TAB, 3, false));
            if (!prefix_first)
                CHECK(!event(MC_1, 5, false));
            drain();
            CHECK_INT(taps, 1);
            CHECK_INT(last_code, LCTL(KC_C)); /* forced Ctrl, including Apple */
            CHECK_INT(macros, 0);
            CHECK_INT(engine.mode, mode);
        }
        fresh((t9_mode_t)mode);
        event(MC_1, 5, true);
        tap(KC_ESC, 1); /* absent from M1 map: must not inherit slash+Esc */
        tap(KC_P8, 12);
        event(MC_1, 5, false);
        CHECK_INT(macros, 0);
        CHECK_INT(t9_output_count(&engine.out), 0);
        CHECK(!t9_is_composing(&engine));

        event(MC_1, 5, true);
        now += T9_SHORTCUT_TAP_MS;
        event(MC_1, 5, false);
        CHECK_INT(macros, 0);
    }
    fresh(T9_MODE_T9);
    type_th();
    tap(MC_1, 5);
    CHECK_STR(text_at_tap, "th"); /* preview edits drain before the macro */
    CHECK(!t9_is_composing(&engine));
    CHECK_INT(macros, 1);

    fresh(T9_MODE_T9);
    event(MC_1, 5, true);
    suspend_power_down_user();
    event(MC_1, 5, false);
    CHECK_INT(macros, 0); /* no deferred macro reaches another host */
    slash(true);
    suspend_power_down_user();
    slash(false);
    CHECK_INT(engine.caps, T9_CAPS_OFF);
}

static void test_late_chord_release_preserves_new_digit_hold(void) {
    fresh(T9_MODE_T9);
    slash(true);
    CHECK(!event(KC_TAB, 3, true));
    slash(false);
    CHECK(!event(KC_P4, 16, true)); /* start holding phone digit 4 */
    CHECK(engine.held);
    CHECK(!event(KC_TAB, 3, false)); /* old chord release must not cancel it */
    CHECK(engine.held);
    now += T9_LONG_PRESS_MS;
    housekeeping_task_user();
    CHECK(engine.held_fired);
    event(KC_P4, 16, false);
    drain();
    CHECK_STR(host.text, "4");
    CHECK_INT(taps, 1);
    CHECK_INT(last_code, EDIT(KC_C));
}

static void test_app_switch_hold_and_release(void) {
    for (int mode = 0; mode < T9_MODE_COUNT; mode++) {
        fresh((t9_mode_t)mode);
        slash(true);
        now += 500;
        housekeeping_task_user();
        CHECK_INT(mods, 0); /* prefix alone never holds Command/Alt */
        tap(KC_PPLS, 14);
        drain();
        CHECK_INT(last_code, KC_TAB);
        CHECK_INT(last_tap_mods, APP_MOD);
        CHECK_INT(mods, APP_MOD); /* releasing + keeps the chooser open */
        tap(KC_P8, 12);           /* unmapped chord leaves chooser open */
        CHECK_INT(mods, APP_MOD);
        tap(KC_PPLS, 14);
        drain();
        CHECK_INT(taps, 2);
        CHECK_INT(mod_presses, 1);
        CHECK_INT(mod_releases, 0);
        slash(false);
        CHECK_INT(mods, 0);
        CHECK_INT(mod_releases, 1);
        CHECK_INT(engine.caps, T9_CAPS_OFF);
    }

    fresh(T9_MODE_T9);
    mods = APP_MOD | MOD_BIT(KC_LSFT); /* independently held modifiers */
    slash(true);
    tap(KC_PPLS, 14);
    drain();
    slash(false);
    CHECK_INT(mods, APP_MOD | MOD_BIT(KC_LSFT));
    CHECK_INT(mod_releases, 0);
}

static void test_app_switch_backwards(void) {
    for (int mode = 0; mode < T9_MODE_COUNT; mode++) {
        for (int queued = 0; queued < 2; queued++) {
            fresh((t9_mode_t)mode);
            slash(true);
            tap(KC_PAST, 8);
            drain(); /* before +: ordinary Shift+Tab */
            CHECK_INT(last_code, LSFT(KC_TAB));
            CHECK_INT(last_tap_mods, 0);
            tap(KC_PPLS, 14);
            if (!queued)
                drain();
            tap(KC_PAST, 8);
            drain();
            CHECK_INT(last_code, LSFT(KC_TAB));
            CHECK_INT(last_tap_mods, APP_MOD);
            CHECK_INT(mods, APP_MOD);
            tap(KC_PAST, 8);
            drain(); /* repeat backwards */
            CHECK_INT(last_tap_mods, APP_MOD);
            tap(KC_PPLS, 14);
            drain(); /* then forwards again */
            CHECK_INT(last_code, KC_TAB);
            CHECK_INT(last_tap_mods, APP_MOD);
            CHECK_INT(mod_presses, 1);
            CHECK_INT(mod_releases, 0);
            event(KC_PAST, 8, true);
            if (!queued)
                drain();
            slash(false); /* prefix can release before the reverse key */
            event(KC_PAST, 8, false);
            drain();
            CHECK_INT(last_code, LSFT(KC_TAB));
            CHECK_INT(last_tap_mods, APP_MOD);
            CHECK_INT(mods, 0);
            CHECK_INT(mod_releases, 1);

            slash(true);
            tap(KC_PAST, 8);
            drain(); /* new gesture must not inherit state */
            CHECK_INT(last_code, LSFT(KC_TAB));
            CHECK_INT(last_tap_mods, 0);
            slash(false);
        }
    }

    fresh(T9_MODE_T9);
    slash(true);
    tap(KC_PPLS, 14);
    for (unsigned i = 1; i < T9_OUTPUT_QUEUE_LEN; i++)
        tap(KC_PAST, 8);
    CHECK_INT(t9_output_free(&engine.out), 0);
    tap(KC_PAST, 8); /* refused reverse tap must preserve the release boundary */
    CHECK(t9_take_events(&engine) & T9_EVENT_BUSY);
    slash(false);
    drain();
    CHECK_INT(taps, T9_OUTPUT_QUEUE_LEN);
    CHECK_INT(last_code, LSFT(KC_TAB));
    CHECK_INT(last_tap_mods, APP_MOD);
    CHECK_INT(mods, 0);
    CHECK_INT(mod_presses, 1);
    CHECK_INT(mod_releases, 1);

    fresh(T9_MODE_T9);
    slash(true);
    tap(KC_PPLS, 14);
    drain();
    tap(KC_PAST, 8);
    tap(KC_TAB, 3); /* copy ends switching after the queued reverse step */
    drain();
    CHECK_INT(last_code, EDIT(KC_C));
    CHECK_INT(last_tap_mods, 0);
    tap(KC_PAST, 8);
    drain();
    CHECK_INT(last_tap_mods, 0);
    tap(KC_PPLS, 14);
    drain();
    tap(KC_PAST, 8);
    unsigned previous_taps = taps;
    suspend_power_down_user();
    slash(false);
    drain();
    CHECK_INT(taps, previous_taps);
    CHECK_INT(mods, 0);
}

static void test_app_switch_queued_gestures(void) {
    for (int queued = 0; queued < 2; queued++) {
        fresh(T9_MODE_T9);
        type_th();
        if (!queued)
            drain();
        slash(true);
        tap(KC_PPLS, 14);
        tap(KC_PPLS, 14);
        slash(false);
        CHECK_INT(mods, 0); /* first complete gesture is still queued */
        slash(true);
        tap(KC_PPLS, 14); /* next gesture remains held */
        drain();
        CHECK_STR(text_at_mod_press, "th"); /* preview removed before modifier */
        CHECK_STR(text_at_tap, "th");
        CHECK_INT(taps, 3);
        CHECK_INT(mod_presses, 2);
        CHECK_INT(mod_releases, 1);
        CHECK_INT(mods, APP_MOD);
        slash(false);
        CHECK_INT(mods, 0);
        CHECK_INT(mod_releases, 2);
        tap(KC_P4, 16);
        drain();
        CHECK_INT(mods, 0); /* subsequent typing cannot inherit modifier */
    }
}

static void test_app_switch_other_actions_and_reset(void) {
    for (int queued = 0; queued < 2; queued++) {
        fresh(T9_MODE_T9);
        slash(true);
        tap(KC_PPLS, 14);
        if (!queued)
            drain();
        tap(KC_TAB, 3); /* finish switching before copy */
        drain();
        CHECK_INT(last_code, EDIT(KC_C));
        CHECK_INT(last_tap_mods, 0);
        CHECK_INT(mods, 0);
        CHECK_INT(mod_presses, 1);
        CHECK_INT(mod_releases, 1);
        tap(KC_PPLS, 14);
        drain(); /* restart without releasing slash */
        CHECK_INT(mods, APP_MOD);
        tap(KC_NUM, 6);
        drain();
        CHECK_INT(engine.mode, T9_MODE_NUMPAD);
        CHECK_INT(mods, 0);
        slash(false);

        fresh(T9_MODE_T9);
        slash(true);
        tap(KC_PPLS, 14);
        if (!queued)
            drain();
        tap(KC_PPLS, 14); /* reset drops remaining Tabs and releases held mod */
        suspend_power_down_user();
        slash(false);
        drain();
        CHECK_INT(mods, 0);
        CHECK_INT(taps, queued ? 0 : 1);
        CHECK_INT(t9_output_count(&engine.out), 0);
    }
}

static void test_app_switch_full_queue(void) {
    fresh(T9_MODE_T9);
    slash(true);
    for (unsigned i = 0; i < T9_OUTPUT_QUEUE_LEN; i++)
        tap(KC_PPLS, 14);
    CHECK_INT(t9_output_free(&engine.out), 0);
    tap(KC_PPLS, 14); /* refused without damaging the existing gesture */
    CHECK(t9_take_events(&engine) & T9_EVENT_BUSY);
    slash(false); /* must release even when no queue slot is available */
    drain();
    CHECK_INT(taps, T9_OUTPUT_QUEUE_LEN);
    CHECK_INT(mods, 0);
    CHECK_INT(mod_presses, 1);
    CHECK_INT(mod_releases, 1);

    fresh(T9_MODE_T9);
    while (t9_output_free(&engine.out))
        t9_output_push(&engine.out, T9_OUT_KEY, KC_TAB);
    slash(true);
    tap(KC_PPLS, 14); /* first + refused: never start holding */
    slash(false);
    drain();
    CHECK_INT(mod_presses, 0);
    CHECK_INT(mods, 0);
}

static void test_unique_word_without_suggestions(void) {
    static const struct {
        uint16_t key;
        const char *text;
    } cases[] = {
        {KC_PENT, "world "},
        {KC_P0, "worl "},
        {KC_PDOT, "worl"},
    };
    for (int queued = 0; queued < 2; queued++) {
        for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
            fresh(T9_MODE_T9);
            tap(T9_COMPLETIONS, 24); /* Fn+Enter disables suggestions */
            CHECK_INT(eeprom, 0);
            tap(KC_P3, 23);
            tap(KC_P6, 18);
            tap(KC_P1, 21);
            tap(KC_P5, 17); /* phone 9675 */
            CHECK_STR(t9_preview(&engine), "worl>d");
            if (!queued)
                drain();
            tap(cases[i].key, 24);
            drain();
            CHECK_STR(host.text, cases[i].text);
            CHECK_INT(taps, 0); /* completion does not send a host Enter */
            CHECK(!t9_is_composing(&engine));
        }
    }
}

static void check_rgb(uint8_t index, uint8_t r, uint8_t g, uint8_t b) {
    CHECK_INT(rgb[index][0], r);
    CHECK_INT(rgb[index][1], g);
    CHECK_INT(rgb[index][2], b);
}

/* Model a decorative frame rendered in batches, then the basic user hook,
 * Keychron notices and the final advanced batch, in the pinned QMK order. */
static void render_rgb_frame(void) {
    rgb_matrix_set_color_all(12, 34, 56);
    CHECK(rgb_matrix_indicators_advanced_user(0, 13));
    CHECK(rgb_matrix_indicators_user());
    rgb_matrix_set_color(0, 0, 0, 255);     /* connection notice */
    rgb_matrix_set_color(23, 255, 0, 0);    /* battery notice */
    rgb_matrix_set_color(5, 255, 255, 255); /* host Num Lock */
    CHECK(rgb_matrix_indicators_advanced_user(24, 26));
}

static void test_status_only_rgb(void) {
    CHECK_INT(T9_LED_COMPLETE, 7); /* visible * key, not hidden Enter */
    for (int mode = 0; mode < T9_MODE_COUNT; mode++) {
        fresh((t9_mode_t)mode);
        render_rgb_frame();
        for (uint8_t i = 0; i < RGB_MATRIX_LED_COUNT; i++) {
            if (i == 0 || i == 23 || i == T9_LED_MODE)
                continue;
            if (mode != T9_MODE_NUMPAD && i == T9_LED_COMPLETE) {
                check_rgb(i, T9_RGB_COMPLETIONS_ON);
            } else {
                check_rgb(i, 0, 0, 0); /* no decorative color remains */
            }
        }
        check_rgb(0, 0, 0, 255);
        check_rgb(23, 255, 0, 0);
        if (mode == T9_MODE_T9)
            check_rgb(T9_LED_MODE, T9_RGB_MODE_T9);
        else if (mode == T9_MODE_MULTITAP)
            check_rgb(T9_LED_MODE, T9_RGB_MODE_MULTITAP);
        else
            check_rgb(T9_LED_MODE, 255, 255, 255); /* stock Num Lock status */
    }

    fresh(T9_MODE_T9);
    tap(T9_COMPLETIONS, 24);
    tap(KC_P3, 23);
    tap(KC_P6, 18);
    tap(KC_P1, 21);
    tap(KC_P5, 17);
    CHECK_STR(t9_preview(&engine), "worl>d");
    render_rgb_frame();
    check_rgb(T9_LED_COMPLETE, T9_RGB_COMPLETE);
    check_rgb(T9_LED_ACCEPT, T9_RGB_ACCEPT);
    check_rgb(25, 0, 0, 0); /* hidden Enter LED stays dark */
    tap(KC_PENT, 24);
    render_rgb_frame();
    check_rgb(T9_LED_COMPLETE, 0, 0, 0);
    check_rgb(T9_LED_ACCEPT, 0, 0, 0);

    engine.caps     = T9_CAPS_LOCK;
    engine.spelling = true;
    render_rgb_frame();
    check_rgb(T9_LED_CAPS, T9_RGB_CAPS_LOCK);
    check_rgb(T9_LED_MODE, T9_RGB_SPELL);
    check_rgb(T9_LED_SPELL, T9_RGB_SPELL);
    engine.caps     = T9_CAPS_OFF;
    engine.spelling = false;
    flash_event     = T9_EVENT_MISS;
    flash_until     = now + T9_FLASH_MS;
    render_rgb_frame();
    const uint8_t digits[] = T9_LED_DIGITS;
    for (size_t i = 0; i < sizeof(digits); i++)
        check_rgb(digits[i], T9_RGB_MISS);
    now += T9_FLASH_MS;
    render_rgb_frame();
    for (size_t i = 0; i < sizeof(digits); i++)
        check_rgb(digits[i], 0, 0, 0);
    check_rgb(T9_LED_CAPS, 0, 0, 0);
    check_rgb(T9_LED_SPELL, 0, 0, 0);
}

static void test_wrapper_typing_chords(void) {
    for (int mode = T9_MODE_T9; mode <= T9_MODE_MULTITAP; mode++) {
        for (int queued = 0; queued < 2; queued++) {
            for (int fired = 0; fired < 2; fired++) {
                fresh((t9_mode_t)mode);
                CHECK(!event(KC_P7, 11, true)); /* phone 1, printed numpad 7 */
                if (fired) {
                    now += T9_LONG_PRESS_MS;
                    housekeeping_task_user();
                }
                if (!queued)
                    drain();
                tap(KC_P8, 12); /* phone 2 */
                CHECK_STR(t9_preview(&engine), "(");
                tap(KC_P8, 12);
                CHECK_STR(t9_preview(&engine), "[");
                tap(KC_P9, 13); /* phone 3 replaces [, starts at ) */
                CHECK_STR(t9_preview(&engine), ")");
                CHECK(!event(KC_P9, 13, true));
                CHECK_STR(t9_preview(&engine), "]");
                CHECK(!event(KC_P9, 13, false));
                const char *cycles[] = {":;|~", "+`@\\"};
                for (int group = 0; group < 2; group++) {
                    for (int i = 0; i < 4; i++) {
                        tap(group ? KC_P5 : KC_P4, group ? 17 : 16);
                        char expected[] = {cycles[group][i], 0};
                        CHECK_STR(t9_preview(&engine), expected);
                    }
                }
                CHECK(!event(KC_P5, 17, true)); /* wrap back to + */
                now += 600;
                housekeeping_task_user();
                CHECK(!event(KC_P7, 11, false)); /* release prefix first */
                CHECK(!event(KC_P5, 17, false));
                drain();
                CHECK_STR(host.text, "+");
                CHECK(!t9_is_composing(&engine));
                CHECK_INT(taps + registrations + unregistrations, 0);
            }
        }
    }

    fresh(T9_MODE_NUMPAD);
    CHECK(event(KC_P7, 11, true));
    now += T9_LONG_PRESS_MS;
    housekeeping_task_user();
    CHECK(event(KC_P8, 12, true));
    CHECK(event(KC_P8, 12, false));
    CHECK(event(KC_P9, 13, true));
    CHECK(event(KC_P4, 16, true));
    CHECK(event(KC_P4, 16, false));
    CHECK(event(KC_P5, 17, true));
    CHECK(event(KC_P5, 17, false));
    CHECK(event(KC_P7, 11, false));
    CHECK(event(KC_P9, 13, false));
    CHECK_INT(t9_output_count(&engine.out), 0); /* stock numpad rollover */
}

static void test_family_completion_keys_and_led(void) {
    static const t9_simple_entry_t words[] = {{"canes", 63}, {"cane", 40}, {"can", 20}};
    for (int queued = 0; queued < 2; queued++) {
        fresh(T9_MODE_T9);
        t9_dict_simple_set(words, sizeof(words) / sizeof(words[0]));
        tap(T9_COMPLETIONS, 24);
        tap(KC_P8, 12);
        tap(KC_P8, 12); /* phone 22 */
        CHECK_STR(t9_preview(&engine), "ca>n");
        if (!queued)
            drain();
        tap(KC_PENT, 24);
        CHECK_STR(t9_preview(&engine), "can>e");
        render_rgb_frame();
        check_rgb(T9_LED_COMPLETE, T9_RGB_COMPLETE);
        check_rgb(T9_LED_ACCEPT, T9_RGB_ACCEPT);
        if (!queued)
            drain();
        tap(KC_PENT, 24);
        CHECK_STR(t9_preview(&engine), "cane>s");
        tap(KC_P0, 24);
        drain();
        CHECK_STR(host.text, "cane ");
        CHECK_INT(taps, 0); /* no host Enter during intermediate steps */
        CHECK(!t9_is_composing(&engine));
        render_rgb_frame();
        check_rgb(T9_LED_COMPLETE, 0, 0, 0);
        check_rgb(T9_LED_ACCEPT, 0, 0, 0);
    }
}

int main(void) {
    RUN_TEST(test_word_delete_profiles);
    RUN_TEST(test_wireless_flush_failures);
    RUN_TEST(test_family_completion_keys_and_led);
    RUN_TEST(test_wrapper_typing_chords);
    RUN_TEST(test_status_only_rgb);
    RUN_TEST(test_unique_word_without_suggestions);
    RUN_TEST(test_slash_tap_hold_and_unmapped);
    RUN_TEST(test_inherited_shortcuts_and_release_orders);
    RUN_TEST(test_reverse_cycle_and_dial);
    RUN_TEST(test_composition_order_refusal_and_reset);
    RUN_TEST(test_existing_release_and_fn_behavior);
    RUN_TEST(test_m1_tap_hold_and_interrupt);
    RUN_TEST(test_late_chord_release_preserves_new_digit_hold);
    RUN_TEST(test_app_switch_hold_and_release);
    RUN_TEST(test_app_switch_backwards);
    RUN_TEST(test_app_switch_queued_gestures);
    RUN_TEST(test_app_switch_other_actions_and_reset);
    RUN_TEST(test_app_switch_full_queue);
    return t9_test_summary(T9_WORD_NAV_APPLE_USB ? "t9_keymap_test (Apple)" : "t9_keymap_test (PC)");
}
