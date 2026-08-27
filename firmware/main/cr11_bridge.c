/*
 * SPDX-License-Identifier: MIT
 */

#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_app_desc.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_log_buffer.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

#include "esp_zigbee.h"
#include "ezbee/platform/radio.h"
#include "ezbee/zcl/cluster/basic.h"
#include "ezbee/zcl/cluster/custom.h"
#include "ezbee/zcl/cluster/window_covering.h"
#include "ezbee/zcl/zcl_core.h"
#include "ezbee/zha.h"

#include "alarm_timer.h"
#include "cr11_debug.h"
#include "cr11_event.h"
#include "cr11_gateway_packet.h"
#include "cr11_ui_runtime.h"
#include "cr11_usb_transport.h"

#ifndef CONFIG_ZB_ZED
#error "CR11 Gateway must be built with the Zigbee End Device library"
#endif

#define CR11_APP_ENDPOINT_COUNT           6U
#define CR11_INTERNAL_ENDPOINT_COUNT      1U
#define CR11_ENDPOINT_CAPACITY \
    (CR11_APP_ENDPOINT_COUNT + CR11_INTERNAL_ENDPOINT_COUNT)
#define CR11_ALL_CHANNELS_MASK \
    ((uint32_t)EZB_RADIO_2P4GHZ_ALL_CHANNEL_MASK)
#define CR11_PRIMARY_CHANNEL_MASK \
    ((uint32_t)CONFIG_CR11_BRIDGE_PRIMARY_CHANNEL_MASK)
#define CR11_SECONDARY_CHANNEL_MASK \
    (CR11_ALL_CHANNELS_MASK & ~CR11_PRIMARY_CHANNEL_MASK)
#define CR11_ED_KEEP_ALIVE_MS 30000U
#define CR11_PAYLOAD_DUMP_MAX 32U
#define CR11_MOTION_SLOTS    32U
#define CR11_STORAGE_PARTITION "zb_storage"

_Static_assert(CR11_PRIMARY_CHANNEL_MASK != 0U,
               "At least one primary commissioning channel is required");
_Static_assert((CR11_PRIMARY_CHANNEL_MASK & ~CR11_ALL_CHANNELS_MASK) == 0U,
               "Commissioning mask may contain only Zigbee channels 11-26");

static const char *TAG = "CR11_BRIDGE";
static const char MANUFACTURER_NAME[] = "\x08" "Skydev0h";
static const char MODEL_IDENTIFIER[] = "\x0b" "CR11-Bridge";

typedef struct {
    bool used;
    ezb_addr_mode_t address_mode;
    uint64_t address;
    uint8_t destination_endpoint;
    cr11_direction_t direction;
} motion_slot_t;

static motion_slot_t motion_slots[CR11_MOTION_SLOTS];
static size_t next_motion_slot;
static uint32_t gateway_event_sequence;

static void schedule_commissioning_retry(ezb_bdb_comm_mode_mask_t mode);

static void zigbee_start_steering(void *context)
{
    (void)context;
    const ezb_err_t error = ezb_bdb_start_top_level_commissioning(
        EZB_BDB_MODE_NETWORK_STEERING);
    if (error != EZB_ERR_NONE) {
        ESP_LOGE(TAG, "Unable to start network steering: %d", error);
        schedule_commissioning_retry(EZB_BDB_MODE_NETWORK_STEERING);
    }
}

static esp_err_t ui_start_steering(void *context)
{
    (void)context;
    return esp_zigbee_task_queue_post(zigbee_start_steering, NULL);
}

static void zigbee_begin_local_reset(void *context)
{
    (void)context;
    ESP_LOGW(TAG, "Starting BDB reset via local action");
    ezb_bdb_reset_via_local_action();
}

static esp_err_t ui_begin_local_reset(void *context)
{
    (void)context;
    return esp_zigbee_task_queue_post(zigbee_begin_local_reset, NULL);
}

static bool is_cr11_endpoint(uint8_t endpoint)
{
    return endpoint == CR11_LEFT_O_ENDPOINT ||
           endpoint == CR11_LEFT_DOUBLE_O_ENDPOINT ||
           endpoint == CR11_RIGHT_SQUARE_ENDPOINT ||
           endpoint == CR11_RIGHT_DOUBLE_SQUARE_ENDPOINT ||
           endpoint == CR11_WINDOW_ENDPOINT;
}

static bool is_right_column_endpoint(uint8_t endpoint)
{
    return endpoint == CR11_RIGHT_SQUARE_ENDPOINT ||
           endpoint == CR11_RIGHT_DOUBLE_SQUARE_ENDPOINT ||
           endpoint == CR11_WINDOW_ENDPOINT;
}

static bool is_secondary_target_endpoint(uint8_t endpoint)
{
    return endpoint == CR11_LEFT_DOUBLE_O_ENDPOINT ||
           endpoint == CR11_RIGHT_DOUBLE_SQUARE_ENDPOINT;
}

static const char *target_name(uint8_t endpoint)
{
    switch (endpoint) {
    case CR11_LEFT_O_ENDPOINT:
        return "left.O";
    case CR11_LEFT_DOUBLE_O_ENDPOINT:
        return "left.OO";
    case CR11_RIGHT_SQUARE_ENDPOINT:
        return "right.square";
    case CR11_RIGHT_DOUBLE_SQUARE_ENDPOINT:
        return "right.double-square";
    case CR11_WINDOW_ENDPOINT:
        return "right.window-diagnostic";
    default:
        return "unknown";
    }
}

