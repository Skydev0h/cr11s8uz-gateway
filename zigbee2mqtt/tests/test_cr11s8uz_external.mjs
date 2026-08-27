/* SPDX-License-Identifier: MIT */

import assert from "node:assert/strict";
import test from "node:test";

import definition, {
    discoverCr11Gateway,
    executeCommand,
    fzAssignmentMode,
    protocolResponse,
    routerPlan,
    tzCr11Human,
} from "../cr11s8uz_external.mjs";

function decode(type, data) {
    return JSON.parse(protocolResponse({type, data: {data: Buffer.from(data)}}).cr11_response);
}

function assignmentMode(device, value) {
    return fzAssignmentMode.convert(undefined, {device, data: {0x00aa: value}});
}

test("preserves the canonical model identity used by the frontend image catalog", () => {
    assert.equal(definition.model, "CR11S8UZ");
    assert.equal(definition.vendor, "ORVIBO");
});

function registryDeviceClass() {
    return class RegistryDevice {
        static devices = [];

        static *allIterator() {
            yield* this.devices;
        }

        constructor({ieeeAddr, modelID, manufacturerName, interviewState, endpointIDs = [1]}) {
            this.ieeeAddr = ieeeAddr;
            this.modelID = modelID;
            this.manufacturerName = manufacturerName;
            this.interviewState = interviewState;
            this.endpointIDs = new Set(endpointIDs);
            this.pendingRequestTimeout = 5_000;
        }

        addCustomCluster() {}

        getEndpoint(id) {
            return this.endpointIDs.has(id) ? {ID: id} : undefined;
        }
    };
}

function gateway(RegistryDevice, ieeeAddr = "0x1020304050607080", manufacturerName = "Skydev0h") {
    return new RegistryDevice({
        ieeeAddr,
        modelID: "CR11-Bridge",
        manufacturerName,
        interviewState: "SUCCESSFUL",
        endpointIDs: [10, 11, 12, 13, 21],
    });
}

test("add-big response is status-only", () => {
    assert.deepEqual(decode("commandAddBigResponse", [0x00]), {
        kind: "commandAddBigResponse",
        status_code: 0,
        status: "success",
    });
});

test("query-big response still decodes its returned record", () => {
    const response = decode("commandQueryBigResponse", [
        0x00,
        0x03, 0x00, 0x00,
        0x80, 0x70, 0x60, 0x50, 0x40, 0x30, 0x20, 0x10,
        0x0a,
        0x04, 0x01,
        0x06, 0x00,
        0x01, 0x02,
    ]);
    assert.deepEqual(response, {
        kind: "commandQueryBigResponse",
        status_code: 0,
        status: "success",
        big_table_authoritative: false,
        firmware_warning:
            "Known v3.1.04 firmware bug: the Big-table lookup uses an uninitialized key; this response is diagnostic and does not authoritatively describe the Big table",
        big_action: {
            key_no: 3,
            key_bank: 0,
            key_action: 0,
            target_ieee: "0x************7080",
            target_endpoint: 10,
            profile_id: 0x0104,
            cluster_id: 0x0006,
            action_length: 1,
            command_id: 0x02,
            command_payload_hex: "",
        },
    });
});

test("query uses immediate policy only with an explicit awake assertion", async () => {
    const calls = [];
    const entity = {command: async (...args) => calls.push(args)};
    const device = {ieeeAddr: "0x0123456789abcdef", pendingRequestTimeout: 1000, addCustomCluster() {}};

    const queued = await executeCommand(
        entity,
        {op: "query", key_no: 3, key_bank: 0, key_action: "click"},
        {device},
    );
    const immediate = await executeCommand(
        entity,
        {op: "query", key_no: 7, key_bank: 0, key_action: "hold", assume_awake: true},
        {device},
    );

    assert.equal(queued.send_policy, "queue");
    assert.match(queued.note, /Big-table lookup uses an uninitialized key/);
    assert.equal(calls[0][3].sendPolicy, "queue");
    assert.equal(immediate.send_policy, "immediate");
    assert.equal(calls[1][3].sendPolicy, "immediate");
    await assert.rejects(
        executeCommand(
            entity,
            {op: "query", key_no: 3, key_bank: 0, key_action: "click", assume_awake: "yes"},
            {device},
        ),
        /assume_awake must be a boolean/,
    );
});

