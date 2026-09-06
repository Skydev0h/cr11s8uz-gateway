# SPDX-License-Identifier: MIT

from __future__ import annotations

import json
import tempfile
import unittest
from pathlib import Path

from scripts.build_release import (
    BOARD_PROFILES,
    DEFAULT_BOARD,
    ReleaseError,
    board_profile,
    build_configuration,
    compare_release_directories,
    release_directory,
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
            "CR11_UI_RGB_ORDER_GRB": True,
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

    def test_rejects_a_foreign_board_led_colour_order(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            build = Path(temporary)
            self.write_configuration(build, CR11_UI_RGB_ORDER_GRB=False)
            with self.assertRaisesRegex(ReleaseError, "LED colour order"):
                build_configuration(build)

    def test_c6zero_profile_requires_the_rgb_colour_order(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            build = Path(temporary)
            self.write_configuration(
                build,
                CR11_UI_RGB_ORDER_GRB=False,
                CR11_UI_RGB_ORDER_RGB=True,
            )
            build_configuration(build, "c6zero")
            with self.assertRaisesRegex(ReleaseError, "GRB LED colour order"):
                build_configuration(build, DEFAULT_BOARD)

    def test_unknown_board_profile_is_rejected(self) -> None:
        with self.assertRaisesRegex(ReleaseError, "Unknown board profile"):
            board_profile("nosuchboard")

    def test_each_profile_has_a_distinct_release_directory(self) -> None:
        names = {release_directory("9.9.9", name).name for name in BOARD_PROFILES}
        self.assertEqual(len(names), len(BOARD_PROFILES))
        self.assertEqual(release_directory("9.9.9", DEFAULT_BOARD).name, "v9.9.9")

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
