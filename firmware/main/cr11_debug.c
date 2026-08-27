/*
 * SPDX-License-Identifier: MIT
 */

#include "cr11_debug.h"

#include <stdatomic.h>

static atomic_bool debug_enabled = false;

bool cr11_debug_is_enabled(void)
{
    return atomic_load_explicit(&debug_enabled, memory_order_relaxed);
}
void cr11_debug_set_enabled(bool enabled)
{
    atomic_store_explicit(&debug_enabled, enabled, memory_order_relaxed);
}
