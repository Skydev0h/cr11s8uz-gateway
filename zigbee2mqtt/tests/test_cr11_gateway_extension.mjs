/* SPDX-License-Identifier: MIT */

import assert from "node:assert/strict";
import test from "node:test";

import CR11GatewayRouter, {
    CR11_GATEWAY_EVENT_OPTION,
    formatRoutedAction,
    validateGatewayEvent,
    validateSoftRejoinRequest,
} from "../cr11_gateway_router_extension.mjs";

function event(overrides = {}) {
    return {
        valid: true,
        v: 1,
        seq: 7,
        source: {mode: "short", address: "0x7111", endpoint: 1},
        selector: 3,
        button: 5,
        action: "click",
        forwardable: true,
        ...overrides,
    };
}

test("validates and formats plain/contextual actions", () => {
    assert.ok(validateGatewayEvent(event()));
    assert.equal(formatRoutedAction(event(), "plain"), "button_5_click");
    assert.equal(formatRoutedAction(event(), "selected"), "button_5_click_selected_3");
    assert.equal(formatRoutedAction(event({button: 2}), "selected"), "button_2_click");
    assert.equal(validateGatewayEvent(event({source: {mode: "short", address: "0x10000"}})), undefined);
    assert.equal(validateGatewayEvent(event({button: 9})), undefined);
});

test("strictly validates soft-rejoin requests", () => {
    assert.deepEqual(
        validateSoftRejoinRequest({id: "0x0123456789ABCDEF", confirm: "leave-and-rejoin"}),
        {id: "0x0123456789abcdef"},
    );
    assert.equal(validateSoftRejoinRequest({id: "0x0123456789abcdef"}), undefined);
    assert.equal(
        validateSoftRejoinRequest({id: "0x0123456789abcdef", confirm: "leave-and-rejoin", force: true}),
        undefined,
    );
    assert.equal(
        validateSoftRejoinRequest({id: "0x0123456789abcdef", confirm: "yes"}),
        undefined,
    );
});

test("hides raw gateway events by default without mutating unrelated state", () => {
    const router = new CR11GatewayRouter(
        {},
        undefined,
        undefined,
        async () => {},
        {},
        undefined,
        undefined,
        undefined,
        undefined,
        {info() {}, warning() {}},
    );
    const gateway = {
        definition: {model: "CR11Gateway"},
        options: {},
        isDevice: () => true,
    };
    const message = {cr11_gateway_event: "diagnostic", linkquality: 123};

    router.adjustMessageBeforePublish(gateway, message);
    assert.deepEqual(message, {linkquality: 123});

    gateway.options[CR11_GATEWAY_EVENT_OPTION] = true;
    const visible = {cr11_gateway_event: "diagnostic", linkquality: 124};
    router.adjustMessageBeforePublish(gateway, visible);
    assert.deepEqual(visible, {cr11_gateway_event: "diagnostic", linkquality: 124});

    const remoteMessage = {cr11_gateway_event: "unrelated"};
    router.adjustMessageBeforePublish({definition: {model: "other"}, isDevice: () => true}, remoteMessage);
    assert.deepEqual(remoteMessage, {cr11_gateway_event: "unrelated"});
});

test("routes each gateway sequence even when consecutive actions are identical", async () => {
    const remote = {
        zh: {modelID: "3c4e4fc81ed442efaf69353effcdfc5f"},
        options: {cr11_gateway_action_mode: "selected"},
        isDevice: () => true,
    };
    const gateway = {
        ieeeAddr: "0x1020304050607080",
        definition: {model: "CR11Gateway"},
        isDevice: () => true,
    };
    const published = [];
    const eventBus = {
        onPublishEntityState(owner, callback) { this.callback = callback; },
        removeListeners() {},
    };
    const router = new CR11GatewayRouter(
        {
            deviceByNetworkAddress: (address) => address === 0x7111 ? remote : undefined,
            devicesIterator: function* () {},
        },
        undefined,
        undefined,
        async (entity, payload) => published.push([entity, payload]),
        eventBus,
        undefined,
        undefined,
        undefined,
        undefined,
        {info() {}, warning() {}},
    );
    await router.start();
    const data = {
        entity: gateway,
        payload: {cr11_gateway_event: JSON.stringify(event())},
        stateChangeReason: undefined,
    };
    await eventBus.callback(data);
    await eventBus.callback(data);
    await eventBus.callback({
        ...data,
        payload: {cr11_gateway_event: JSON.stringify(event({seq: 8}))},
    });
    assert.deepEqual(published, [
        [remote, {action: "button_5_click_selected_3"}],
        [remote, {action: "button_5_click_selected_3"}],
    ]);

    await eventBus.callback({...data, stateChangeReason: "publishCached"});
    assert.equal(published.length, 2);
    await router.stop();
});