static uint64_t address_value(const ezb_address_t *address)
{
    if (address == NULL) {
        return 0U;
    }

    switch (address->addr_mode) {
    case EZB_ADDR_MODE_SHORT:
        return address->u.short_addr;
    case EZB_ADDR_MODE_EXT:
        return address->u.extended_addr.u64;
    case EZB_ADDR_MODE_GROUP:
        return ((uint64_t)address->u.group_addr.bcast << 16U) |
               address->u.group_addr.group;
    case EZB_ADDR_MODE_NONE:
    default:
        return 0U;
    }
}

static void format_address(const ezb_address_t *address,
                           char *output,
                           size_t output_size)
{
    if (output == NULL || output_size == 0U) {
        return;
    }

    if (address == NULL) {
        (void)snprintf(output, output_size, "invalid");
        return;
    }

    switch (address->addr_mode) {
    case EZB_ADDR_MODE_SHORT:
        (void)snprintf(output, output_size, "short:0x%04" PRIx16,
                       address->u.short_addr);
        break;
    case EZB_ADDR_MODE_EXT:
        (void)snprintf(output, output_size, "ieee:0x%016" PRIx64,
                       address->u.extended_addr.u64);
        break;
    case EZB_ADDR_MODE_GROUP:
        (void)snprintf(output, output_size, "group:0x%04" PRIx16,
                       address->u.group_addr.group);
        break;
    case EZB_ADDR_MODE_NONE:
    default:
        (void)snprintf(output, output_size, "mode:%u", address->addr_mode);
        break;
    }
}

static motion_slot_t *find_motion_slot(const ezb_address_t *address,
                                       uint8_t destination_endpoint,
                                       bool create)
{
    if (address == NULL) {
        return NULL;
    }

    const uint64_t value = address_value(address);
    motion_slot_t *unused = NULL;

    for (size_t index = 0; index < CR11_MOTION_SLOTS; ++index) {
        motion_slot_t *slot = &motion_slots[index];
        if (slot->used && slot->address_mode == address->addr_mode &&
            slot->address == value &&
            slot->destination_endpoint == destination_endpoint) {
            return slot;
        }
        if (!slot->used && unused == NULL) {
            unused = slot;
        }
    }

    if (!create) {
        return NULL;
    }

    motion_slot_t *slot = unused;
    if (slot == NULL) {
        slot = &motion_slots[next_motion_slot];
        next_motion_slot = (next_motion_slot + 1U) % CR11_MOTION_SLOTS;
    }

    *slot = (motion_slot_t){
        .used = true,
        .address_mode = address->addr_mode,
        .address = value,
        .destination_endpoint = destination_endpoint,
        .direction = CR11_DIRECTION_NONE,
    };
    return slot;
}

static void remember_motion(const ezb_address_t *address,
                            uint8_t destination_endpoint,
                            cr11_direction_t direction)
{
    motion_slot_t *slot =
        find_motion_slot(address, destination_endpoint, true);
    if (slot != NULL) {
        slot->direction = direction;
    }
}

static cr11_direction_t take_previous_motion(const ezb_address_t *address,
                                             uint8_t destination_endpoint)
{
    motion_slot_t *slot =
        find_motion_slot(address, destination_endpoint, false);
    if (slot == NULL) {
        return CR11_DIRECTION_NONE;
    }
    const cr11_direction_t direction = slot->direction;
    slot->direction = CR11_DIRECTION_NONE;
    return direction;
}

static void log_network(const char *status)
{
    const uint8_t channel = ezb_nwk_get_current_channel();
    ESP_LOGI(TAG, "%s channel=%u", status, channel);

    if (!cr11_debug_is_enabled()) {
        return;
    }

    ezb_extaddr_t ieee_address = {0};
    ezb_extpanid_t extended_pan_id = {0};

    ezb_nwk_get_extended_address(&ieee_address);
    ezb_nwk_get_extended_panid(&extended_pan_id);

    ESP_LOGI(TAG,
             "%s identifiers ieee=0x%016" PRIx64
             " ext_pan=0x%016" PRIx64 " pan=0x%04" PRIx16
             " channel=%u short=0x%04" PRIx16,
             status,
             ieee_address.u64,
             extended_pan_id.u64,
             ezb_nwk_get_panid(),
             channel,
             ezb_nwk_get_short_address());
}

static void commissioning_retry(alarm_timer_arg_t argument)
{
    const ezb_bdb_comm_mode_mask_t mode =
        (ezb_bdb_comm_mode_mask_t)argument;

    esp_zigbee_lock_acquire(portMAX_DELAY);
    const ezb_err_t error = ezb_bdb_start_top_level_commissioning(mode);
    esp_zigbee_lock_release();

    if (error != EZB_ERR_NONE) {
        ESP_LOGE(TAG, "Commissioning retry failed mode=0x%02x error=%d",
                 mode, error);
    }
}

