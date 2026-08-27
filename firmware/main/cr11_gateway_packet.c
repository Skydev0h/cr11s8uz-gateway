/*
 * SPDX-License-Identifier: MIT
 */

#include "cr11_gateway_packet.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

static uint8_t selector_for_endpoint(uint8_t endpoint)
{
    switch (endpoint) {
    case CR11_LEFT_O_ENDPOINT:
        return 1U;
    case CR11_RIGHT_SQUARE_ENDPOINT:
        return 2U;
    case CR11_LEFT_DOUBLE_O_ENDPOINT:
        return 3U;
    case CR11_RIGHT_DOUBLE_SQUARE_ENDPOINT:
        return 4U;
    default:
        return 0U;
    }
}

static bool is_left_level_endpoint(uint8_t endpoint)
{
    return endpoint == CR11_LEFT_O_ENDPOINT ||
           endpoint == CR11_LEFT_DOUBLE_O_ENDPOINT;
}

static bool is_right_level_endpoint(uint8_t endpoint)
{
    return endpoint == CR11_RIGHT_SQUARE_ENDPOINT ||
           endpoint == CR11_RIGHT_DOUBLE_SQUARE_ENDPOINT;
}

static uint8_t lower_button(uint8_t endpoint, cr11_direction_t direction)
{
    if (is_left_level_endpoint(endpoint)) {
        if (direction == CR11_DIRECTION_UP) {
            return 5U;
        }
        if (direction == CR11_DIRECTION_DOWN) {
            return 6U;
        }
    } else if (is_right_level_endpoint(endpoint) ||
               endpoint == CR11_WINDOW_ENDPOINT) {
        if (direction == CR11_DIRECTION_UP) {
            return 7U;
        }
        if (direction == CR11_DIRECTION_DOWN) {
            return 8U;
        }
    }
    return 0U;
}

