# SPDX-License-Identifier: MIT

from __future__ import annotations

import json
import tempfile
import unittest
from pathlib import Path

from scripts.build_release import (
    ReleaseError,
    build_configuration,
    compare_release_directories,
)


class ReleaseBuilderTests(unittest.TestCase):
    def write_configuration(self, root: Path, **overrides: object) -> None:
        config = root / "config"
        config.mkdir()
        values: dict[str, object] = {
            "CR11_BRIDGE_PRIMARY_CHANNEL_MASK": 0x07FFF800,
            "IDF_TARGET": "esp32c6",
            "ESPTOOLPY_FLASHSIZE": "8MB",
            "ZB_ZED": True,
            "APP_REPRODUCIBLE_BUILD": True,
        }
        values.update(overrides)
        (config / "sdkconfig.json").write_text(
            json.dumps(values) + "\n",
            encoding="utf-8",
        )

    def test_accepts_the_supported_release_configuration(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            build = Path(temporary)
            self.write_configuration(build)
            self.assertEqual(
                build_configuration(build)["CR11_BRIDGE_PRIMARY_CHANNEL_MASK"],
                0x07FFF800,
            )

    def test_rejects_a_single_channel_release(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            build = Path(temporary)
            self.write_configuration(
                build,
                CR11_BRIDGE_PRIMARY_CHANNEL_MASK=1 << 25,
            )
            with self.assertRaisesRegex(ReleaseError, "all Zigbee channels"):
                build_configuration(build)

    def test_rejects_a_router_or_wrong_target(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            build = Path(temporary)
            self.write_configuration(build, ZB_ZED=False)
            with self.assertRaisesRegex(ReleaseError, "End Device"):
                build_configuration(build)

        with tempfile.TemporaryDirectory() as temporary:
            build = Path(temporary)
            self.write_configuration(build, IDF_TARGET="esp32")
            with self.assertRaisesRegex(ReleaseError, "esp32c6"):
                build_configuration(build)

    def test_rejects_non_reproducible_release_configuration(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            build = Path(temporary)
            self.write_configuration(build, APP_REPRODUCIBLE_BUILD=False)
            with self.assertRaisesRegex(ReleaseError, "reproducible"):
                build_configuration(build)

    def test_release_comparison_ignores_readme_but_detects_binary_drift(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            generated = root / "generated"
            committed = root / "committed"
            generated.mkdir()
            committed.mkdir()
            (generated / "firmware.bin").write_bytes(b"same")
            (committed / "firmware.bin").write_bytes(b"same")
            (committed / "README.md").write_text("Explanation.\n", encoding="utf-8")
            compare_release_directories(generated, committed)
            (committed / "firmware.bin").write_bytes(b"different")
            with self.assertRaisesRegex(ReleaseError, "differs from this build"):
                compare_release_directories(generated, committed)


if __name__ == "__main__":
    unittest.main()