static void schedule_commissioning_retry(ezb_bdb_comm_mode_mask_t mode)
{
    const esp_err_t error = alarm_timer_schedule(
        commissioning_retry,
        (alarm_timer_arg_t)mode,
        CONFIG_CR11_BRIDGE_RETRY_MS);

    if (error != ESP_OK) {
        ESP_LOGE(TAG, "Unable to schedule commissioning retry: %s",
                 esp_err_to_name(error));
    }
}

static bool read_signal_status(const ezb_app_signal_t *signal,
                               ezb_bdb_comm_status_t *status)
{
    if (signal == NULL || status == NULL) {
        return false;
    }

    const void *parameters = ezb_app_signal_get_params(signal);
    if (parameters == NULL) {
        return false;
    }

    *status = *(const ezb_bdb_comm_status_t *)parameters;
    return true;
}

static bool zigbee_signal_handler(const ezb_app_signal_t *signal)
{
    if (signal == NULL) {
        ESP_LOGE(TAG, "Received null Zigbee signal");
        return false;
    }

    const ezb_app_signal_type_t type = ezb_app_signal_get_type(signal);
    ezb_bdb_comm_status_t status = EZB_BDB_STATUS_IN_PROGRESS;

    switch (type) {
    case EZB_ZDO_SIGNAL_SKIP_STARTUP:
        ESP_LOGI(TAG, "Initializing Zigbee stack");
        (void)ezb_bdb_start_top_level_commissioning(
            EZB_BDB_MODE_INITIALIZATION);
        break;

    case EZB_BDB_SIGNAL_DEVICE_FIRST_START:
    case EZB_BDB_SIGNAL_DEVICE_REBOOT:
        if (!read_signal_status(signal, &status)) {
            ESP_LOGE(TAG, "Missing BDB status for signal 0x%x", type);
            break;
        }
        if (status == EZB_BDB_STATUS_SUCCESS) {
            const bool factory_new = ezb_bdb_is_factory_new();
            ESP_LOGI(TAG, "Stack started factory_new=%s",
                     factory_new ? "yes" : "no");
            cr11_ui_runtime_stack_started(factory_new);
            if (!factory_new) {
                log_network("Restored network");
            }
        } else {
            ESP_LOGW(TAG, "Stack initialization failed status=0x%02x", status);
            cr11_ui_runtime_stack_retrying();
            schedule_commissioning_retry(EZB_BDB_MODE_INITIALIZATION);
        }
        break;

    case EZB_BDB_SIGNAL_STEERING:
        if (!read_signal_status(signal, &status)) {
            ESP_LOGE(TAG, "Missing steering status");
            break;
        }
        if (status == EZB_BDB_STATUS_SUCCESS) {
            log_network("Joined network");
            cr11_ui_runtime_joined();
        } else {
            ESP_LOGW(TAG,
                     "Network steering failed status=0x%02x; retry in %d ms",
                     status, CONFIG_CR11_BRIDGE_RETRY_MS);
            schedule_commissioning_retry(EZB_BDB_MODE_NETWORK_STEERING);
        }
        break;

    case EZB_ZDO_SIGNAL_LEAVE: {
        const ezb_zdo_signal_leave_params_t *leave =
            (const ezb_zdo_signal_leave_params_t *)
                ezb_app_signal_get_params(signal);
        const bool will_rejoin =
            leave != NULL &&
            leave->leave_type == EZB_ZDO_LEAVE_TYPE_REJOIN;
        ESP_LOGW(TAG, "Left Zigbee network rejoin=%s",
                 will_rejoin ? "yes" : "no");
        cr11_ui_runtime_network_left(will_rejoin);
        break;
    }

    default:
        ESP_LOGI(TAG, "Zigbee signal %s (0x%x)",
                 ezb_app_signal_to_string(type), type);
        break;
    }

    return false;
}

static cr11_direction_t track_motion(const ezb_zcl_cmd_hdr_t *header,
                                     const cr11_event_t *event)
{
    if (event->kind == CR11_EVENT_LEVEL_MOVE) {
        remember_motion(&header->src_addr, header->dst_ep, event->direction);
    } else if (event->kind == CR11_EVENT_WINDOW_OPEN) {
        remember_motion(&header->src_addr, header->dst_ep,
                        CR11_DIRECTION_UP);
    } else if (event->kind == CR11_EVENT_WINDOW_CLOSE) {
        remember_motion(&header->src_addr, header->dst_ep,
                        CR11_DIRECTION_DOWN);
    } else if (event->kind == CR11_EVENT_LEVEL_STOP ||
               event->kind == CR11_EVENT_WINDOW_STOP) {
        return take_previous_motion(&header->src_addr, header->dst_ep);
    }
    return CR11_DIRECTION_NONE;
}