static void map_event(cr11_gateway_packet_t *packet,
                      const cr11_event_t *event,
                      cr11_direction_t prior_direction)
{
    packet->selector = selector_for_endpoint(packet->target_endpoint);
    packet->direction = event->direction;

    switch (event->kind) {
    case CR11_EVENT_ON_OFF_OFF:
    case CR11_EVENT_ON_OFF_ON:
    case CR11_EVENT_ON_OFF_TOGGLE:
        packet->button = packet->selector;
        packet->action = packet->button == 0U
                             ? CR11_GATEWAY_ACTION_UNKNOWN
                             : CR11_GATEWAY_ACTION_CLICK;
        break;

    case CR11_EVENT_LEVEL_MOVE_TO:
        if (packet->selector != 0U &&
            cr11_event_is_upper_hold_marker(event)) {
            packet->button = packet->selector;
            packet->action = CR11_GATEWAY_ACTION_HOLD;
        } else if (packet->selector != 0U &&
                   cr11_event_is_upper_release_marker(event)) {
            packet->button = packet->selector;
            packet->action = CR11_GATEWAY_ACTION_RELEASE;
        }
        break;

    case CR11_EVENT_LEVEL_STEP:
        packet->button = lower_button(packet->target_endpoint,
                                      event->direction);
        packet->action = packet->button == 0U
                             ? CR11_GATEWAY_ACTION_UNKNOWN
                             : CR11_GATEWAY_ACTION_CLICK;
        break;

    case CR11_EVENT_LEVEL_MOVE:
        packet->button = lower_button(packet->target_endpoint,
                                      event->direction);
        packet->action = packet->button == 0U
                             ? CR11_GATEWAY_ACTION_UNKNOWN
                             : CR11_GATEWAY_ACTION_HOLD;
        break;

    case CR11_EVENT_LEVEL_STOP:
        packet->direction = prior_direction;
        packet->button = lower_button(packet->target_endpoint,
                                      prior_direction);
        packet->action = packet->button == 0U
                             ? CR11_GATEWAY_ACTION_UNKNOWN
                             : CR11_GATEWAY_ACTION_RELEASE;
        if (packet->button != 0U) {
            packet->flags |= CR11_GATEWAY_FLAG_DIRECTION_INFERRED;
        }
        break;

    case CR11_EVENT_WINDOW_OPEN:
        packet->direction = CR11_DIRECTION_UP;
        packet->button = 7U;
        packet->action = CR11_GATEWAY_ACTION_HOLD;
        break;

    case CR11_EVENT_WINDOW_CLOSE:
        packet->direction = CR11_DIRECTION_DOWN;
        packet->button = 8U;
        packet->action = CR11_GATEWAY_ACTION_HOLD;
        break;

    case CR11_EVENT_WINDOW_STOP:
        packet->direction = prior_direction;
        packet->button = lower_button(CR11_WINDOW_ENDPOINT, prior_direction);
        packet->action = packet->button == 0U
                             ? CR11_GATEWAY_ACTION_UNKNOWN
                             : CR11_GATEWAY_ACTION_RELEASE;
        if (packet->button != 0U) {
            packet->flags |= CR11_GATEWAY_FLAG_DIRECTION_INFERRED;
        }
        break;

    case CR11_EVENT_WINDOW_GOTO_LIFT_PERCENTAGE:
        if (event->value == 0U) {
            packet->selector = 2U;
            packet->button = 2U;
            packet->action = CR11_GATEWAY_ACTION_CLICK;
        } else if (event->value == 100U) {
            packet->selector = 4U;
            packet->button = 4U;
            packet->action = CR11_GATEWAY_ACTION_CLICK;
        }
        break;

    case CR11_EVENT_UNKNOWN:
    case CR11_EVENT_MALFORMED:
    case CR11_EVENT_WINDOW_GOTO_LIFT_VALUE:
    default:
        break;
    }
}

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
                               uint32_t sequence)
{
    if (packet == NULL || event == NULL ||
        event->kind <= CR11_EVENT_UNKNOWN ||
        event->kind > CR11_EVENT_WINDOW_GOTO_LIFT_PERCENTAGE ||
        source_mode > CR11_GATEWAY_SOURCE_GROUP) {
        return false;
    }

    *packet = (cr11_gateway_packet_t){
        .version = CR11_GATEWAY_PACKET_VERSION,
        .action = CR11_GATEWAY_ACTION_UNKNOWN,
        .event_kind = event->kind,
        .direction = CR11_DIRECTION_NONE,
        .flags = 0U,
        .selector = 0U,
        .button = 0U,
        .source_mode = source_mode,
        .source_endpoint = source_endpoint,
        .target_endpoint = target_endpoint,
        .zcl_transaction_sequence = zcl_transaction_sequence,
        .profile_id = profile_id,
        .cluster_id = cluster_id,
        .command_id = command_id,
        .rssi = rssi,
        .value = event->value,
        .source_address = source_address,
        .sequence = sequence,
    };

    if (event->has_value) {
        packet->flags |= CR11_GATEWAY_FLAG_HAS_VALUE;
    }
    if (event->with_on_off) {
        packet->flags |= CR11_GATEWAY_FLAG_WITH_ON_OFF;
    }

    map_event(packet, event, prior_direction);
    if (packet->action != CR11_GATEWAY_ACTION_UNKNOWN &&
        packet->button >= 1U && packet->button <= 8U) {
        packet->flags |= CR11_GATEWAY_FLAG_FORWARDABLE;
    }
    return true;
}

static void write_u16_le(uint8_t *output, uint16_t value)
{
    output[0] = (uint8_t)(value & 0xffU);
    output[1] = (uint8_t)(value >> 8U);
}

static void write_u32_le(uint8_t *output, uint32_t value)
{
    for (size_t index = 0; index < 4U; ++index) {
        output[index] = (uint8_t)(value >> (index * 8U));
    }
}

static void write_u64_le(uint8_t *output, uint64_t value)
{
    for (size_t index = 0; index < 8U; ++index) {
        output[index] = (uint8_t)(value >> (index * 8U));
    }
}

