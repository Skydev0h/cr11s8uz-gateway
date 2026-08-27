/*
 * SPDX-License-Identifier: MIT
 */

#include "cr11_ui_state.h"

#include <stddef.h>

#define CR11_UI_JOIN_HOLD_MS            1000U
#define CR11_UI_RESET_HOLD_MS           5000U
#define CR11_UI_LEAVE_MIN_RED_MS         1000U
#define CR11_UI_LEAVE_TIMEOUT_MS         3000U
#define CR11_UI_RESULT_MS                1000U
#define CR11_UI_EVENT_PULSE_MS           100U
#define CR11_UI_HEARTBEAT_PERIOD_MS      5000U
#define CR11_UI_HEARTBEAT_PULSE_MS       100U
#define CR11_UI_RESTORE_PERIOD_MS        2000U
#define CR11_UI_RESTORE_PULSE_MS          100U
#define CR11_UI_LEASE_WARNING_PERIOD_MS  2000U
#define CR11_UI_LEASE_UNARMED_PERIOD_MS  3000U

static uint64_t monotonic_now(cr11_ui_state_t *state, uint64_t now_ms)
{
    if (now_ms < state->last_now_ms) {
        return state->last_now_ms;
    }
    state->last_now_ms = now_ms;
    return now_ms;
}

static void enter_phase(cr11_ui_state_t *state,
                        cr11_ui_phase_t phase,
                        uint64_t now_ms)
{
    state->phase = phase;
    state->phase_since_ms = now_ms;
}

static bool phase_accepts_reset(cr11_ui_phase_t phase)
{
    return phase == CR11_UI_PHASE_READY ||
           phase == CR11_UI_PHASE_POST_JOIN ||
           phase == CR11_UI_PHASE_RESTORING;
}

void cr11_ui_state_init(cr11_ui_state_t *state, uint64_t now_ms)
{
    if (state == NULL) {
        return;
    }

    *state = (cr11_ui_state_t){
        .phase = CR11_UI_PHASE_STARTING,
        .phase_since_ms = now_ms,
        .last_now_ms = now_ms,
        .press_since_ms = now_ms,
        .leave_started_ms = now_ms,
        .leave_succeeded_ms = now_ms,
        .heartbeat_anchor_ms = now_ms,
        .last_lease_ping_ms = now_ms,
        .event_until_ms = now_ms,
        .pending_actions = CR11_UI_ACTION_NONE,
        .press = CR11_UI_PRESS_NONE,
        .event_color = CR11_UI_COLOR_OFF,
        .boot_down = false,
        .require_release = false,
        .leave_succeeded = false,
        .lease_armed = false,
        .boot_was_watchdog = false,
    };
}

void cr11_ui_state_stack_started(cr11_ui_state_t *state,
                                 bool factory_new,
                                 bool watchdog_reset,
                                 uint64_t now_ms)
{
    if (state == NULL) {
        return;
    }
    now_ms = monotonic_now(state, now_ms);
    if (state->phase == CR11_UI_PHASE_TERMINAL ||
        state->phase == CR11_UI_PHASE_ERROR) {
        return;
    }

    state->press = CR11_UI_PRESS_NONE;
    state->event_color = CR11_UI_COLOR_OFF;
    state->event_until_ms = now_ms;
    state->lease_armed = false;
    state->boot_was_watchdog = watchdog_reset;
    if (factory_new) {
        enter_phase(state, CR11_UI_PHASE_UNPROVISIONED, now_ms);
    } else {
        enter_phase(state, CR11_UI_PHASE_READY, now_ms);
        state->heartbeat_anchor_ms = now_ms;
        if (!watchdog_reset) {
            state->lease_armed = true;
            state->last_lease_ping_ms = now_ms;
        }
    }

    if (state->boot_down) {
        state->press = factory_new ? CR11_UI_PRESS_JOIN :
                                     CR11_UI_PRESS_RESET;
        state->press_since_ms = now_ms;
    }
}