static void log_decoded_event(const ezb_zcl_raw_frame_t *frame,
                              const cr11_event_t *event,
                              cr11_direction_t prior_direction)
{
    const ezb_zcl_cmd_hdr_t *header = frame->header;
    const bool right_column = is_right_column_endpoint(header->dst_ep);
    const bool secondary_target =
        is_secondary_target_endpoint(header->dst_ep);

    char source[32];
    format_address(&header->src_addr, source, sizeof(source));

    if (event->has_value) {
        ESP_LOGI(TAG,
                 "CR11_EVENT src=%s src_ep=%u dst_ep=%u cluster=0x%04" PRIx16
                 " cmd=0x%02x tsn=%u rssi=%d target=%s event=%s button=%s"
                 " direction=%s value=%" PRIu16 " with_on_off=%s",
                 source,
                 header->src_ep,
                 header->dst_ep,
                 header->cluster_id,
                 header->cmd_id,
                 header->tsn,
                 header->rssi,
                 target_name(header->dst_ep),
                 cr11_event_name(event),
                 cr11_event_button_guess(event, right_column, secondary_target),
                 cr11_direction_name(event->direction),
                 event->value,
                 event->with_on_off ? "yes" : "no");
    } else {
        ESP_LOGI(TAG,
                 "CR11_EVENT src=%s src_ep=%u dst_ep=%u cluster=0x%04" PRIx16
                 " cmd=0x%02x tsn=%u rssi=%d target=%s event=%s button=%s"
                 " direction=%s previous_direction=%s with_on_off=%s",
                 source,
                 header->src_ep,
                 header->dst_ep,
                 header->cluster_id,
                 header->cmd_id,
                 header->tsn,
                 header->rssi,
                 target_name(header->dst_ep),
                 cr11_event_name(event),
                 cr11_event_button_guess(event, right_column, secondary_target),
                 cr11_direction_name(event->direction),
                 cr11_direction_name(prior_direction),
                 event->with_on_off ? "yes" : "no");
    }
}

static cr11_gateway_source_mode_t gateway_source_mode(
    const ezb_address_t *address)
{
    if (address == NULL) {
        return CR11_GATEWAY_SOURCE_UNKNOWN;
    }
    switch (address->addr_mode) {
    case EZB_ADDR_MODE_SHORT:
        return CR11_GATEWAY_SOURCE_SHORT;
    case EZB_ADDR_MODE_EXT:
        return CR11_GATEWAY_SOURCE_EXTENDED;
    case EZB_ADDR_MODE_GROUP:
        return CR11_GATEWAY_SOURCE_GROUP;
    case EZB_ADDR_MODE_NONE:
    default:
        return CR11_GATEWAY_SOURCE_UNKNOWN;
    }
}

static void gateway_send_confirmation(ezb_af_user_cnf_t *confirmation,
                                      void *user_context)
{
    const uint32_t sequence = (uint32_t)(uintptr_t)user_context;
    if (confirmation == NULL) {
        ESP_LOGE(TAG, "CR11_FORWARD seq=%" PRIu32 " confirmation=null",
                 sequence);
        return;
    }
    if (confirmation->status != 0U) {
        ESP_LOGW(TAG,
                 "CR11_FORWARD seq=%" PRIu32
                 " status=0x%02x aps_tsn=%u",
                 sequence,
                 confirmation->status,
                 confirmation->tsn);
    } else if (cr11_debug_is_enabled()) {
        ESP_LOGI(TAG,
                 "CR11_FORWARD seq=%" PRIu32
                 " status=0x%02x aps_tsn=%u",
                 sequence,
                 confirmation->status,
                 confirmation->tsn);
    }
}

static void emit_gateway_packet(const ezb_zcl_raw_frame_t *frame,
                                const cr11_event_t *event,
                                cr11_direction_t prior_direction)
{
    const ezb_zcl_cmd_hdr_t *header = frame->header;
    ++gateway_event_sequence;
    if (gateway_event_sequence == 0U) {
        ++gateway_event_sequence;
    }

    cr11_gateway_packet_t packet;
    if (!cr11_gateway_packet_build(
            &packet,
            event,
            prior_direction,
            gateway_source_mode(&header->src_addr),
            address_value(&header->src_addr),
            header->src_ep,
            header->dst_ep,
            header->tsn,
            header->profile_id,
            header->cluster_id,
            header->cmd_id,
            header->rssi,
            gateway_event_sequence)) {
        ESP_LOGE(TAG, "Unable to build CR11 gateway packet");
        return;
    }

    const bool debug_enabled = cr11_debug_is_enabled();
    if (debug_enabled) {
        char json[512];
        if (cr11_gateway_packet_format_json(&packet, json, sizeof(json))) {
            ESP_LOGI(TAG, "CR11_PACKET %s", json);
        } else {
            ESP_LOGE(TAG, "Unable to format CR11 packet seq=%" PRIu32,
                     packet.sequence);
        }
    }

    if (!cr11_gateway_packet_is_forwardable(&packet)) {
        if (debug_enabled) {
            ESP_LOGW(TAG,
                     "CR11_FORWARD seq=%" PRIu32
                     " status=not_forwardable",
                     packet.sequence);
        }
        return;
    }

    cr11_ui_runtime_gateway_event(packet.button);

    uint8_t wire[CR11_GATEWAY_PACKET_SIZE];
    if (!cr11_gateway_packet_encode(&packet, wire)) {
        ESP_LOGE(TAG, "CR11_FORWARD seq=%" PRIu32 " status=encode_error",
                 packet.sequence);
        return;
    }

    const esp_err_t usb_error =
        cr11_usb_transport_submit(wire, sizeof(wire));
    if (usb_error != ESP_OK && debug_enabled) {
        ESP_LOGW(TAG,
                 "CR11_USB seq=%" PRIu32 " status=drop error=%s",
                 packet.sequence,
                 esp_err_to_name(usb_error));
    }

    const ezb_zcl_custom_cluster_cmd_t request = {
        .cmd_ctrl = {
            .dst_addr = EZB_ADDRESS_SHORT(0x0000U),
            .dst_ep = CR11_GATEWAY_COORDINATOR_ENDPOINT,
            .src_ep = CR11_GATEWAY_ENDPOINT,
            .cluster_id = CR11_GATEWAY_CLUSTER_ID,
            .manuf_code = EZB_ZCL_STD_MANUF_CODE,
            .fc = {
                .manuf_specific = 0U,
                .direction = EZB_ZCL_CMD_DIRECTION_TO_CLI,
                .dis_default_rsp = 1U,
            },
            .cnf_ctx = {
                .cb = gateway_send_confirmation,
                .user_ctx = (void *)(uintptr_t)packet.sequence,
            },
        },
        .cmd_id = CR11_GATEWAY_EVENT_COMMAND_ID,
        .data_length = sizeof(wire),
        .data = wire,
    };
    const ezb_err_t error = ezb_zcl_custom_cluster_cmd_req(&request);
    if (error == EZB_ERR_NONE) {
        if (debug_enabled) {
            ESP_LOGI(TAG,
                     "CR11_FORWARD seq=%" PRIu32 " status=queued",
                     packet.sequence);
        }
    } else {
        ESP_LOGE(TAG,
                 "CR11_FORWARD seq=%" PRIu32 " status=queue_error error=%d",
                 packet.sequence,
                 error);
    }
}

