/* SPDX-License-Identifier: MIT */

/*
 * ORVIBO CR11S8UZ research/router converter for Zigbee2MQTT.
 *
 * Copy this file to data/external_converters/. Configuration writes are only
 * performed by the explicit human setup buttons or `cr11_command`; loading the
 * converter by itself does not alter the remote.
 */

import {Zcl} from "zigbee-herdsman";
import * as exposes from "zigbee-herdsman-converters/lib/exposes";
import * as m from "zigbee-herdsman-converters/lib/modernExtend";

const e = exposes.presets;
const ea = exposes.access;

const CLUSTER_NAME = "manuSpecificCr11S8uz";
const CLUSTER_ID = 0x0017;
const PROFILE_ID_HA = 0x0104;
const MAX_ACTION_PAYLOAD = 7;
const MAX_ROUTER_RECORDS = 50;
const WAKE_QUEUE_MS = 45_000;
const RESPONSE_TIMEOUT_MS = 2_000;
const INSTALL_RESPONSE_TIMEOUT_MS = 45_000;
const UPPER_HOLD_MARKER_LEVEL = 0xa5;
const UPPER_RELEASE_MARKER_LEVEL = 0x5a;
const UPPER_MARKER_TRANSITION_TIME = [0x00, 0x00];
const GATEWAY_MODEL_ID = "CR11-Bridge";
const GATEWAY_MANUFACTURERS = new Set(["Skydev0h", "SkyDev"]);
const GATEWAY_TARGET_ENDPOINTS = Object.freeze([10, 11, 12, 13]);
const GATEWAY_EVENT_ENDPOINT = 21;
const CONFIGURE_GATEWAY_VALUE = "Configure";
const RESTORE_DIRECT_VALUE = "Restore";
const QUERY_FIRMWARE_WARNING =
    "Known v3.1.04 firmware bug: the Big-table lookup uses an uninitialized key; this response is diagnostic and does not authoritatively describe the Big table";
const pendingRouterInstalls = new Map();
const assignmentModeByDevice = new Map();
const setupStateByDevice = new Map();

const statusNames = new Map([
    [0x00, "success"],
    [0x10, "zmem_error"],
    [0x87, "invalid_value"],
    [0x89, "insufficient_space"],
    [0x8b, "not_found"],
]);

const keyActionNames = new Map([
    [0x00, "click"],
    [0x02, "hold"],
    [0x03, "release"],
]);

const upperButtons = new Map([
    [3, "button_1"],
    [11, "button_2"],
    [7, "button_3"],
    [15, "button_4"],
]);

const dataParameter = [{name: "data", type: Zcl.BuffaloZclDataType.BUFFER}];

export const cr11Cluster = {
    name: CLUSTER_NAME,
    ID: CLUSTER_ID,
    attributes: {},
    commands: {
        addBig: {name: "addBig", ID: 0x00, response: 0x00, parameters: dataParameter},
        // Query can answer either as 0x01 (big table) or 0x05 (small table),
        // therefore it intentionally has no single declared response.  The
        // known v3.1.04 CR11 firmware has a broken Big-table lookup; keep this
        // command for diagnostics, not installation verification.
        query: {name: "query", ID: 0x01, parameters: dataParameter},
        deleteBig: {name: "deleteBig", ID: 0x02, response: 0x02, parameters: dataParameter},
        clearBig: {name: "clearBig", ID: 0x03, response: 0x03, parameters: dataParameter},
        addSmall: {name: "addSmall", ID: 0x04, response: 0x04, parameters: dataParameter},
        clearSmall: {name: "clearSmall", ID: 0x05, response: 0x06, parameters: dataParameter},
        clearAll: {name: "clearAll", ID: 0x06, response: 0x07, parameters: dataParameter},
    },
    commandsResponse: {
        addBigResponse: {name: "addBigResponse", ID: 0x00, parameters: dataParameter},
        queryBigResponse: {name: "queryBigResponse", ID: 0x01, parameters: dataParameter},
        deleteBigResponse: {name: "deleteBigResponse", ID: 0x02, parameters: dataParameter},
        clearBigResponse: {name: "clearBigResponse", ID: 0x03, parameters: dataParameter},
        addSmallResponse: {name: "addSmallResponse", ID: 0x04, parameters: dataParameter},
        querySmallResponse: {name: "querySmallResponse", ID: 0x05, parameters: dataParameter},
        clearSmallResponse: {name: "clearSmallResponse", ID: 0x06, parameters: dataParameter},
        clearAllResponse: {name: "clearAllResponse", ID: 0x07, parameters: dataParameter},
        keyEvent: {name: "keyEvent", ID: 0x08, parameters: dataParameter},
    },
};

function fail(message) {
    throw new Error(`CR11: ${message}`);
}

function safeErrorMessage(error) {
    const message = error instanceof Error ? error.message : String(error);
    return message.replace(/[\u0000-\u001f\u007f]/g, " ").slice(0, 240);
}

function rememberSetupState(deviceIeee, state) {
    if (deviceIeee) setupStateByDevice.set(deviceIeee, {...state});
    return state;
}

function publishSetupState(meta, state) {
    const deviceIeee = meta.device?.ieeeAddr;
    rememberSetupState(deviceIeee, state);
    if (typeof meta.publish === "function") meta.publish(state);
}

