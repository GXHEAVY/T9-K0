/* Host test boundary for Keychron radio state and servicing.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#pragma once
typedef enum {
    WT_RESET,
    WT_INITIALIZED,
    WT_DISCONNECTED,
    WT_CONNECTED,
    WT_PARING,
    WT_RECONNECTING,
    WT_SUSPEND
} wt_state_t;
wt_state_t wireless_get_state(void);
void send_string_task(void);
