/*
 * SPDX-License-Identifier: MIT
 */

#include "cr11_ui_runtime.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/uart.h"
#include "driver/uart_vfs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "led_strip.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

#include "esp_err.h"
#include "esp_log.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"

#include "cr11_debug.h"
#include "cr11_ui_state.h"

#define CR11_UI_QUEUE_LENGTH           32U
#define CR11_UI_TASK_STACK             4096U
#define CR11_UI_UART_TASK_STACK        3072U
#define CR11_UI_TASK_PERIOD_MS         10U
#define CR11_UI_BUTTON_DEBOUNCE_MS     30U
#define CR11_UI_UART_LINE_CAPACITY     64U
#define CR11_UI_UART_READ_CHUNK        32U
#define CR11_UI_NVS_NAMESPACE          "cr11_ui"
#define CR11_UI_NVS_FORCE_WIPE_KEY     "force_wipe"
#define CR11_UI_LEASE_WDT_USER         "cr11_z2m_lease"
#define CR11_UI_LEASE_WDT_RETRY_MS     1000U

#if !CONFIG_ESP_TASK_WDT_PANIC
#error "CR11 supervisor lease requires CONFIG_ESP_TASK_WDT_PANIC"
#endif

_Static_assert(CONFIG_ESP_TASK_WDT_TIMEOUT_S * 1000U ==
                   CR11_UI_LEASE_WDT_GUARD_MS,
               "Task WDT timeout must match CR11 lease guard");

static const char *TAG = "CR11_UI";

typedef enum {
    CR11_UI_MESSAGE_STACK_STARTED = 0,
    CR11_UI_MESSAGE_STACK_RETRYING,
    CR11_UI_MESSAGE_JOINED,
    CR11_UI_MESSAGE_READY,
    CR11_UI_MESSAGE_LEASE_PING,
    CR11_UI_MESSAGE_NETWORK_LEFT,
    CR11_UI_MESSAGE_LEAVE_SUCCEEDED,
    CR11_UI_MESSAGE_GATEWAY_EVENT,
    CR11_UI_MESSAGE_VIRTUAL_BOOT,
    CR11_UI_MESSAGE_STATUS,
} cr11_ui_message_kind_t;

typedef struct {
    cr11_ui_message_kind_t kind;
    uint64_t timestamp_ms;
    bool value;
} cr11_ui_message_t;

typedef struct {
    uint8_t red;
    uint8_t green;
    uint8_t blue;
} cr11_ui_rgb_t;

typedef struct {
    cr11_ui_runtime_config_t config;
    cr11_ui_state_t state;
    QueueHandle_t queue;
    led_strip_handle_t strip;
    esp_task_wdt_user_handle_t lease_watchdog;
    cr11_ui_phase_t emitted_phase;
    cr11_ui_led_t emitted_led;
    uint64_t physical_candidate_since_ms;
    uint64_t virtual_pressed_ms;
    uint64_t lease_watchdog_arm_retry_ms;
    uint32_t state_sequence;
    uint32_t led_sequence;
    bool physical_candidate;
    bool physical_down;
    bool virtual_down;
    bool effective_down;
    bool emitted_phase_valid;
    bool emitted_led_valid;
    bool emitted_lease_armed;
    bool emitted_boot_watchdog;
    bool lease_watchdog_cancel_error_logged;
} cr11_ui_runtime_t;

static cr11_ui_runtime_t runtime;

static uint64_t uptime_ms(void)
{
    return (uint64_t)esp_timer_get_time() / 1000U;
}

