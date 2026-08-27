#!/usr/bin/env python3
# SPDX-License-Identifier: MIT

"""Read and validate the CR11Gateway native-USB event stream."""

from __future__ import annotations

import argparse
import binascii
import json
import os
import select
import sys
import termios
import tty
from collections.abc import Sequence


FRAME_PREFIX = b"CR11:"
PACKET_SIZE = 32
PACKET_HEX_SIZE = PACKET_SIZE * 2
CRC_HEX_SIZE = 4
FRAME_BODY_SIZE = len(FRAME_PREFIX) + PACKET_HEX_SIZE + 1 + CRC_HEX_SIZE
MAX_BUFFERED_LINE = 128
UPPER_HEX = frozenset(b"0123456789ABCDEF")

ACTION_NAMES = ("unknown", "click", "hold", "release")
EVENT_NAMES = (
    "unknown",
    "malformed",
    "on_off.off",
    "on_off.on",
    "on_off.toggle",
    "level.move_to",
    "level.move",
    "level.step",
    "level.stop",
    "window.open",
    "window.close",
    "window.stop",
    "window.goto_lift_value",
    "window.goto_lift_percentage",
)
DIRECTION_NAMES = ("none", "up", "down")
SOURCE_MODE_NAMES = ("unknown", "short", "extended", "group")


class FrameError(ValueError):
    """Raised when a native-USB line is not a valid CR11Gateway frame."""


def _address_text(mode: int, value: int) -> str:
    if mode in (1, 3):
        return f"0x{value:04x}"
    if mode == 2:
        return f"0x{value:016x}"
    return "unknown"


def decode_frame_line(line: bytes) -> dict[str, object]:
    """Validate one bounded ASCII frame and return its decoded v1 fields."""

    body = line
    if body.endswith(b"\n"):
        body = body[:-1]
    if body.endswith(b"\r"):
        body = body[:-1]

    if len(body) != FRAME_BODY_SIZE:
        raise FrameError(f"frame_length:{len(body)}")
    if not body.startswith(FRAME_PREFIX):
        raise FrameError("frame_prefix")

    separator = len(FRAME_PREFIX) + PACKET_HEX_SIZE
    if body[separator : separator + 1] != b":":
        raise FrameError("crc_separator")

    packet_hex = body[len(FRAME_PREFIX) : separator]
    crc_hex = body[separator + 1 :]
    if any(byte not in UPPER_HEX for byte in packet_hex + crc_hex):
        raise FrameError("non_uppercase_hex")

    packet = bytes.fromhex(packet_hex.decode("ascii"))
    received_crc = int(crc_hex, 16)
    calculated_crc = binascii.crc_hqx(packet, 0xFFFF)
    if received_crc != calculated_crc:
        raise FrameError(
            f"crc:received={received_crc:04X},calculated={calculated_crc:04X}"
        )
    if packet[0] != 1:
        raise FrameError(f"packet_version:{packet[0]}")
    if packet[1] != PACKET_SIZE:
        raise FrameError(f"declared_length:{packet[1]}")

    action = packet[2]
    event = packet[3]
    direction = packet[4]
    flags = packet[5]
    selector = packet[6]
    button = packet[7]
    source_mode = packet[8]
    if action >= len(ACTION_NAMES):
        raise FrameError(f"action:{action}")
    if event == 0 or event >= len(EVENT_NAMES):
        raise FrameError(f"event:{event}")
    if direction >= len(DIRECTION_NAMES):
        raise FrameError(f"direction:{direction}")
    if flags & ~0x0F:
        raise FrameError(f"reserved_flags:{flags:02X}")
    if selector > 4:
        raise FrameError(f"selector:{selector}")
    if button > 8:
        raise FrameError(f"button:{button}")
    if source_mode >= len(SOURCE_MODE_NAMES):
        raise FrameError(f"source_mode:{source_mode}")

    source_address = int.from_bytes(packet[20:28], "little")
    if source_mode in (1, 3) and source_address > 0xFFFF:
        raise FrameError(f"short_address_range:{source_address}")
    forwardable = bool(flags & 0x08)
    if forwardable and (action == 0 or button == 0):
        raise FrameError("forwardable_semantics")

    return {
        "valid": True,
        "v": packet[0],
        "seq": int.from_bytes(packet[28:32], "little"),
        "source": {
            "mode": SOURCE_MODE_NAMES[source_mode],
            "address": _address_text(source_mode, source_address),
            "endpoint": packet[9],
        },
        "target_endpoint": packet[10],
        "selector": selector,
        "button": button,
        "action": ACTION_NAMES[action],
        "event": EVENT_NAMES[event],
        "event_code": event,
        "direction": DIRECTION_NAMES[direction],
        "direction_inferred": bool(flags & 0x04),
        "profile_id": int.from_bytes(packet[12:14], "little"),
        "cluster_id": int.from_bytes(packet[14:16], "little"),
        "command_id": packet[16],
        "zcl_tsn": packet[11],
        "rssi": int.from_bytes(packet[17:18], "little", signed=True),
        "rssi_scope": "gateway_last_hop",
        "has_value": bool(flags & 0x01),
        "value": int.from_bytes(packet[18:20], "little"),
        "with_on_off": bool(flags & 0x02),
        "forwardable": forwardable,
        "packet_hex": packet_hex.decode("ascii"),
        "crc16_ccitt_false": crc_hex.decode("ascii"),
    }