static bool raw_zcl_handler(const ezb_zcl_raw_frame_t *frame)
{
    if (frame == NULL || frame->header == NULL) {
        ESP_LOGE(TAG, "Invalid raw ZCL frame");
        return false;
    }

    const ezb_zcl_cmd_hdr_t *header = frame->header;
    if (frame->payload_length > 0U && frame->payload == NULL) {
        ESP_LOGE(TAG, "Raw ZCL frame has a null payload");
        return false;
    }

    const bool debug_enabled = cr11_debug_is_enabled();
    if (debug_enabled) {
        char source[32];
        format_address(&header->src_addr, source, sizeof(source));

        ESP_LOGI(TAG,
                 "ZCL_RAW src=%s src_ep=%u dst_ep=%u"
                 " profile=0x%04" PRIx16 " cluster=0x%04" PRIx16
                 " fc=0x%02x cmd=0x%02x tsn=%u rssi=%d"
                 " payload_len=%" PRIu16,
                 source,
                 header->src_ep,
                 header->dst_ep,
                 header->profile_id,
                 header->cluster_id,
                 header->fc,
                 header->cmd_id,
                 header->tsn,
                 header->rssi,
                 frame->payload_length);

        if (frame->payload_length > 0U) {
            const uint16_t dump_length =
                frame->payload_length > CR11_PAYLOAD_DUMP_MAX
                    ? CR11_PAYLOAD_DUMP_MAX
                    : frame->payload_length;
            ESP_LOG_BUFFER_HEX_LEVEL(TAG, frame->payload, dump_length,
                                     ESP_LOG_INFO);
            if (dump_length != frame->payload_length) {
                ESP_LOGW(TAG, "Payload dump truncated to %u bytes",
                         dump_length);
            }
        }
    }

    const bool is_target_endpoint = is_cr11_endpoint(header->dst_ep);
    const bool is_cluster_command =
        EZB_ZCL_CMD_FC_GET_FRAME_TYPE(header->fc) ==
        EZB_ZCL_FRAME_TYPE_CLUSTER_SPECIFIC;
    const bool is_to_server =
        !EZB_ZCL_CMD_FC_IS_TO_CLI_DIRECTION(header->fc);
    const bool is_standard =
        !EZB_ZCL_CMD_FC_IS_MANUF_SPEC(header->fc);

    if (!is_target_endpoint || !is_cluster_command ||
        !is_to_server || !is_standard) {
        return false;
    }

    const cr11_event_t event = cr11_event_decode(
        header->cluster_id,
        header->cmd_id,
        frame->payload,
        frame->payload_length);

    if (event.kind == CR11_EVENT_MALFORMED) {
        if (debug_enabled) {
            ESP_LOGW(TAG,
                     "Rejected malformed CR11 candidate"
                     " cluster=0x%04" PRIx16
                     " cmd=0x%02x len=%" PRIu16,
                     header->cluster_id,
                     header->cmd_id,
                     frame->payload_length);
        }
    } else if (event.kind != CR11_EVENT_UNKNOWN) {
        const cr11_direction_t prior_direction =
            track_motion(header, &event);
        if (debug_enabled) {
            log_decoded_event(frame, &event, prior_direction);
        }
        emit_gateway_packet(frame, &event, prior_direction);
    }

    /* Let the Zigbee stack perform normal standard-cluster processing. */
    return false;
}