function requireAssignmentMode(deviceIeee, operation) {
    if (assignmentModeByDevice.get(deviceIeee) !== "active") {
        fail(`${operation} requires a fresh active assignment-mode report from the CR11`);
    }
}

function requireNoPendingOperation(deviceIeee, operation) {
    if (pendingRouterInstalls.has(deviceIeee)) {
        fail(`${operation} cannot start while another action-table operation is in progress`);
    }
}

function gatewayEndpointsPresent(gateway) {
    if (typeof gateway?.getEndpoint !== "function") return false;
    return [...GATEWAY_TARGET_ENDPOINTS, GATEWAY_EVENT_ENDPOINT].every((endpoint) => gateway.getEndpoint(endpoint));
}

export function discoverCr11Gateway(cr11Device, candidates) {
    if (!cr11Device) fail("gateway discovery requires a concrete CR11 device");
    if (candidates === undefined) {
        const Device = cr11Device.constructor;
        if (typeof Device?.allIterator !== "function") {
            fail("this Zigbee stack cannot enumerate joined devices for CR11Gateway discovery");
        }
        candidates = Device.allIterator();
    }
    if (!candidates || typeof candidates[Symbol.iterator] !== "function") {
        fail("gateway discovery received a non-iterable device list");
    }

    const gateways = [];
    for (const candidate of candidates) {
        if (
            candidate !== cr11Device &&
            candidate?.modelID === GATEWAY_MODEL_ID &&
            GATEWAY_MANUFACTURERS.has(candidate?.manufacturerName)
        ) {
            gateways.push(candidate);
        }
    }
    if (gateways.length === 0) fail("no joined CR11Gateway was found");
    if (gateways.length !== 1) fail(`expected exactly one joined CR11Gateway, found ${gateways.length}`);

    const gateway = gateways[0];
    const gatewayIeee = normalizeIeee(gateway.ieeeAddr);
    if (gateway.interviewState !== undefined && gateway.interviewState !== "SUCCESSFUL") {
        fail(`CR11Gateway ${gatewayIeee} has not completed its interview`);
    }
    if (!gatewayEndpointsPresent(gateway)) {
        fail(`CR11Gateway ${gatewayIeee} is missing one or more required endpoints (10, 11, 12, 13, 21)`);
    }
    return gateway;
}

function asInteger(name, value, minimum, maximum, defaultValue) {
    if (value === undefined && defaultValue !== undefined) value = defaultValue;
    if (typeof value === "string" && /^(?:0x[0-9a-f]+|[0-9]+)$/i.test(value)) value = Number(value);
    if (!Number.isInteger(value) || value < minimum || value > maximum) {
        fail(`${name} must be an integer in range ${minimum}..${maximum}`);
    }
    return value;
}

export function normalizeIeee(value) {
    if (typeof value !== "string") fail("target_ieee must be a string");
    const match = /^(?:0x)?([0-9a-f]{16})$/i.exec(value.trim());
    if (!match) fail("target_ieee must contain exactly 16 hexadecimal digits");
    return `0x${match[1].toLowerCase()}`;
}

function ieeeToWire(value) {
    return Buffer.from(normalizeIeee(value).slice(2), "hex").reverse();
}

function ieeeFromWireRedacted(value) {
    if (!Buffer.isBuffer(value) || value.length !== 8) return "invalid";
    const address = Buffer.from(value).reverse().toString("hex");
    return `0x************${address.slice(-4)}`;
}

function parseHex(value, name = "data") {
    if (value === undefined || value === null || value === "") return Buffer.alloc(0);
    if (Array.isArray(value)) {
        return Buffer.from(value.map((item, index) => asInteger(`${name}[${index}]`, item, 0, 0xff)));
    }
    if (typeof value !== "string") fail(`${name} must be a hex string or byte array`);
    let cleaned = value.trim().toLowerCase();
    if (cleaned.startsWith("0x")) cleaned = cleaned.slice(2);
    cleaned = cleaned.replace(/[\s:_-]/g, "");
    if (cleaned.length % 2 !== 0 || !/^[0-9a-f]*$/.test(cleaned)) fail(`${name} is not complete hexadecimal bytes`);
    return Buffer.from(cleaned, "hex");
}

function parseKeyAction(value = "click") {
    if (typeof value === "string") {
        const found = new Map([
            ["click", 0],
            ["hold", 2],
            ["release", 3],
        ]).get(value.toLowerCase());
        if (found !== undefined) return found;
    }
    return asInteger("key_action", value, 0, 0xff);
}

function parseObject(value) {
    if (typeof value === "string") {
        if (value.length > 4096) fail("cr11_command JSON is too long");
        try {
            value = JSON.parse(value);
        } catch (error) {
            fail(`cr11_command is not valid JSON (${error.message})`);
        }
    }
    if (value === null || typeof value !== "object" || Array.isArray(value)) fail("cr11_command must be an object or JSON object string");
    return value;
}