test("query-small not-found cannot be mistaken for an empty Big table", () => {
    const response = decode("commandQuerySmallResponse", [0x8b, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00]);
    assert.equal(response.status, "not_found");
    assert.equal(response.big_table_authoritative, false);
    assert.match(response.firmware_warning, /v3\.1\.04 firmware bug/);
    assert.deepEqual(response.small_action, {key_no: 3, key_bank: 0, key_action: 0, opaque_hex: "000000"});
});

test("four-target plan covers click, hold, and release without losing selection", () => {
    const records = routerPlan("0x1020304050607080", 1, "four-target", undefined, [10, 11, 12, 13]);
    assert.equal(records.length, 12);
    assert.deepEqual(records.map((record) => record.target_endpoint), [10, 10, 10, 11, 11, 11, 12, 12, 12, 13, 13, 13]);
    for (let offset = 0; offset < records.length; offset += 3) {
        assert.deepEqual(
            records.slice(offset, offset + 3).map((record) => [record.key_action, record.cluster_id, record.command_id, record.data]),
            [
                [0x00, 0x0006, 0x02, []],
                [0x02, 0x0008, 0x00, [0xa5, 0x00, 0x00]],
                [0x03, 0x0008, 0x04, [0x5a, 0x00, 0x00]],
            ],
        );
    }
});

test("install refuses to guess that a CR11 is listening", async () => {
    const device = {
        ieeeAddr: "0x0123456789abcdea",
        pendingRequestTimeout: 5_000,
        addCustomCluster() {},
    };
    await assert.rejects(
        executeCommand(
            {async command() { throw new Error("must not send"); }},
            {
                op: "install_router",
                target_ieee: "0x1020304050607080",
                target_endpoints: [10, 11, 12, 13],
                mode: "four-target",
                confirm: "replace_big_actions",
            },
            {device},
        ),
        /fresh active assignment-mode report/,
    );
});

test("install progress is driven to success by actual device responses", async () => {
    const ieeeAddr = "0x0123456789abcdef";
    const device = {
        ieeeAddr,
        pendingRequestTimeout: 5_000,
        addCustomCluster() {},
    };
    const progress = [];
    let tsn = 0;
    assignmentMode(device, 0);
    const entity = {
        async command(cluster, command, payload, options) {
            assert.equal(cluster, "manuSpecificCr11S8uz");
            assert.ok(Buffer.isBuffer(payload.data));
            assert.equal(options.sendPolicy, "immediate");
            assert.equal(options.timeout, 45_000);
            const type = command === "clearBig" ? "commandClearBigResponse" : "commandAddBigResponse";
            const response = {data: Buffer.from([0x00])};
            const decoded = protocolResponse({type, data: response, device, meta: {zclTransactionSequenceNumber: tsn++}});
            progress.push(JSON.parse(decoded.cr11_last_result));
            return response;
        },
    };

    const result = await executeCommand(
        entity,
        {
            op: "install_router",
            target_ieee: "0x1020304050607080",
            target_endpoints: [10, 11, 12, 13],
            mode: "four-target",
            confirm: "replace_big_actions",
        },
        {device},
    );

    assert.equal(result.status, "success");
    assert.equal(result.records_written, 12);
    assert.deepEqual(progress.map((item) => item.records_confirmed), Array.from({length: 13}, (_, index) => index));
    assert.equal(progress.at(-1).records_expected, 12);
    assert.equal(progress.at(-1).status, "success");
    assert.equal(device.pendingRequestTimeout, 5_000);
});

