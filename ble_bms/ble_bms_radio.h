// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "ble.h"
#include <stdbool.h>
#include <stdint.h>
void ble_bms_radio_init(void);
bool ble_bms_radio_packet(const uint8_t *p,unsigned n);
bool ble_bms_radio_event(ble_evt_t const *event);
void ble_bms_radio_tick(void);
void ble_bms_radio_uart_error(uint32_t error);