export function encodeBigAction(input) {
    const keyNo = asInteger("key_no", input.key_no, 0, 24);
    const keyBank = asInteger("key_bank", input.key_bank, 0, 2, 0);
    const keyAction = parseKeyAction(input.key_action);
    const endpoint = asInteger("target_endpoint", input.target_endpoint, 1, 240, 1);
    const profile = asInteger("profile_id", input.profile_id, 0, 0xffff, PROFILE_ID_HA);
    const cluster = asInteger("cluster_id", input.cluster_id, 0, 0xffff);
    const command = asInteger("command_id", input.command_id, 0, 0xff);
    const payload = parseHex(input.data, "data");
    if (payload.length > MAX_ACTION_PAYLOAD) fail(`data is limited to ${MAX_ACTION_PAYLOAD} bytes`);
    return Buffer.concat([
        Buffer.from([keyNo, keyBank, keyAction]),
        ieeeToWire(input.target_ieee),
        Buffer.from([endpoint]),
        Buffer.from([profile & 0xff, profile >> 8]),
        Buffer.from([cluster & 0xff, cluster >> 8]),
        Buffer.from([payload.length + 1, command]),
        payload,
    ]);
}

function encodeKey(input) {
    return Buffer.from([asInteger("key_no", input.key_no, 0, 24), asInteger("key_bank", input.key_bank, 0, 2, 0), parseKeyAction(input.key_action)]);
}

function routingMode(value = "independent") {
    if (value !== "independent" && value !== "portable" && value !== "dual-level" && value !== "four-target") {
        fail("mode must be independent, portable, dual-level, or four-target");
    }
    return value;
}

export function routerPlan(targetIeee, targetEndpoint = 1, mode = "independent", targetRightEndpoint, targetEndpoints) {
    mode = routingMode(mode);
    const leftEndpoint = asInteger("target_endpoint", targetEndpoint, 1, 240);
    if (mode === "four-target") {
        if (targetRightEndpoint !== undefined) {
            fail("four-target mode uses target_endpoints, not target_right_endpoint");
        }
        if (!Array.isArray(targetEndpoints) || targetEndpoints.length !== 4) {
            fail("four-target mode requires exactly four target_endpoints");
        }
        const endpoints = targetEndpoints.map((endpoint, index) =>
            asInteger(`target_endpoints[${index}]`, endpoint, 1, 240),
        );
        if (new Set(endpoints).size !== 4) {
            fail("four-target mode requires four distinct target_endpoints");
        }
        const target = normalizeIeee(targetIeee);
        return [3, 7, 11, 15].flatMap((keyNo, index) => {
            const common = {
                target_ieee: target,
                target_endpoint: endpoints[index],
                profile_id: PROFILE_ID_HA,
                key_bank: 0,
                key_no: keyNo,
            };
            return [
                {...common, key_action: 0x00, cluster_id: 0x0006, command_id: 0x02, data: []},
                {
                    ...common,
                    key_action: 0x02,
                    cluster_id: 0x0008,
                    command_id: 0x00,
                    data: [UPPER_HOLD_MARKER_LEVEL, ...UPPER_MARKER_TRANSITION_TIME],
                },
                {
                    ...common,
                    key_action: 0x03,
                    cluster_id: 0x0008,
                    command_id: 0x04,
                    data: [UPPER_RELEASE_MARKER_LEVEL, ...UPPER_MARKER_TRANSITION_TIME],
                },
            ];
        });
    }
    if (targetEndpoints !== undefined) {
        fail("target_endpoints is only valid in four-target mode");
    }
    const rightEndpoint =
        targetRightEndpoint === undefined
            ? leftEndpoint
            : asInteger("target_right_endpoint", targetRightEndpoint, 1, 240);
    if (mode === "dual-level" && targetRightEndpoint === undefined) {
        fail("dual-level mode requires target_right_endpoint");
    }
    if (mode === "dual-level" && rightEndpoint === leftEndpoint) {
        fail("dual-level mode requires different left and right endpoints");
    }
    const common = {
        target_ieee: normalizeIeee(targetIeee),
        target_endpoint: leftEndpoint,
        profile_id: PROFILE_ID_HA,
        key_bank: 0,
        key_action: 0,
    };
    const right = {...common, target_endpoint: rightEndpoint};
    const left = [
        {...common, key_no: 3, cluster_id: 0x0006, command_id: 0x00, data: []},
        {...common, key_no: 7, cluster_id: 0x0006, command_id: 0x01, data: []},
    ];
    if (mode === "portable") {
        return [
            ...left,
            {...common, key_no: 11, cluster_id: 0x0006, command_id: 0x02, data: []},
            {...common, key_no: 15, cluster_id: 0x0006, command_id: 0x40, data: [0, 0]},
        ];
    }
    if (mode === "dual-level") {
        return [
            ...left,
            {...right, key_no: 11, cluster_id: 0x0006, command_id: 0x00, data: []},
            {...right, key_no: 15, cluster_id: 0x0006, command_id: 0x01, data: []},
        ];
    }
    return [
        ...left,
        {...right, key_no: 11, cluster_id: 0x0102, command_id: 0x05, data: [0]},
        {...right, key_no: 15, cluster_id: 0x0102, command_id: 0x05, data: [100]},
    ];
}

