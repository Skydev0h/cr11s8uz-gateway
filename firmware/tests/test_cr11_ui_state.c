/*
 * SPDX-License-Identifier: MIT
 */

#include <assert.h>
#include <stdio.h>

#include "cr11_ui_state.h"

static void expect_led(const cr11_ui_state_t *state,
                       uint64_t now_ms,
                       cr11_ui_color_t color,
                       cr11_ui_effect_t effect)
{
    const cr11_ui_led_t actual = cr11_ui_state_led(state, now_ms);
    assert(actual.color == color);
    assert(actual.effect == effect);
}

static void expect_led_rgb(const cr11_ui_state_t *state,
                           uint64_t now_ms,
                           cr11_ui_color_t color,
                           cr11_ui_effect_t effect,
                           uint8_t red,
                           uint8_t green,
                           uint8_t blue)
{
    const cr11_ui_led_t actual = cr11_ui_state_led(state, now_ms);
    assert(actual.color == color);
    assert(actual.effect == effect);
    assert(actual.red == red);
    assert(actual.green == green);
    assert(actual.blue == blue);
}

static void start_reset(cr11_ui_state_t *state, uint64_t pressed_at);

static void test_join_and_release_latch(void)
{
    cr11_ui_state_t state;
    cr11_ui_state_init(&state, 0U);
    expect_led(&state, 0U, CR11_UI_COLOR_OFF, CR11_UI_EFFECT_OFF);

    cr11_ui_state_stack_started(&state, true, false, 10U);
    assert(state.phase == CR11_UI_PHASE_UNPROVISIONED);
    expect_led(&state, 10U, CR11_UI_COLOR_RED,
               CR11_UI_EFFECT_UNPROVISIONED_HEARTBEAT);
    expect_led(&state, 110U, CR11_UI_COLOR_OFF,
               CR11_UI_EFFECT_UNPROVISIONED_HEARTBEAT);

    cr11_ui_state_boot_input(&state, true, 200U);
    expect_led(&state, 200U, CR11_UI_COLOR_YELLOW_HALF,
               CR11_UI_EFFECT_JOIN_HOLD);
    cr11_ui_state_advance(&state, 1199U);
    assert(state.phase == CR11_UI_PHASE_UNPROVISIONED);
    assert(cr11_ui_state_take_actions(&state) == CR11_UI_ACTION_NONE);

    cr11_ui_state_advance(&state, 1200U);
    assert(state.phase == CR11_UI_PHASE_JOINING);
    assert(cr11_ui_state_take_actions(&state) ==
           CR11_UI_ACTION_START_STEERING);
    expect_led(&state, 1200U, CR11_UI_COLOR_YELLOW,
               CR11_UI_EFFECT_JOINING);
    expect_led(&state, 1700U, CR11_UI_COLOR_OFF,
               CR11_UI_EFFECT_JOINING);

    cr11_ui_state_joined(&state, 1800U);
    assert(state.phase == CR11_UI_PHASE_POST_JOIN);
    expect_led(&state, 1800U, CR11_UI_COLOR_YELLOW,
               CR11_UI_EFFECT_POST_JOIN);
    expect_led(&state, 2300U, CR11_UI_COLOR_GREEN,
               CR11_UI_EFFECT_POST_JOIN);

    cr11_ui_state_ready(&state, 2500U);
    assert(state.phase == CR11_UI_PHASE_READY);
    assert(state.require_release);
    cr11_ui_state_advance(&state, 9000U);
    assert(state.phase == CR11_UI_PHASE_READY);
    assert(cr11_ui_state_take_actions(&state) == CR11_UI_ACTION_NONE);

    cr11_ui_state_boot_input(&state, false, 9010U);
    assert(!state.require_release);
    cr11_ui_state_boot_input(&state, true, 9100U);
    cr11_ui_state_boot_input(&state, false, 9500U);
    assert(state.phase == CR11_UI_PHASE_READY);
    assert(cr11_ui_state_take_actions(&state) == CR11_UI_ACTION_NONE);
}

