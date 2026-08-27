/*
 * SPDX-License-Identifier: MIT
 */

#include "cr11_event.h"

enum {
    ZCL_ON_OFF_OFF = 0x00,
    ZCL_ON_OFF_ON = 0x01,
    ZCL_ON_OFF_TOGGLE = 0x02,

    ZCL_LEVEL_MOVE_TO = 0x00,
    ZCL_LEVEL_MOVE = 0x01,
    ZCL_LEVEL_STEP = 0x02,
    ZCL_LEVEL_STOP = 0x03,
    ZCL_LEVEL_MOVE_TO_WITH_ON_OFF = 0x04,
    ZCL_LEVEL_MOVE_WITH_ON_OFF = 0x05,
    ZCL_LEVEL_STEP_WITH_ON_OFF = 0x06,
    ZCL_LEVEL_STOP_WITH_ON_OFF = 0x07,

    ZCL_WINDOW_OPEN = 0x00,
    ZCL_WINDOW_CLOSE = 0x01,
    ZCL_WINDOW_STOP = 0x02,
    ZCL_WINDOW_GOTO_LIFT_VALUE = 0x04,
    ZCL_WINDOW_GOTO_LIFT_PERCENTAGE = 0x05,
};

static cr11_event_t event_of(cr11_event_kind_t kind)
{
    const cr11_event_t event = {
        .kind = kind,
        .direction = CR11_DIRECTION_NONE,
        .value = 0,
        .transition_time = 0,
        .has_value = false,
        .with_on_off = false,
    };
    return event;
}

static bool payload_is_exact(const uint8_t *payload,
                             size_t payload_length,
                             size_t expected_length)
{
    return payload_length == expected_length &&
           (expected_length == 0U || payload != NULL);
}

static cr11_event_t malformed(void)
{
    return event_of(CR11_EVENT_MALFORMED);
}

static cr11_event_t decode_on_off(uint8_t command_id,
                                 const uint8_t *payload,
                                 size_t payload_length)
{
    cr11_event_kind_t kind = CR11_EVENT_UNKNOWN;

    switch (command_id) {
    case ZCL_ON_OFF_OFF:
        kind = CR11_EVENT_ON_OFF_OFF;
        break;
    case ZCL_ON_OFF_ON:
        kind = CR11_EVENT_ON_OFF_ON;
        break;
    case ZCL_ON_OFF_TOGGLE:
        kind = CR11_EVENT_ON_OFF_TOGGLE;
        break;
    default:
        return event_of(CR11_EVENT_UNKNOWN);
    }

    return payload_is_exact(payload, payload_length, 0U) ? event_of(kind)
                                                         : malformed();
}

static cr11_direction_t decode_direction(uint8_t value)
{
    if (value == 0U) {
        return CR11_DIRECTION_UP;
    }
    if (value == 1U) {
        return CR11_DIRECTION_DOWN;
    }
    return CR11_DIRECTION_NONE;
}

static cr11_event_t decode_level(uint8_t command_id,
                                const uint8_t *payload,
                                size_t payload_length)
{
    cr11_event_t event = event_of(CR11_EVENT_UNKNOWN);

    switch (command_id) {
    case ZCL_LEVEL_MOVE_TO:
    case ZCL_LEVEL_MOVE_TO_WITH_ON_OFF:
        if (!payload_is_exact(payload, payload_length, 3U)) {
            return malformed();
        }
        event.kind = CR11_EVENT_LEVEL_MOVE_TO;
        event.value = payload[0];
        event.transition_time =
            (uint16_t)payload[1] | ((uint16_t)payload[2] << 8U);
        event.has_value = true;
        event.with_on_off = command_id == ZCL_LEVEL_MOVE_TO_WITH_ON_OFF;
        return event;

    case ZCL_LEVEL_MOVE:
    case ZCL_LEVEL_MOVE_WITH_ON_OFF:
        if (!payload_is_exact(payload, payload_length, 2U)) {
            return malformed();
        }
        event.direction = decode_direction(payload[0]);
        if (event.direction == CR11_DIRECTION_NONE) {
            return malformed();
        }
        event.kind = CR11_EVENT_LEVEL_MOVE;
        event.value = payload[1];
        event.has_value = true;
        event.with_on_off = command_id == ZCL_LEVEL_MOVE_WITH_ON_OFF;
        return event;

    case ZCL_LEVEL_STEP:
    case ZCL_LEVEL_STEP_WITH_ON_OFF:
        if (!payload_is_exact(payload, payload_length, 4U)) {
            return malformed();
        }
        event.direction = decode_direction(payload[0]);
        if (event.direction == CR11_DIRECTION_NONE) {
            return malformed();
        }
        event.kind = CR11_EVENT_LEVEL_STEP;
        event.value = payload[1];
        event.has_value = true;
        event.with_on_off = command_id == ZCL_LEVEL_STEP_WITH_ON_OFF;
        return event;

    case ZCL_LEVEL_STOP:
    case ZCL_LEVEL_STOP_WITH_ON_OFF:
        if (!payload_is_exact(payload, payload_length, 0U)) {
            return malformed();
        }
        event.kind = CR11_EVENT_LEVEL_STOP;
        event.with_on_off = command_id == ZCL_LEVEL_STOP_WITH_ON_OFF;
        return event;

    default:
        return event;
    }
}