function statusResult(command, payload) {
    if (!payload || !Buffer.isBuffer(payload.data)) fail(`${command} did not return a CR11 response`);
    const data = payload.data;
    if (data.length < 1) fail(`${command} returned an empty response`);
    const code = data[0];
    const result = {command, status: statusNames.get(code) ?? `unknown_0x${code.toString(16).padStart(2, "0")}`, status_code: code};
    if (code !== 0) fail(`${command} failed with ${result.status} (0x${code.toString(16).padStart(2, "0")})`);
    return result;
}

async function sendAwaited(entity, command, data) {
    const response = await entity.command(
        CLUSTER_NAME,
        command,
        {data: Buffer.from(data)},
        {
            disableDefaultResponse: true,
            timeout: RESPONSE_TIMEOUT_MS,
            sendPolicy: "queue",
        },
    );
    return statusResult(command, response);
}

async function sendInstallAwaited(entity, command, data) {
    const response = await entity.command(
        CLUSTER_NAME,
        command,
        {data: Buffer.from(data)},
        {
            disableDefaultResponse: true,
            timeout: INSTALL_RESPONSE_TIMEOUT_MS,
            sendPolicy: "immediate",
        },
    );
    return statusResult(command, response);
}

async function sendQuery(entity, data, assumeAwake) {
    const sendPolicy = assumeAwake ? "immediate" : "queue";
    await entity.command(
        CLUSTER_NAME,
        "query",
        {data: Buffer.from(data)},
        {
            disableDefaultResponse: true,
            disableResponse: true,
            timeout: RESPONSE_TIMEOUT_MS,
            sendPolicy,
        },
    );
    return {
        command: "query",
        status: "sent",
        send_policy: sendPolicy,
        note: `answer is published as cr11_response; ${QUERY_FIRMWARE_WARNING}`,
    };
}

async function withWakeQueue(meta, callback) {
    const device = meta.device;
    if (!device) fail("operation requires a concrete Zigbee device, not a group");
    device.addCustomCluster(CLUSTER_NAME, cr11Cluster);
    const previousTimeout = device.pendingRequestTimeout;
    if (previousTimeout < WAKE_QUEUE_MS) device.pendingRequestTimeout = WAKE_QUEUE_MS;
    try {
        return await callback();
    } finally {
        device.pendingRequestTimeout = previousTimeout;
    }
}

function requireConfirmation(input, expected) {
    if (input.confirm !== expected) fail(`destructive operation requires confirm=${JSON.stringify(expected)}`);
}

export function executeCommand(entity, value, meta, context = {}) {
    const input = parseObject(value);
    if (typeof input.op !== "string") fail("op must be a string");
    const op = input.op.toLowerCase();
    const uiOperation = context.uiOperation;
    if (uiOperation !== undefined && uiOperation !== "gateway" && uiOperation !== "direct") {
        fail("invalid internal UI operation context");
    }
    return withWakeQueue(meta, async () => {
        if (op === "install_router") {
            requireConfirmation(input, "replace_big_actions");
            const deviceIeee = meta.device.ieeeAddr;
            requireAssignmentMode(deviceIeee, "install_router");
            requireNoPendingOperation(deviceIeee, "install_router");
            const mode = routingMode(input.mode);
            const records = routerPlan(
                input.target_ieee,
                input.target_endpoint,
                mode,
                input.target_right_endpoint,
                input.target_endpoints,
            );
            if (records.length < 1 || records.length > MAX_ROUTER_RECORDS) {
                fail(`router plan must contain 1..${MAX_ROUTER_RECORDS} records`);
            }
            pendingRouterInstalls.set(deviceIeee, {
                op,
                mode,
                ui_operation: uiOperation,
                clear_confirmed: false,
                records_confirmed: 0,
                records_expected: records.length,
                seen_responses: new Set(),
            });
            try {
                const results = [await sendInstallAwaited(entity, "clearBig", Buffer.alloc(0))];
                for (const record of records) {
                    results.push(await sendInstallAwaited(entity, "addBig", encodeBigAction(record)));
                }
                routingModeByDevice.set(deviceIeee, mode);
                pendingRouterInstalls.delete(deviceIeee);
                return {op, status: "success", mode, records_written: records.length, commands: results};
            } catch (error) {
                pendingRouterInstalls.delete(deviceIeee);
                throw error;
            }
        }
        if (op === "restore_direct") {
            requireConfirmation(input, "restore_direct_mode");
            const deviceIeee = meta.device.ieeeAddr;
            requireAssignmentMode(deviceIeee, "restore_direct");
            requireNoPendingOperation(deviceIeee, "restore_direct");
            pendingRouterInstalls.set(deviceIeee, {
                op,
                mode: "direct",
                ui_operation: uiOperation,
                clear_confirmed: false,
                records_confirmed: 0,
                records_expected: 0,
                seen_responses: new Set(),
            });
            try {
                const result = await sendInstallAwaited(entity, "clearBig", Buffer.alloc(0));
                routingModeByDevice.delete(deviceIeee);
                pendingRouterInstalls.delete(deviceIeee);
                return {op, status: "success", mode: "direct", commands: [result]};
            } catch (error) {
                pendingRouterInstalls.delete(deviceIeee);
                throw error;
            }
        }
        if (op === "add_big") {
            return {op, ...(await sendAwaited(entity, "addBig", encodeBigAction(input)))};
        }
        if (op === "query") {
            if (input.assume_awake !== undefined && typeof input.assume_awake !== "boolean") {
                fail("assume_awake must be a boolean");
            }
            return {op, ...(await sendQuery(entity, encodeKey(input), input.assume_awake === true))};
        }
        if (op === "delete_big") {
            return {op, ...(await sendAwaited(entity, "deleteBig", encodeKey(input)))};
        }
        if (op === "clear_big") {
            requireConfirmation(input, "replace_big_actions");
            return {op, ...(await sendAwaited(entity, "clearBig", Buffer.alloc(0)))};
        }
        if (op === "clear_small") {
            requireConfirmation(input, "clear_small_actions");
            return {op, ...(await sendAwaited(entity, "clearSmall", Buffer.alloc(0)))};
        }
        if (op === "clear_all") {
            requireConfirmation(input, "clear_all_actions");
            return {op, ...(await sendAwaited(entity, "clearAll", Buffer.alloc(0)))};
        }
        fail(`unsupported op ${JSON.stringify(input.op)}`);
    });
}

