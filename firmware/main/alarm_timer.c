/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 *
 * Adapted from ESP Zigbee SDK examples/utils/alarm_timer.
 */

#include "alarm_timer.h"

#include <stdlib.h>

#include "esp_timer.h"

typedef struct {
    esp_timer_handle_t timer;
    alarm_timer_callback_t callback;
    alarm_timer_arg_t argument;
} alarm_timer_t;

static void alarm_timer_fired(void *argument)
{
    alarm_timer_t *alarm = (alarm_timer_t *)argument;

    if (alarm == NULL) {
        return;
    }

    (void)esp_timer_delete(alarm->timer);
    alarm->callback(alarm->argument);
    free(alarm);
}

esp_err_t alarm_timer_schedule(alarm_timer_callback_t callback,
                               alarm_timer_arg_t argument,
                               uint32_t delay_ms)
{
    if (callback == NULL || delay_ms == 0U) {
        return ESP_ERR_INVALID_ARG;
    }

    alarm_timer_t *alarm = calloc(1, sizeof(*alarm));
    if (alarm == NULL) {
        return ESP_ERR_NO_MEM;
    }

    alarm->callback = callback;
    alarm->argument = argument;

    const esp_timer_create_args_t timer_args = {
        .callback = alarm_timer_fired,
        .arg = alarm,
        .name = "cr11_zb_retry",
    };

    esp_err_t error = esp_timer_create(&timer_args, &alarm->timer);
    if (error != ESP_OK) {
        free(alarm);
        return error;
    }

    error = esp_timer_start_once(alarm->timer, (uint64_t)delay_ms * 1000U);
    if (error != ESP_OK) {
        (void)esp_timer_delete(alarm->timer);
        free(alarm);
        return error;
    }

    return ESP_OK;
}