void cr11_ui_state_stack_retrying(cr11_ui_state_t *state,
                                  bool watchdog_reset,
                                  uint64_t now_ms)
{
    if (state == NULL) {
        return;
    }
    now_ms = monotonic_now(state, now_ms);
    if (state->phase == CR11_UI_PHASE_LEAVING ||
        state->phase == CR11_UI_PHASE_FORCE_PENDING ||
        state->phase == CR11_UI_PHASE_RESULT_GRACEFUL ||
        state->phase == CR11_UI_PHASE_RESULT_FORCED ||
        state->phase == CR11_UI_PHASE_TERMINAL ||
        state->phase == CR11_UI_PHASE_ERROR) {
        return;
    }

    state->event_color = CR11_UI_COLOR_OFF;
    state->event_until_ms = now_ms;
    state->lease_armed = false;
    state->boot_was_watchdog = watchdog_reset;

    /* Keep the pulse train stable across the five-second BDB retries. */
    if (state->phase == CR11_UI_PHASE_RESTORING) {
        return;
    }

    enter_phase(state, CR11_UI_PHASE_RESTORING, now_ms);
    state->press = state->boot_down ? CR11_UI_PRESS_RESET :
                                      CR11_UI_PRESS_NONE;
    state->press_since_ms = now_ms;
    state->require_release = false;
}

void cr11_ui_state_boot_input(cr11_ui_state_t *state,
                              bool down,
                              uint64_t now_ms)
{
    if (state == NULL) {
        return;
    }
    cr11_ui_state_advance(state, now_ms);
    now_ms = state->last_now_ms;
    if (down == state->boot_down) {
        return;
    }

    state->boot_down = down;
    if (!down) {
        const bool cancelled_reset =
            state->press == CR11_UI_PRESS_RESET &&
            phase_accepts_reset(state->phase);
        state->press = CR11_UI_PRESS_NONE;
        state->require_release = false;
        if (cancelled_reset && state->phase == CR11_UI_PHASE_READY) {
            state->heartbeat_anchor_ms =
                now_ms + CR11_UI_HEARTBEAT_PERIOD_MS;
        }
        return;
    }

    if (state->require_release || state->phase == CR11_UI_PHASE_TERMINAL ||
        state->phase == CR11_UI_PHASE_ERROR ||
        state->phase == CR11_UI_PHASE_LEAVING ||
        state->phase == CR11_UI_PHASE_FORCE_PENDING ||
        state->phase == CR11_UI_PHASE_RESULT_GRACEFUL ||
        state->phase == CR11_UI_PHASE_RESULT_FORCED) {
        state->press = CR11_UI_PRESS_IGNORED;
        return;
    }

    state->press_since_ms = now_ms;
    if (state->phase == CR11_UI_PHASE_UNPROVISIONED) {
        state->press = CR11_UI_PRESS_JOIN;
    } else if (phase_accepts_reset(state->phase)) {
        state->press = CR11_UI_PRESS_RESET;
    } else {
        state->press = CR11_UI_PRESS_IGNORED;
    }
}

void cr11_ui_state_joined(cr11_ui_state_t *state, uint64_t now_ms)
{
    if (state == NULL) {
        return;
    }
    cr11_ui_state_advance(state, now_ms);
    now_ms = state->last_now_ms;
    if (state->phase != CR11_UI_PHASE_JOINING) {
        return;
    }
    enter_phase(state, CR11_UI_PHASE_POST_JOIN, now_ms);
    if (state->boot_down) {
        state->require_release = true;
        state->press = CR11_UI_PRESS_IGNORED;
    }
}

void cr11_ui_state_ready(cr11_ui_state_t *state, uint64_t now_ms)
{
    if (state == NULL) {
        return;
    }
    cr11_ui_state_advance(state, now_ms);
    now_ms = state->last_now_ms;
    if (state->phase != CR11_UI_PHASE_POST_JOIN &&
        state->phase != CR11_UI_PHASE_READY) {
        return;
    }
    if (state->phase == CR11_UI_PHASE_POST_JOIN) {
        enter_phase(state, CR11_UI_PHASE_READY, now_ms);
        state->heartbeat_anchor_ms = now_ms;
    }
    state->lease_armed = true;
    state->last_lease_ping_ms = now_ms;
}