static void test_ready_events_and_heartbeat(void)
{
    cr11_ui_state_t state;
    cr11_ui_state_init(&state, 0U);
    cr11_ui_state_stack_started(&state, false, false, 100U);
    assert(state.lease_armed);
    expect_led(&state, 100U, CR11_UI_COLOR_GREEN,
               CR11_UI_EFFECT_READY_HEARTBEAT);
    expect_led(&state, 200U, CR11_UI_COLOR_OFF,
               CR11_UI_EFFECT_READY_HEARTBEAT);

    cr11_ui_state_gateway_event(&state, false, 1000U);
    expect_led(&state, 1000U, CR11_UI_COLOR_BLUE,
               CR11_UI_EFFECT_UPPER_EVENT);
    expect_led(&state, 1099U, CR11_UI_COLOR_BLUE,
               CR11_UI_EFFECT_UPPER_EVENT);
    expect_led(&state, 1100U, CR11_UI_COLOR_OFF,
               CR11_UI_EFFECT_READY_HEARTBEAT);
    expect_led(&state, 5999U, CR11_UI_COLOR_OFF,
               CR11_UI_EFFECT_READY_HEARTBEAT);
    expect_led(&state, 6000U, CR11_UI_COLOR_GREEN,
               CR11_UI_EFFECT_READY_HEARTBEAT);

    cr11_ui_state_gateway_event(&state, true, 6050U);
    cr11_ui_state_lease_ping(&state, 6050U);
    expect_led(&state, 6050U, CR11_UI_COLOR_PINK,
               CR11_UI_EFFECT_LOWER_EVENT);
    expect_led(&state, 6150U, CR11_UI_COLOR_OFF,
               CR11_UI_EFFECT_READY_HEARTBEAT);
    expect_led(&state, 11050U, CR11_UI_COLOR_GREEN,
               CR11_UI_EFFECT_READY_HEARTBEAT);
}

static void test_network_restore_retry_and_recovery(void)
{
    cr11_ui_state_t state;
    cr11_ui_state_init(&state, 0U);
    cr11_ui_state_stack_retrying(&state, true, 100U);
    assert(state.phase == CR11_UI_PHASE_RESTORING);
    assert(state.phase_since_ms == 100U);
    assert(state.boot_was_watchdog);
    assert(!state.lease_armed);

    expect_led(&state, 100U, CR11_UI_COLOR_YELLOW,
               CR11_UI_EFFECT_RESTORING);
    expect_led(&state, 200U, CR11_UI_COLOR_OFF,
               CR11_UI_EFFECT_RESTORING);
    expect_led(&state, 300U, CR11_UI_COLOR_ORANGE,
               CR11_UI_EFFECT_RESTORING);
    expect_led(&state, 400U, CR11_UI_COLOR_OFF,
               CR11_UI_EFFECT_RESTORING);
    expect_led(&state, 2100U, CR11_UI_COLOR_YELLOW,
               CR11_UI_EFFECT_RESTORING);

    cr11_ui_state_stack_retrying(&state, true, 5100U);
    assert(state.phase_since_ms == 100U);
    expect_led(&state, 6100U, CR11_UI_COLOR_YELLOW,
               CR11_UI_EFFECT_RESTORING);

    cr11_ui_state_boot_input(&state, true, 6200U);
    expect_led(&state, 6200U, CR11_UI_COLOR_RED,
               CR11_UI_EFFECT_RESET_HOLD);
    cr11_ui_state_boot_input(&state, false, 6700U);
    assert(state.phase == CR11_UI_PHASE_RESTORING);
    assert(cr11_ui_state_take_actions(&state) == CR11_UI_ACTION_NONE);

    cr11_ui_state_stack_started(&state, false, true, 7000U);
    assert(state.phase == CR11_UI_PHASE_READY);
    assert(!state.lease_armed);
    expect_led(&state, 7000U, CR11_UI_COLOR_GREEN,
               CR11_UI_EFFECT_LEASE_UNARMED);

    cr11_ui_state_t reset_state;
    cr11_ui_state_init(&reset_state, 0U);
    cr11_ui_state_stack_retrying(&reset_state, false, 10U);
    start_reset(&reset_state, 100U);
}