static esp_err_t post_message(cr11_ui_message_kind_t kind, bool value)
{
    if (runtime.queue == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    const cr11_ui_message_t message = {
        .kind = kind,
        .timestamp_ms = uptime_ms(),
        .value = value,
    };
    return xQueueSend(runtime.queue, &message, 0) == pdTRUE
               ? ESP_OK
               : ESP_ERR_TIMEOUT;
}

static uint8_t scale_component(uint8_t value)
{
    const uint8_t full = (uint8_t)CONFIG_CR11_UI_LED_BRIGHTNESS;
    return (uint8_t)(((uint16_t)value * full + 127U) / 255U);
}

static cr11_ui_rgb_t led_to_rgb(const cr11_ui_led_t *value)
{
    if (value == NULL) {
        return (cr11_ui_rgb_t){0U, 0U, 0U};
    }
    return (cr11_ui_rgb_t){
        scale_component(value->red),
        scale_component(value->green),
        scale_component(value->blue),
    };
}

static void emit_state(uint64_t now_ms)
{
    if (!cr11_debug_is_enabled()) {
        return;
    }
    if (runtime.emitted_phase_valid &&
        runtime.emitted_phase == runtime.state.phase &&
        runtime.emitted_lease_armed == runtime.state.lease_armed &&
        runtime.emitted_boot_watchdog == runtime.state.boot_was_watchdog) {
        return;
    }
    runtime.emitted_phase = runtime.state.phase;
    runtime.emitted_lease_armed = runtime.state.lease_armed;
    runtime.emitted_boot_watchdog = runtime.state.boot_was_watchdog;
    runtime.emitted_phase_valid = true;
    ++runtime.state_sequence;
    ESP_LOGI(TAG,
             "CR11_UI_STATE {\"seq\":%" PRIu32
             ",\"uptime_ms\":%" PRIu64
             ",\"phase\":\"%s\",\"boot\":{\"physical\":%s,"
             "\"virtual\":%s,\"effective\":%s},"
             "\"lease\":{\"armed\":%s,\"age_ms\":%" PRIu64
             ",\"watchdog_boot\":%s}}",
             runtime.state_sequence,
             now_ms,
             cr11_ui_phase_name(runtime.state.phase),
             runtime.physical_down ? "true" : "false",
             runtime.virtual_down ? "true" : "false",
             runtime.effective_down ? "true" : "false",
             runtime.state.lease_armed ? "true" : "false",
             cr11_ui_state_lease_age_ms(&runtime.state, now_ms),
             runtime.state.boot_was_watchdog ? "true" : "false");
}

static bool same_led(const cr11_ui_led_t *left,
                     const cr11_ui_led_t *right)
{
    return left->color == right->color && left->effect == right->effect &&
           left->red == right->red && left->green == right->green &&
           left->blue == right->blue;
}

static void apply_led(uint64_t now_ms)
{
    const cr11_ui_led_t next = cr11_ui_state_led(&runtime.state, now_ms);
    if (runtime.emitted_led_valid && same_led(&runtime.emitted_led, &next)) {
        return;
    }

    const cr11_ui_rgb_t rgb = led_to_rgb(&next);
    esp_err_t error = ESP_OK;
    if (next.color == CR11_UI_COLOR_OFF) {
        error = led_strip_clear(runtime.strip);
    } else {
        error = led_strip_set_pixel(runtime.strip, 0U,
                                    rgb.red, rgb.green, rgb.blue);
        if (error == ESP_OK) {
            error = led_strip_refresh(runtime.strip);
        }
    }
    if (error != ESP_OK) {
        ESP_LOGE(TAG, "Unable to update RGB LED: %s",
                 esp_err_to_name(error));
    }

    runtime.emitted_led = next;
    runtime.emitted_led_valid = true;
    ++runtime.led_sequence;
    if (cr11_debug_is_enabled()) {
        ESP_LOGI(TAG,
                 "CR11_LED {\"seq\":%" PRIu32
                 ",\"uptime_ms\":%" PRIu64
                 ",\"phase\":\"%s\",\"effect\":\"%s\","
                 "\"color\":\"%s\",\"rgb\":[%u,%u,%u]}",
                 runtime.led_sequence,
                 now_ms,
                 cr11_ui_phase_name(runtime.state.phase),
                 cr11_ui_effect_name(next.effect),
                 cr11_ui_color_name(next.color),
                 rgb.red,
                 rgb.green,
                 rgb.blue);
    }
}

static void sync_lease_watchdog(uint64_t now_ms)
{
    const bool due =
        cr11_ui_state_lease_wdt_due(&runtime.state, now_ms);
    if (!due) {
        runtime.lease_watchdog_arm_retry_ms = 0U;
        if (runtime.lease_watchdog != NULL) {
            const esp_err_t error =
                esp_task_wdt_delete_user(runtime.lease_watchdog);
            if (error == ESP_OK || error == ESP_ERR_NOT_FOUND) {
                runtime.lease_watchdog = NULL;
                runtime.lease_watchdog_cancel_error_logged = false;
                if (cr11_debug_is_enabled()) {
                    ESP_LOGI(TAG, "Z2M lease recovered before watchdog reset");
                }
            } else if (!runtime.lease_watchdog_cancel_error_logged) {
                runtime.lease_watchdog_cancel_error_logged = true;
                ESP_LOGE(TAG, "Unable to cancel Z2M lease watchdog: %s",
                         esp_err_to_name(error));
            }
        }
        return;
    }
    if (runtime.lease_watchdog != NULL) {
        return;
    }
    if (now_ms < runtime.lease_watchdog_arm_retry_ms) {
        return;
    }

    esp_task_wdt_user_handle_t handle = NULL;
    esp_err_t error =
        esp_task_wdt_add_user(CR11_UI_LEASE_WDT_USER, &handle);
    if (error != ESP_OK) {
        runtime.lease_watchdog_arm_retry_ms =
            now_ms + CR11_UI_LEASE_WDT_RETRY_MS;
        ESP_LOGE(TAG, "Unable to arm Z2M lease watchdog: %s",
                 esp_err_to_name(error));
        return;
    }

    runtime.lease_watchdog = handle;
    error = esp_task_wdt_reset_user(handle);
    if (error != ESP_OK) {
        /* Keep the registered user: fail closed and let the WDT recover us. */
        ESP_LOGE(TAG, "Unable to start Z2M lease watchdog: %s",
                 esp_err_to_name(error));
        return;
    }
    ESP_LOGW(TAG,
             "Z2M lease is 25.2 s stale; watchdog reset in about 5 s");
}

static esp_err_t arm_forced_wipe(void)
{
    nvs_handle_t handle = 0;
    esp_err_t error = nvs_open(CR11_UI_NVS_NAMESPACE,
                               NVS_READWRITE, &handle);
    if (error != ESP_OK) {
        return error;
    }
    error = nvs_set_u8(handle, CR11_UI_NVS_FORCE_WIPE_KEY, 1U);
    if (error == ESP_OK) {
        error = nvs_commit(handle);
    }
    nvs_close(handle);
    return error;
}

static void dispatch_actions(uint64_t now_ms)
{
    const uint32_t actions = cr11_ui_state_take_actions(&runtime.state);
    if ((actions & CR11_UI_ACTION_START_STEERING) != 0U) {
        const esp_err_t error = runtime.config.start_steering == NULL
                                    ? ESP_ERR_INVALID_STATE
                                    : runtime.config.start_steering(
                                          runtime.config.context);
        if (error != ESP_OK) {
            ESP_LOGE(TAG,
                     "CR11_UI_ACTION action=start_steering status=%s",
                     esp_err_to_name(error));
        } else if (cr11_debug_is_enabled()) {
            ESP_LOGI(TAG,
                     "CR11_UI_ACTION action=start_steering status=%s",
                     esp_err_to_name(error));
        }
    }
    if ((actions & CR11_UI_ACTION_BEGIN_LOCAL_RESET) != 0U) {
        const esp_err_t error = runtime.config.begin_local_reset == NULL
                                    ? ESP_ERR_INVALID_STATE
                                    : runtime.config.begin_local_reset(
                                          runtime.config.context);
        if (error != ESP_OK) {
            ESP_LOGE(TAG,
                     "CR11_UI_ACTION action=begin_local_reset status=%s",
                     esp_err_to_name(error));
        } else if (cr11_debug_is_enabled()) {
            ESP_LOGI(TAG,
                     "CR11_UI_ACTION action=begin_local_reset status=%s",
                     esp_err_to_name(error));
        }
    }
    if ((actions & CR11_UI_ACTION_ARM_FORCED_WIPE) != 0U) {
        const esp_err_t error = arm_forced_wipe();
        ESP_LOGW(TAG,
                 "CR11_UI_ACTION action=arm_forced_wipe status=%s",
                 esp_err_to_name(error));
        cr11_ui_state_forced_wipe_armed(&runtime.state,
                                        error == ESP_OK,
                                        now_ms);
    }
}

static void emit_status(uint64_t now_ms)
{
    const cr11_ui_led_t current =
        cr11_ui_state_led(&runtime.state, now_ms);
    const cr11_ui_rgb_t rgb = led_to_rgb(&current);
    ESP_LOGI(TAG,
             "CR11_UI_STATUS {\"uptime_ms\":%" PRIu64
             ",\"phase\":\"%s\",\"color\":\"%s\","
             "\"effect\":\"%s\",\"rgb\":[%u,%u,%u],"
             "\"boot\":{\"physical\":%s,\"virtual\":%s,"
             "\"effective\":%s},\"lease\":{\"armed\":%s,"
             "\"age_ms\":%" PRIu64 ",\"watchdog_pending\":%s,"
             "\"watchdog_boot\":%s}}",
             now_ms,
             cr11_ui_phase_name(runtime.state.phase),
             cr11_ui_color_name(current.color),
             cr11_ui_effect_name(current.effect),
             rgb.red,
             rgb.green,
             rgb.blue,
             runtime.physical_down ? "true" : "false",
             runtime.virtual_down ? "true" : "false",
             runtime.effective_down ? "true" : "false",
             runtime.state.lease_armed ? "true" : "false",
             cr11_ui_state_lease_age_ms(&runtime.state, now_ms),
             runtime.lease_watchdog != NULL ? "true" : "false",
             runtime.state.boot_was_watchdog ? "true" : "false");
}

static void handle_message(const cr11_ui_message_t *message,
                           uint64_t now_ms)
{
    if (message == NULL) {
        return;
    }
    switch (message->kind) {
    case CR11_UI_MESSAGE_STACK_STARTED:
        cr11_ui_state_stack_started(&runtime.state,
                                    message->value,
                                    runtime.config.watchdog_reset,
                                    now_ms);
        break;
    case CR11_UI_MESSAGE_STACK_RETRYING:
        cr11_ui_state_stack_retrying(&runtime.state,
                                     runtime.config.watchdog_reset,
                                     now_ms);
        break;
    case CR11_UI_MESSAGE_JOINED:
        cr11_ui_state_joined(&runtime.state, now_ms);
        break;
    case CR11_UI_MESSAGE_READY:
        cr11_ui_state_ready(&runtime.state, now_ms);
        break;
    case CR11_UI_MESSAGE_LEASE_PING:
        cr11_ui_state_lease_ping(&runtime.state, now_ms);
        break;
    case CR11_UI_MESSAGE_NETWORK_LEFT:
        cr11_ui_state_network_left(&runtime.state,
                                   message->value, now_ms);
        break;
    case CR11_UI_MESSAGE_LEAVE_SUCCEEDED:
        cr11_ui_state_leave_succeeded(&runtime.state, now_ms);
        break;
    case CR11_UI_MESSAGE_GATEWAY_EVENT:
        cr11_ui_state_gateway_event(&runtime.state,
                                    message->value, now_ms);
        break;
    case CR11_UI_MESSAGE_VIRTUAL_BOOT:
        runtime.virtual_down = message->value;
        if (runtime.virtual_down) {
            runtime.virtual_pressed_ms = now_ms;
        }
        break;
    case CR11_UI_MESSAGE_STATUS:
        emit_status(now_ms);
        break;
    default:
        break;
    }
}

static void update_physical_button(uint64_t now_ms)
{
    const bool sample = gpio_get_level(CONFIG_CR11_UI_BOOT_GPIO) == 0;
    if (sample != runtime.physical_candidate) {
        runtime.physical_candidate = sample;
        runtime.physical_candidate_since_ms = now_ms;
    } else if (sample != runtime.physical_down &&
               now_ms - runtime.physical_candidate_since_ms >=
                   CR11_UI_BUTTON_DEBOUNCE_MS) {
        runtime.physical_down = sample;
        if (cr11_debug_is_enabled()) {
            ESP_LOGI(TAG, "CR11_BOOT source=physical edge=%s",
                     sample ? "press" : "release");
        }
    }
}

static void update_effective_button(uint64_t now_ms)
{
    if (runtime.virtual_down &&
        now_ms - runtime.virtual_pressed_ms >=
            CONFIG_CR11_UI_VIRTUAL_HOLD_LIMIT_MS) {
        runtime.virtual_down = false;
        ESP_LOGW(TAG,
                 "CR11_BOOT source=uart edge=auto_release reason=hold_limit");
    }

    const bool effective = runtime.physical_down || runtime.virtual_down;
    if (effective != runtime.effective_down) {
        runtime.effective_down = effective;
        cr11_ui_state_boot_input(&runtime.state, effective, now_ms);
    }
}

static void ui_task(void *argument)
{
    (void)argument;
    TickType_t last_wake = xTaskGetTickCount();

    for (;;) {
        const uint64_t now_ms = uptime_ms();
        update_physical_button(now_ms);

        cr11_ui_message_t message;
        while (xQueueReceive(runtime.queue, &message, 0) == pdTRUE) {
            handle_message(&message, message.timestamp_ms);
        }
        update_effective_button(now_ms);
        cr11_ui_state_advance(&runtime.state, now_ms);
        sync_lease_watchdog(now_ms);
        dispatch_actions(now_ms);
        emit_state(now_ms);
        apply_led(now_ms);

        vTaskDelayUntil(&last_wake,
                        pdMS_TO_TICKS(CR11_UI_TASK_PERIOD_MS));
    }
}

static void uart_ack(const char *command, const char *status)
{
    ESP_LOGI(TAG, "CR11_UART {\"command\":\"%s\",\"status\":\"%s\"}",
             command, status);
}

static void parse_uart_line(const char *line)
{
    if (line == NULL || line[0] == '\0') {
        return;
    }

    cr11_ui_message_kind_t kind = CR11_UI_MESSAGE_STATUS;
    bool value = false;
    bool recognized = true;
    if (strcmp(line, "debug on") == 0) {
        cr11_debug_set_enabled(true);
        uart_ack(line, "ok");
        (void)post_message(CR11_UI_MESSAGE_STATUS, false);
        return;
    } else if (strcmp(line, "debug off") == 0) {
        cr11_debug_set_enabled(false);
        uart_ack(line, "ok");
        return;
    } else if (strcmp(line, "debug status") == 0) {
        uart_ack(line, cr11_debug_is_enabled() ? "on" : "off");
        return;
    } else if (strcmp(line, "boot press") == 0) {
        kind = CR11_UI_MESSAGE_VIRTUAL_BOOT;
        value = true;
    } else if (strcmp(line, "boot release") == 0) {
        kind = CR11_UI_MESSAGE_VIRTUAL_BOOT;
        value = false;
    } else if (strcmp(line, "ui status") == 0) {
        kind = CR11_UI_MESSAGE_STATUS;
    } else if (strcmp(line, "ui ready") == 0) {
        kind = CR11_UI_MESSAGE_READY;
    } else if (strcmp(line, "ui lease ping") == 0) {
        kind = CR11_UI_MESSAGE_LEASE_PING;
    } else if (strcmp(line, "ui event upper") == 0) {
        kind = CR11_UI_MESSAGE_GATEWAY_EVENT;
        value = false;
    } else if (strcmp(line, "ui event lower") == 0) {
        kind = CR11_UI_MESSAGE_GATEWAY_EVENT;
        value = true;
    } else if (strcmp(line, "help") == 0) {
        ESP_LOGI(TAG,
                 "CR11_UART_HELP commands=\"debug on|debug off|debug status|"
                 "boot press|boot release|"
                 "ui status|ui ready|ui lease ping|"
                 "ui event upper|ui event lower|help\"");
        uart_ack(line, "ok");
        return;
    } else {
        recognized = false;
    }

    if (!recognized) {
        uart_ack("unknown", "invalid_command");
        return;
    }
    const esp_err_t error = post_message(kind, value);
    uart_ack(line, error == ESP_OK ? "queued" : "queue_full");
}

static void uart_task(void *argument)
{
    (void)argument;
    char line[CR11_UI_UART_LINE_CAPACITY];
    size_t line_length = 0U;
    bool discarding = false;
    uint8_t input[CR11_UI_UART_READ_CHUNK];

    for (;;) {
        const int length = uart_read_bytes(
            (uart_port_t)CONFIG_ESP_CONSOLE_UART_NUM,
            input,
            sizeof(input),
            pdMS_TO_TICKS(100));
        if (length <= 0) {
            continue;
        }

        for (int index = 0; index < length; ++index) {
            const uint8_t byte = input[index];
            if (byte == '\n') {
                if (discarding) {
                    uart_ack("unknown", "line_too_long_or_non_ascii");
                } else {
                    line[line_length] = '\0';
                    parse_uart_line(line);
                }
                line_length = 0U;
                discarding = false;
                continue;
            }
            if (byte == '\r') {
                continue;
            }
            if (discarding) {
                continue;
            }
            if (byte < 0x20U || byte > 0x7eU ||
                line_length + 1U >= sizeof(line)) {
                discarding = true;
                continue;
            }
            line[line_length++] = (char)byte;
        }
    }
}

esp_err_t cr11_ui_runtime_apply_pending_wipe(const char *partition_label,
                                             bool *wipe_applied)
{
    if (partition_label == NULL || partition_label[0] == '\0' ||
        wipe_applied == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *wipe_applied = false;

    nvs_handle_t handle = 0;
    esp_err_t error = nvs_open(CR11_UI_NVS_NAMESPACE,
                               NVS_READWRITE, &handle);
    if (error != ESP_OK) {
        return error;
    }

    uint8_t pending = 0U;
    error = nvs_get_u8(handle, CR11_UI_NVS_FORCE_WIPE_KEY, &pending);
    if (error == ESP_ERR_NVS_NOT_FOUND) {
        nvs_close(handle);
        return ESP_OK;
    }
    if (error != ESP_OK) {
        nvs_close(handle);
        return error;
    }
    if (pending != 1U) {
        error = nvs_erase_key(handle, CR11_UI_NVS_FORCE_WIPE_KEY);
        if (error == ESP_OK) {
            error = nvs_commit(handle);
        }
        nvs_close(handle);
        return error;
    }

    error = nvs_flash_erase_partition(partition_label);
    if (error == ESP_OK) {
        *wipe_applied = true;
        error = nvs_erase_key(handle, CR11_UI_NVS_FORCE_WIPE_KEY);
        if (error == ESP_OK) {
            error = nvs_commit(handle);
        }
    }
    nvs_close(handle);
    return error;
}

esp_err_t cr11_ui_runtime_init(const cr11_ui_runtime_config_t *config)
{
    if (config == NULL || config->start_steering == NULL ||
        config->begin_local_reset == NULL || runtime.queue != NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    runtime.config = *config;
    runtime.queue = xQueueCreate(CR11_UI_QUEUE_LENGTH,
                                 sizeof(cr11_ui_message_t));
    if (runtime.queue == NULL) {
        return ESP_ERR_NO_MEM;
    }

    const gpio_config_t button_config = {
        .pin_bit_mask = 1ULL << CONFIG_CR11_UI_BOOT_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t error = gpio_config(&button_config);
    if (error != ESP_OK) {
        return error;
    }
    runtime.physical_candidate =
        gpio_get_level(CONFIG_CR11_UI_BOOT_GPIO) == 0;
    runtime.physical_candidate_since_ms = uptime_ms();

    const led_strip_config_t strip_config = {
        .strip_gpio_num = CONFIG_CR11_UI_RGB_GPIO,
        .max_leds = 1U,
        .led_model = LED_MODEL_WS2812,
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
    };
    const led_strip_rmt_config_t rmt_config = {
        .resolution_hz = 10U * 1000U * 1000U,
        .flags.with_dma = false,
    };
    error = led_strip_new_rmt_device(&strip_config, &rmt_config,
                                     &runtime.strip);
    if (error != ESP_OK) {
        return error;
    }
    error = led_strip_clear(runtime.strip);
    if (error != ESP_OK) {
        return error;
    }

    const uart_port_t uart =
        (uart_port_t)CONFIG_ESP_CONSOLE_UART_NUM;
    if (!uart_is_driver_installed(uart)) {
        error = uart_driver_install(uart,
                                    CONFIG_CR11_UI_UART_RX_BUFFER,
                                    0,
                                    0,
                                    NULL,
                                    0);
        if (error != ESP_OK) {
            return error;
        }
    }
    uart_vfs_dev_use_driver(uart);

    const uint64_t now_ms = uptime_ms();
    cr11_ui_state_init(&runtime.state, now_ms);
    runtime.effective_down = false;

    BaseType_t created = xTaskCreate(ui_task,
                                    "cr11_ui",
                                    CR11_UI_TASK_STACK,
                                    NULL,
                                    4,
                                    NULL);
    if (created != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    created = xTaskCreate(uart_task,
                          "cr11_ui_uart",
                          CR11_UI_UART_TASK_STACK,
                          NULL,
                          3,
                          NULL);
    return created == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}

void cr11_ui_runtime_stack_started(bool factory_new)
{
    if (post_message(CR11_UI_MESSAGE_STACK_STARTED, factory_new) != ESP_OK) {
        ESP_LOGE(TAG, "Unable to queue stack-started UI event");
    }
}

void cr11_ui_runtime_stack_retrying(void)
{
    if (post_message(CR11_UI_MESSAGE_STACK_RETRYING, false) != ESP_OK) {
        ESP_LOGE(TAG, "Unable to queue stack-retrying UI event");
    }
}

void cr11_ui_runtime_joined(void)
{
    if (post_message(CR11_UI_MESSAGE_JOINED, false) != ESP_OK) {
        ESP_LOGE(TAG, "Unable to queue joined UI event");
    }
}

void cr11_ui_runtime_ready(void)
{
    if (post_message(CR11_UI_MESSAGE_READY, false) != ESP_OK) {
        ESP_LOGE(TAG, "Unable to queue ready UI event");
    }
}

void cr11_ui_runtime_lease_ping(void)
{
    if (post_message(CR11_UI_MESSAGE_LEASE_PING, false) != ESP_OK) {
        ESP_LOGE(TAG, "Unable to queue Z2M lease heartbeat");
    }
}

void cr11_ui_runtime_network_left(bool will_rejoin)
{
    if (post_message(CR11_UI_MESSAGE_NETWORK_LEFT, will_rejoin) != ESP_OK) {
        ESP_LOGE(TAG, "Unable to queue network-left UI event");
    }
}

void cr11_ui_runtime_leave_succeeded(void)
{
    if (post_message(CR11_UI_MESSAGE_LEAVE_SUCCEEDED, false) != ESP_OK) {
        ESP_LOGE(TAG, "Unable to queue leave-success UI event");
    }
}

void cr11_ui_runtime_gateway_event(uint8_t button)
{
    if (button < 1U || button > 8U) {
        return;
    }
    if (post_message(CR11_UI_MESSAGE_GATEWAY_EVENT, button >= 5U) != ESP_OK) {
        ESP_LOGW(TAG, "Dropped gateway LED event button=%u", button);
    }
}