function bufferData(msg) {
    const data = msg?.data?.data;
    return Buffer.isBuffer(data) ? data : Buffer.alloc(0);
}

function keyEvent(data) {
    if (data.length !== 3) return {cr11_response: JSON.stringify({kind: "malformed_key_event", length: data.length})};
    const button = upperButtons.get(data[0]);
    const action = keyActionNames.get(data[2]);
    if (!button || !action) {
        return {
            cr11_response: JSON.stringify({kind: "unknown_key_event", key_no: data[0], key_bank: data[1], key_action: data[2]}),
        };
    }
    return {action: `${button}_${action}`};
}

function decodeBigResponseBody(body) {
    if (body.length < 18) return {body_length: body.length, malformed: true};
    const actionLength = body[16];
    if (actionLength < 1 || actionLength > 8 || body.length !== 17 + actionLength) {
        return {body_length: body.length, action_length: actionLength, malformed: true};
    }
    return {
        key_no: body[0],
        key_bank: body[1],
        key_action: body[2],
        target_ieee: ieeeFromWireRedacted(body.subarray(3, 11)),
        target_endpoint: body[11],
        profile_id: body.readUInt16LE(12),
        cluster_id: body.readUInt16LE(14),
        action_length: actionLength,
        command_id: body[17],
        command_payload_hex: body.subarray(18).toString("hex"),
    };
}

function setupStateForProgress(progress) {
    if (progress.ui_operation === "gateway") {
        if (progress.status === "failed") {
            return {
                cr11_setup_state: "error",
                cr11_setup_progress: `${progress.records_confirmed}/${progress.records_expected}`,
                cr11_setup_detail: `Gateway configuration failed: ${progress.device_status}`,
            };
        }
        if (progress.status === "success") {
            return {
                cr11_routing_mode: "gateway",
                cr11_setup_state: "gateway_ready",
                cr11_setup_progress: `${progress.records_expected}/${progress.records_expected}`,
                cr11_setup_detail: "Gateway mode configured; all eight buttons are routed through CR11Gateway",
            };
        }
        return {
            cr11_setup_state: "configuring_gateway",
            cr11_setup_progress: `${progress.records_confirmed}/${progress.records_expected}`,
            cr11_setup_detail: progress.clear_confirmed
                ? "Big action table cleared; writing gateway routing records"
                : "Waiting for the CR11 to confirm action-table clear",
        };
    }
    if (progress.ui_operation === "direct") {
        if (progress.status === "failed") {
            return {
                cr11_setup_state: "error",
                cr11_setup_progress: "0/1",
                cr11_setup_detail: `Direct-mode restore failed: ${progress.device_status}`,
            };
        }
        if (progress.status === "success") {
            return {
                cr11_routing_mode: "direct",
                cr11_setup_state: "direct_ready",
                cr11_setup_progress: "1/1",
                cr11_setup_detail: "Direct mode restored; buttons 1-4 report to the coordinator and buttons 5-8 are inactive",
            };
        }
        return {
            cr11_setup_state: "restoring_direct",
            cr11_setup_progress: "0/1",
            cr11_setup_detail: "Waiting for the CR11 to confirm action-table clear",
        };
    }
}

