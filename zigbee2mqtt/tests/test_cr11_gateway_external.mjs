/* SPDX-License-Identifier: MIT */

import assert from "node:assert/strict";
import test from "node:test";

import definition, {
    CR11_GATEWAY_CLUSTER_NAME,
    CR11_GATEWAY_EVENT_OPTION,
    CR11_GATEWAY_HEARTBEAT_COMMAND_ID,
    CR11_GATEWAY_READY_COMMAND_ID,
    cr11GatewayCluster,
    decodeGatewayPacket,
} from "../cr11_gateway_external.mjs";

test("offers an explicit raw-event publication switch", () => {
    const option = definition.options.find((candidate) => candidate.property === CR11_GATEWAY_EVENT_OPTION);
    assert.ok(option);
    assert.equal(option.type, "binary");
    assert.equal(option.value_on, true);
    assert.equal(option.value_off, false);
});

test("declares and sends the coordinator-ready command", async () => {
    assert.equal(cr11GatewayCluster.commands.ready.ID, CR11_GATEWAY_READY_COMMAND_ID);
    assert.deepEqual(cr11GatewayCluster.commands.ready.parameters, []);
    assert.equal(cr11GatewayCluster.commands.heartbeat.ID, CR11_GATEWAY_HEARTBEAT_COMMAND_ID);
    assert.deepEqual(cr11GatewayCluster.commands.heartbeat.parameters, []);

    const calls = [];
    const endpoint = {
        command: async (...args) => calls.push(args),
    };
    await definition.configure({getEndpoint: (id) => id === 21 ? endpoint : undefined});
    assert.deepEqual(calls, [[
        CR11_GATEWAY_CLUSTER_NAME,
        "ready",
        {},
        {disableDefaultResponse: true},
    ]]);
});

test("configure rejects a malformed gateway descriptor", async () => {
    await assert.rejects(
        definition.configure({getEndpoint: () => undefined}),
        /endpoint 21 is missing/,
    );
});

test("packet decoder behavior remains unchanged", () => {
    const packet = Buffer.alloc(32);
    packet[0] = 1;
    packet[1] = 32;
    packet[2] = 1;
    packet[3] = 4;
    packet[5] = 0x08;
    packet[6] = 1;
    packet[7] = 1;
    packet[8] = 1;
    packet.writeUInt16LE(0x0104, 12);
    packet.writeUInt16LE(0x0006, 14);
    packet[16] = 0x02;
    packet.writeInt8(-70, 17);
    packet.writeBigUInt64LE(0x1234n, 20);
    packet.writeUInt32LE(7, 28);

    const decoded = decodeGatewayPacket(packet);
    assert.equal(decoded.valid, true);
    assert.equal(decoded.button, 1);
    assert.equal(decoded.action, "click");
    assert.equal(decoded.rssi, -70);
    assert.equal(decoded.rssi_scope, "gateway_last_hop");
});