test("duplicate response TSNs do not advance install progress", async () => {
    const ieeeAddr = "0x0123456789abc003";
    const device = {
        ieeeAddr,
        pendingRequestTimeout: 5_000,
        addCustomCluster() {},
    };
    let call = 0;
    const observed = [];
    assignmentMode(device, 0);
    const entity = {
        async command(cluster, command, payload, options) {
            assert.equal(options.sendPolicy, "immediate");
            const type = command === "clearBig" ? "commandClearBigResponse" : "commandAddBigResponse";
            const response = {data: Buffer.from([0x00])};
            const msg = {type, data: response, device, meta: {zclTransactionSequenceNumber: call}};
            const first = protocolResponse(msg);
            const duplicate = protocolResponse(msg);
            if (first.cr11_last_result) observed.push(JSON.parse(first.cr11_last_result));
            assert.equal(duplicate.cr11_last_result, undefined);
            call += 1;
            return response;
        },
    };

    await executeCommand(
        entity,
        {
            op: "install_router",
            target_ieee: "0x1020304050607080",
            target_endpoint: 10,
            mode: "independent",
            confirm: "replace_big_actions",
        },
        {device},
    );

    assert.deepEqual(observed.map((item) => item.records_confirmed), [0, 1, 2, 3, 4]);
});

test("human setup exposes native buttons and hides the raw command editor", () => {
    const byName = new Map(definition.exposes.map((expose) => [expose.name, expose]));
    assert.deepEqual(byName.get("cr11_use_gateway")?.values, ["Configure"]);
    assert.equal(byName.get("cr11_use_gateway")?.access, 2);
    assert.deepEqual(byName.get("cr11_restore_direct")?.values, ["Restore"]);
    assert.equal(byName.get("cr11_restore_direct")?.access, 2);
    assert.ok(byName.has("cr11_setup_state"));
    assert.ok(byName.has("cr11_setup_progress"));
    assert.ok(byName.has("cr11_setup_detail"));
    assert.equal(byName.has("cr11_command"), false);
    assert.ok(definition.toZigbee.some((converter) => converter.key.includes("cr11_command")));
});

test("gateway discovery accepts exactly one complete joined gateway", () => {
    const RegistryDevice = registryDeviceClass();
    const cr11 = new RegistryDevice({ieeeAddr: "0x0123456789abc101"});
    const selected = gateway(RegistryDevice);
    assert.equal(discoverCr11Gateway(cr11, [cr11, selected]), selected);

    const legacy = gateway(RegistryDevice, "0x1020304050607081", "SkyDev");
    assert.equal(discoverCr11Gateway(cr11, [cr11, legacy]), legacy);

    assert.throws(() => discoverCr11Gateway(cr11, [cr11]), /no joined CR11Gateway/);
    assert.throws(
        () => discoverCr11Gateway(cr11, [cr11, selected, legacy]),
        /expected exactly one joined CR11Gateway, found 2/,
    );
    const unsupported = gateway(RegistryDevice, "0x1020304050607082", "SomeoneElse");
    assert.throws(() => discoverCr11Gateway(cr11, [cr11, unsupported]), /no joined CR11Gateway/);

    const incomplete = gateway(RegistryDevice, "0x1020304050607083");
    incomplete.endpointIDs.delete(12);
    assert.throws(() => discoverCr11Gateway(cr11, [cr11, incomplete]), /missing one or more required endpoints/);
});

test("gateway button fails closed and publishes a useful error when no gateway exists", async () => {
    const RegistryDevice = registryDeviceClass();
    const cr11 = new RegistryDevice({ieeeAddr: "0x0123456789abc102"});
    RegistryDevice.devices = [cr11];
    assignmentMode(cr11, 0);
    const published = [];
    let sent = false;

    await assert.rejects(
        tzCr11Human.convertSet(
            {async command() { sent = true; }},
            "cr11_use_gateway",
            "Configure",
            {device: cr11, state: {}, publish: (state) => published.push(state)},
        ),
        /no joined CR11Gateway/,
    );
    assert.equal(sent, false);
    assert.equal(published.at(-1).cr11_setup_state, "error");
    assert.match(published.at(-1).cr11_setup_detail, /no joined CR11Gateway/);
});

