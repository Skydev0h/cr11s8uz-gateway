/* SPDX-License-Identifier: MIT */

/*
 * Skydev0h CR11Gateway external converter for Zigbee2MQTT 2.13.x.
 *
 * The gateway emits one fixed 32-byte v1 event on server-to-client command
 * 0x00 of cluster 0xFC11.  Malformed packets are exposed as diagnostics but
 * are never marked valid for the routing extension.
 */

import {Zcl} from "zigbee-herdsman";
import * as exposes from "zigbee-herdsman-converters/lib/exposes";
import * as m from "zigbee-herdsman-converters/lib/modernExtend";

const e = exposes.presets;
const ea = exposes.access;

export const CR11_GATEWAY_CLUSTER_NAME = "cr11GatewayEvents";
export const CR11_GATEWAY_CLUSTER_ID = 0xfc11;
export const CR11_GATEWAY_PACKET_VERSION = 1;
export const CR11_GATEWAY_PACKET_SIZE = 32;
export const CR11_GATEWAY_READY_COMMAND_ID = 0x01;
export const CR11_GATEWAY_HEARTBEAT_COMMAND_ID = 0x02;
export const CR11_GATEWAY_EVENT_OPTION = "cr11_publish_gateway_event";
const FLAGS_MASK = 0x0f;

const actionNames = ["unknown", "click", "hold", "release"];
const eventNames = [
    "unknown",
    "malformed",
    "on_off.off",
    "on_off.on",
    "on_off.toggle",
    "level.move_to",
    "level.move",
    "level.step",
    "level.stop",
    "window.open",
    "window.close",
    "window.stop",
    "window.goto_lift_value",
    "window.goto_lift_percentage",
];
const directionNames = ["none", "up", "down"];
const sourceModeNames = ["unknown", "short", "extended", "group"];
const dataParameter = [{name: "data", type: Zcl.BuffaloZclDataType.BUFFER}];

export const cr11GatewayCluster = {
    name: CR11_GATEWAY_CLUSTER_NAME,
    ID: CR11_GATEWAY_CLUSTER_ID,
    attributes: {},
    commands: {
        ready: {
            name: "ready",
            ID: CR11_GATEWAY_READY_COMMAND_ID,
            parameters: [],
        },
        heartbeat: {
            name: "heartbeat",
            ID: CR11_GATEWAY_HEARTBEAT_COMMAND_ID,
            parameters: [],
        },
    },
    commandsResponse: {
        event: {
            name: "event",
            ID: 0x00,
            parameters: dataParameter,
        },
    },
};

function invalid(reason, data) {
    const diagnostic = {valid: false, error: reason};
    if (Buffer.isBuffer(data)) diagnostic.packet_length = data.length;
    return diagnostic;
}

function addressText(mode, value) {
    if (mode === 1) return `0x${value.toString(16).padStart(4, "0")}`;
    if (mode === 2) return `0x${value.toString(16).padStart(16, "0")}`;
    if (mode === 3) return `0x${value.toString(16).padStart(4, "0")}`;
    return "unknown";
}