static cr11_event_t decode_window(uint8_t command_id,
                                 const uint8_t *payload,
                                 size_t payload_length)
{
    cr11_event_t event = event_of(CR11_EVENT_UNKNOWN);

    switch (command_id) {
    case ZCL_WINDOW_OPEN:
        return payload_is_exact(payload, payload_length, 0U)
                   ? event_of(CR11_EVENT_WINDOW_OPEN)
                   : malformed();
    case ZCL_WINDOW_CLOSE:
        return payload_is_exact(payload, payload_length, 0U)
                   ? event_of(CR11_EVENT_WINDOW_CLOSE)
                   : malformed();
    case ZCL_WINDOW_STOP:
        return payload_is_exact(payload, payload_length, 0U)
                   ? event_of(CR11_EVENT_WINDOW_STOP)
                   : malformed();
    case ZCL_WINDOW_GOTO_LIFT_VALUE:
        if (!payload_is_exact(payload, payload_length, 2U)) {
            return malformed();
        }
        event.kind = CR11_EVENT_WINDOW_GOTO_LIFT_VALUE;
        event.value = (uint16_t)payload[0] | ((uint16_t)payload[1] << 8U);
        event.has_value = true;
        return event;
    case ZCL_WINDOW_GOTO_LIFT_PERCENTAGE:
        if (!payload_is_exact(payload, payload_length, 1U) || payload[0] > 100U) {
            return malformed();
        }
        event.kind = CR11_EVENT_WINDOW_GOTO_LIFT_PERCENTAGE;
        event.value = payload[0];
        event.has_value = true;
        return event;
    default:
        return event;
    }
}

cr11_event_t cr11_event_decode(uint16_t cluster_id,
                               uint8_t command_id,
                               const uint8_t *payload,
                               size_t payload_length)
{
    if (payload_length > 0U && payload == NULL) {
        return malformed();
    }

    switch (cluster_id) {
    case CR11_ZCL_CLUSTER_ON_OFF:
        return decode_on_off(command_id, payload, payload_length);
    case CR11_ZCL_CLUSTER_LEVEL_CONTROL:
        return decode_level(command_id, payload, payload_length);
    case CR11_ZCL_CLUSTER_WINDOW_COVERING:
        return decode_window(command_id, payload, payload_length);
    default:
        return event_of(CR11_EVENT_UNKNOWN);
    }
}

const char *cr11_direction_name(cr11_direction_t direction)
{
    switch (direction) {
    case CR11_DIRECTION_UP:
        return "up";
    case CR11_DIRECTION_DOWN:
        return "down";
    case CR11_DIRECTION_NONE:
    default:
        return "none";
    }
}

