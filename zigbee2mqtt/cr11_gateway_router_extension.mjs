/* SPDX-License-Identifier: MIT */

/*
 * Zigbee2MQTT external extension joining CR11Gateway events back onto the
 * original ORVIBO CR11S8UZ entity. It also holds the single CR11Gateway's
 * supervisor lease with one zero-payload heartbeat every five seconds.
 */

const GATEWAY_MODEL = "CR11Gateway";
const GATEWAY_ENDPOINT = 21;
const GATEWAY_CLUSTER = "cr11GatewayEvents";
const GATEWAY_HEARTBEAT_COMMAND = "heartbeat";
export const CR11_GATEWAY_EVENT_OPTION = "cr11_publish_gateway_event";
export const GATEWAY_HEARTBEAT_INTERVAL_MS = 5_000;
const SOFT_REJOIN_REQUEST = "bridge/request/cr11_soft_rejoin";
const SOFT_REJOIN_RESPONSE = "bridge/response/cr11_soft_rejoin";
const SOFT_REJOIN_CONFIRMATION = "leave-and-rejoin";
const ZDO_PROFILE_ID = 0x0000;
const ZDO_LEAVE_REQUEST = 0x0034;
const ZDO_LEAVE_AND_REJOIN = 0x80;
const SOFT_REJOIN_JOIN_WINDOW_SECONDS = 60;
const CR11_MODEL_IDS = new Set([
    "3c4e4fc81ed442efaf69353effcdfc5f",
    "51725b7bcba945c8a595b325127461e9",
]);
const ACTIONS = new Set(["click", "hold", "release"]);
const DEDUPE_WINDOW_MS = 5_000;
const DEDUPE_LIMIT = 256;

function safeErrorMessage(error) {
    const message = error instanceof Error ? error.message : String(error);
    return message.replace(/[\u0000-\u001f\u007f]/g, " ").slice(0, 240);
}

export function validateSoftRejoinRequest(value) {
    if (!value || typeof value !== "object" || Array.isArray(value)) return undefined;
    if (Object.getPrototypeOf(value) !== Object.prototype) return undefined;
    const keys = Object.keys(value);
    if (keys.length !== 2 || !keys.includes("id") || !keys.includes("confirm")) return undefined;
    if (typeof value.id !== "string" || !/^0x[0-9a-f]{16}$/i.test(value.id)) return undefined;
    if (value.confirm !== SOFT_REJOIN_CONFIRMATION) return undefined;
    return {id: value.id.toLowerCase()};
}

export function validateGatewayEvent(value) {
    if (!value || typeof value !== "object" || value.valid !== true || value.v !== 1) return undefined;
    if (!Number.isInteger(value.seq) || value.seq < 0 || value.seq > 0xffffffff) return undefined;
    if (!Number.isInteger(value.button) || value.button < 1 || value.button > 8) return undefined;
    if (!Number.isInteger(value.selector) || value.selector < 0 || value.selector > 4) return undefined;
    if (!ACTIONS.has(value.action) || value.forwardable !== true) return undefined;
    if (!value.source || typeof value.source !== "object") return undefined;
    if (value.source.mode === "short") {
        if (typeof value.source.address !== "string" || !/^0x[0-9a-f]{4}$/i.test(value.source.address)) return undefined;
    } else if (value.source.mode === "extended") {
        if (typeof value.source.address !== "string" || !/^0x[0-9a-f]{16}$/i.test(value.source.address)) return undefined;
    } else {
        return undefined;
    }
    return value;
}

export function formatRoutedAction(event, mode = "plain") {
    const checked = validateGatewayEvent(event);
    if (!checked) return undefined;
    const base = `button_${checked.button}_${checked.action}`;
    if (mode === "selected" && checked.button >= 5 && checked.selector >= 1 && checked.selector <= 4) {
        return `${base}_selected_${checked.selector}`;
    }
    return base;
}

export default class CR11GatewayRouter {
    constructor(
        zigbee,
        mqtt,
        state,
        publishEntityState,
        eventBus,
        enableDisableExtension,
        restartCallback,
        addExtension,
        settings,
        logger,
    ) {
        this.zigbee = zigbee;
        this.mqtt = mqtt;
        this.publishEntityState = publishEntityState;
        this.eventBus = eventBus;
        this.logger = logger;
        this.baseTopic = settings?.get?.()?.mqtt?.base_topic;
        this.softRejoinRunning = false;
        this.seen = new Map();
        this.heartbeatTimer = undefined;
        this.heartbeatRunning = false;
        this.heartbeatProblem = undefined;
        this.stopping = false;
        this.boundPublish = (data) => this.onPublishEntityState(data);
        this.boundMQTT = (data) => this.onMQTTMessage(data);
    }

