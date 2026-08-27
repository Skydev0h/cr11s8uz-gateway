/*
 * SPDX-License-Identifier: MIT
 */

#include "cr11_usb_frame.h"

#include <string.h>

static const char HEX_DIGITS[] = "0123456789ABCDEF";

uint16_t cr11_usb_frame_crc16_ccitt_false(const uint8_t *data,
                                          size_t data_length)
{
    uint16_t crc = 0xffffU;
    if (data == NULL && data_length != 0U) {
        return crc;
    }

    for (size_t index = 0; index < data_length; ++index) {
        crc ^= (uint16_t)data[index] << 8U;
        for (unsigned int bit = 0; bit < 8U; ++bit) {
            crc = (crc & 0x8000U) != 0U
                      ? (uint16_t)((crc << 1U) ^ 0x1021U)
                      : (uint16_t)(crc << 1U);
        }
    }
    return crc;
}
static void append_hex_byte(char *output, size_t *offset, uint8_t value)
{
    output[(*offset)++] = HEX_DIGITS[value >> 4U];
    output[(*offset)++] = HEX_DIGITS[value & 0x0fU];
}

bool cr11_usb_frame_format(const uint8_t *packet,
                           size_t packet_length,
                           char *output,
                           size_t output_size)
{
    if (packet == NULL || output == NULL ||
        packet_length != CR11_GATEWAY_PACKET_SIZE ||
        output_size < CR11_USB_FRAME_BUFFER_SIZE ||
        packet[0] != CR11_GATEWAY_PACKET_VERSION ||
        packet[1] != CR11_GATEWAY_PACKET_SIZE) {
        return false;
    }

    size_t offset = 0U;
    memcpy(output, CR11_USB_FRAME_PREFIX, CR11_USB_FRAME_PREFIX_SIZE);
    offset += CR11_USB_FRAME_PREFIX_SIZE;

    for (size_t index = 0; index < packet_length; ++index) {
        append_hex_byte(output, &offset, packet[index]);
    }
    output[offset++] = ':';

    const uint16_t crc =
        cr11_usb_frame_crc16_ccitt_false(packet, packet_length);
    append_hex_byte(output, &offset, (uint8_t)(crc >> 8U));
    append_hex_byte(output, &offset, (uint8_t)(crc & 0xffU));
    output[offset++] = '\n';
    output[offset] = '\0';
    return offset == CR11_USB_FRAME_TEXT_SIZE;
}