static void start_reset(cr11_ui_state_t *state, uint64_t pressed_at)
{
    cr11_ui_state_boot_input(state, true, pressed_at);
    expect_led(state, pressed_at, CR11_UI_COLOR_RED,
               CR11_UI_EFFECT_RESET_HOLD);
    expect_led(state, pressed_at + 499U, CR11_UI_COLOR_RED,
               CR11_UI_EFFECT_RESET_HOLD);
    expect_led(state, pressed_at + 500U, CR11_UI_COLOR_OFF,
               CR11_UI_EFFECT_RESET_HOLD);
    expect_led(state, pressed_at + 2000U, CR11_UI_COLOR_RED,
               CR11_UI_EFFECT_RESET_HOLD);
    expect_led(state, pressed_at + 2250U, CR11_UI_COLOR_OFF,
               CR11_UI_EFFECT_RESET_HOLD);
    expect_led(state, pressed_at + 4000U, CR11_UI_COLOR_RED,
               CR11_UI_EFFECT_RESET_HOLD);
    expect_led(state, pressed_at + 4100U, CR11_UI_COLOR_OFF,
               CR11_UI_EFFECT_RESET_HOLD);
    cr11_ui_state_advance(state, pressed_at + 5000U);
    assert(state->phase == CR11_UI_PHASE_LEAVING);
    assert(cr11_ui_state_take_actions(state) ==
           CR11_UI_ACTION_BEGIN_LOCAL_RESET);
    expect_led(state, pressed_at + 5000U, CR11_UI_COLOR_RED,
               CR11_UI_EFFECT_LEAVE);
}

static void test_graceful_reset_fast(void)
{
    cr11_ui_state_t state;
    cr11_ui_state_init(&state, 0U);
    cr11_ui_state_stack_started(&state, false, false, 0U);
    start_reset(&state, 100U);

    cr11_ui_state_leave_succeeded(&state, 5300U);
    assert(state.phase == CR11_UI_PHASE_LEAVING);
    expect_led(&state, 6099U, CR11_UI_COLOR_RED,
               CR11_UI_EFFECT_LEAVE);
    cr11_ui_state_advance(&state, 6100U);
    assert(state.phase == CR11_UI_PHASE_RESULT_GRACEFUL);
    expect_led(&state, 6100U, CR11_UI_COLOR_GREEN,
               CR11_UI_EFFECT_GRACEFUL_RESULT);
    cr11_ui_state_advance(&state, 7100U);
    assert(state.phase == CR11_UI_PHASE_TERMINAL);
    expect_led(&state, 7100U, CR11_UI_COLOR_RED,
               CR11_UI_EFFECT_TERMINAL);
    expect_led(&state, 7200U, CR11_UI_COLOR_OFF,
               CR11_UI_EFFECT_TERMINAL);
    expect_led(&state, 7300U, CR11_UI_COLOR_RED,
               CR11_UI_EFFECT_TERMINAL);
    expect_led(&state, 7400U, CR11_UI_COLOR_OFF,
               CR11_UI_EFFECT_TERMINAL);
}

static void test_graceful_reset_slow(void)
{
    cr11_ui_state_t state;
    cr11_ui_state_init(&state, 0U);
    cr11_ui_state_stack_started(&state, false, false, 0U);
    start_reset(&state, 0U);

    cr11_ui_state_leave_succeeded(&state, 7000U);
    assert(state.phase == CR11_UI_PHASE_RESULT_GRACEFUL);
    assert(state.phase_since_ms == 7000U);
    expect_led(&state, 7000U, CR11_UI_COLOR_GREEN,
               CR11_UI_EFFECT_GRACEFUL_RESULT);
}

