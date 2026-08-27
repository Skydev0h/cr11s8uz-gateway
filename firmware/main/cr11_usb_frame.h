/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "cr11_gateway_packet.h"

#define CR11_USB_FRAME_PREFIX "CR11:"
#define CR11_USB_FRAME_PREFIX_SIZE (sizeof(CR11_USB_FRAME_PREFIX) - 1U)
#define CR11_USB_FRAME_HEX_SIZE (CR11_GATEWAY_PACKET_SIZE * 2U)
#define CR11_USB_FRAME_CRC_HEX_SIZE 4U
#define CR11_USB_FRAME_TEXT_SIZE \
    (CR11_USB_FRAME_PREFIX_SIZE + CR11_USB_FRAME_HEX_SIZE + 1U + \
     CR11_USB_FRAME_CRC_HEX_SIZE + 1U)
#define CR11_USB_FRAME_BUFFER_SIZE (CR11_USB_FRAME_TEXT_SIZE + 1U)

uint16_t cr11_usb_frame_crc16_ccitt_false(const uint8_t *data,
                                          size_t data_length);

bool cr11_usb_frame_format(const uint8_t *packet,
                           size_t packet_length,
                           char *output,
                           size_t output_size);
