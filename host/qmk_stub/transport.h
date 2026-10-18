/* Host test boundary for Keychron transport selection.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#pragma once
typedef enum { TRANSPORT_NONE, TRANSPORT_USB = 1, TRANSPORT_BLUETOOTH = 2, TRANSPORT_P2P4 = 4 } transport_t;
#define TRANSPORT_WIRELESS (TRANSPORT_BLUETOOTH | TRANSPORT_P2P4)
transport_t get_transport(void);
