/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#include "cr11_gateway_packet.h"

esp_err_t cr11_usb_transport_init(void);
esp_err_t cr11_usb_transport_submit(const uint8_t *packet,
                                    size_t packet_length);