static void core_action_handler(ezb_zcl_core_action_callback_id_t callback_id,
                                void *message)
{
    if (message == NULL) {
        ESP_LOGE(TAG, "Null ZCL core message id=0x%" PRIx32, callback_id);
        return;
    }

    if (callback_id == EZB_ZCL_CORE_SET_ATTR_VALUE_CB_ID) {
        if (cr11_debug_is_enabled()) {
            const ezb_zcl_set_attr_value_message_t *attribute_message =
                (const ezb_zcl_set_attr_value_message_t *)message;
            const ezb_zcl_attribute_t *attribute =
                &attribute_message->in.attribute;

            ESP_LOGI(TAG,
                     "ATTR_SET ep=%u cluster=0x%04" PRIx16
                     " attr=0x%04" PRIx16 " type=0x%02x size=%" PRIu16
                     " status=0x%02x",
                     attribute_message->info.dst_ep,
                     attribute_message->info.cluster_id,
                     attribute->id,
                     attribute->data.type,
                     attribute->data.size,
                     attribute_message->info.status);
        }
        return;
    }

    if (callback_id ==
        EZB_ZCL_CORE_WINDOW_COVERING_MOVEMENT_CB_ID) {
        ezb_zcl_window_covering_movement_message_t *movement =
            (ezb_zcl_window_covering_movement_message_t *)message;
        movement->out.result = EZB_ZCL_STATUS_SUCCESS;
        if (cr11_debug_is_enabled()) {
            ESP_LOGI(TAG,
                     "Accepted Window Covering movement on endpoint %u",
                     movement->info.dst_ep);
        }
    }
}

static esp_err_t add_basic_identity(ezb_af_ep_desc_t endpoint)
{
    if (endpoint == EZB_INVALID_AF_EP_DESC) {
        return ESP_ERR_NO_MEM;
    }

    const ezb_zcl_cluster_desc_t basic =
        ezb_af_endpoint_get_cluster_desc(
            endpoint,
            EZB_ZCL_CLUSTER_ID_BASIC,
            EZB_ZCL_CLUSTER_SERVER);
    if (basic == EZB_INVALID_ZCL_CLUSTER_DESC) {
        return ESP_ERR_NOT_FOUND;
    }

    ESP_ERROR_CHECK(ezb_zcl_basic_cluster_desc_add_attr(
        basic,
        EZB_ZCL_ATTR_BASIC_MANUFACTURER_NAME_ID,
        MANUFACTURER_NAME));
    ESP_ERROR_CHECK(ezb_zcl_basic_cluster_desc_add_attr(
        basic,
        EZB_ZCL_ATTR_BASIC_MODEL_IDENTIFIER_ID,
        MODEL_IDENTIFIER));
    return ESP_OK;
}

static uint8_t gateway_command_discovery(bool is_received, uint8_t **list)
{
    static uint8_t received_commands[] = {
        CR11_GATEWAY_READY_COMMAND_ID,
        CR11_GATEWAY_HEARTBEAT_COMMAND_ID,
    };
    static uint8_t sent_commands[] = {CR11_GATEWAY_EVENT_COMMAND_ID};
    if (list == NULL) {
        return 0U;
    }
    *list = is_received ? received_commands : sent_commands;
    return is_received ? (uint8_t)(sizeof(received_commands) /
                                   sizeof(received_commands[0])) : 1U;
}

static ezb_zcl_status_t gateway_process_command(
    const ezb_zcl_cmd_hdr_t *header,
    const uint8_t *payload,
    uint16_t payload_length)
{
    if (header == NULL || header->cluster_id != CR11_GATEWAY_CLUSTER_ID) {
        return EZB_ZCL_STATUS_UNSUPPORTED_CLUSTER;
    }
    if (EZB_ZCL_CMD_FC_IS_TO_CLI_DIRECTION(header->fc)) {
        return EZB_ZCL_STATUS_INVALID_FIELD;
    }
    if (header->cmd_id != CR11_GATEWAY_READY_COMMAND_ID &&
        header->cmd_id != CR11_GATEWAY_HEARTBEAT_COMMAND_ID) {
        return EZB_ZCL_STATUS_UNSUP_CMD;
    }
    (void)payload;
    if (payload_length != 0U) {
        return EZB_ZCL_STATUS_MALFORMED_CMD;
    }
    if (header->src_addr.addr_mode != EZB_ADDR_MODE_SHORT ||
        header->src_addr.u.short_addr != 0x0000U) {
        ESP_LOGW(TAG, "Rejected gateway control command from non-coordinator");
        return EZB_ZCL_STATUS_NOT_AUTHORIZED;
    }

    if (header->cmd_id == CR11_GATEWAY_READY_COMMAND_ID) {
        ESP_LOGI(TAG, "Coordinator confirmed gateway interview readiness");
        cr11_ui_runtime_ready();
    } else {
        cr11_ui_runtime_lease_ping();
    }
    return EZB_ZCL_STATUS_SUCCESS;
}

static void gateway_cluster_init(uint8_t endpoint)
{
    (void)endpoint;
    const ezb_zcl_custom_cluster_handlers_t handlers = {
        .cluster_id = CR11_GATEWAY_CLUSTER_ID,
        .cluster_role = EZB_ZCL_CLUSTER_SERVER,
        .check_value_cb = NULL,
        .write_attr_cb = NULL,
        .cmd_disc_cb = gateway_command_discovery,
        .process_cmd_cb = gateway_process_command,
    };
    const ezb_err_t error =
        ezb_zcl_custom_cluster_handlers_register(&handlers);
    if (error != EZB_ERR_NONE) {
        ESP_LOGE(TAG, "Unable to register gateway cluster handlers: %d",
                 error);
    }
}

static void gateway_cluster_deinit(uint8_t endpoint)
{
    (void)endpoint;
}