static uint16_t read_u16_le(const uint8_t *input)
{
    return (uint16_t)input[0] | ((uint16_t)input[1] << 8U);
}

static uint32_t read_u32_le(const uint8_t *input)
{
    uint32_t value = 0U;
    for (size_t index = 0; index < 4U; ++index) {
        value |= (uint32_t)input[index] << (index * 8U);
    }
    return value;
}

static uint64_t read_u64_le(const uint8_t *input)
{
    uint64_t value = 0U;
    for (size_t index = 0; index < 8U; ++index) {
        value |= (uint64_t)input[index] << (index * 8U);
    }
    return value;
}

bool cr11_gateway_packet_encode(const cr11_gateway_packet_t *packet,
                                uint8_t output[CR11_GATEWAY_PACKET_SIZE])
{
    if (packet == NULL || output == NULL ||
        packet->version != CR11_GATEWAY_PACKET_VERSION ||
        packet->action > CR11_GATEWAY_ACTION_RELEASE ||
        packet->event_kind <= CR11_EVENT_UNKNOWN ||
        packet->event_kind > CR11_EVENT_WINDOW_GOTO_LIFT_PERCENTAGE ||
        packet->direction > CR11_DIRECTION_DOWN ||
        (packet->flags & (uint8_t)~CR11_GATEWAY_FLAG_MASK) != 0U ||
        packet->selector > 4U || packet->button > 8U ||
        packet->source_mode > CR11_GATEWAY_SOURCE_GROUP) {
        return false;
    }

    output[0] = packet->version;
    output[1] = CR11_GATEWAY_PACKET_SIZE;
    output[2] = (uint8_t)packet->action;
    output[3] = (uint8_t)packet->event_kind;
    output[4] = (uint8_t)packet->direction;
    output[5] = packet->flags;
    output[6] = packet->selector;
    output[7] = packet->button;
    output[8] = (uint8_t)packet->source_mode;
    output[9] = packet->source_endpoint;
    output[10] = packet->target_endpoint;
    output[11] = packet->zcl_transaction_sequence;
    write_u16_le(&output[12], packet->profile_id);
    write_u16_le(&output[14], packet->cluster_id);
    output[16] = packet->command_id;
    output[17] = (uint8_t)packet->rssi;
    write_u16_le(&output[18], packet->value);
    write_u64_le(&output[20], packet->source_address);
    write_u32_le(&output[28], packet->sequence);
    return true;
}

bool cr11_gateway_packet_decode(const uint8_t *input,
                                size_t input_length,
                                cr11_gateway_packet_t *packet)
{
    if (input == NULL || packet == NULL ||
        input_length != CR11_GATEWAY_PACKET_SIZE ||
        input[0] != CR11_GATEWAY_PACKET_VERSION ||
        input[1] != CR11_GATEWAY_PACKET_SIZE ||
        input[2] > CR11_GATEWAY_ACTION_RELEASE ||
        input[3] <= CR11_EVENT_UNKNOWN ||
        input[3] > CR11_EVENT_WINDOW_GOTO_LIFT_PERCENTAGE ||
        input[4] > CR11_DIRECTION_DOWN ||
        (input[5] & (uint8_t)~CR11_GATEWAY_FLAG_MASK) != 0U ||
        input[6] > 4U || input[7] > 8U ||
        input[8] > CR11_GATEWAY_SOURCE_GROUP) {
        return false;
    }

    *packet = (cr11_gateway_packet_t){
        .version = input[0],
        .action = (cr11_gateway_action_t)input[2],
        .event_kind = (cr11_event_kind_t)input[3],
        .direction = (cr11_direction_t)input[4],
        .flags = input[5],
        .selector = input[6],
        .button = input[7],
        .source_mode = (cr11_gateway_source_mode_t)input[8],
        .source_endpoint = input[9],
        .target_endpoint = input[10],
        .zcl_transaction_sequence = input[11],
        .profile_id = read_u16_le(&input[12]),
        .cluster_id = read_u16_le(&input[14]),
        .command_id = input[16],
        .rssi = (int8_t)input[17],
        .value = read_u16_le(&input[18]),
        .source_address = read_u64_le(&input[20]),
        .sequence = read_u32_le(&input[28]),
    };
    return true;
}

