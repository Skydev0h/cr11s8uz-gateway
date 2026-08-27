/*
 * SPDX-License-Identifier: MIT
 */

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "cr11_event.h"
#include "cr11_gateway_packet.h"

static cr11_event_t decode(uint16_t cluster,
                           uint8_t command,
                           const uint8_t *payload,
                           size_t length)
{
    return cr11_event_decode(cluster, command, payload, length);
}

int main(void)
{
    cr11_event_t event = decode(CR11_ZCL_CLUSTER_ON_OFF, 0x00, NULL, 0);
    assert(event.kind == CR11_EVENT_ON_OFF_OFF);
    assert(strcmp(cr11_event_button_guess(&event, false, false), "left.upper.O") == 0);
    assert(strcmp(cr11_event_button_guess(&event, true, false),
                  "right.upper.square") == 0);

    event = decode(CR11_ZCL_CLUSTER_ON_OFF, 0x01, NULL, 0);
    assert(event.kind == CR11_EVENT_ON_OFF_ON);
    assert(strcmp(cr11_event_button_guess(&event, false, false), "left.upper.OO") == 0);
    assert(strcmp(cr11_event_button_guess(&event, true, false),
                  "right.upper.double-square") == 0);

    event = decode(CR11_ZCL_CLUSTER_ON_OFF, 0x02, NULL, 0);
    assert(event.kind == CR11_EVENT_ON_OFF_TOGGLE);
    assert(strcmp(cr11_event_button_guess(&event, false, false),
                  "left.upper.O") == 0);
    assert(strcmp(cr11_event_button_guess(&event, false, true),
                  "left.upper.OO") == 0);
    assert(strcmp(cr11_event_button_guess(&event, true, false),
                  "right.upper.square") == 0);
    assert(strcmp(cr11_event_button_guess(&event, true, true),
                  "right.upper.double-square") == 0);

    const uint8_t extra = 0;
    event = decode(CR11_ZCL_CLUSTER_ON_OFF, 0x00, &extra, 1);
    assert(event.kind == CR11_EVENT_MALFORMED);

    const uint8_t upper_hold_payload[] = {
        CR11_UPPER_HOLD_MARKER_LEVEL, 0x00, 0x00,
    };
    const cr11_event_t upper_hold = decode(
        CR11_ZCL_CLUSTER_LEVEL_CONTROL, 0x00,
        upper_hold_payload, sizeof(upper_hold_payload));
    assert(upper_hold.kind == CR11_EVENT_LEVEL_MOVE_TO);
    assert(upper_hold.value == CR11_UPPER_HOLD_MARKER_LEVEL);
    assert(upper_hold.transition_time == CR11_UPPER_MARKER_TRANSITION_TIME);
    assert(cr11_event_is_upper_hold_marker(&upper_hold));
    assert(!cr11_event_is_upper_release_marker(&upper_hold));
    assert(strcmp(cr11_event_button_guess(&upper_hold, false, true),
                  "left.upper.OO") == 0);

    const uint8_t upper_release_payload[] = {
        CR11_UPPER_RELEASE_MARKER_LEVEL, 0x00, 0x00,
    };
    const cr11_event_t upper_release = decode(
        CR11_ZCL_CLUSTER_LEVEL_CONTROL, 0x04,
        upper_release_payload, sizeof(upper_release_payload));
    assert(upper_release.kind == CR11_EVENT_LEVEL_MOVE_TO);
    assert(upper_release.with_on_off);
    assert(!cr11_event_is_upper_hold_marker(&upper_release));
    assert(cr11_event_is_upper_release_marker(&upper_release));
    assert(strcmp(cr11_event_button_guess(&upper_release, true, true),
                  "right.upper.double-square") == 0);

    const uint8_t non_marker_transition[] = {
        CR11_UPPER_HOLD_MARKER_LEVEL, 0x01, 0x00,
    };
    const cr11_event_t upper_non_marker = decode(
        CR11_ZCL_CLUSTER_LEVEL_CONTROL, 0x00,
        non_marker_transition, sizeof(non_marker_transition));
    assert(upper_non_marker.transition_time == 1U);
    assert(!cr11_event_is_upper_hold_marker(&upper_non_marker));
    assert(strcmp(cr11_event_button_guess(&upper_non_marker, false, false),
                  "unknown") == 0);
    assert(!cr11_event_is_upper_hold_marker(NULL));
    assert(!cr11_event_is_upper_release_marker(NULL));

    const uint8_t move_up[] = {0x00, 0x32};
    event = decode(CR11_ZCL_CLUSTER_LEVEL_CONTROL, 0x01,
                   move_up, sizeof(move_up));
    assert(event.kind == CR11_EVENT_LEVEL_MOVE);
    assert(event.direction == CR11_DIRECTION_UP);
    assert(event.value == 0x32);
    assert(strcmp(cr11_event_button_guess(&event, false, false), "left.lower.plus") == 0);
    assert(strcmp(cr11_event_button_guess(&event, true, false), "right.lower.plus") == 0);

    const uint8_t move_down_with_on_off[] = {0x01, 0x10};
    event = decode(CR11_ZCL_CLUSTER_LEVEL_CONTROL, 0x05,
                   move_down_with_on_off, sizeof(move_down_with_on_off));
    assert(event.kind == CR11_EVENT_LEVEL_MOVE);
    assert(event.direction == CR11_DIRECTION_DOWN);
    assert(event.with_on_off);
    assert(strcmp(cr11_event_button_guess(&event, false, false), "left.lower.minus") == 0);
    assert(strcmp(cr11_event_button_guess(&event, true, false), "right.lower.minus") == 0);

    const uint8_t step_up[] = {0x00, 0x0a, 0x34, 0x12};
    event = decode(CR11_ZCL_CLUSTER_LEVEL_CONTROL, 0x02,
                   step_up, sizeof(step_up));
    assert(event.kind == CR11_EVENT_LEVEL_STEP);
    assert(event.direction == CR11_DIRECTION_UP);
    assert(event.value == 10);

    event = decode(CR11_ZCL_CLUSTER_LEVEL_CONTROL, 0x03, NULL, 0);
    assert(event.kind == CR11_EVENT_LEVEL_STOP);

    const uint8_t bad_direction[] = {0x02, 0x10};
    event = decode(CR11_ZCL_CLUSTER_LEVEL_CONTROL, 0x01,
                   bad_direction, sizeof(bad_direction));
    assert(event.kind == CR11_EVENT_MALFORMED);

    event = decode(CR11_ZCL_CLUSTER_WINDOW_COVERING, 0x00, NULL, 0);
    assert(event.kind == CR11_EVENT_WINDOW_OPEN);
    assert(strcmp(cr11_event_button_guess(&event, true, false), "right.lower.plus") == 0);

    event = decode(CR11_ZCL_CLUSTER_WINDOW_COVERING, 0x01, NULL, 0);
    assert(event.kind == CR11_EVENT_WINDOW_CLOSE);
    assert(strcmp(cr11_event_button_guess(&event, true, false), "right.lower.minus") == 0);

    const uint8_t zero_percent[] = {0};
    event = decode(CR11_ZCL_CLUSTER_WINDOW_COVERING, 0x05,
                   zero_percent, sizeof(zero_percent));
    assert(event.kind == CR11_EVENT_WINDOW_GOTO_LIFT_PERCENTAGE);
    assert(strcmp(cr11_event_button_guess(&event, true, false),
                  "right.upper.square") == 0);

    const uint8_t full_percent[] = {100};
    event = decode(CR11_ZCL_CLUSTER_WINDOW_COVERING, 0x05,
                   full_percent, sizeof(full_percent));
    assert(strcmp(cr11_event_button_guess(&event, true, false),
                  "right.upper.double-square") == 0);

    const uint8_t invalid_percent[] = {101};
    event = decode(CR11_ZCL_CLUSTER_WINDOW_COVERING, 0x05,
                   invalid_percent, sizeof(invalid_percent));
    assert(event.kind == CR11_EVENT_MALFORMED);

    event = decode(CR11_ZCL_CLUSTER_LEVEL_CONTROL, 0x01, NULL, 2);
    assert(event.kind == CR11_EVENT_MALFORMED);

    event = decode(0xffff, 0xff, NULL, 0);
    assert(event.kind == CR11_EVENT_UNKNOWN);
    assert(strcmp(cr11_event_name(NULL), "invalid") == 0);

    const cr11_event_t toggle = decode(CR11_ZCL_CLUSTER_ON_OFF, 0x02,
                                       NULL, 0);
    cr11_gateway_packet_t packet;
    assert(cr11_gateway_packet_build(
        &packet, &toggle, CR11_DIRECTION_NONE,
        CR11_GATEWAY_SOURCE_SHORT, 0x7111U, 1U,
        CR11_LEFT_DOUBLE_O_ENDPOINT, 0x42U, 0x0104U,
        CR11_ZCL_CLUSTER_ON_OFF, 0x02U, -61, 0x12345678U));
    assert(packet.selector == 3U);
    assert(packet.button == 3U);
    assert(packet.action == CR11_GATEWAY_ACTION_CLICK);
    assert(cr11_gateway_packet_is_forwardable(&packet));

    uint8_t wire[CR11_GATEWAY_PACKET_SIZE];
    assert(cr11_gateway_packet_encode(&packet, wire));
    const uint8_t expected[] = {
        0x01, 0x20, 0x01, 0x04, 0x00, 0x08, 0x03, 0x03,
        0x01, 0x01, 0x0b, 0x42, 0x04, 0x01, 0x06, 0x00,
        0x02, 0xc3, 0x00, 0x00, 0x11, 0x71, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x78, 0x56, 0x34, 0x12,
    };
    assert(sizeof(expected) == sizeof(wire));
    assert(memcmp(wire, expected, sizeof(expected)) == 0);

    cr11_gateway_packet_t decoded_packet;
    assert(cr11_gateway_packet_decode(wire, sizeof(wire), &decoded_packet));
    assert(decoded_packet.sequence == packet.sequence);
    assert(decoded_packet.source_address == packet.source_address);
    assert(decoded_packet.rssi == packet.rssi);
    assert(!cr11_gateway_packet_decode(wire, sizeof(wire) - 1U,
                                       &decoded_packet));
    wire[5] |= 0x80U;
    assert(!cr11_gateway_packet_decode(wire, sizeof(wire), &decoded_packet));
    wire[5] &= (uint8_t)~0x80U;

    assert(cr11_gateway_packet_build(
        &packet, &upper_hold, CR11_DIRECTION_NONE,
        CR11_GATEWAY_SOURCE_SHORT, 0x7111U, 1U,
        CR11_LEFT_DOUBLE_O_ENDPOINT, 0x43U, 0x0104U,
        CR11_ZCL_CLUSTER_LEVEL_CONTROL, 0x00U, -62, 4U));
    assert(packet.selector == 3U);
    assert(packet.button == 3U);
    assert(packet.action == CR11_GATEWAY_ACTION_HOLD);
    assert(cr11_gateway_packet_is_forwardable(&packet));

    assert(cr11_gateway_packet_build(
        &packet, &upper_release, CR11_DIRECTION_NONE,
        CR11_GATEWAY_SOURCE_SHORT, 0x7111U, 1U,
        CR11_RIGHT_DOUBLE_SQUARE_ENDPOINT, 0x44U, 0x0104U,
        CR11_ZCL_CLUSTER_LEVEL_CONTROL, 0x04U, -63, 5U));
    assert(packet.selector == 4U);
    assert(packet.button == 4U);
    assert(packet.action == CR11_GATEWAY_ACTION_RELEASE);
    assert((packet.flags & CR11_GATEWAY_FLAG_WITH_ON_OFF) != 0U);
    assert(cr11_gateway_packet_is_forwardable(&packet));

    assert(cr11_gateway_packet_build(
        &packet, &upper_non_marker, CR11_DIRECTION_NONE,
        CR11_GATEWAY_SOURCE_SHORT, 0x7111U, 1U,
        CR11_LEFT_O_ENDPOINT, 0x45U, 0x0104U,
        CR11_ZCL_CLUSTER_LEVEL_CONTROL, 0x00U, -64, 6U));
    assert(packet.selector == 1U);
    assert(packet.button == 0U);
    assert(packet.action == CR11_GATEWAY_ACTION_UNKNOWN);
    assert(!cr11_gateway_packet_is_forwardable(&packet));

    const uint8_t step_down[] = {0x01, 0x05, 0x00, 0x00};
    const cr11_event_t right_step = decode(CR11_ZCL_CLUSTER_LEVEL_CONTROL,
                                           0x02, step_down,
                                           sizeof(step_down));
    assert(cr11_gateway_packet_build(
        &packet, &right_step, CR11_DIRECTION_NONE,
        CR11_GATEWAY_SOURCE_SHORT, 0x7111U, 1U,
        CR11_RIGHT_SQUARE_ENDPOINT, 7U, 0x0104U,
        CR11_ZCL_CLUSTER_LEVEL_CONTROL, 0x02U, -80, 2U));
    assert(packet.selector == 2U);
    assert(packet.button == 8U);
    assert(packet.action == CR11_GATEWAY_ACTION_CLICK);

    const cr11_event_t stop = decode(CR11_ZCL_CLUSTER_LEVEL_CONTROL, 0x03,
                                     NULL, 0);
    assert(cr11_gateway_packet_build(
        &packet, &stop, CR11_DIRECTION_UP,
        CR11_GATEWAY_SOURCE_SHORT, 0x7111U, 1U,
        CR11_LEFT_O_ENDPOINT, 8U, 0x0104U,
        CR11_ZCL_CLUSTER_LEVEL_CONTROL, 0x03U, -70, 3U));
    assert(packet.selector == 1U);
    assert(packet.button == 5U);
    assert(packet.action == CR11_GATEWAY_ACTION_RELEASE);
    assert((packet.flags & CR11_GATEWAY_FLAG_DIRECTION_INFERRED) != 0U);

    char json[512];
    assert(cr11_gateway_packet_format_json(&packet, json, sizeof(json)));
    assert(strstr(json, "\"action\":\"release\"") != NULL);
    assert(strstr(json, "\"source\":{\"mode\":\"short\"") != NULL);
    assert(strstr(json, "\"rssi_scope\":\"gateway_last_hop\"") != NULL);
    assert(strstr(json, "\"forwardable\":true") != NULL);
    char too_small[8];
    assert(!cr11_gateway_packet_format_json(&packet, too_small,
                                            sizeof(too_small)));

    puts("cr11_event tests: OK");
    return 0;
}