const char *cr11_event_name(const cr11_event_t *event)
{
    if (event == NULL) {
        return "invalid";
    }

    switch (event->kind) {
    case CR11_EVENT_MALFORMED:
        return "malformed";
    case CR11_EVENT_ON_OFF_OFF:
        return "on_off.off";
    case CR11_EVENT_ON_OFF_ON:
        return "on_off.on";
    case CR11_EVENT_ON_OFF_TOGGLE:
        return "on_off.toggle";
    case CR11_EVENT_LEVEL_MOVE_TO:
        return "level.move_to";
    case CR11_EVENT_LEVEL_MOVE:
        return "level.move";
    case CR11_EVENT_LEVEL_STEP:
        return "level.step";
    case CR11_EVENT_LEVEL_STOP:
        return "level.stop";
    case CR11_EVENT_WINDOW_OPEN:
        return "window.open";
    case CR11_EVENT_WINDOW_CLOSE:
        return "window.close";
    case CR11_EVENT_WINDOW_STOP:
        return "window.stop";
    case CR11_EVENT_WINDOW_GOTO_LIFT_VALUE:
        return "window.goto_lift_value";
    case CR11_EVENT_WINDOW_GOTO_LIFT_PERCENTAGE:
        return "window.goto_lift_percentage";
    case CR11_EVENT_UNKNOWN:
    default:
        return "unknown";
    }
}

bool cr11_event_is_upper_hold_marker(const cr11_event_t *event)
{
    return event != NULL && event->kind == CR11_EVENT_LEVEL_MOVE_TO &&
           event->has_value && !event->with_on_off &&
           event->value == CR11_UPPER_HOLD_MARKER_LEVEL &&
           event->transition_time == CR11_UPPER_MARKER_TRANSITION_TIME;
}

bool cr11_event_is_upper_release_marker(const cr11_event_t *event)
{
    return event != NULL && event->kind == CR11_EVENT_LEVEL_MOVE_TO &&
           event->has_value && event->with_on_off &&
           event->value == CR11_UPPER_RELEASE_MARKER_LEVEL &&
           event->transition_time == CR11_UPPER_MARKER_TRANSITION_TIME;
}

static const char *upper_button(bool right_column, bool secondary_target)
{
    if (right_column) {
        return secondary_target ? "right.upper.double-square"
                                : "right.upper.square";
    }
    return secondary_target ? "left.upper.OO" : "left.upper.O";
}

const char *cr11_event_button_guess(const cr11_event_t *event,
                                    bool right_column,
                                    bool secondary_target)
{
    if (event == NULL) {
        return "invalid";
    }

    switch (event->kind) {
    case CR11_EVENT_ON_OFF_OFF:
        return right_column ? "right.upper.square" : "left.upper.O";
    case CR11_EVENT_ON_OFF_ON:
        return right_column ? "right.upper.double-square" : "left.upper.OO";
    case CR11_EVENT_ON_OFF_TOGGLE:
        return upper_button(right_column, secondary_target);
    case CR11_EVENT_LEVEL_MOVE_TO:
        return cr11_event_is_upper_hold_marker(event) ||
                       cr11_event_is_upper_release_marker(event)
                   ? upper_button(right_column, secondary_target)
                   : "unknown";
    case CR11_EVENT_LEVEL_MOVE:
    case CR11_EVENT_LEVEL_STEP:
        if (right_column) {
            return event->direction == CR11_DIRECTION_UP ? "right.lower.plus"
                   : event->direction == CR11_DIRECTION_DOWN
                       ? "right.lower.minus"
                       : "right.lower.unknown";
        }
        return event->direction == CR11_DIRECTION_UP ? "left.lower.plus"
               : event->direction == CR11_DIRECTION_DOWN
                   ? "left.lower.minus"
                   : "left.lower.unknown";
    case CR11_EVENT_LEVEL_STOP:
        return right_column ? "right.lower.stop" : "left.lower.stop";
    case CR11_EVENT_WINDOW_OPEN:
        return "right.lower.plus";
    case CR11_EVENT_WINDOW_CLOSE:
        return "right.lower.minus";
    case CR11_EVENT_WINDOW_STOP:
        return "right.lower.stop";
    case CR11_EVENT_WINDOW_GOTO_LIFT_PERCENTAGE:
        if (event->value == 0U) {
            return "right.upper.square";
        }
        if (event->value == 100U) {
            return "right.upper.double-square";
        }
        return "right.upper.percentage";
    case CR11_EVENT_MALFORMED:
        return "malformed";
    case CR11_EVENT_UNKNOWN:
    case CR11_EVENT_WINDOW_GOTO_LIFT_VALUE:
    default:
        return "unknown";
    }
}
