#!/usr/bin/env python3
# SPDX-License-Identifier: MIT

"""Install the CR11 integration into an existing Zigbee2MQTT data directory."""

from __future__ import annotations

import argparse
import os
import shutil
import sys
import tempfile
from datetime import UTC, datetime
from pathlib import Path
from typing import Callable


REPOSITORY_ROOT = Path(__file__).resolve().parents[1]
SOURCE_LAYOUT = (
    ("zigbee2mqtt/cr11s8uz_external.mjs", "external_converters/cr11s8uz_external.mjs"),
    ("zigbee2mqtt/cr11_gateway_external.mjs", "external_converters/cr11_gateway_external.mjs"),
    (
        "zigbee2mqtt/cr11_gateway_router_extension.mjs",
        "external_extensions/cr11_gateway_router_extension.mjs",
    ),
)


class InstallError(RuntimeError):
    """A safe installation precondition was not met."""


def resolve_data_directory(value: str | Path) -> Path:
    candidate = Path(value).expanduser().resolve(strict=True)
    if not candidate.is_dir():
        raise InstallError(f"Zigbee2MQTT data path is not a directory: {candidate}")
    if candidate == Path(candidate.anchor):
        raise InstallError("Refusing to use a filesystem root as the Zigbee2MQTT data directory")
    configuration = candidate / "configuration.yaml"
    if not configuration.is_file():
        raise InstallError(f"configuration.yaml was not found in {candidate}")
    return candidate


def identical(left: Path, right: Path) -> bool:
    return left.read_bytes() == right.read_bytes()


def atomic_copy(source: Path, destination: Path) -> None:
    temporary_name: str | None = None
    try:
        with tempfile.NamedTemporaryFile(
            mode="wb",
            prefix=f".{destination.name}.",
            suffix=".tmp",
            dir=destination.parent,
            delete=False,
        ) as temporary:
            temporary_name = temporary.name
            with source.open("rb") as source_file:
                shutil.copyfileobj(source_file, temporary)
            temporary.flush()
            os.fsync(temporary.fileno())
        os.chmod(temporary_name, 0o644)
        os.replace(temporary_name, destination)
        temporary_name = None
    finally:
        if temporary_name is not None:
            Path(temporary_name).unlink(missing_ok=True)


def backup_file(source: Path, timestamp: str) -> Path:
    backup = source.with_name(f"{source.name}.backup-{timestamp}")
    try:
        with backup.open("xb") as output, source.open("rb") as current:
            shutil.copyfileobj(current, output)
    except FileExistsError as error:
        raise InstallError(f"Backup already exists: {backup}") from error
    os.chmod(backup, 0o600)
    return backup


def install(
    data_directory: str | Path,
    *,
    replace: bool = False,
    dry_run: bool = False,
    check: bool = False,
    now: datetime | None = None,
    output: Callable[[str], None] = print,
) -> bool:
    data = resolve_data_directory(data_directory)
    operations: list[tuple[Path, Path, str]] = []

    for source_relative, destination_relative in SOURCE_LAYOUT:
        source = (REPOSITORY_ROOT / source_relative).resolve(strict=True)
        destination = data / destination_relative
        parent = destination.parent

        if parent.exists() and (parent.is_symlink() or not parent.is_dir()):
            raise InstallError(f"Destination directory is unsafe: {parent}")
        if destination.is_symlink():
            raise InstallError(f"Refusing to replace a symbolic link: {destination}")
        if destination.exists() and not destination.is_file():
            raise InstallError(f"Destination is not a regular file: {destination}")

        if not destination.exists():
            status = "missing" if check else "install"
        elif identical(source, destination):
            status = "current"
        elif check:
            status = "different"
        elif replace:
            status = "replace"
        else:
            raise InstallError(
                f"A different file already exists at {destination}; rerun with --replace to back it up",
            )
        operations.append((source, destination, status))

    if check:
        current = all(status == "current" for _, _, status in operations)
        for _, destination, status in operations:
            output(f"{status}: {destination}")
        return current

    if dry_run:
        for _, destination, status in operations:
            output(f"{status}: {destination}")
        return True

    timestamp = (now or datetime.now(UTC)).strftime("%Y%m%dT%H%M%SZ")
    for source, destination, status in operations:
        if status == "current":
            output(f"current: {destination}")
            continue
        destination.parent.mkdir(mode=0o755, parents=True, exist_ok=True)
        if status == "replace":
            backup = backup_file(destination, timestamp)
            output(f"backup: {backup}")
        atomic_copy(source, destination)
        output(f"installed: {destination}")
    return True


def parse_arguments(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--data-dir", required=True, help="directory containing Zigbee2MQTT configuration.yaml")
    parser.add_argument("--replace", action="store_true", help="back up and replace different managed files")
    parser.add_argument("--dry-run", action="store_true", help="show the planned file operations")
    parser.add_argument("--check", action="store_true", help="verify that all installed files match this checkout")
    args = parser.parse_args(argv)
    if args.check and (args.replace or args.dry_run):
        parser.error("--check cannot be combined with --replace or --dry-run")
    return args


def main(argv: list[str] | None = None) -> int:
    args = parse_arguments(argv)
    try:
        success = install(
            args.data_dir,
            replace=args.replace,
            dry_run=args.dry_run,
            check=args.check,
        )
    except (InstallError, OSError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1

    if args.check:
        return 0 if success else 1
    if not args.dry_run:
        print("Enable advanced.enable_external_js, restart Zigbee2MQTT, and verify bridge/converters and bridge/extensions.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