static ezb_af_ep_desc_t create_gateway_endpoint(void)
{
    const ezb_af_ep_config_t endpoint_config = {
        .ep_id = CR11_GATEWAY_ENDPOINT,
        .app_profile_id = EZB_AF_HA_PROFILE_ID,
        .app_device_id = EZB_ZHA_SIMPLE_SENSOR_DEVICE_ID,
        .app_device_version = 1U,
    };
    const ezb_af_ep_desc_t endpoint =
        ezb_af_create_endpoint_desc(&endpoint_config);
    if (endpoint == EZB_INVALID_AF_EP_DESC) {
        return EZB_INVALID_AF_EP_DESC;
    }

    const ezb_zcl_basic_cluster_server_config_t basic_config = {
        .zcl_version = EZB_ZCL_BASIC_ZCL_VERSION_DEFAULT_VALUE,
        .power_source = EZB_ZCL_BASIC_POWER_SOURCE_DC_SOURCE,
    };
    const ezb_zcl_cluster_desc_t basic =
        ezb_zcl_basic_create_cluster_desc(&basic_config,
                                          EZB_ZCL_CLUSTER_SERVER);
    const ezb_zcl_custom_cluster_config_t gateway_config = {
        .cluster_id = CR11_GATEWAY_CLUSTER_ID,
        .init_func = gateway_cluster_init,
        .deinit_func = gateway_cluster_deinit,
    };
    const ezb_zcl_cluster_desc_t gateway =
        ezb_zcl_custom_create_cluster_desc(&gateway_config,
                                           EZB_ZCL_CLUSTER_SERVER);
    if (basic == EZB_INVALID_ZCL_CLUSTER_DESC ||
        gateway == EZB_INVALID_ZCL_CLUSTER_DESC) {
        return EZB_INVALID_AF_EP_DESC;
    }
    if (ezb_af_endpoint_add_cluster_desc(endpoint, basic) != EZB_ERR_NONE ||
        ezb_af_endpoint_add_cluster_desc(endpoint, gateway) != EZB_ERR_NONE) {
        return EZB_INVALID_AF_EP_DESC;
    }
    return endpoint;
}

static esp_err_t register_endpoints(void)
{
    const ezb_err_t endpoint_limit_error =
        ezb_af_dev_set_max_endpoint_num(CR11_ENDPOINT_CAPACITY);
    if (endpoint_limit_error != EZB_ERR_NONE) {
        ESP_LOGE(TAG, "Unable to set endpoint capacity to %u: error=%d",
                 CR11_ENDPOINT_CAPACITY, endpoint_limit_error);
        return ESP_FAIL;
    }

    const ezb_af_device_desc_t device = ezb_af_create_device_desc();
    if (device == EZB_INVALID_AF_DEVICE_DESC) {
        return ESP_ERR_NO_MEM;
    }

    const ezb_zha_dimmable_light_config_t light_config =
        EZB_ZHA_DIMMABLE_LIGHT_CONFIG();
    const ezb_zha_window_covering_config_t window_config =
        EZB_ZHA_WINDOW_COVERING_CONFIG();

    const ezb_af_ep_desc_t left_o = ezb_zha_create_dimmable_light(
        CR11_LEFT_O_ENDPOINT, &light_config);
    const ezb_af_ep_desc_t left_double_o = ezb_zha_create_dimmable_light(
        CR11_LEFT_DOUBLE_O_ENDPOINT, &light_config);
    const ezb_af_ep_desc_t right_square = ezb_zha_create_dimmable_light(
        CR11_RIGHT_SQUARE_ENDPOINT, &light_config);
    const ezb_af_ep_desc_t right_double_square = ezb_zha_create_dimmable_light(
        CR11_RIGHT_DOUBLE_SQUARE_ENDPOINT, &light_config);
    const ezb_af_ep_desc_t window = ezb_zha_create_window_covering(
        CR11_WINDOW_ENDPOINT, &window_config);
    const ezb_af_ep_desc_t gateway = create_gateway_endpoint();

    ESP_ERROR_CHECK(add_basic_identity(left_o));
    ESP_ERROR_CHECK(add_basic_identity(left_double_o));
    ESP_ERROR_CHECK(add_basic_identity(right_square));
    ESP_ERROR_CHECK(add_basic_identity(right_double_square));
    ESP_ERROR_CHECK(add_basic_identity(window));
    ESP_ERROR_CHECK(add_basic_identity(gateway));
    ESP_ERROR_CHECK(ezb_af_device_add_endpoint_desc(device, left_o));
    ESP_ERROR_CHECK(ezb_af_device_add_endpoint_desc(device, left_double_o));
    ESP_ERROR_CHECK(ezb_af_device_add_endpoint_desc(device, right_square));
    ESP_ERROR_CHECK(ezb_af_device_add_endpoint_desc(device, right_double_square));
    ESP_ERROR_CHECK(ezb_af_device_add_endpoint_desc(device, window));
    ESP_ERROR_CHECK(ezb_af_device_add_endpoint_desc(device, gateway));
    ESP_ERROR_CHECK(ezb_af_device_desc_register(device));

    ezb_zcl_core_action_handler_register(core_action_handler);
    ezb_zcl_raw_command_handler_register(raw_zcl_handler);

    ESP_LOGI(TAG,
             "Registered %u/%u endpoints: %u=left.O, %u=left.OO, "
             "%u=right.square, %u=right.double-square, %u=WindowDiagnostic, "
             "%u=GatewayEvent(0x%04x)",
             CR11_APP_ENDPOINT_COUNT,
             ezb_af_dev_get_max_endpoint_num(),
             CR11_LEFT_O_ENDPOINT,
             CR11_LEFT_DOUBLE_O_ENDPOINT,
             CR11_RIGHT_SQUARE_ENDPOINT,
             CR11_RIGHT_DOUBLE_SQUARE_ENDPOINT,
             CR11_WINDOW_ENDPOINT,
             CR11_GATEWAY_ENDPOINT,
             CR11_GATEWAY_CLUSTER_ID);
    return ESP_OK;
}

