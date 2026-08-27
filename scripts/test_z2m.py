#!/usr/bin/env python3
# SPDX-License-Identifier: MIT

"""Run the Zigbee2MQTT runtime tests in the exact supported container image."""

from __future__ import annotations

import subprocess
from pathlib import Path


REPOSITORY_ROOT = Path(__file__).resolve().parents[1]
ZIGBEE2MQTT_IMAGE = (
    "ghcr.io/koenkk/zigbee2mqtt:2.13.0"
    "@sha256:4fb4db4d49a217bed6d0204f454ce809febc565bbe051b85c556ee2bcef73d8c"
)
TESTS = (
    "/app/cr11-tests/zigbee2mqtt/tests/test_cr11s8uz_external.mjs",
    "/app/cr11-tests/zigbee2mqtt/tests/test_cr11_gateway_external.mjs",
    "/app/cr11-tests/zigbee2mqtt/tests/test_cr11_gateway_extension.mjs",
)


def command() -> list[str]:
    return [
        "docker",
        "run",
        "--rm",
        "--read-only",
        "--network",
        "none",
        "--cap-drop",
        "ALL",
        "--security-opt",
        "no-new-privileges:true",
        "--tmpfs",
        "/tmp:size=32m,mode=1777",
        "--user",
        "1000:1000",
        "--volume",
        f"{REPOSITORY_ROOT}:/app/cr11-tests:ro",
        "--workdir",
        "/app",
        "--entrypoint",
        "node",
        ZIGBEE2MQTT_IMAGE,
        "--test",
        *TESTS,
    ]


def main() -> int:
    try:
        subprocess.run(command(), check=True)
    except (OSError, subprocess.CalledProcessError) as error:
        print(f"Zigbee2MQTT runtime tests failed: {error}")
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