void cr11_ui_state_lease_ping(cr11_ui_state_t *state, uint64_t now_ms)
{
    if (state == NULL) {
        return;
    }
    cr11_ui_state_advance(state, now_ms);
    now_ms = state->last_now_ms;
    if (state->phase != CR11_UI_PHASE_READY) {
        return;
    }
    state->lease_armed = true;
    state->last_lease_ping_ms = now_ms;
}

void cr11_ui_state_network_left(cr11_ui_state_t *state,
                                bool will_rejoin,
                                uint64_t now_ms)
{
    if (state == NULL) {
        return;
    }
    cr11_ui_state_advance(state, now_ms);
    now_ms = state->last_now_ms;
    if (state->phase == CR11_UI_PHASE_LEAVING) {
        cr11_ui_state_leave_succeeded(state, now_ms);
        return;
    }
    if (state->phase == CR11_UI_PHASE_FORCE_PENDING ||
        state->phase == CR11_UI_PHASE_RESULT_GRACEFUL ||
        state->phase == CR11_UI_PHASE_RESULT_FORCED ||
        state->phase == CR11_UI_PHASE_TERMINAL ||
        state->phase == CR11_UI_PHASE_ERROR) {
        return;
    }

    state->press = CR11_UI_PRESS_NONE;
    state->require_release = state->boot_down;
    state->lease_armed = false;
    enter_phase(state, will_rejoin ? CR11_UI_PHASE_RESTORING :
                                    CR11_UI_PHASE_UNPROVISIONED,
                now_ms);
}

void cr11_ui_state_leave_succeeded(cr11_ui_state_t *state,
                                   uint64_t now_ms)
{
    if (state == NULL) {
        return;
    }
    cr11_ui_state_advance(state, now_ms);
    now_ms = state->last_now_ms;
    if (state->phase != CR11_UI_PHASE_LEAVING) {
        return;
    }

    state->leave_succeeded = true;
    state->leave_succeeded_ms = now_ms;
    cr11_ui_state_advance(state, now_ms);
}

void cr11_ui_state_forced_wipe_armed(cr11_ui_state_t *state,
                                    bool success,
                                    uint64_t now_ms)
{
    if (state == NULL) {
        return;
    }
    cr11_ui_state_advance(state, now_ms);
    now_ms = state->last_now_ms;
    if (state->phase != CR11_UI_PHASE_FORCE_PENDING) {
        return;
    }
    enter_phase(state,
                success ? CR11_UI_PHASE_RESULT_FORCED :
                          CR11_UI_PHASE_ERROR,
                now_ms);
}

void cr11_ui_state_gateway_event(cr11_ui_state_t *state,
                                 bool lower_button,
                                 uint64_t now_ms)
{
    if (state == NULL) {
        return;
    }
    cr11_ui_state_advance(state, now_ms);
    now_ms = state->last_now_ms;
    if (state->phase != CR11_UI_PHASE_READY) {
        return;
    }

    state->event_color = lower_button ? CR11_UI_COLOR_PINK :
                                        CR11_UI_COLOR_BLUE;
    state->event_until_ms = now_ms + CR11_UI_EVENT_PULSE_MS;
    state->heartbeat_anchor_ms = now_ms + CR11_UI_HEARTBEAT_PERIOD_MS;
}