function installProgress(msg, response) {
    const ieee = msg?.device?.ieeeAddr;
    const pending = ieee ? pendingRouterInstalls.get(ieee) : undefined;
    if (!pending || (msg.type !== "commandClearBigResponse" && msg.type !== "commandAddBigResponse")) return;

    const tsn = msg?.meta?.zclTransactionSequenceNumber;
    const responseKey = Number.isInteger(tsn) && tsn >= 0 && tsn <= 0xff ? `${msg.type}:${tsn}` : undefined;
    if (responseKey && pending.seen_responses.has(responseKey)) return;
    if (responseKey) pending.seen_responses.add(responseKey);

    if (response.status_code !== 0) {
        pendingRouterInstalls.delete(ieee);
        return {
            op: pending.op,
            status: "failed",
            mode: pending.mode,
            ui_operation: pending.ui_operation,
            clear_confirmed: pending.clear_confirmed,
            records_confirmed: pending.records_confirmed,
            records_expected: pending.records_expected,
            failed_response: msg.type,
            device_status: response.status,
            device_status_code: response.status_code,
        };
    }

    if (msg.type === "commandClearBigResponse") {
        if (pending.clear_confirmed) return;
        pending.clear_confirmed = true;
    } else {
        if (!pending.clear_confirmed || pending.records_confirmed >= pending.records_expected) return;
        pending.records_confirmed += 1;
    }

    const complete = pending.clear_confirmed && pending.records_confirmed === pending.records_expected;
    if (complete) {
        if (pending.mode === "direct") routingModeByDevice.delete(ieee);
        else routingModeByDevice.set(ieee, pending.mode);
        pendingRouterInstalls.delete(ieee);
    }
    return {
        op: pending.op,
        status: complete ? "success" : "in_progress",
        mode: pending.mode,
        ui_operation: pending.ui_operation,
        clear_confirmed: pending.clear_confirmed,
        records_confirmed: pending.records_confirmed,
        records_expected: pending.records_expected,
    };
}

export function protocolResponse(msg) {
    const data = bufferData(msg);
    if (data.length < 1) return {cr11_response: JSON.stringify({kind: msg.type, malformed: true})};
    const response = {
        kind: msg.type,
        status_code: data[0],
        status: statusNames.get(data[0]) ?? `unknown_0x${data[0].toString(16).padStart(2, "0")}`,
    };
    if (msg.type === "commandQueryBigResponse" || msg.type === "commandQuerySmallResponse") {
        response.big_table_authoritative = false;
        response.firmware_warning = QUERY_FIRMWARE_WARNING;
    }
    const body = data.subarray(1);
    if (msg.type === "commandQueryBigResponse") {
        response.big_action = decodeBigResponseBody(body);
    } else if (msg.type === "commandDeleteBigResponse" && body.length === 3) {
        response.key = {key_no: body[0], key_bank: body[1], key_action: body[2]};
    } else if (msg.type === "commandAddSmallResponse" || msg.type === "commandQuerySmallResponse") {
        response.small_action =
            body.length === 6
                ? {key_no: body[0], key_bank: body[1], key_action: body[2], opaque_hex: body.subarray(3).toString("hex")}
                : {malformed: true, body_length: body.length};
    } else if (body.length) {
        response.unexpected_body_length = body.length;
    }
    const result = {cr11_response: JSON.stringify(response)};
    const progress = installProgress(msg, response);
    if (progress) {
        result.cr11_last_result = JSON.stringify(progress);
        const setupState = setupStateForProgress(progress);
        if (setupState) {
            rememberSetupState(msg.device?.ieeeAddr, setupState);
            Object.assign(result, setupState);
        }
    }
    return result;
}

const fzCr11 = {
    cluster: CLUSTER_NAME,
    type: [
        "commandAddBigResponse",
        "commandQueryBigResponse",
        "commandDeleteBigResponse",
        "commandClearBigResponse",
        "commandAddSmallResponse",
        "commandQuerySmallResponse",
        "commandClearSmallResponse",
        "commandClearAllResponse",
        "commandKeyEvent",
    ],
    convert: (model, msg) => (msg.type === "commandKeyEvent" ? keyEvent(bufferData(msg)) : protocolResponse(msg)),
};

// Fallback for a message received before the custom cluster was registered.
const fzCr11Raw = {
    cluster: CLUSTER_ID,
    type: "raw",
    convert: (model, msg) => {
        const data = Buffer.from(msg.data);
        if (data.length >= 3 && data[2] === 0x08) return keyEvent(data.subarray(3));
        return {
            cr11_response: JSON.stringify({
                kind: "raw",
                frame_hex: data.subarray(0, 3).toString("hex"),
                payload_length: Math.max(0, data.length - 3),
            }),
        };
    },
};

export const fzAssignmentMode = {
    cluster: "genBasic",
    type: ["attributeReport", "readResponse"],
    convert: (model, msg) => {
        if (!Object.hasOwn(msg.data, 0x00aa)) return;
        const value = msg.data[0x00aa];
        const ieee = msg.device?.ieeeAddr;
        if (value === 0) {
            if (ieee) assignmentModeByDevice.set(ieee, "active");
            const setupState = {
                cr11_setup_state: "ready",
                cr11_setup_progress: "ready",
                cr11_setup_detail: "The CR11 is listening; choose Configure or Restore",
            };
            rememberSetupState(ieee, setupState);
            return {cr11_assignment_mode: "active", ...setupState};
        }
        if (value === 1) {
            if (ieee) assignmentModeByDevice.set(ieee, "idle");
            const current = ieee ? setupStateByDevice.get(ieee) : undefined;
            if (!pendingRouterInstalls.has(ieee) && (!current || current.cr11_setup_state === "ready")) {
                const setupState = {
                    cr11_setup_state: "idle",
                    cr11_setup_progress: "idle",
                    cr11_setup_detail: "Assignment mode is closed; enter it before changing the route",
                };
                rememberSetupState(ieee, setupState);
                return {cr11_assignment_mode: "idle", ...setupState};
            }
            return {cr11_assignment_mode: "idle"};
        }
        return {
            cr11_assignment_mode: "unknown",
            cr11_response: JSON.stringify({kind: "assignment_mode", raw_value: value}),
        };
    },
};

