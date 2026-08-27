/*
 * SPDX-License-Identifier: MIT
 */

#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "cr11_usb_frame.h"

int main(void)
{
    static const uint8_t crc_vector[] = "123456789";
    assert(cr11_usb_frame_crc16_ccitt_false(
               crc_vector, sizeof(crc_vector) - 1U) == 0x29b1U);

    uint8_t packet[CR11_GATEWAY_PACKET_SIZE] = {
        0x01, 0x20, 0x01, 0x04, 0x00, 0x08, 0x01, 0x01,
        0x01, 0x01, 0x0a, 0x14, 0x04, 0x01, 0x06, 0x00,
        0x02, 0xcf, 0x00, 0x00, 0x3d, 0xb1, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
    };
    assert(cr11_usb_frame_crc16_ccitt_false(packet, sizeof(packet)) ==
           0x1428U);

    char output[CR11_USB_FRAME_BUFFER_SIZE];
    assert(cr11_usb_frame_format(packet, sizeof(packet),
                                 output, sizeof(output)));
    assert(strcmp(
               output,
               "CR11:012001040008010101010A140401060002CF00003DB1"
               "00000000000001000000:1428\n") == 0);
    assert(strlen(output) == CR11_USB_FRAME_TEXT_SIZE);

    char too_small[CR11_USB_FRAME_TEXT_SIZE];
    assert(!cr11_usb_frame_format(packet, sizeof(packet),
                                  too_small, sizeof(too_small)));
    assert(!cr11_usb_frame_format(packet, sizeof(packet) - 1U,
                                  output, sizeof(output)));
    assert(!cr11_usb_frame_format(NULL, sizeof(packet),
                                  output, sizeof(output)));

    packet[0] = 2U;
    assert(!cr11_usb_frame_format(packet, sizeof(packet),
                                  output, sizeof(output)));
    packet[0] = CR11_GATEWAY_PACKET_VERSION;
    packet[1] = 31U;
    assert(!cr11_usb_frame_format(packet, sizeof(packet),
                                  output, sizeof(output)));
    return 0;
}
