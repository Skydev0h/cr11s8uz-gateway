# SPDX-License-Identifier: MIT

from __future__ import annotations

import hashlib
import json
import tempfile
import unittest
from pathlib import Path

from scripts.build_release import DEFAULT_BOARD, project_version
from scripts.flash_firmware import (
    APPLICATION_IMAGE,
    EXPECTED_RGB_ORDER,
    EXPECTED_ZIGBEE_CONFIGURATION,
    FACTORY_IMAGE,
    FIRMWARE_VERSION,
    REQUIRED_FILES,
    FlashError,
    confirm_factory,
    flash_commands,
    validate_port,
    verify_release,
)


class FirmwareFlasherTests(unittest.TestCase):
    def test_flasher_version_comes_from_project_metadata(self) -> None:
        self.assertEqual(FIRMWARE_VERSION, project_version())

    def write_checksums(self, root: Path) -> None:
        checksums = []
        for filename in sorted(REQUIRED_FILES):
            payload = (root / filename).read_bytes()
            checksums.append(f"{hashlib.sha256(payload).hexdigest()}  {filename}\n")
        (root / "SHA256SUMS").write_text("".join(checksums), encoding="ascii")

    def make_release(self, root: Path) -> None:
        binary_names = sorted(REQUIRED_FILES - {"manifest.json"})
        binary_hashes = {}
        for index, filename in enumerate(binary_names):
            payload = f"artifact-{index}\n".encode("ascii")
            (root / filename).write_bytes(payload)
            binary_hashes[filename] = hashlib.sha256(payload).hexdigest()
        manifest = {
            "schema": 2,
            "project": "cr11s8uz-gateway",
            "version": FIRMWARE_VERSION,
            "target": "esp32c6",
            "zigbee": EXPECTED_ZIGBEE_CONFIGURATION,
            "rgb_order": EXPECTED_RGB_ORDER,
            "board_profile": DEFAULT_BOARD,
            "flash": {
                "mode": "dio",
                "frequency": "80m",
                "size": "8MB",
                "offsets": {
                    "bootloader.bin": "0x0",
                    "partition-table.bin": "0x8000",
                    APPLICATION_IMAGE: "0x10000",
                    FACTORY_IMAGE: "0x0",
                },
            },
            "sha256": binary_hashes,
        }
        (root / "manifest.json").write_text(
            json.dumps(manifest, sort_keys=True) + "\n",
            encoding="utf-8",
        )
        self.write_checksums(root)

    def test_release_verification_detects_tampering(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            release = Path(temporary)
            self.make_release(release)
            verify_release(release)
            (release / sorted(REQUIRED_FILES)[0]).write_bytes(b"tampered")
            with self.assertRaisesRegex(FlashError, "Checksum mismatch"):
                verify_release(release)

    def test_factory_requires_exact_confirmation(self) -> None:
        confirm_factory(lambda _: "ERASE")
        with self.assertRaisesRegex(FlashError, "cancelled"):
            confirm_factory(lambda _: "erase")

    def test_manifest_configuration_is_enforced_after_hash_validation(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            release = Path(temporary)
            self.make_release(release)
            manifest_path = release / "manifest.json"
            manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
            manifest["zigbee"]["commissioning_channel_mask"] = "0x02000000"
            manifest_path.write_text(json.dumps(manifest) + "\n", encoding="utf-8")
            self.write_checksums(release)
            with self.assertRaisesRegex(FlashError, "Zigbee configuration"):
                verify_release(release)

    def test_manifest_from_a_foreign_board_build_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            release = Path(temporary)
            self.make_release(release)
            manifest_path = release / "manifest.json"
            manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
            manifest["rgb_order"] = "RGB"
            manifest_path.write_text(json.dumps(manifest) + "\n", encoding="utf-8")
            self.write_checksums(release)
            with self.assertRaisesRegex(FlashError, "LED colour order"):
                verify_release(release)

    def test_manifest_for_another_board_profile_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            release = Path(temporary)
            self.make_release(release)
            manifest_path = release / "manifest.json"
            manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
            manifest["board_profile"] = "c6zero"
            manifest_path.write_text(json.dumps(manifest) + "\n", encoding="utf-8")
            self.write_checksums(release)
            with self.assertRaisesRegex(FlashError, "board profile"):
                verify_release(release)

    def test_commands_are_argument_arrays_with_expected_offsets(self) -> None:
        factory = flash_commands("factory", "/dev/ttyACM0", 460800)
        upgrade = flash_commands("upgrade", "/dev/ttyACM0", 460800)
        self.assertEqual(len(factory), 1)
        self.assertIn("--erase-all", factory[0])
        self.assertEqual(factory[0][-2], "0x0")
        self.assertEqual(upgrade[0][-2], "0x10000")
        self.assertNotIn("shell", factory[0])

    def test_control_characters_in_port_are_rejected(self) -> None:
        self.assertEqual(validate_port("/dev/serial/by-id/example"), "/dev/serial/by-id/example")
        with self.assertRaises(FlashError):
            validate_port("/dev/ttyACM0\n--help")


if __name__ == "__main__":
    unittest.main()
