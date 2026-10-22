// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// 2026-10-21 reset-catch RAM probe decoded CRC-valid controller HELLO frames
// on P0.06. The installed firmware's RX=5/TX=6 assignment was backwards.
// Keep build-time overrides for other board revisions; confirm TX by an actual
// end-to-end VESC firmware query, not just an advertisement or PSEL read.
#ifndef BLE_BMS_TRONIC_RX
#define BLE_BMS_TRONIC_RX 6
#endif
#ifndef BLE_BMS_TRONIC_TX
#define BLE_BMS_TRONIC_TX 5
#endif
#if BLE_BMS_TRONIC_RX > 47 || BLE_BMS_TRONIC_TX > 47 || BLE_BMS_TRONIC_RX == BLE_BMS_TRONIC_TX
#error Invalid Tronic UART pin selection
#endif

// Leave unverified LED/alternate-TX pins untouched. SDK UART supports a
// disconnected PSEL value when forwarding is disabled by the controller.
#define BLE_BMS_TRONIC_UNUSED_PIN 0xFFFFFFFFu
