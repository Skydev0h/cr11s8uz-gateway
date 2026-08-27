/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

typedef esp_err_t (*cr11_ui_runtime_action_t)(void *context);

typedef struct {
    cr11_ui_runtime_action_t start_steering;
    cr11_ui_runtime_action_t begin_local_reset;
    void *context;
    bool watchdog_reset;
} cr11_ui_runtime_config_t;

esp_err_t cr11_ui_runtime_apply_pending_wipe(const char *partition_label,
                                             bool *wipe_applied);
esp_err_t cr11_ui_runtime_init(const cr11_ui_runtime_config_t *config);

void cr11_ui_runtime_stack_started(bool factory_new);
void cr11_ui_runtime_stack_retrying(void);
void cr11_ui_runtime_joined(void);
void cr11_ui_runtime_ready(void);
void cr11_ui_runtime_lease_ping(void);
void cr11_ui_runtime_network_left(bool will_rejoin);
void cr11_ui_runtime_leave_succeeded(void);
void cr11_ui_runtime_gateway_event(uint8_t button);
