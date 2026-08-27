#!/usr/bin/env python3
# SPDX-License-Identifier: MIT

"""Flash the tested CR11Gateway firmware with checksum verification."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import shlex
import subprocess
import sys
from pathlib import Path
from typing import Callable

if __package__:
    from .build_release import project_version
else:
    from build_release import project_version


REPOSITORY_ROOT = Path(__file__).resolve().parents[1]
FIRMWARE_VERSION = project_version()
RELEASE_DIRECTORY = REPOSITORY_ROOT / "firmware" / "prebuilt" / f"v{FIRMWARE_VERSION}"
FACTORY_IMAGE = f"cr11s8uz-gateway-v{FIRMWARE_VERSION}.factory.bin"
APPLICATION_IMAGE = f"cr11s8uz-gateway-v{FIRMWARE_VERSION}.app.bin"
BINARY_FILES = {FACTORY_IMAGE, APPLICATION_IMAGE, "bootloader.bin", "partition-table.bin"}
REQUIRED_FILES = {*BINARY_FILES, "manifest.json"}
CHECKSUM_LINE = re.compile(r"^([0-9a-f]{64})  ([A-Za-z0-9][A-Za-z0-9._-]*)$")
ALLOWED_BAUD_RATES = (115200, 230400, 460800, 921600)
EXPECTED_ZIGBEE_CONFIGURATION = {
    "commissioning_channel_mask": "0x07fff800",
    "commissioning_channels": list(range(11, 27)),
    "commissioning_mode": "bdb-network-steering",
    "role": "always-on-end-device",
}


class FlashError(RuntimeError):
    """Release validation or flashing failed."""


def file_sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def load_checksums(release_directory: Path = RELEASE_DIRECTORY) -> dict[str, str]:
    checksum_path = release_directory / "SHA256SUMS"
    checksums: dict[str, str] = {}
    for line_number, line in enumerate(checksum_path.read_text(encoding="ascii").splitlines(), start=1):
        match = CHECKSUM_LINE.fullmatch(line)
        if not match:
            raise FlashError(f"Malformed SHA256SUMS line {line_number}")
        digest, filename = match.groups()
        if filename in checksums:
            raise FlashError(f"Duplicate checksum entry: {filename}")
        checksums[filename] = digest
    return checksums


def verify_release(release_directory: Path = RELEASE_DIRECTORY) -> dict[str, object]:
    checksums = load_checksums(release_directory)
    if set(checksums) != REQUIRED_FILES:
        missing = sorted(REQUIRED_FILES - set(checksums))
        extra = sorted(set(checksums) - REQUIRED_FILES)
        raise FlashError(f"Unexpected checksum manifest; missing={missing}, extra={extra}")
    for filename, expected in checksums.items():
        path = release_directory / filename
        if not path.is_file() or path.is_symlink():
            raise FlashError(f"Missing or unsafe release artifact: {path}")
        actual = file_sha256(path)
        if actual != expected:
            raise FlashError(f"Checksum mismatch for {filename}: expected {expected}, got {actual}")

    manifest_path = release_directory / "manifest.json"
    try:
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise FlashError("Release manifest is not valid JSON") from error
    if not isinstance(manifest, dict):
        raise FlashError("Release manifest must be a JSON object")
    if (
        manifest.get("schema") != 2
        or manifest.get("project") != "cr11s8uz-gateway"
        or manifest.get("version") != FIRMWARE_VERSION
        or manifest.get("target") != "esp32c6"
    ):
        raise FlashError("Release manifest identity does not match this flasher")
    flash = manifest.get("flash")
    expected_offsets = {
        "bootloader.bin": "0x0",
        "partition-table.bin": "0x8000",
        APPLICATION_IMAGE: "0x10000",
        FACTORY_IMAGE: "0x0",
    }
    if not isinstance(flash, dict) or (
        flash.get("mode") != "dio"
        or flash.get("frequency") != "80m"
        or flash.get("size") != "8MB"
        or flash.get("offsets") != expected_offsets
    ):
        raise FlashError("Release manifest flash layout does not match this flasher")
    zigbee = manifest.get("zigbee")
    if zigbee != EXPECTED_ZIGBEE_CONFIGURATION:
        raise FlashError("Release manifest has an invalid Zigbee configuration")
    binary_hashes = manifest.get("sha256")
    if not isinstance(binary_hashes, dict) or set(binary_hashes) != BINARY_FILES:
        raise FlashError("Release manifest has an unexpected binary hash set")
    for filename in BINARY_FILES:
        if binary_hashes.get(filename) != checksums[filename]:
            raise FlashError(f"Release manifest hash mismatch for {filename}")
    return manifest


def validate_port(port: str) -> str:
    if not port or len(port) > 512 or any(ord(character) < 32 for character in port):
        raise FlashError("Serial port must be a non-empty path or COM name without control characters")
    return port


def esptool_prefix(port: str, baud: int) -> list[str]:
    return [
        sys.executable,
        "-m",
        "esptool",
        "--chip",
        "esp32c6",
        "--port",
        validate_port(port),
        "--baud",
        str(baud),
        "--before",
        "default-reset",
        "--after",
        "hard-reset",
    ]


def flash_commands(mode: str, port: str, baud: int) -> list[list[str]]:
    prefix = esptool_prefix(port, baud)
    flash_options = ["--flash-mode", "dio", "--flash-freq", "80m", "--flash-size", "8MB"]
    if mode == "factory":
        return [[
            *prefix,
            "write-flash",
            "--erase-all",
            *flash_options,
            "0x0",
            str(RELEASE_DIRECTORY / FACTORY_IMAGE),
        ]]
    if mode == "upgrade":
        return [[
            *prefix,
            "write-flash",
            *flash_options,
            "0x10000",
            str(RELEASE_DIRECTORY / APPLICATION_IMAGE),
        ]]
    raise FlashError(f"Unsupported flash mode: {mode}")


def confirm_factory(input_function: Callable[[str], str] = input) -> None:
    response = input_function(
        "Factory mode erases the entire ESP32-C6 flash, including its saved Zigbee network. Type ERASE to continue: ",
    )
    if response != "ERASE":
        raise FlashError("Factory flash cancelled")


def parse_arguments(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mode", choices=("factory", "upgrade"))
    parser.add_argument("--port", required=True, help="stable serial path or COM port for the USB-to-UART connector")
    parser.add_argument("--baud", type=int, choices=ALLOWED_BAUD_RATES, default=460800)
    parser.add_argument("--yes", action="store_true", help="skip the destructive factory confirmation")
    parser.add_argument("--dry-run", action="store_true", help="verify files and print commands without opening hardware")
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_arguments(argv)
    try:
        manifest = verify_release()
        commands = flash_commands(args.mode, args.port, args.baud)
        if args.mode == "factory" and not args.yes and not args.dry_run:
            confirm_factory()
        for command in commands:
            print(shlex.join(command))
            if not args.dry_run:
                subprocess.run(command, check=True)
    except (FlashError, OSError, subprocess.CalledProcessError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1
    print(
        f"CR11Gateway firmware v{FIRMWARE_VERSION} "
        f"{'plan verified' if args.dry_run else 'flashed successfully'} "
        "with all-channel Zigbee commissioning (11-26).",
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