const lastLevelButton = new Map();
const lastCoverButton = new Map();
const routingModeByDevice = new Map();
const selectedColumn = new Map();

const fzUpperMarker = {
    cluster: "genOnOff",
    type: ["commandOff", "commandOn", "commandToggle", "commandOffWithEffect"],
    convert: (model, msg) => {
        const ieee = msg.device.ieeeAddr;
        const lookup = {
            commandOff: ["button_1_click", "left"],
            commandOn: ["button_3_click", "left"],
            commandToggle: ["button_2_click", "right"],
            commandOffWithEffect: ["button_4_click", "right"],
        };
        const [action, column] = lookup[msg.type];
        selectedColumn.set(ieee, column);
        if (column === "right") routingModeByDevice.set(ieee, "portable");
        return {action};
    },
};

const fzLeftRocker = {
    cluster: "genLevelCtrl",
    type: ["commandStep", "commandMove", "commandStop"],
    convert: (model, msg) => {
        const ieee = msg.device.ieeeAddr;
        const portableRight = routingModeByDevice.get(ieee) === "portable" && selectedColumn.get(ieee) === "right";
        const upButton = portableRight ? 7 : 5;
        const downButton = portableRight ? 8 : 6;
        if (msg.type === "commandStep") return {action: `button_${msg.data.stepmode === 1 ? downButton : upButton}_click`};
        if (msg.type === "commandMove") {
            const button = msg.data.movemode === 1 ? downButton : upButton;
            lastLevelButton.set(ieee, button);
            return {action: `button_${button}_hold`};
        }
        const button = lastLevelButton.get(ieee);
        lastLevelButton.delete(ieee);
        return {action: button ? `button_${button}_release` : "button_5_or_6_release"};
    },
};

const fzRightRocker = {
    cluster: "closuresWindowCovering",
    type: ["commandUpOpen", "commandDownClose", "commandStop", "commandGoToLiftPercentage"],
    convert: (model, msg) => {
        const ieee = msg.device.ieeeAddr;
        if (msg.type === "commandGoToLiftPercentage") {
            routingModeByDevice.set(ieee, "independent");
            selectedColumn.set(ieee, "right");
            if (msg.data.percentageliftvalue === 0) return {action: "button_2_click"};
            if (msg.data.percentageliftvalue === 100) return {action: "button_4_click"};
            return {cr11_response: JSON.stringify({kind: "unknown_right_marker", percentage: msg.data.percentageliftvalue})};
        }
        if (msg.type === "commandUpOpen" || msg.type === "commandDownClose") {
            const button = msg.type === "commandUpOpen" ? 7 : 8;
            lastCoverButton.set(ieee, button);
            return {action: `button_${button}_press`};
        }
        const button = lastCoverButton.get(ieee);
        lastCoverButton.delete(ieee);
        return {action: button ? `button_${button}_release` : "button_7_or_8_release"};
    },
};

const tzCr11 = {
    key: ["cr11_command"],
    convertSet: async (entity, key, value, meta) => {
        const result = await executeCommand(entity, value, meta);
        return {state: {cr11_last_result: JSON.stringify(result)}};
    },
};

function setupErrorState(meta, operation, error) {
    const previous = setupStateByDevice.get(meta.device?.ieeeAddr);
    const defaultProgress = operation === "gateway" ? "0/12" : "0/1";
    return {
        cr11_setup_state: "error",
        cr11_setup_progress: previous?.cr11_setup_progress ?? defaultProgress,
        cr11_setup_detail: safeErrorMessage(error),
    };
}