test("gateway button auto-discovers the gateway and reports 0/12 through 12/12", async () => {
    const RegistryDevice = registryDeviceClass();
    const cr11 = new RegistryDevice({ieeeAddr: "0x0123456789abc103"});
    const selected = gateway(RegistryDevice);
    RegistryDevice.devices = [cr11, selected];
    assignmentMode(cr11, 0);
    const published = [];
    const progress = [];
    const commands = [];
    let tsn = 0;
    const entity = {
        async command(cluster, command, payload, options) {
            commands.push({cluster, command, payload, options});
            const type = command === "clearBig" ? "commandClearBigResponse" : "commandAddBigResponse";
            const response = {data: Buffer.from([0x00])};
            const decoded = protocolResponse({type, data: response, device: cr11, meta: {zclTransactionSequenceNumber: tsn++}});
            if (decoded.cr11_setup_progress) progress.push(decoded.cr11_setup_progress);
            return response;
        },
    };

    const result = await tzCr11Human.convertSet(entity, "cr11_use_gateway", "Configure", {
        device: cr11,
        state: {},
        publish: (state) => published.push(state),
    });

    assert.equal(commands.length, 13);
    assert.deepEqual(commands.map((call) => call.command), ["clearBig", ...Array(12).fill("addBig")]);
    assert.ok(commands.every((call) => call.options.sendPolicy === "immediate"));
    assert.deepEqual(progress, Array.from({length: 13}, (_, index) => `${index}/12`));
    assert.equal(published[0].cr11_setup_state, "configuring_gateway");
    assert.equal(published[0].cr11_setup_progress, "0/12");
    assert.equal(result.state.cr11_routing_mode, "gateway");
    assert.equal(result.state.cr11_setup_state, "gateway_ready");
    assert.equal(result.state.cr11_setup_progress, "12/12");
});

test("restore button works without any gateway and clears only the Big table", async () => {
    const RegistryDevice = registryDeviceClass();
    const cr11 = new RegistryDevice({ieeeAddr: "0x0123456789abc104"});
    RegistryDevice.devices = [cr11];
    assignmentMode(cr11, 0);
    const published = [];
    const commands = [];
    const entity = {
        async command(cluster, command, payload, options) {
            commands.push({cluster, command, payload, options});
            const response = {data: Buffer.from([0x00])};
            protocolResponse({
                type: "commandClearBigResponse",
                data: response,
                device: cr11,
                meta: {zclTransactionSequenceNumber: 1},
            });
            return response;
        },
    };

    const result = await tzCr11Human.convertSet(entity, "cr11_restore_direct", "Restore", {
        device: cr11,
        state: {},
        publish: (state) => published.push(state),
    });

    assert.equal(commands.length, 1);
    assert.equal(commands[0].command, "clearBig");
    assert.equal(commands[0].payload.data.length, 0);
    assert.equal(commands[0].options.sendPolicy, "immediate");
    assert.equal(published[0].cr11_setup_state, "restoring_direct");
    assert.equal(result.state.cr11_routing_mode, "direct");
    assert.equal(result.state.cr11_setup_state, "direct_ready");
    assert.equal(result.state.cr11_setup_progress, "1/1");
});

test("human setup accepts only its exact button values", async () => {
    const RegistryDevice = registryDeviceClass();
    const cr11 = new RegistryDevice({ieeeAddr: "0x0123456789abc105"});
    const published = [];
    await assert.rejects(
        tzCr11Human.convertSet({}, "cr11_restore_direct", {Restore: true}, {
            device: cr11,
            state: {},
            publish: (state) => published.push(state),
        }),
        /accepts only "Restore"/,
    );
    assert.equal(published.at(-1).cr11_setup_state, "error");
});