const char *cr11_gateway_action_name(cr11_gateway_action_t action)
{
    switch (action) {
    case CR11_GATEWAY_ACTION_CLICK:
        return "click";
    case CR11_GATEWAY_ACTION_HOLD:
        return "hold";
    case CR11_GATEWAY_ACTION_RELEASE:
        return "release";
    case CR11_GATEWAY_ACTION_UNKNOWN:
    default:
        return "unknown";
    }
}

const char *cr11_gateway_source_mode_name(cr11_gateway_source_mode_t mode)
{
    switch (mode) {
    case CR11_GATEWAY_SOURCE_SHORT:
        return "short";
    case CR11_GATEWAY_SOURCE_EXTENDED:
        return "extended";
    case CR11_GATEWAY_SOURCE_GROUP:
        return "group";
    case CR11_GATEWAY_SOURCE_UNKNOWN:
    default:
        return "unknown";
    }
}

bool cr11_gateway_packet_is_forwardable(const cr11_gateway_packet_t *packet)
{
    return packet != NULL &&
           (packet->flags & CR11_GATEWAY_FLAG_FORWARDABLE) != 0U &&
           packet->action != CR11_GATEWAY_ACTION_UNKNOWN &&
           packet->button >= 1U && packet->button <= 8U;
}

bool cr11_gateway_packet_format_json(const cr11_gateway_packet_t *packet,
                                     char *output,
                                     size_t output_size)
{
    if (packet == NULL || output == NULL || output_size == 0U) {
        return false;
    }

    const cr11_event_t event = {
        .kind = packet->event_kind,
        .direction = packet->direction,
        .value = packet->value,
        .transition_time = 0U,
        .has_value = (packet->flags & CR11_GATEWAY_FLAG_HAS_VALUE) != 0U,
        .with_on_off =
            (packet->flags & CR11_GATEWAY_FLAG_WITH_ON_OFF) != 0U,
    };
    const int written = snprintf(
        output,
        output_size,
        "{\"v\":%u,\"seq\":%" PRIu32
        ",\"source\":{\"mode\":\"%s\",\"address\":\"0x%016" PRIx64
        "\",\"endpoint\":%u},\"target_endpoint\":%u,\"selector\":%u"
        ",\"button\":%u,\"action\":\"%s\",\"event\":\"%s\""
        ",\"event_code\":%u,\"direction\":\"%s\""
        ",\"direction_inferred\":%s,\"profile_id\":%u"
        ",\"cluster_id\":%u,\"command_id\":%u,\"zcl_tsn\":%u"
        ",\"rssi\":%d,\"rssi_scope\":\"gateway_last_hop\""
        ",\"has_value\":%s,\"value\":%u"
        ",\"with_on_off\":%s,\"forwardable\":%s}",
        packet->version,
        packet->sequence,
        cr11_gateway_source_mode_name(packet->source_mode),
        packet->source_address,
        packet->source_endpoint,
        packet->target_endpoint,
        packet->selector,
        packet->button,
        cr11_gateway_action_name(packet->action),
        cr11_event_name(&event),
        (unsigned int)packet->event_kind,
        cr11_direction_name(packet->direction),
        (packet->flags & CR11_GATEWAY_FLAG_DIRECTION_INFERRED) != 0U
            ? "true"
            : "false",
        packet->profile_id,
        packet->cluster_id,
        packet->command_id,
        packet->zcl_transaction_sequence,
        packet->rssi,
        (packet->flags & CR11_GATEWAY_FLAG_HAS_VALUE) != 0U
            ? "true"
            : "false",
        packet->value,
        (packet->flags & CR11_GATEWAY_FLAG_WITH_ON_OFF) != 0U
            ? "true"
            : "false",
        cr11_gateway_packet_is_forwardable(packet) ? "true" : "false");

    return written >= 0 && (size_t)written < output_size;
}
