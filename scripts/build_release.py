#!/usr/bin/env python3
# SPDX-License-Identifier: MIT

"""Create a checksummed prebuilt release from an ESP-IDF build directory."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path


REPOSITORY_ROOT = Path(__file__).resolve().parents[1]
VERSION_PATTERN = re.compile(r"^[0-9]+\.[0-9]+\.[0-9]+$")
SOURCE_ARTIFACTS = {
    "bootloader.bin": Path("bootloader/bootloader.bin"),
    "partition-table.bin": Path("partition_table/partition-table.bin"),
}
RELEASE_COMMISSIONING_CHANNEL_MASK = 0x07FFF800
RELEASE_COMMISSIONING_CHANNELS = tuple(range(11, 27))
RELEASE_COMPARISON_IGNORES = {"README.md"}


class ReleaseError(RuntimeError):
    """The build cannot be converted into a safe release."""


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def project_version() -> str:
    cmake = (REPOSITORY_ROOT / "firmware" / "CMakeLists.txt").read_text(encoding="utf-8")
    match = re.search(r'set\(PROJECT_VER\s+"([0-9]+\.[0-9]+\.[0-9]+)"\)', cmake)
    if not match:
        raise ReleaseError("Unable to read PROJECT_VER from firmware/CMakeLists.txt")
    return match.group(1)


def build_configuration(build: Path) -> dict[str, object]:
    path = build / "config" / "sdkconfig.json"
    try:
        configuration = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise ReleaseError(f"Unable to read build configuration: {path}") from error

    channel_mask = configuration.get("CR11_BRIDGE_PRIMARY_CHANNEL_MASK")
    if (
        not isinstance(channel_mask, int)
        or isinstance(channel_mask, bool)
        or channel_mask != RELEASE_COMMISSIONING_CHANNEL_MASK
    ):
        raise ReleaseError(
            "Release build must scan all Zigbee channels during commissioning; "
            f"found {channel_mask!r} in {path}"
        )
    if configuration.get("IDF_TARGET") != "esp32c6":
        raise ReleaseError("Release build target must be esp32c6")
    if configuration.get("ESPTOOLPY_FLASHSIZE") != "8MB":
        raise ReleaseError("Release build flash size must be 8MB")
    if configuration.get("ZB_ZED") is not True:
        raise ReleaseError("Release build must use the Zigbee End Device library")
    if configuration.get("APP_REPRODUCIBLE_BUILD") is not True:
        raise ReleaseError("Release build must enable reproducible ESP-IDF output")
    return configuration


def release_file_hashes(directory: Path) -> dict[str, str]:
    release = directory.expanduser().resolve(strict=True)
    if not release.is_dir():
        raise ReleaseError(f"Release path is not a directory: {release}")

    hashes: dict[str, str] = {}
    for path in sorted(release.iterdir()):
        if path.name in RELEASE_COMPARISON_IGNORES:
            continue
        if path.is_symlink() or not path.is_file():
            raise ReleaseError(f"Unexpected or unsafe release entry: {path}")
        hashes[path.name] = sha256(path)
    return hashes


def compare_release_directories(generated: Path, committed: Path) -> None:
    generated_hashes = release_file_hashes(generated)
    committed_hashes = release_file_hashes(committed)
    if generated_hashes.keys() != committed_hashes.keys():
        missing = sorted(generated_hashes.keys() - committed_hashes.keys())
        extra = sorted(committed_hashes.keys() - generated_hashes.keys())
        raise ReleaseError(
            f"Prebuilt release file set differs; missing={missing}, extra={extra}"
        )
    mismatches = sorted(
        name
        for name, digest in generated_hashes.items()
        if committed_hashes[name] != digest
    )
    if mismatches:
        raise ReleaseError(f"Prebuilt release differs from this build: {mismatches}")


def build_release(build_directory: Path, output_directory: Path) -> None:
    build = build_directory.expanduser().resolve(strict=True)
    if not build.is_dir():
        raise ReleaseError(f"Build path is not a directory: {build}")
    output = output_directory.expanduser().resolve(strict=False)
    if output.exists():
        raise ReleaseError(f"Output directory already exists: {output}")
    if output == Path(output.anchor):
        raise ReleaseError("Refusing to use a filesystem root as output")

    version = project_version()
    if not VERSION_PATTERN.fullmatch(version):
        raise ReleaseError(f"Invalid project version: {version}")
    configuration = build_configuration(build)
    app_source = build / "cr11_zigbee_bridge.bin"
    sources = {name: build / relative for name, relative in SOURCE_ARTIFACTS.items()}
    sources[f"cr11s8uz-gateway-v{version}.app.bin"] = app_source
    for source in sources.values():
        if not source.is_file() or source.is_symlink():
            raise ReleaseError(f"Missing or unsafe build artifact: {source}")

    output.parent.mkdir(mode=0o755, parents=True, exist_ok=True)
    staging = Path(tempfile.mkdtemp(prefix=f".v{version}.", dir=output.parent))
    try:
        for destination_name, source in sources.items():
            shutil.copyfile(source, staging / destination_name)

        factory_name = f"cr11s8uz-gateway-v{version}.factory.bin"
        factory = staging / factory_name
        subprocess.run(
            [
                sys.executable,
                "-m",
                "esptool",
                "--chip",
                "esp32c6",
                "merge-bin",
                "-o",
                str(factory),
                "--flash-mode",
                "dio",
                "--flash-freq",
                "80m",
                "--flash-size",
                "8MB",
                "0x0",
                str(staging / "bootloader.bin"),
                "0x8000",
                str(staging / "partition-table.bin"),
                "0x10000",
                str(staging / f"cr11s8uz-gateway-v{version}.app.bin"),
            ],
            check=True,
        )

        binary_names = sorted([*sources, factory_name])
        binary_hashes = {name: sha256(staging / name) for name in binary_names}
        manifest = {
            "schema": 2,
            "project": "cr11s8uz-gateway",
            "version": version,
            "target": "esp32c6",
            "tested_board": "ESP32-C6-DevKitC-1 v1.2 with 8 MB flash",
            "zigbee": {
                "commissioning_channel_mask": (
                    f"0x{RELEASE_COMMISSIONING_CHANNEL_MASK:08x}"
                ),
                "commissioning_channels": RELEASE_COMMISSIONING_CHANNELS,
                "commissioning_mode": "bdb-network-steering",
                "role": "always-on-end-device",
            },
            "toolchain": {
                "esp_idf": "5.5.4",
                "esp_zigbee_lib": "2.0.3",
                "led_strip": "3.0.3",
                "reproducible_build": True,
            },
            "flash": {
                "mode": "dio",
                "frequency": "80m",
                "size": "8MB",
                "offsets": {
                    "bootloader.bin": "0x0",
                    "partition-table.bin": "0x8000",
                    f"cr11s8uz-gateway-v{version}.app.bin": "0x10000",
                    factory_name: "0x0",
                },
            },
            "sha256": binary_hashes,
        }
        manifest_path = staging / "manifest.json"
        manifest_path.write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8")

        checksum_names = sorted([*binary_names, "manifest.json"])
        checksum_text = "".join(f"{sha256(staging / name)}  {name}\n" for name in checksum_names)
        (staging / "SHA256SUMS").write_text(checksum_text, encoding="ascii")
        os.rename(staging, output)
    except Exception:
        shutil.rmtree(staging, ignore_errors=True)
        raise


def verify_current_release(build_directory: Path) -> Path:
    version = project_version()
    committed = REPOSITORY_ROOT / "firmware" / "prebuilt" / f"v{version}"
    with tempfile.TemporaryDirectory(prefix="cr11-release-verify-") as temporary:
        generated = Path(temporary) / f"v{version}"
        build_release(build_directory, generated)
        compare_release_directories(generated, committed)
    return committed


def parse_arguments(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", default="firmware/build", type=Path)
    output = parser.add_mutually_exclusive_group()
    output.add_argument("--output-dir", type=Path)
    output.add_argument(
        "--verify-current",
        action="store_true",
        help="rebuild and compare byte-for-byte with the checked-in current release",
    )
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_arguments(argv)
    version = project_version()
    try:
        if args.verify_current:
            output = verify_current_release(args.build_dir)
        else:
            output = args.output_dir or REPOSITORY_ROOT / "firmware" / "prebuilt" / f"v{version}"
            build_release(args.build_dir, output)
    except (ReleaseError, OSError, subprocess.CalledProcessError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1
    action = "verified" if args.verify_current else "created"
    print(f"Release {action} at {output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