static void test_forced_reset(void)
{
    cr11_ui_state_t state;
    cr11_ui_state_init(&state, 0U);
    cr11_ui_state_stack_started(&state, false, false, 0U);
    start_reset(&state, 0U);

    cr11_ui_state_advance(&state, 7999U);
    assert(state.phase == CR11_UI_PHASE_LEAVING);
    cr11_ui_state_advance(&state, 8000U);
    assert(state.phase == CR11_UI_PHASE_FORCE_PENDING);
    assert(cr11_ui_state_take_actions(&state) ==
           CR11_UI_ACTION_ARM_FORCED_WIPE);
    expect_led(&state, 8000U, CR11_UI_COLOR_RED,
               CR11_UI_EFFECT_LEAVE);

    cr11_ui_state_forced_wipe_armed(&state, true, 8010U);
    assert(state.phase == CR11_UI_PHASE_RESULT_FORCED);
    expect_led(&state, 8010U, CR11_UI_COLOR_PURPLE,
               CR11_UI_EFFECT_FORCED_RESULT);
    cr11_ui_state_network_left(&state, false, 8500U);
    assert(state.phase == CR11_UI_PHASE_RESULT_FORCED);
    cr11_ui_state_advance(&state, 9010U);
    assert(state.phase == CR11_UI_PHASE_TERMINAL);

    cr11_ui_state_boot_input(&state, false, 9100U);
    cr11_ui_state_boot_input(&state, true, 9200U);
    cr11_ui_state_advance(&state, 20000U);
    assert(state.phase == CR11_UI_PHASE_TERMINAL);
    assert(cr11_ui_state_take_actions(&state) == CR11_UI_ACTION_NONE);
}

static void test_forced_wipe_error(void)
{
    cr11_ui_state_t state;
    cr11_ui_state_init(&state, 0U);
    cr11_ui_state_stack_started(&state, false, false, 0U);
    start_reset(&state, 0U);
    cr11_ui_state_advance(&state, 8000U);
    (void)cr11_ui_state_take_actions(&state);
    cr11_ui_state_forced_wipe_armed(&state, false, 8010U);
    assert(state.phase == CR11_UI_PHASE_ERROR);
    expect_led(&state, 20000U, CR11_UI_COLOR_RED,
               CR11_UI_EFFECT_ERROR);
}

static void test_monotonic_time(void)
{
    cr11_ui_state_t state;
    cr11_ui_state_init(&state, 1000U);
    cr11_ui_state_stack_started(&state, true, false, 900U);
    assert(state.last_now_ms == 1000U);
    assert(state.phase_since_ms == 1000U);
}

static void test_lease_unarmed_after_watchdog_boot(void)
{
    cr11_ui_state_t state;
    cr11_ui_state_init(&state, 0U);
    cr11_ui_state_stack_started(&state, false, true, 100U);
    assert(state.phase == CR11_UI_PHASE_READY);
    assert(state.boot_was_watchdog);
    assert(!state.lease_armed);
    assert(!cr11_ui_state_lease_wdt_due(&state, 60000U));

    expect_led_rgb(&state, 100U, CR11_UI_COLOR_GREEN,
                   CR11_UI_EFFECT_LEASE_UNARMED, 0U, 255U, 0U);
    expect_led(&state, 200U, CR11_UI_COLOR_OFF,
               CR11_UI_EFFECT_LEASE_UNARMED);
    expect_led_rgb(&state, 300U, CR11_UI_COLOR_ORANGE,
                   CR11_UI_EFFECT_LEASE_UNARMED, 255U, 127U, 0U);
    expect_led(&state, 400U, CR11_UI_COLOR_OFF,
               CR11_UI_EFFECT_LEASE_UNARMED);
    expect_led(&state, 3099U, CR11_UI_COLOR_OFF,
               CR11_UI_EFFECT_LEASE_UNARMED);
    expect_led(&state, 3100U, CR11_UI_COLOR_GREEN,
               CR11_UI_EFFECT_LEASE_UNARMED);

    cr11_ui_state_lease_ping(&state, 4000U);
    assert(state.lease_armed);
    assert(state.last_lease_ping_ms == 4000U);
    assert(!cr11_ui_state_lease_wdt_due(&state, 29199U));
    assert(cr11_ui_state_lease_wdt_due(&state, 29200U));
}

