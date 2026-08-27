#!/usr/bin/env python3
# SPDX-License-Identifier: MIT

import unittest

from tools.cr11_usb_monitor import FrameError, decode_frame_line


SAMPLE_FRAME = (
    b"CR11:012001040008010101010A4D0401060002D500005634"
    b"00000000000001000000:1276\n"
)


class UsbMonitorTest(unittest.TestCase):
    def test_decodes_live_frame(self) -> None:
        decoded = decode_frame_line(SAMPLE_FRAME)
        self.assertTrue(decoded["valid"])
        self.assertEqual(decoded["seq"], 1)
        self.assertEqual(decoded["button"], 1)
        self.assertEqual(decoded["action"], "click")
        self.assertEqual(decoded["event"], "on_off.toggle")
        self.assertEqual(decoded["target_endpoint"], 10)
        self.assertEqual(decoded["source"]["address"], "0x3456")
        self.assertEqual(decoded["rssi"], -43)
        self.assertEqual(decoded["crc16_ccitt_false"], "1276")

    def test_accepts_crlf_transport(self) -> None:
        decoded = decode_frame_line(SAMPLE_FRAME[:-1] + b"\r\n")
        self.assertEqual(decoded["seq"], 1)

    def test_rejects_bad_crc(self) -> None:
        with self.assertRaisesRegex(FrameError, "crc:"):
            decode_frame_line(SAMPLE_FRAME[:-5] + b"0000\n")

    def test_rejects_lowercase_hex(self) -> None:
        with self.assertRaisesRegex(FrameError, "non_uppercase_hex"):
            decode_frame_line(SAMPLE_FRAME.replace(b"D5", b"d5"))

    def test_rejects_wrong_length(self) -> None:
        with self.assertRaisesRegex(FrameError, "frame_length"):
            decode_frame_line(SAMPLE_FRAME[:-2] + b"\n")


if __name__ == "__main__":
    unittest.main()