    async start() {
        this.stopping = false;
        this.eventBus.onPublishEntityState(this, this.boundPublish);
        if (this.baseTopic && this.mqtt) this.eventBus.onMQTTMessage(this, this.boundMQTT);
        this.logger.info("CR11Gateway router extension started");
        await this.heartbeatTick();
        this.heartbeatTimer = setInterval(
            () => void this.heartbeatTick(),
            GATEWAY_HEARTBEAT_INTERVAL_MS,
        );
        this.heartbeatTimer.unref?.();
    }

    async stop() {
        this.stopping = true;
        clearInterval(this.heartbeatTimer);
        this.heartbeatTimer = undefined;
        this.eventBus.removeListeners(this);
        this.seen.clear();
    }

    async publishSoftRejoinResponse(response) {
        await this.mqtt.publish(SOFT_REJOIN_RESPONSE, JSON.stringify(response));
    }

    async onMQTTMessage(data) {
        if (data?.topic !== `${this.baseTopic}/${SOFT_REJOIN_REQUEST}`) return;
        if (typeof data.message !== "string" || data.message.length > 512) {
            await this.publishSoftRejoinResponse({status: "error", error: "invalid payload"});
            return;
        }

        let parsed;
        try {
            parsed = JSON.parse(data.message);
        } catch {
            await this.publishSoftRejoinResponse({status: "error", error: "invalid JSON"});
            return;
        }
        const request = validateSoftRejoinRequest(parsed);
        if (!request) {
            await this.publishSoftRejoinResponse({
                status: "error",
                error: `expected exactly {id, confirm:\"${SOFT_REJOIN_CONFIRMATION}\"}`,
            });
            return;
        }
        if (this.softRejoinRunning) {
            await this.publishSoftRejoinResponse({status: "error", id: request.id, error: "request already running"});
            return;
        }

        const remote = this.zigbee.resolveEntity(request.id);
        if (!this.isCR11(remote) || remote.ieeeAddr.toLowerCase() !== request.id) {
            await this.publishSoftRejoinResponse({status: "error", id: request.id, error: "CR11 not found"});
            return;
        }
        const networkAddress = remote.zh?.networkAddress;
        if (!Number.isInteger(networkAddress) || networkAddress < 0 || networkAddress > 0xffff) {
            await this.publishSoftRejoinResponse({
                status: "error",
                id: request.id,
                error: "CR11 has no valid network address",
            });
            return;
        }

        this.softRejoinRunning = true;
        let joinWindowMayBeOpen = false;
        let leaveSent = false;
        try {
            joinWindowMayBeOpen = true;
            await this.zigbee.permitJoin(SOFT_REJOIN_JOIN_WINDOW_SECONDS);
            await this.zigbee.zhController.sendRaw({
                ieeeAddress: remote.ieeeAddr,
                networkAddress,
                profileId: ZDO_PROFILE_ID,
                clusterKey: ZDO_LEAVE_REQUEST,
                zdoParams: [remote.ieeeAddr, ZDO_LEAVE_AND_REJOIN],
                disableResponse: true,
            });
            leaveSent = true;
            this.logger.warning(`CR11 soft rejoin requested for ${remote.ieeeAddr}`);
            await this.publishSoftRejoinResponse({
                status: "sent",
                id: request.id,
                network_address: networkAddress,
                join_window_seconds: SOFT_REJOIN_JOIN_WINDOW_SECONDS,
            });
        } catch (error) {
            const detail = safeErrorMessage(error);
            if (joinWindowMayBeOpen && !leaveSent) {
                try {
                    await this.zigbee.permitJoin(0);
                } catch (cleanupError) {
                    const cleanupDetail = safeErrorMessage(cleanupError);
                    this.logger.warning(`Unable to close permit-join after CR11 soft rejoin failure: ${cleanupDetail}`);
                }
            }
            this.logger.warning(`CR11 soft rejoin failed for ${remote.ieeeAddr}: ${detail}`);
            await this.publishSoftRejoinResponse({status: "error", id: request.id, error: detail});
        } finally {
            this.softRejoinRunning = false;
        }
    }