void cr11_ui_state_advance(cr11_ui_state_t *state, uint64_t now_ms)
{
    if (state == NULL) {
        return;
    }
    now_ms = monotonic_now(state, now_ms);

    if (state->press == CR11_UI_PRESS_JOIN && state->boot_down &&
        state->phase == CR11_UI_PHASE_UNPROVISIONED &&
        now_ms - state->press_since_ms >= CR11_UI_JOIN_HOLD_MS) {
        enter_phase(state, CR11_UI_PHASE_JOINING, now_ms);
        state->pending_actions |= CR11_UI_ACTION_START_STEERING;
        state->press = CR11_UI_PRESS_IGNORED;
        state->require_release = true;
    }

    if (state->press == CR11_UI_PRESS_RESET && state->boot_down &&
        phase_accepts_reset(state->phase) &&
        now_ms - state->press_since_ms >= CR11_UI_RESET_HOLD_MS) {
        enter_phase(state, CR11_UI_PHASE_LEAVING, now_ms);
        state->leave_started_ms = now_ms;
        state->leave_succeeded = false;
        state->pending_actions |= CR11_UI_ACTION_BEGIN_LOCAL_RESET;
        state->press = CR11_UI_PRESS_IGNORED;
        state->require_release = true;
    }

    if (state->phase == CR11_UI_PHASE_LEAVING) {
        if (state->leave_succeeded) {
            uint64_t result_at =
                state->leave_started_ms + CR11_UI_LEAVE_MIN_RED_MS;
            if (state->leave_succeeded_ms > result_at) {
                result_at = state->leave_succeeded_ms;
            }
            if (now_ms >= result_at) {
                enter_phase(state, CR11_UI_PHASE_RESULT_GRACEFUL,
                            result_at);
            }
        } else if (now_ms - state->leave_started_ms >=
                   CR11_UI_LEAVE_TIMEOUT_MS) {
            enter_phase(state, CR11_UI_PHASE_FORCE_PENDING, now_ms);
            state->pending_actions |= CR11_UI_ACTION_ARM_FORCED_WIPE;
        }
    }

    if ((state->phase == CR11_UI_PHASE_RESULT_GRACEFUL ||
         state->phase == CR11_UI_PHASE_RESULT_FORCED) &&
        now_ms - state->phase_since_ms >= CR11_UI_RESULT_MS) {
        enter_phase(state, CR11_UI_PHASE_TERMINAL,
                    state->phase_since_ms + CR11_UI_RESULT_MS);
        state->press = CR11_UI_PRESS_IGNORED;
        state->require_release = true;
    }
}

uint32_t cr11_ui_state_take_actions(cr11_ui_state_t *state)
{
    if (state == NULL) {
        return CR11_UI_ACTION_NONE;
    }
    const uint32_t actions = state->pending_actions;
    state->pending_actions = CR11_UI_ACTION_NONE;
    return actions;
}

static bool pulse(uint64_t elapsed_ms, uint64_t period_ms, uint64_t on_ms)
{
    return elapsed_ms % period_ms < on_ms;
}

static cr11_ui_led_t led(cr11_ui_color_t color, cr11_ui_effect_t effect)
{
    cr11_ui_led_t result = {.color = color, .effect = effect};
    switch (color) {
    case CR11_UI_COLOR_RED:
        result.red = 255U;
        break;
    case CR11_UI_COLOR_GREEN:
        result.green = 255U;
        break;
    case CR11_UI_COLOR_YELLOW_HALF:
        result.red = 128U;
        result.green = 128U;
        break;
    case CR11_UI_COLOR_YELLOW:
        result.red = 255U;
        result.green = 255U;
        break;
    case CR11_UI_COLOR_BLUE:
        result.blue = 255U;
        break;
    case CR11_UI_COLOR_PINK:
        result.red = 255U;
        result.blue = 85U;
        break;
    case CR11_UI_COLOR_PURPLE:
        result.red = 128U;
        result.blue = 255U;
        break;
    case CR11_UI_COLOR_ORANGE:
        result.red = 255U;
        result.green = 127U;
        break;
    case CR11_UI_COLOR_LEASE_GRADIENT:
    case CR11_UI_COLOR_OFF:
    default:
        break;
    }
    return result;
}

