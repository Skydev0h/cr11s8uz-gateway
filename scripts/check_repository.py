#!/usr/bin/env python3
# SPDX-License-Identifier: MIT

"""Enforce the repository's public-release policy."""

from __future__ import annotations

import re
import sys
from pathlib import Path


REPOSITORY_ROOT = Path(__file__).resolve().parents[1]
EXCLUDED_DIRECTORIES = {
    ".git",
    ".venv",
    "__pycache__",
    "build",
    "build-host",
    "dist",
    "managed_components",
}
FORBIDDEN_FILENAMES = {
    ".env",
    "configuration.yaml",
    "coordinator_backup.json",
    "database.db",
    "state.json",
}
TEXT_FILENAMES = {
    ".editorconfig",
    ".gitattributes",
    ".gitignore",
    "Dockerfile",
    "LICENSE",
    "Makefile",
}
TEXT_SUFFIXES = {
    ".c",
    ".csv",
    ".css",
    ".defaults",
    ".h",
    ".html",
    ".ini",
    ".js",
    ".json",
    ".jsx",
    ".lock",
    ".md",
    ".mjs",
    ".py",
    ".sh",
    ".toml",
    ".ts",
    ".tsx",
    ".txt",
    ".xml",
    ".yaml",
    ".yml",
}
DISALLOWED_SCRIPT = re.compile(
    "[\u0400-\u052f\u1c80-\u1c8f\u2de0-\u2dff\ua640-\ua69f]",
)
PRIVATE_MARKERS = (
    "/home/" + "skydev",
    "zb-" + "buttonchik",
)
ZIGBEE_SECRET = re.compile(
    r"(?im)^[ \t]*(?:network_key|pan_id|ext_pan_id)[ \t]*:[ \t]*(.+)$",
)


def is_excluded(path: Path, root: Path) -> bool:
    relative = path.relative_to(root)
    return any(part in EXCLUDED_DIRECTORIES for part in relative.parts)


def is_text_file(path: Path) -> bool:
    return path.name in TEXT_FILENAMES or path.suffix.lower() in TEXT_SUFFIXES


def scan_repository(root: Path = REPOSITORY_ROOT) -> list[str]:
    root = root.resolve(strict=True)
    issues: list[str] = []

    for path in sorted(root.rglob("*")):
        if is_excluded(path, root):
            continue
        relative = path.relative_to(root)
        if DISALLOWED_SCRIPT.search(str(relative)):
            issues.append(f"{relative}: path violates the project language policy")
        if path.is_symlink():
            issues.append(f"{relative}: symbolic links are not allowed")
            continue
        if path.is_dir():
            continue
        if path.name in FORBIDDEN_FILENAMES:
            issues.append(f"{relative}: forbidden runtime or secret-bearing filename")
        if not is_text_file(path):
            data = path.read_bytes()
            for marker in PRIVATE_MARKERS:
                if marker.encode("utf-8") in data:
                    issues.append(
                        f"{relative}: binary contains private development path marker {marker!r}"
                    )
            continue

        try:
            text = path.read_text(encoding="utf-8")
        except UnicodeDecodeError:
            issues.append(f"{relative}: declared text file is not valid UTF-8")
            continue

        for line_number, line in enumerate(text.splitlines(), start=1):
            if DISALLOWED_SCRIPT.search(line):
                issues.append(
                    f"{relative}:{line_number}: text violates the project language policy"
                )
            for marker in PRIVATE_MARKERS:
                if marker in line:
                    issues.append(f"{relative}:{line_number}: private development path marker {marker!r}")
        secret_values = (
            match.group(1).split("#", 1)[0].strip()
            for match in ZIGBEE_SECRET.finditer(text)
        )
        if any(value != "GENERATE" for value in secret_values):
            issues.append(f"{relative}: possible Zigbee network secret or identifier")

    return issues


def main() -> int:
    issues = scan_repository()
    if issues:
        for issue in issues:
            print(issue, file=sys.stderr)
        return 1
    print("Repository policy check passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