    isGateway(entity) {
        return entity?.isDevice?.() === true && entity.definition?.model === GATEWAY_MODEL;
    }

    adjustMessageBeforePublish(entity, message) {
        if (!this.isGateway(entity) || entity.options?.[CR11_GATEWAY_EVENT_OPTION] === true) return;
        if (!message || typeof message !== "object" || Array.isArray(message)) return;
        delete message.cr11_gateway_event;
    }

    gatewayDevices() {
        return [...this.zigbee.devicesIterator()].filter((entity) => this.isGateway(entity));
    }

    reportHeartbeatProblem(code, message) {
        if (this.heartbeatProblem === code) return;
        this.heartbeatProblem = code;
        this.logger.warning(message);
    }

    clearHeartbeatProblem(gateway) {
        if (this.heartbeatProblem !== undefined) {
            this.logger.info(`CR11Gateway heartbeat recovered for ${gateway.ieeeAddr}`);
        }
        this.heartbeatProblem = undefined;
    }

    async heartbeatTick() {
        if (this.stopping || this.heartbeatRunning) return;
        this.heartbeatRunning = true;
        try {
            const gateways = this.gatewayDevices();
            if (gateways.length === 0) {
                this.reportHeartbeatProblem(
                    "missing",
                    "CR11Gateway heartbeat paused: no gateway is present",
                );
                return;
            }
            if (gateways.length !== 1) {
                this.reportHeartbeatProblem(
                    "multiple",
                    `CR11Gateway heartbeat paused: expected one gateway, found ${gateways.length}`,
                );
                return;
            }

            const gateway = gateways[0];
            const endpoint = gateway.endpoint(GATEWAY_ENDPOINT);
            if (!endpoint) {
                this.reportHeartbeatProblem(
                    `endpoint:${gateway.ieeeAddr}`,
                    `CR11Gateway heartbeat paused: ${gateway.ieeeAddr} has no endpoint ${GATEWAY_ENDPOINT}`,
                );
                return;
            }
            await endpoint.command(
                GATEWAY_CLUSTER,
                GATEWAY_HEARTBEAT_COMMAND,
                {},
                {disableDefaultResponse: true},
            );
            this.clearHeartbeatProblem(gateway);
        } catch (error) {
            const detail = safeErrorMessage(error);
            this.reportHeartbeatProblem(
                `send:${detail}`,
                `CR11Gateway heartbeat failed: ${detail}`,
            );
        } finally {
            this.heartbeatRunning = false;
        }
    }

    sourceDevice(event) {
        if (event.source.mode === "short") {
            return this.zigbee.deviceByNetworkAddress(Number.parseInt(event.source.address.slice(2), 16));
        }
        return this.zigbee.resolveEntity(event.source.address);
    }

    isCR11(entity) {
        return entity?.isDevice?.() === true && CR11_MODEL_IDS.has(entity.zh?.modelID);
    }

    isDuplicate(gateway, event) {
        const now = Date.now();
        for (const [key, timestamp] of this.seen) {
            if (now - timestamp > DEDUPE_WINDOW_MS) this.seen.delete(key);
        }
        const key = `${gateway.ieeeAddr}:${event.source.mode}:${event.source.address}:${event.seq}`;
        if (this.seen.has(key)) return true;
        this.seen.set(key, now);
        while (this.seen.size > DEDUPE_LIMIT) {
            this.seen.delete(this.seen.keys().next().value);
        }
        return false;
    }

    async onPublishEntityState(data) {
        if (!this.isGateway(data?.entity) || data.stateChangeReason !== undefined) return;
        const encoded = data.payload?.cr11_gateway_event;
        if (typeof encoded !== "string" || encoded.length > 4096) return;

        let parsed;
        try {
            parsed = JSON.parse(encoded);
        } catch {
            this.logger.warning("CR11Gateway router ignored invalid event JSON");
            return;
        }
        const event = validateGatewayEvent(parsed);
        if (!event || this.isDuplicate(data.entity, event)) return;

        const source = this.sourceDevice(event);
        if (!this.isCR11(source)) {
            this.logger.warning(
                `CR11Gateway router could not match ${event.source.mode} source ${event.source.address} to a CR11S8UZ`,
            );
            return;
        }

        const mode = source.options?.cr11_gateway_action_mode === "selected" ? "selected" : "plain";
        const action = formatRoutedAction(event, mode);
        if (!action) return;
        await this.publishEntityState(source, {action});
    }
}