static void zigbee_task(void *argument)
{
    (void)argument;

    const esp_zigbee_config_t config = {
        .platform_config = {
            .storage_partition_name = CR11_STORAGE_PARTITION,
            .radio_config = {
                .radio_mode = ESP_ZIGBEE_RADIO_MODE_NATIVE,
            },
        },
        .device_config = {
            .device_type = EZB_NWK_DEVICE_TYPE_END_DEVICE,
            .install_code_policy = false,
            .zed_config = {
                .ed_timeout = EZB_NWK_ED_TIMEOUT_64MIN,
                .keep_alive = CR11_ED_KEEP_ALIVE_MS,
            },
        },
    };

    ESP_ERROR_CHECK(esp_zigbee_init(&config));
    ezb_nwk_set_rx_on_when_idle(true);
    ezb_aps_secur_enable_distributed_security(false);
    ESP_ERROR_CHECK(
        ezb_bdb_set_primary_channel_set(CR11_PRIMARY_CHANNEL_MASK));
    if (CR11_SECONDARY_CHANNEL_MASK != 0U) {
        ESP_ERROR_CHECK(
            ezb_bdb_set_secondary_channel_set(CR11_SECONDARY_CHANNEL_MASK));
    }
    ESP_ERROR_CHECK(ezb_app_signal_add_handler(zigbee_signal_handler));
    ESP_ERROR_CHECK(register_endpoints());
    ESP_ERROR_CHECK(esp_zigbee_start(false));

    esp_zigbee_launch_mainloop();
    esp_zigbee_deinit();
    vTaskDelete(NULL);
}

static void init_nvs(void)
{
    esp_err_t error = nvs_flash_init();
    if (error == ESP_ERR_NVS_NO_FREE_PAGES ||
        error == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        error = nvs_flash_init();
    }
    ESP_ERROR_CHECK(error);

    bool forced_wipe_applied = false;
    error = cr11_ui_runtime_apply_pending_wipe(
        CR11_STORAGE_PARTITION, &forced_wipe_applied);
    if (error != ESP_OK && forced_wipe_applied) {
        ESP_LOGE(TAG,
                 "Forced wipe succeeded, but marker cleanup failed: %s; "
                 "continuing startup",
                 esp_err_to_name(error));
        error = ESP_OK;
    }
    ESP_ERROR_CHECK(error);
    if (forced_wipe_applied) {
        ESP_LOGW(TAG, "Applied deferred forced wipe to %s",
                 CR11_STORAGE_PARTITION);
    }

    error = nvs_flash_init_partition(CR11_STORAGE_PARTITION);
    if (error == ESP_ERR_NVS_NO_FREE_PAGES ||
        error == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase_partition(CR11_STORAGE_PARTITION));
        error = nvs_flash_init_partition(CR11_STORAGE_PARTITION);
    }
    ESP_ERROR_CHECK(error);
}

void app_main(void)
{
    const esp_app_desc_t *app = esp_app_get_description();
    const esp_reset_reason_t reset_reason = esp_reset_reason();
    const bool watchdog_reset =
        reset_reason == ESP_RST_INT_WDT ||
        reset_reason == ESP_RST_TASK_WDT ||
        reset_reason == ESP_RST_WDT;
    init_nvs();
    ESP_LOGI(TAG,
             "Starting CR11 bridge version %s "
             "commissioning_primary=0x%08" PRIx32 " "
             "commissioning_secondary=0x%08" PRIx32 " "
             "role=always-on-zed reset_reason=%d watchdog_reset=%s",
             app->version,
             CR11_PRIMARY_CHANNEL_MASK,
             CR11_SECONDARY_CHANNEL_MASK,
             (int)reset_reason,
             watchdog_reset ? "yes" : "no");

    const cr11_ui_runtime_config_t ui_config = {
        .start_steering = ui_start_steering,
        .begin_local_reset = ui_begin_local_reset,
        .context = NULL,
        .watchdog_reset = watchdog_reset,
    };
    ESP_ERROR_CHECK(cr11_ui_runtime_init(&ui_config));

    const esp_err_t usb_error = cr11_usb_transport_init();
    if (usb_error != ESP_OK) {
        ESP_LOGE(TAG, "Native USB event transport unavailable: %s",
                 esp_err_to_name(usb_error));
    }

    const BaseType_t created = xTaskCreate(
        zigbee_task,
        "cr11_zigbee",
        6144,
        NULL,
        5,
        NULL);
    ESP_ERROR_CHECK(created == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
}