static cr11_ui_led_t led_rgb(uint8_t red,
                             uint8_t green,
                             uint8_t blue,
                             cr11_ui_effect_t effect)
{
    return (cr11_ui_led_t){
        .color = CR11_UI_COLOR_LEASE_GRADIENT,
        .effect = effect,
        .red = red,
        .green = green,
        .blue = blue,
    };
}

static cr11_ui_led_t reset_hold_led(uint64_t elapsed_ms)
{
    bool on = false;
    if (elapsed_ms < 2000U) {
        on = pulse(elapsed_ms, 1000U, 500U);
    } else if (elapsed_ms < 4000U) {
        on = pulse(elapsed_ms - 2000U, 500U, 250U);
    } else {
        on = pulse(elapsed_ms - 4000U, 200U, 100U);
    }
    return led(on ? CR11_UI_COLOR_RED : CR11_UI_COLOR_OFF,
               CR11_UI_EFFECT_RESET_HOLD);
}

uint64_t cr11_ui_state_lease_age_ms(const cr11_ui_state_t *state,
                                    uint64_t now_ms)
{
    if (state == NULL || !state->lease_armed) {
        return 0U;
    }
    if (now_ms < state->last_lease_ping_ms) {
        now_ms = state->last_lease_ping_ms;
    }
    return now_ms - state->last_lease_ping_ms;
}

bool cr11_ui_state_lease_wdt_due(const cr11_ui_state_t *state,
                                 uint64_t now_ms)
{
    return state != NULL && state->phase == CR11_UI_PHASE_READY &&
           state->lease_armed &&
           cr11_ui_state_lease_age_ms(state, now_ms) >=
               CR11_UI_LEASE_WDT_ARM_MS;
}

static cr11_ui_led_t lease_unarmed_led(const cr11_ui_state_t *state,
                                       uint64_t now_ms)
{
    const uint64_t offset =
        (now_ms - state->phase_since_ms) % CR11_UI_LEASE_UNARMED_PERIOD_MS;
    if (offset < 100U) {
        return led(CR11_UI_COLOR_GREEN, CR11_UI_EFFECT_LEASE_UNARMED);
    }
    if (offset >= 200U && offset < 300U) {
        return led(CR11_UI_COLOR_ORANGE, CR11_UI_EFFECT_LEASE_UNARMED);
    }
    return led(CR11_UI_COLOR_OFF, CR11_UI_EFFECT_LEASE_UNARMED);
}

static cr11_ui_led_t lease_warning_led(uint64_t age_ms)
{
    const uint64_t warning_age = age_ms - CR11_UI_LEASE_WARN_MS;
    if (!pulse(warning_age, CR11_UI_LEASE_WARNING_PERIOD_MS,
               CR11_UI_HEARTBEAT_PULSE_MS)) {
        return led(CR11_UI_COLOR_OFF, CR11_UI_EFFECT_LEASE_WARNING);
    }

    const uint64_t span =
        CR11_UI_LEASE_TIMEOUT_MS - CR11_UI_LEASE_WARN_MS;
    uint64_t visible_age =
        (warning_age / CR11_UI_LEASE_WARNING_PERIOD_MS) *
        CR11_UI_LEASE_WARNING_PERIOD_MS;
    if (visible_age > span) {
        visible_age = span;
    }
    const uint8_t red = (uint8_t)((255U * visible_age) / span);
    const uint8_t green =
        (uint8_t)(255U - ((128U * visible_age) / span));
    return led_rgb(red, green, 0U, CR11_UI_EFFECT_LEASE_WARNING);
}

