/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#define CR11_UI_LEASE_WARN_MS        10000U
#define CR11_UI_LEASE_TIMEOUT_MS     30000U
#define CR11_UI_LEASE_WDT_GUARD_MS    5000U
#define CR11_UI_LEASE_FINAL_PULSE_GRACE_MS 200U
#define CR11_UI_LEASE_WDT_ARM_MS \
    (CR11_UI_LEASE_TIMEOUT_MS - CR11_UI_LEASE_WDT_GUARD_MS + \
     CR11_UI_LEASE_FINAL_PULSE_GRACE_MS)

typedef enum {
    CR11_UI_PHASE_STARTING = 0,
    CR11_UI_PHASE_RESTORING,
    CR11_UI_PHASE_UNPROVISIONED,
    CR11_UI_PHASE_JOINING,
    CR11_UI_PHASE_POST_JOIN,
    CR11_UI_PHASE_READY,
    CR11_UI_PHASE_LEAVING,
    CR11_UI_PHASE_FORCE_PENDING,
    CR11_UI_PHASE_RESULT_GRACEFUL,
    CR11_UI_PHASE_RESULT_FORCED,
    CR11_UI_PHASE_TERMINAL,
    CR11_UI_PHASE_ERROR,
} cr11_ui_phase_t;

typedef enum {
    CR11_UI_COLOR_OFF = 0,
    CR11_UI_COLOR_RED,
    CR11_UI_COLOR_GREEN,
    CR11_UI_COLOR_YELLOW_HALF,
    CR11_UI_COLOR_YELLOW,
    CR11_UI_COLOR_BLUE,
    CR11_UI_COLOR_PINK,
    CR11_UI_COLOR_PURPLE,
    CR11_UI_COLOR_ORANGE,
    CR11_UI_COLOR_LEASE_GRADIENT,
} cr11_ui_color_t;

typedef enum {
    CR11_UI_EFFECT_OFF = 0,
    CR11_UI_EFFECT_RESTORING,
    CR11_UI_EFFECT_UNPROVISIONED_HEARTBEAT,
    CR11_UI_EFFECT_JOIN_HOLD,
    CR11_UI_EFFECT_JOINING,
    CR11_UI_EFFECT_POST_JOIN,
    CR11_UI_EFFECT_READY_HEARTBEAT,
    CR11_UI_EFFECT_LEASE_UNARMED,
    CR11_UI_EFFECT_LEASE_WARNING,
    CR11_UI_EFFECT_UPPER_EVENT,
    CR11_UI_EFFECT_LOWER_EVENT,
    CR11_UI_EFFECT_RESET_HOLD,
    CR11_UI_EFFECT_LEAVE,
    CR11_UI_EFFECT_GRACEFUL_RESULT,
    CR11_UI_EFFECT_FORCED_RESULT,
    CR11_UI_EFFECT_TERMINAL,
    CR11_UI_EFFECT_ERROR,
} cr11_ui_effect_t;

enum {
    CR11_UI_ACTION_NONE = 0U,
    CR11_UI_ACTION_START_STEERING = 1U << 0,
    CR11_UI_ACTION_BEGIN_LOCAL_RESET = 1U << 1,
    CR11_UI_ACTION_ARM_FORCED_WIPE = 1U << 2,
};

typedef struct {
    cr11_ui_color_t color;
    cr11_ui_effect_t effect;
    uint8_t red;
    uint8_t green;
    uint8_t blue;
} cr11_ui_led_t;

typedef enum {
    CR11_UI_PRESS_NONE = 0,
    CR11_UI_PRESS_JOIN,
    CR11_UI_PRESS_RESET,
    CR11_UI_PRESS_IGNORED,
} cr11_ui_press_t;

typedef struct {
    cr11_ui_phase_t phase;
    uint64_t phase_since_ms;
    uint64_t last_now_ms;
    uint64_t press_since_ms;
    uint64_t leave_started_ms;
    uint64_t leave_succeeded_ms;
    uint64_t heartbeat_anchor_ms;
    uint64_t last_lease_ping_ms;
    uint64_t event_until_ms;
    uint32_t pending_actions;
    cr11_ui_press_t press;
    cr11_ui_color_t event_color;
    bool boot_down;
    bool require_release;
    bool leave_succeeded;
    bool lease_armed;
    bool boot_was_watchdog;
} cr11_ui_state_t;

void cr11_ui_state_init(cr11_ui_state_t *state, uint64_t now_ms);
void cr11_ui_state_stack_started(cr11_ui_state_t *state,
                                 bool factory_new,
                                 bool watchdog_reset,
                                 uint64_t now_ms);
void cr11_ui_state_stack_retrying(cr11_ui_state_t *state,
                                  bool watchdog_reset,
                                  uint64_t now_ms);
void cr11_ui_state_boot_input(cr11_ui_state_t *state,
                              bool down,
                              uint64_t now_ms);
void cr11_ui_state_joined(cr11_ui_state_t *state, uint64_t now_ms);
void cr11_ui_state_ready(cr11_ui_state_t *state, uint64_t now_ms);
void cr11_ui_state_lease_ping(cr11_ui_state_t *state, uint64_t now_ms);
void cr11_ui_state_network_left(cr11_ui_state_t *state,
                                bool will_rejoin,
                                uint64_t now_ms);
void cr11_ui_state_leave_succeeded(cr11_ui_state_t *state,
                                   uint64_t now_ms);
void cr11_ui_state_forced_wipe_armed(cr11_ui_state_t *state,
                                    bool success,
                                    uint64_t now_ms);
void cr11_ui_state_gateway_event(cr11_ui_state_t *state,
                                 bool lower_button,
                                 uint64_t now_ms);
void cr11_ui_state_advance(cr11_ui_state_t *state, uint64_t now_ms);

uint32_t cr11_ui_state_take_actions(cr11_ui_state_t *state);
cr11_ui_led_t cr11_ui_state_led(const cr11_ui_state_t *state,
                                uint64_t now_ms);
uint64_t cr11_ui_state_lease_age_ms(const cr11_ui_state_t *state,
                                    uint64_t now_ms);
bool cr11_ui_state_lease_wdt_due(const cr11_ui_state_t *state,
                                 uint64_t now_ms);

const char *cr11_ui_phase_name(cr11_ui_phase_t phase);
const char *cr11_ui_color_name(cr11_ui_color_t color);
const char *cr11_ui_effect_name(cr11_ui_effect_t effect);
