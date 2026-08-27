/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 *
 * Adapted from ESP Zigbee SDK examples/utils/alarm_timer.
 */

#pragma once

#include <stdint.h>

#include "esp_err.h"

typedef uintptr_t alarm_timer_arg_t;
typedef void (*alarm_timer_callback_t)(alarm_timer_arg_t arg);

esp_err_t alarm_timer_schedule(alarm_timer_callback_t callback,
                               alarm_timer_arg_t argument,
                               uint32_t delay_ms);
