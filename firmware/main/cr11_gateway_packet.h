/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "cr11_event.h"

#define CR11_LEFT_O_ENDPOINT              10U
#define CR11_LEFT_DOUBLE_O_ENDPOINT       11U
#define CR11_RIGHT_SQUARE_ENDPOINT        12U
#define CR11_RIGHT_DOUBLE_SQUARE_ENDPOINT 13U
#define CR11_WINDOW_ENDPOINT              20U
#define CR11_GATEWAY_ENDPOINT             21U

#define CR11_GATEWAY_CLUSTER_ID           0xfc11U
#define CR11_GATEWAY_EVENT_COMMAND_ID     0x00U
#define CR11_GATEWAY_READY_COMMAND_ID     0x01U
#define CR11_GATEWAY_HEARTBEAT_COMMAND_ID 0x02U
#define CR11_GATEWAY_COORDINATOR_ENDPOINT 1U
#define CR11_GATEWAY_PACKET_VERSION       1U
#define CR11_GATEWAY_PACKET_SIZE          32U

#define CR11_GATEWAY_FLAG_HAS_VALUE          0x01U
#define CR11_GATEWAY_FLAG_WITH_ON_OFF        0x02U
#define CR11_GATEWAY_FLAG_DIRECTION_INFERRED 0x04U
#define CR11_GATEWAY_FLAG_FORWARDABLE        0x08U
#define CR11_GATEWAY_FLAG_MASK               0x0fU

typedef enum {
    CR11_GATEWAY_ACTION_UNKNOWN = 0,
    CR11_GATEWAY_ACTION_CLICK = 1,
    CR11_GATEWAY_ACTION_HOLD = 2,
    CR11_GATEWAY_ACTION_RELEASE = 3,
} cr11_gateway_action_t;

/* Stable wire values; intentionally independent of the ESP Zigbee SDK enum. */
typedef enum {
    CR11_GATEWAY_SOURCE_UNKNOWN = 0,
    CR11_GATEWAY_SOURCE_SHORT = 1,
    CR11_GATEWAY_SOURCE_EXTENDED = 2,
    CR11_GATEWAY_SOURCE_GROUP = 3,
} cr11_gateway_source_mode_t;

typedef struct {
    uint8_t version;
    cr11_gateway_action_t action;
    cr11_event_kind_t event_kind;
    cr11_direction_t direction;
    uint8_t flags;
    uint8_t selector;
    uint8_t button;
    cr11_gateway_source_mode_t source_mode;
    uint8_t source_endpoint;
    uint8_t target_endpoint;
    uint8_t zcl_transaction_sequence;
    uint16_t profile_id;
    uint16_t cluster_id;
    uint8_t command_id;
    int8_t rssi;
    uint16_t value;
    uint64_t source_address;
    uint32_t sequence;
} cr11_gateway_packet_t;

bool cr11_gateway_packet_build(cr11_gateway_packet_t *packet,
                               const cr11_event_t *event,
                               cr11_direction_t prior_direction,
                               cr11_gateway_source_mode_t source_mode,
                               uint64_t source_address,
                               uint8_t source_endpoint,
                               uint8_t target_endpoint,
                               uint8_t zcl_transaction_sequence,
                               uint16_t profile_id,
                               uint16_t cluster_id,
                               uint8_t command_id,
                               int8_t rssi,
                               uint32_t sequence);

bool cr11_gateway_packet_encode(const cr11_gateway_packet_t *packet,
                                uint8_t output[CR11_GATEWAY_PACKET_SIZE]);

bool cr11_gateway_packet_decode(const uint8_t *input,
                                size_t input_length,
                                cr11_gateway_packet_t *packet);

bool cr11_gateway_packet_format_json(const cr11_gateway_packet_t *packet,
                                     char *output,
                                     size_t output_size);

bool cr11_gateway_packet_is_forwardable(const cr11_gateway_packet_t *packet);
const char *cr11_gateway_action_name(cr11_gateway_action_t action);
const char *cr11_gateway_source_mode_name(cr11_gateway_source_mode_t mode);
