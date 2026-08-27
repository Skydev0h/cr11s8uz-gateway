/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define CR11_ZCL_CLUSTER_ON_OFF          0x0006U
#define CR11_ZCL_CLUSTER_LEVEL_CONTROL   0x0008U
#define CR11_ZCL_CLUSTER_WINDOW_COVERING 0x0102U

#define CR11_UPPER_HOLD_MARKER_LEVEL       0xa5U
#define CR11_UPPER_RELEASE_MARKER_LEVEL    0x5aU
#define CR11_UPPER_MARKER_TRANSITION_TIME  0x0000U

typedef enum {
    CR11_DIRECTION_NONE = 0,
    CR11_DIRECTION_UP,
    CR11_DIRECTION_DOWN,
} cr11_direction_t;

typedef enum {
    CR11_EVENT_UNKNOWN = 0,
    CR11_EVENT_MALFORMED,
    CR11_EVENT_ON_OFF_OFF,
    CR11_EVENT_ON_OFF_ON,
    CR11_EVENT_ON_OFF_TOGGLE,
    CR11_EVENT_LEVEL_MOVE_TO,
    CR11_EVENT_LEVEL_MOVE,
    CR11_EVENT_LEVEL_STEP,
    CR11_EVENT_LEVEL_STOP,
    CR11_EVENT_WINDOW_OPEN,
    CR11_EVENT_WINDOW_CLOSE,
    CR11_EVENT_WINDOW_STOP,
    CR11_EVENT_WINDOW_GOTO_LIFT_VALUE,
    CR11_EVENT_WINDOW_GOTO_LIFT_PERCENTAGE,
} cr11_event_kind_t;

typedef struct {
    cr11_event_kind_t kind;
    cr11_direction_t direction;
    uint16_t value;
    uint16_t transition_time;
    bool has_value;
    bool with_on_off;
} cr11_event_t;

cr11_event_t cr11_event_decode(uint16_t cluster_id,
                               uint8_t command_id,
                               const uint8_t *payload,
                               size_t payload_length);

const char *cr11_event_name(const cr11_event_t *event);
bool cr11_event_is_upper_hold_marker(const cr11_event_t *event);
bool cr11_event_is_upper_release_marker(const cr11_event_t *event);
const char *cr11_event_button_guess(const cr11_event_t *event,
                                    bool right_column,
                                    bool secondary_target);
const char *cr11_direction_name(cr11_direction_t direction);