static void test_ready_finished_arms_lease(void)
{
    cr11_ui_state_t state;
    cr11_ui_state_init(&state, 0U);
    cr11_ui_state_stack_started(&state, false, true, 100U);
    assert(!state.lease_armed);

    cr11_ui_state_ready(&state, 500U);
    assert(state.lease_armed);
    assert(state.last_lease_ping_ms == 500U);
}

static void test_lease_ping_scope_and_network_loss(void)
{
    cr11_ui_state_t state;
    cr11_ui_state_init(&state, 0U);

    cr11_ui_state_lease_ping(&state, 10U);
    assert(!state.lease_armed);

    cr11_ui_state_stack_started(&state, true, false, 20U);
    cr11_ui_state_lease_ping(&state, 30U);
    assert(!state.lease_armed);

    cr11_ui_state_boot_input(&state, true, 100U);
    cr11_ui_state_advance(&state, 1100U);
    (void)cr11_ui_state_take_actions(&state);
    cr11_ui_state_joined(&state, 1200U);
    cr11_ui_state_lease_ping(&state, 1300U);
    assert(state.phase == CR11_UI_PHASE_POST_JOIN);
    assert(!state.lease_armed);

    cr11_ui_state_ready(&state, 1400U);
    assert(state.lease_armed);
    cr11_ui_state_network_left(&state, true, 1500U);
    assert(state.phase == CR11_UI_PHASE_RESTORING);
    assert(!state.lease_armed);
    cr11_ui_state_lease_ping(&state, 1600U);
    assert(!state.lease_armed);
}

static void test_lease_warning_gradient_and_recovery(void)
{
    cr11_ui_state_t state;
    cr11_ui_state_init(&state, 0U);
    cr11_ui_state_stack_started(&state, false, false, 0U);

    expect_led_rgb(&state, 10000U, CR11_UI_COLOR_LEASE_GRADIENT,
                   CR11_UI_EFFECT_LEASE_WARNING, 0U, 255U, 0U);
    expect_led(&state, 10100U, CR11_UI_COLOR_OFF,
               CR11_UI_EFFECT_LEASE_WARNING);
    expect_led_rgb(&state, 12000U, CR11_UI_COLOR_LEASE_GRADIENT,
                   CR11_UI_EFFECT_LEASE_WARNING, 25U, 243U, 0U);
    expect_led_rgb(&state, 20000U, CR11_UI_COLOR_LEASE_GRADIENT,
                   CR11_UI_EFFECT_LEASE_WARNING, 127U, 191U, 0U);
    expect_led_rgb(&state, 30000U, CR11_UI_COLOR_LEASE_GRADIENT,
                   CR11_UI_EFFECT_LEASE_WARNING, 255U, 127U, 0U);
    assert(!cr11_ui_state_lease_wdt_due(&state, 25199U));
    assert(cr11_ui_state_lease_wdt_due(&state, 25200U));

    cr11_ui_state_lease_ping(&state, 29000U);
    assert(!cr11_ui_state_lease_wdt_due(&state, 30000U));
    expect_led(&state, 30000U, CR11_UI_COLOR_GREEN,
               CR11_UI_EFFECT_READY_HEARTBEAT);
}

int main(void)
{
    test_join_and_release_latch();
    test_ready_events_and_heartbeat();
    test_network_restore_retry_and_recovery();
    test_graceful_reset_fast();
    test_graceful_reset_slow();
    test_forced_reset();
    test_forced_wipe_error();
    test_monotonic_time();
    test_lease_unarmed_after_watchdog_boot();
    test_ready_finished_arms_lease();
    test_lease_ping_scope_and_network_loss();
    test_lease_warning_gradient_and_recovery();
    puts("cr11_ui_state tests: OK");
    return 0;
}