cr11_ui_led_t cr11_ui_state_led(const cr11_ui_state_t *state,
                                uint64_t now_ms)
{
    if (state == NULL) {
        return led(CR11_UI_COLOR_OFF, CR11_UI_EFFECT_OFF);
    }
    if (now_ms < state->last_now_ms) {
        now_ms = state->last_now_ms;
    }

    if (state->press == CR11_UI_PRESS_RESET && state->boot_down &&
        phase_accepts_reset(state->phase)) {
        return reset_hold_led(now_ms - state->press_since_ms);
    }

    switch (state->phase) {
    case CR11_UI_PHASE_RESTORING: {
        const uint64_t offset =
            (now_ms - state->phase_since_ms) %
            CR11_UI_RESTORE_PERIOD_MS;
        if (offset < CR11_UI_RESTORE_PULSE_MS) {
            return led(CR11_UI_COLOR_YELLOW,
                       CR11_UI_EFFECT_RESTORING);
        }
        if (offset >= 2U * CR11_UI_RESTORE_PULSE_MS &&
            offset < 3U * CR11_UI_RESTORE_PULSE_MS) {
            return led(CR11_UI_COLOR_ORANGE,
                       CR11_UI_EFFECT_RESTORING);
        }
        return led(CR11_UI_COLOR_OFF, CR11_UI_EFFECT_RESTORING);
    }

    case CR11_UI_PHASE_UNPROVISIONED:
        if (state->press == CR11_UI_PRESS_JOIN && state->boot_down) {
            return led(CR11_UI_COLOR_YELLOW_HALF,
                       CR11_UI_EFFECT_JOIN_HOLD);
        }
        return led(pulse(now_ms - state->phase_since_ms, 1000U, 100U)
                       ? CR11_UI_COLOR_RED : CR11_UI_COLOR_OFF,
                   CR11_UI_EFFECT_UNPROVISIONED_HEARTBEAT);

    case CR11_UI_PHASE_JOINING:
        return led(pulse(now_ms - state->phase_since_ms, 1000U, 500U)
                       ? CR11_UI_COLOR_YELLOW : CR11_UI_COLOR_OFF,
                   CR11_UI_EFFECT_JOINING);

    case CR11_UI_PHASE_POST_JOIN:
        return led(pulse(now_ms - state->phase_since_ms, 1000U, 500U)
                       ? CR11_UI_COLOR_YELLOW : CR11_UI_COLOR_GREEN,
                   CR11_UI_EFFECT_POST_JOIN);

    case CR11_UI_PHASE_READY:
        if (now_ms < state->event_until_ms) {
            return led(state->event_color,
                       state->event_color == CR11_UI_COLOR_PINK
                           ? CR11_UI_EFFECT_LOWER_EVENT
                           : CR11_UI_EFFECT_UPPER_EVENT);
        }
        if (!state->lease_armed) {
            return lease_unarmed_led(state, now_ms);
        }
        const uint64_t lease_age =
            cr11_ui_state_lease_age_ms(state, now_ms);
        if (lease_age >= CR11_UI_LEASE_WARN_MS) {
            return lease_warning_led(lease_age);
        }
        if (now_ms >= state->heartbeat_anchor_ms &&
            pulse(now_ms - state->heartbeat_anchor_ms,
                  CR11_UI_HEARTBEAT_PERIOD_MS,
                  CR11_UI_HEARTBEAT_PULSE_MS)) {
            return led(CR11_UI_COLOR_GREEN,
                       CR11_UI_EFFECT_READY_HEARTBEAT);
        }
        return led(CR11_UI_COLOR_OFF, CR11_UI_EFFECT_READY_HEARTBEAT);

    case CR11_UI_PHASE_LEAVING:
    case CR11_UI_PHASE_FORCE_PENDING:
        return led(CR11_UI_COLOR_RED, CR11_UI_EFFECT_LEAVE);

    case CR11_UI_PHASE_RESULT_GRACEFUL:
        return led(CR11_UI_COLOR_GREEN,
                   CR11_UI_EFFECT_GRACEFUL_RESULT);

    case CR11_UI_PHASE_RESULT_FORCED:
        return led(CR11_UI_COLOR_PURPLE,
                   CR11_UI_EFFECT_FORCED_RESULT);

    case CR11_UI_PHASE_TERMINAL: {
        const uint64_t offset =
            (now_ms - state->phase_since_ms) % 1000U;
        const bool on = offset < 100U ||
                        (offset >= 200U && offset < 300U);
        return led(on ? CR11_UI_COLOR_RED : CR11_UI_COLOR_OFF,
                   CR11_UI_EFFECT_TERMINAL);
    }

    case CR11_UI_PHASE_ERROR:
        return led(CR11_UI_COLOR_RED, CR11_UI_EFFECT_ERROR);

    case CR11_UI_PHASE_STARTING:
    default:
        return led(CR11_UI_COLOR_OFF, CR11_UI_EFFECT_OFF);
    }
}

