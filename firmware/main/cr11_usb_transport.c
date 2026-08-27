/*
 * SPDX-License-Identifier: MIT
 */

#include "cr11_usb_transport.h"

#include <stdbool.h>
#include <stdint.h>

#include "driver/usb_serial_jtag.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "esp_err.h"
#include "esp_log.h"

#include "cr11_debug.h"
#include "cr11_usb_frame.h"

#define CR11_USB_QUEUE_LENGTH 32U
#define CR11_USB_TASK_STACK   3072U
#define CR11_USB_TASK_PRIORITY 2U
#define CR11_USB_TX_BUFFER_SIZE 256U
#define CR11_USB_RX_BUFFER_SIZE 128U

static const char *TAG = "CR11_USB";

typedef struct {
    uint8_t packet[CR11_GATEWAY_PACKET_SIZE];
} cr11_usb_item_t;

static QueueHandle_t usb_queue;
static bool owns_driver;

static void usb_task(void *argument)
{
    (void)argument;
    cr11_usb_item_t item;
    char frame[CR11_USB_FRAME_BUFFER_SIZE];

    for (;;) {
        if (xQueueReceive(usb_queue, &item, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        if (!usb_serial_jtag_is_connected()) {
            continue;
        }
        if (!cr11_usb_frame_format(item.packet, sizeof(item.packet),
                                   frame, sizeof(frame))) {
            if (cr11_debug_is_enabled()) {
                ESP_LOGE(TAG, "Unable to format native USB event frame");
            }
            continue;
        }

        const int written = usb_serial_jtag_write_bytes(
            frame, CR11_USB_FRAME_TEXT_SIZE, 0);
        if (written != (int)CR11_USB_FRAME_TEXT_SIZE &&
            cr11_debug_is_enabled()) {
            ESP_LOGW(TAG,
                     "Dropped native USB event frame written=%d expected=%u",
                     written, (unsigned int)CR11_USB_FRAME_TEXT_SIZE);
        }
    }
}
esp_err_t cr11_usb_transport_init(void)
{
    if (usb_queue != NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    usb_queue = xQueueCreate(CR11_USB_QUEUE_LENGTH,
                             sizeof(cr11_usb_item_t));
    if (usb_queue == NULL) {
        return ESP_ERR_NO_MEM;
    }

    if (!usb_serial_jtag_is_driver_installed()) {
        usb_serial_jtag_driver_config_t config = {
            .tx_buffer_size = CR11_USB_TX_BUFFER_SIZE,
            .rx_buffer_size = CR11_USB_RX_BUFFER_SIZE,
        };
        const esp_err_t error = usb_serial_jtag_driver_install(&config);
        if (error != ESP_OK) {
            vQueueDelete(usb_queue);
            usb_queue = NULL;
            return error;
        }
        owns_driver = true;
    }

    const BaseType_t created = xTaskCreate(
        usb_task,
        "cr11_usb_tx",
        CR11_USB_TASK_STACK,
        NULL,
        CR11_USB_TASK_PRIORITY,
        NULL);
    if (created != pdPASS) {
        if (owns_driver) {
            (void)usb_serial_jtag_driver_uninstall();
            owns_driver = false;
        }
        vQueueDelete(usb_queue);
        usb_queue = NULL;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t cr11_usb_transport_submit(const uint8_t *packet,
                                    size_t packet_length)
{
    if (packet == NULL || packet_length != CR11_GATEWAY_PACKET_SIZE) {
        return ESP_ERR_INVALID_ARG;
    }
    if (usb_queue == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    cr11_usb_item_t item;
    for (size_t index = 0; index < sizeof(item.packet); ++index) {
        item.packet[index] = packet[index];
    }
    return xQueueSend(usb_queue, &item, 0) == pdTRUE
               ? ESP_OK
               : ESP_ERR_TIMEOUT;
}