export const tzCr11Human = {
    key: ["cr11_use_gateway", "cr11_restore_direct"],
    convertSet: async (entity, key, value, meta) => {
        const operation = key === "cr11_use_gateway" ? "gateway" : key === "cr11_restore_direct" ? "direct" : undefined;
        try {
            if (!operation) fail(`unsupported setup property ${JSON.stringify(key)}`);
            if (key === "cr11_use_gateway" && value !== CONFIGURE_GATEWAY_VALUE) {
                fail(`cr11_use_gateway accepts only ${JSON.stringify(CONFIGURE_GATEWAY_VALUE)}`);
            }
            if (key === "cr11_restore_direct" && value !== RESTORE_DIRECT_VALUE) {
                fail(`cr11_restore_direct accepts only ${JSON.stringify(RESTORE_DIRECT_VALUE)}`);
            }
            if (!meta.device) fail("setup actions require a concrete Zigbee device, not a group");

            const deviceIeee = meta.device.ieeeAddr;
            let command;
            if (operation === "gateway") {
                const gateway = discoverCr11Gateway(meta.device);
                requireAssignmentMode(deviceIeee, "gateway configuration");
                requireNoPendingOperation(deviceIeee, "gateway configuration");
                publishSetupState(meta, {
                    cr11_setup_state: "configuring_gateway",
                    cr11_setup_progress: "0/12",
                    cr11_setup_detail: `Writing 12 routing records to CR11Gateway ${gateway.ieeeAddr}`,
                });
                command = {
                    op: "install_router",
                    target_ieee: normalizeIeee(gateway.ieeeAddr),
                    target_endpoints: [...GATEWAY_TARGET_ENDPOINTS],
                    mode: "four-target",
                    confirm: "replace_big_actions",
                };
            } else {
                requireAssignmentMode(deviceIeee, "direct-mode restore");
                requireNoPendingOperation(deviceIeee, "direct-mode restore");
                publishSetupState(meta, {
                    cr11_setup_state: "restoring_direct",
                    cr11_setup_progress: "0/1",
                    cr11_setup_detail: "Clearing gateway routing records from the CR11",
                });
                command = {op: "restore_direct", confirm: "restore_direct_mode"};
            }

            const result = await executeCommand(entity, command, meta, {uiOperation: operation});
            const finalState =
                operation === "gateway"
                    ? {
                          cr11_routing_mode: "gateway",
                          cr11_setup_state: "gateway_ready",
                          cr11_setup_progress: "12/12",
                          cr11_setup_detail: "Gateway mode configured; all eight buttons are routed through CR11Gateway",
                      }
                    : {
                          cr11_routing_mode: "direct",
                          cr11_setup_state: "direct_ready",
                          cr11_setup_progress: "1/1",
                          cr11_setup_detail:
                              "Direct mode restored; buttons 1-4 report to the coordinator and buttons 5-8 are inactive",
                      };
            rememberSetupState(deviceIeee, finalState);
            return {state: {...finalState, cr11_last_result: JSON.stringify(result)}};
        } catch (error) {
            const failedState = setupErrorState(meta, operation, error);
            publishSetupState(meta, failedState);
            throw error;
        }
    },
};

const actions = [
    ...new Set([
        ...[1, 2, 3, 4].flatMap((button) => [`button_${button}_click`, `button_${button}_hold`, `button_${button}_release`]),
        ...[5, 6].flatMap((button) => [`button_${button}_click`, `button_${button}_hold`, `button_${button}_release`]),
        "button_5_or_6_release",
        ...[7, 8].flatMap((button) => [`button_${button}_click`, `button_${button}_hold`, `button_${button}_release`]),
        "button_7_press",
        "button_7_release",
        "button_8_press",
        "button_8_release",
        "button_7_or_8_release",
        ...[5, 6, 7, 8].flatMap((button) =>
            [1, 2, 3, 4].flatMap((selector) =>
                ["click", "hold", "release"].map(
                    (action) => `button_${button}_${action}_selected_${selector}`,
                ),
            ),
        ),
    ]),
];

const gatewayActionModeOption = e
    .enum("cr11_gateway_action_mode", ea.SET, ["plain", "selected"])
    .withDescription(
        "Naming for events relayed by CR11Gateway: plain button_5_click or contextual button_5_click_selected_1 (default: plain)",
    );

const definition = {
    zigbeeModel: ["3c4e4fc81ed442efaf69353effcdfc5f", "51725b7bcba945c8a595b325127461e9"],
    model: "CR11S8UZ",
    vendor: "ORVIBO",
    description: "Smart sticker switch with decoded action-table protocol",
    extend: [m.deviceAddCustomCluster(CLUSTER_NAME, cr11Cluster)],
    fromZigbee: [fzCr11, fzCr11Raw, fzAssignmentMode, fzUpperMarker, fzLeftRocker, fzRightRocker],
    toZigbee: [tzCr11Human, tzCr11],
    options: [gatewayActionModeOption],
    exposes: [
        e.action(actions),
        e
            .enum("cr11_assignment_mode", ea.STATE, ["idle", "active", "unknown"])
            .withLabel("Assignment mode")
            .withDescription("Whether the CR11 is currently listening for configuration commands"),
        e
            .enum("cr11_use_gateway", ea.SET, [CONFIGURE_GATEWAY_VALUE])
            .withLabel("Route through CR11Gateway")
            .withDescription(
                "Enter assignment mode first, then write all 12 records for the single joined CR11Gateway; enables all eight buttons",
            )
            .withCategory("config"),
        e
            .enum("cr11_restore_direct", ea.SET, [RESTORE_DIRECT_VALUE])
            .withLabel("Restore direct mode")
            .withDescription(
                "Enter assignment mode first, then clear gateway routing; buttons 1-4 report directly and buttons 5-8 become inactive",
            )
            .withCategory("config"),
        e
            .enum("cr11_routing_mode", ea.STATE, ["unknown", "gateway", "direct"])
            .withLabel("Last configured route")
            .withDescription("Last route successfully applied by this converter; the stock firmware cannot reliably read it back"),
        e
            .enum("cr11_setup_state", ea.STATE, [
                "idle",
                "ready",
                "configuring_gateway",
                "gateway_ready",
                "restoring_direct",
                "direct_ready",
                "error",
            ])
            .withLabel("Setup state")
            .withDescription("Current or most recent setup operation"),
        e
            .text("cr11_setup_progress", ea.STATE)
            .withLabel("Setup progress")
            .withDescription("Confirmed records, e.g. 0/12 through 12/12"),
        e
            .text("cr11_setup_detail", ea.STATE)
            .withLabel("Setup detail")
            .withDescription("Human-readable result or error; no automatic configuration is attempted"),
    ],
};

export default definition;