test("sends a zero-payload heartbeat only to the single gateway", async () => {
    const calls = [];
    const endpoint = {command: async (...args) => calls.push(args)};
    const gateway = {
        ieeeAddr: "0x1020304050607080",
        definition: {model: "CR11Gateway"},
        isDevice: () => true,
        endpoint: (id) => id === 21 ? endpoint : undefined,
    };
    const logs = [];
    const router = new CR11GatewayRouter(
        {devicesIterator: function* () { yield gateway; }},
        undefined,
        undefined,
        async () => {},
        {onPublishEntityState() {}, removeListeners() {}},
        undefined,
        undefined,
        undefined,
        undefined,
        {info: (message) => logs.push(message), warning: (message) => logs.push(message)},
    );

    await router.heartbeatTick();
    assert.deepEqual(calls, [[
        "cr11GatewayEvents",
        "heartbeat",
        {},
        {disableDefaultResponse: true},
    ]]);
    assert.deepEqual(logs, []);
});

test("does not choose silently when multiple gateways exist", async () => {
    const calls = [];
    const gateway = (ieeeAddr) => ({
        ieeeAddr,
        definition: {model: "CR11Gateway"},
        isDevice: () => true,
        endpoint: () => ({command: async (...args) => calls.push(args)}),
    });
    const warnings = [];
    const router = new CR11GatewayRouter(
        {devicesIterator: function* () {
            yield gateway("0x0000000000000001");
            yield gateway("0x0000000000000002");
        }},
        undefined,
        undefined,
        async () => {},
        {onPublishEntityState() {}, removeListeners() {}},
        undefined,
        undefined,
        undefined,
        undefined,
        {info() {}, warning: (message) => warnings.push(message)},
    );

    await router.heartbeatTick();
    assert.equal(calls.length, 0);
    assert.match(warnings[0], /expected one gateway, found 2/);
});

test("soft rejoin sends only a fixed leave-and-rejoin ZDO request", async () => {
    const remote = {
        ieeeAddr: "0x0123456789abcdef",
        zh: {modelID: "51725b7bcba945c8a595b325127461e9", networkAddress: 0x3456},
        isDevice: () => true,
    };
    const gateway = {
        ieeeAddr: "0x1020304050607080",
        definition: {model: "CR11Gateway"},
        isDevice: () => true,
        endpoint: () => ({command: async () => {}}),
    };
    const calls = [];
    const publications = [];
    const eventBus = {
        onPublishEntityState() {},
        onMQTTMessage(owner, callback) { this.mqttCallback = callback; },
        removeListeners() {},
    };
    const router = new CR11GatewayRouter(
        {
            devicesIterator: function* () { yield gateway; yield remote; },
            resolveEntity: (id) => id === remote.ieeeAddr ? remote : undefined,
            permitJoin: async (seconds) => calls.push(["permitJoin", seconds]),
            zhController: {sendRaw: async (payload) => calls.push(["sendRaw", payload])},
        },
        {publish: async (topic, payload) => publications.push([topic, JSON.parse(payload)])},
        undefined,
        async () => {},
        eventBus,
        undefined,
        undefined,
        undefined,
        {get: () => ({mqtt: {base_topic: "cr11lab"}})},
        {info() {}, warning() {}},
    );
    await router.start();
    await eventBus.mqttCallback({
        topic: "cr11lab/bridge/request/cr11_soft_rejoin",
        message: JSON.stringify({id: remote.ieeeAddr, confirm: "leave-and-rejoin"}),
    });
    assert.deepEqual(calls, [
        ["permitJoin", 60],
        ["sendRaw", {
            ieeeAddress: remote.ieeeAddr,
            networkAddress: 0x3456,
            profileId: 0,
            clusterKey: 0x0034,
            zdoParams: [remote.ieeeAddr, 0x80],
            disableResponse: true,
        }],
    ]);
    assert.equal(publications.at(-1)[0], "bridge/response/cr11_soft_rejoin");
    assert.equal(publications.at(-1)[1].status, "sent");
    await router.stop();
});

test("soft rejoin closes permit-join when the leave request fails", async () => {
    const remote = {
        ieeeAddr: "0x0123456789abcdef",
        zh: {modelID: "51725b7bcba945c8a595b325127461e9", networkAddress: 0x3456},
        isDevice: () => true,
    };
    const calls = [];
    const publications = [];
    const eventBus = {
        onPublishEntityState() {},
        onMQTTMessage(owner, callback) { this.mqttCallback = callback; },
        removeListeners() {},
    };
    const router = new CR11GatewayRouter(
        {
            devicesIterator: function* () {},
            resolveEntity: () => remote,
            permitJoin: async (seconds) => calls.push(["permitJoin", seconds]),
            zhController: {sendRaw: async () => {
                calls.push(["sendRaw"]);
                throw new Error("transport failed");
            }},
        },
        {publish: async (topic, payload) => publications.push([topic, JSON.parse(payload)])},
        undefined,
        async () => {},
        eventBus,
        undefined,
        undefined,
        undefined,
        {get: () => ({mqtt: {base_topic: "cr11lab"}})},
        {info() {}, warning() {}},
    );
    await router.start();
    await eventBus.mqttCallback({
        topic: "cr11lab/bridge/request/cr11_soft_rejoin",
        message: JSON.stringify({id: remote.ieeeAddr, confirm: "leave-and-rejoin"}),
    });
    assert.deepEqual(calls, [["permitJoin", 60], ["sendRaw"], ["permitJoin", 0]]);
    assert.equal(publications.at(-1)[1].status, "error");
    await router.stop();
});