export function decodeGatewayPacket(value) {
    const data = Buffer.isBuffer(value) ? value : undefined;
    if (!data) return invalid("packet_not_buffer");
    if (data.length !== CR11_GATEWAY_PACKET_SIZE) return invalid("packet_length", data);
    if (data[0] !== CR11_GATEWAY_PACKET_VERSION) return invalid("packet_version", data);
    if (data[1] !== CR11_GATEWAY_PACKET_SIZE) return invalid("declared_length", data);

    const actionCode = data[2];
    const eventCode = data[3];
    const directionCode = data[4];
    const flags = data[5];
    const selector = data[6];
    const button = data[7];
    const sourceModeCode = data[8];
    if (actionCode >= actionNames.length) return invalid("action_code", data);
    if (eventCode < 1 || eventCode >= eventNames.length) return invalid("event_code", data);
    if (directionCode >= directionNames.length) return invalid("direction_code", data);
    if ((flags & ~FLAGS_MASK) !== 0) return invalid("reserved_flags", data);
    if (selector > 4) return invalid("selector", data);
    if (button > 8) return invalid("button", data);
    if (sourceModeCode >= sourceModeNames.length) return invalid("source_mode", data);

    const sourceAddressValue = data.readBigUInt64LE(20);
    if (sourceModeCode === 1 && sourceAddressValue > 0xffffn) {
        return invalid("short_address_range", data);
    }
    if ((flags & 0x08) !== 0 && (actionCode === 0 || button === 0)) {
        return invalid("forwardable_semantics", data);
    }

    return {
        valid: true,
        v: data[0],
        seq: data.readUInt32LE(28),
        source: {
            mode: sourceModeNames[sourceModeCode],
            address: addressText(sourceModeCode, sourceAddressValue),
            endpoint: data[9],
        },
        target_endpoint: data[10],
        selector,
        button,
        action: actionNames[actionCode],
        event: eventNames[eventCode],
        event_code: eventCode,
        direction: directionNames[directionCode],
        direction_inferred: (flags & 0x04) !== 0,
        profile_id: data.readUInt16LE(12),
        cluster_id: data.readUInt16LE(14),
        command_id: data[16],
        zcl_tsn: data[11],
        rssi: data.readInt8(17),
        rssi_scope: "gateway_last_hop",
        has_value: (flags & 0x01) !== 0,
        value: data.readUInt16LE(18),
        with_on_off: (flags & 0x02) !== 0,
        forwardable: (flags & 0x08) !== 0,
    };
}

function packetFromMessage(msg) {
    const value = msg?.data?.data;
    return decodeGatewayPacket(Buffer.isBuffer(value) ? value : undefined);
}

const fzGatewayEvent = {
    cluster: CR11_GATEWAY_CLUSTER_NAME,
    type: ["commandEvent"],
    convert: (model, msg) => ({cr11_gateway_event: JSON.stringify(packetFromMessage(msg))}),
};

/* Fallback for the first frame if it races custom-cluster registration. */
const fzGatewayRaw = {
    cluster: CR11_GATEWAY_CLUSTER_ID,
    type: ["raw"],
    convert: (model, msg) => {
        const raw = Buffer.from(msg.data ?? []);
        const packet = raw.length >= 3 && raw[2] === 0x00
            ? decodeGatewayPacket(raw.subarray(3))
            : invalid("raw_zcl_header", raw);
        return {cr11_gateway_event: JSON.stringify(packet)};
    },
};

const publishGatewayEventOption = e
    .binary(CR11_GATEWAY_EVENT_OPTION, ea.SET, true, false)
    .withLabel("Publish gateway event")
    .withDescription(
        "Publish the raw cr11_gateway_event diagnostic in device state (default: off); routing remains active when disabled",
    );

const definition = {
    zigbeeModel: ["CR11-Bridge"],
    model: "CR11Gateway",
    vendor: "Skydev0h",
    description: "CR11S8UZ event gateway",
    extend: [m.deviceAddCustomCluster(CR11_GATEWAY_CLUSTER_NAME, cr11GatewayCluster)],
    fromZigbee: [fzGatewayEvent, fzGatewayRaw],
    toZigbee: [],
    options: [publishGatewayEventOption],
    configure: async (device) => {
        const endpoint = device.getEndpoint(21);
        if (!endpoint) throw new Error("CR11Gateway endpoint 21 is missing");
        await endpoint.command(
            CR11_GATEWAY_CLUSTER_NAME,
            "ready",
            {},
            {disableDefaultResponse: true},
        );
    },
    exposes: [
        e
            .text("cr11_gateway_event", ea.STATE)
            .withDescription("Validated versioned CR11 event; routed to its source remote by the companion extension"),
    ],
};

export default definition;