const char *cr11_ui_phase_name(cr11_ui_phase_t phase)
{
    switch (phase) {
    case CR11_UI_PHASE_STARTING:
        return "starting";
    case CR11_UI_PHASE_RESTORING:
        return "restoring";
    case CR11_UI_PHASE_UNPROVISIONED:
        return "unprovisioned";
    case CR11_UI_PHASE_JOINING:
        return "joining";
    case CR11_UI_PHASE_POST_JOIN:
        return "post_join";
    case CR11_UI_PHASE_READY:
        return "ready";
    case CR11_UI_PHASE_LEAVING:
        return "leaving";
    case CR11_UI_PHASE_FORCE_PENDING:
        return "force_pending";
    case CR11_UI_PHASE_RESULT_GRACEFUL:
        return "result_graceful";
    case CR11_UI_PHASE_RESULT_FORCED:
        return "result_forced";
    case CR11_UI_PHASE_TERMINAL:
        return "terminal";
    case CR11_UI_PHASE_ERROR:
        return "error";
    default:
        return "invalid";
    }
}

const char *cr11_ui_color_name(cr11_ui_color_t color)
{
    switch (color) {
    case CR11_UI_COLOR_OFF:
        return "off";
    case CR11_UI_COLOR_RED:
        return "red";
    case CR11_UI_COLOR_GREEN:
        return "green";
    case CR11_UI_COLOR_YELLOW_HALF:
        return "yellow_half";
    case CR11_UI_COLOR_YELLOW:
        return "yellow";
    case CR11_UI_COLOR_BLUE:
        return "blue";
    case CR11_UI_COLOR_PINK:
        return "pink";
    case CR11_UI_COLOR_PURPLE:
        return "purple";
    case CR11_UI_COLOR_ORANGE:
        return "orange";
    case CR11_UI_COLOR_LEASE_GRADIENT:
        return "lease_gradient";
    default:
        return "invalid";
    }
}

const char *cr11_ui_effect_name(cr11_ui_effect_t effect)
{
    switch (effect) {
    case CR11_UI_EFFECT_OFF:
        return "off";
    case CR11_UI_EFFECT_RESTORING:
        return "restoring";
    case CR11_UI_EFFECT_UNPROVISIONED_HEARTBEAT:
        return "unprovisioned_heartbeat";
    case CR11_UI_EFFECT_JOIN_HOLD:
        return "join_hold";
    case CR11_UI_EFFECT_JOINING:
        return "joining";
    case CR11_UI_EFFECT_POST_JOIN:
        return "post_join";
    case CR11_UI_EFFECT_READY_HEARTBEAT:
        return "ready_heartbeat";
    case CR11_UI_EFFECT_LEASE_UNARMED:
        return "lease_unarmed";
    case CR11_UI_EFFECT_LEASE_WARNING:
        return "lease_warning";
    case CR11_UI_EFFECT_UPPER_EVENT:
        return "upper_event";
    case CR11_UI_EFFECT_LOWER_EVENT:
        return "lower_event";
    case CR11_UI_EFFECT_RESET_HOLD:
        return "reset_hold";
    case CR11_UI_EFFECT_LEAVE:
        return "leave";
    case CR11_UI_EFFECT_GRACEFUL_RESULT:
        return "graceful_result";
    case CR11_UI_EFFECT_FORCED_RESULT:
        return "forced_result";
    case CR11_UI_EFFECT_TERMINAL:
        return "terminal";
    case CR11_UI_EFFECT_ERROR:
        return "error";
    default:
        return "invalid";
    }
}