def monitor(device: str, *, raw: bool, once: bool) -> int:
    flags = os.O_RDONLY | os.O_NONBLOCK | os.O_NOCTTY
    if hasattr(os, "O_CLOEXEC"):
        flags |= os.O_CLOEXEC

    try:
        descriptor = os.open(device, flags)
    except OSError as error:
        print(f"cannot open {device}: {error}", file=sys.stderr)
        return 2

    original_attributes = None
    buffer = bytearray()
    discarding = False
    try:
        original_attributes = termios.tcgetattr(descriptor)
        tty.setraw(descriptor, when=termios.TCSANOW)
        while True:
            readable, _, _ = select.select([descriptor], [], [], 1.0)
            if not readable:
                continue
            chunk = os.read(descriptor, 4096)
            if not chunk:
                print("native USB device disconnected", file=sys.stderr)
                return 2

            for byte in chunk:
                if byte == 0x0A:
                    if discarding:
                        print('{"valid":false,"error":"line_too_long"}',
                              file=sys.stderr, flush=True)
                    else:
                        try:
                            decoded = decode_frame_line(bytes(buffer) + b"\n")
                        except FrameError as error:
                            print(
                                json.dumps(
                                    {"valid": False, "error": str(error)},
                                    separators=(",", ":"),
                                ),
                                file=sys.stderr,
                                flush=True,
                            )
                        else:
                            if raw:
                                print((bytes(buffer) + b"\n").decode("ascii"),
                                      end="", flush=True)
                            else:
                                print(json.dumps(decoded, separators=(",", ":")),
                                      flush=True)
                            if once:
                                return 0
                    buffer.clear()
                    discarding = False
                elif discarding:
                    continue
                elif len(buffer) >= MAX_BUFFERED_LINE:
                    buffer.clear()
                    discarding = True
                else:
                    buffer.append(byte)
    except KeyboardInterrupt:
        return 0
    except OSError as error:
        print(f"native USB read failed: {error}", file=sys.stderr)
        return 2
    finally:
        if original_attributes is not None:
            try:
                termios.tcsetattr(descriptor, termios.TCSANOW,
                                  original_attributes)
            except OSError:
                pass
        os.close(descriptor)


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="Validate and decode CR11Gateway native-USB event frames",
    )
    parser.add_argument("device", help="native USB CDC device or stable by-id path")
    parser.add_argument("--raw", action="store_true",
                        help="print validated wire lines instead of decoded JSON")
    parser.add_argument("--once", action="store_true",
                        help="exit successfully after the first valid frame")
    arguments = parser.parse_args(argv)
    return monitor(arguments.device, raw=arguments.raw, once=arguments.once)


if __name__ == "__main__":
    raise SystemExit(main())
