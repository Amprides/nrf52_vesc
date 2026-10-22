// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// Existing COMM_CUSTOM_APP_DATA envelope, private BB namespace, major version.
// Header: [COMM=36, 'B', 'B', version=1, operation, sequence].
#define BB_COMM 36
#define BB_VERSION 1
#define BB_HEADER 6
#define BB_MAX_DATA 244
#define BB_MAX_PACKET (BB_HEADER + BB_MAX_DATA)
enum {
    BB_HELLO=1, BB_SCAN=2, BB_STOP=3, BB_CONNECT=4, BB_DISCONNECT=5, BB_WRITE=6,
    BB_DEVICE=32, BB_STATE=33, BB_NOTIFY=34,
    BB_STATUS=64, BB_CONFIG=65, BB_FORGET=66, BB_RECONNECT=67, BB_EVENTS=68,
    BB_ACK=128
};
enum { BB_IDLE, BB_SCANNING, BB_CONNECTING, BB_DISCOVERING, BB_ACTIVE, BB_ERROR };
static inline bool bb_namespace(const uint8_t *p, size_t n) {
    return n >= 3 && p[0] == BB_COMM && p[1] == 'B' && p[2] == 'B';
}
static inline bool bb_valid(const uint8_t *p, size_t n) {
    return bb_namespace(p,n) && n >= BB_HEADER && n <= BB_MAX_PACKET && p[3] == BB_VERSION;
}
static inline bool bb_radio_event(uint8_t op) {
    return (op & BB_ACK) || op == BB_DEVICE || op == BB_STATE || op == BB_NOTIFY;
}
static inline bool bb_tool_request(uint8_t op) {
    return op == BB_SCAN || op == BB_STOP || (op >= BB_STATUS && op <= BB_EVENTS);
}
static inline void bb_header(uint8_t *p, uint8_t op, uint8_t seq) {
    p[0]=BB_COMM; p[1]='B'; p[2]='B'; p[3]=BB_VERSION; p[4]=op; p[5]=seq;
}
