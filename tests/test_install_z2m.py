# SPDX-License-Identifier: MIT

from __future__ import annotations

import tempfile
import unittest
from datetime import UTC, datetime
from pathlib import Path

from scripts.install_z2m import SOURCE_LAYOUT, InstallError, install


class Zigbee2MqttInstallerTests(unittest.TestCase):
    def make_data_directory(self, root: Path) -> Path:
        data = root / "data"
        data.mkdir()
        (data / "configuration.yaml").write_text("advanced: {}\n", encoding="utf-8")
        return data

    def test_install_is_idempotent_and_checkable(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            data = self.make_data_directory(Path(temporary))
            output: list[str] = []
            self.assertTrue(install(data, output=output.append))
            self.assertTrue(install(data, check=True, output=output.append))
            for _, destination_relative in SOURCE_LAYOUT:
                self.assertTrue((data / destination_relative).is_file())

    def test_check_reports_different_file_without_modifying_it(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            data = self.make_data_directory(Path(temporary))
            install(data, output=lambda _: None)
            destination = data / SOURCE_LAYOUT[0][1]
            destination.write_text("local change\n", encoding="utf-8")
            output: list[str] = []
            self.assertFalse(install(data, check=True, output=output.append))
            self.assertEqual(destination.read_text(encoding="utf-8"), "local change\n")
            self.assertTrue(any(line.startswith("different:") for line in output))

    def test_replace_creates_private_backup(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            data = self.make_data_directory(Path(temporary))
            install(data, output=lambda _: None)
            destination = data / SOURCE_LAYOUT[0][1]
            destination.write_text("local change\n", encoding="utf-8")
            when = datetime(2026, 8, 27, 12, 0, tzinfo=UTC)
            install(data, replace=True, now=when, output=lambda _: None)
            backup = destination.with_name(destination.name + ".backup-20260827T120000Z")
            self.assertEqual(backup.read_text(encoding="utf-8"), "local change\n")
            self.assertEqual(backup.stat().st_mode & 0o777, 0o600)

    def test_symlink_destination_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            data = self.make_data_directory(root)
            destination = data / SOURCE_LAYOUT[0][1]
            destination.parent.mkdir(parents=True)
            target = root / "target.mjs"
            target.write_text("do not overwrite\n", encoding="utf-8")
            destination.symlink_to(target)
            with self.assertRaises(InstallError):
                install(data, replace=True, output=lambda _: None)
            self.assertEqual(target.read_text(encoding="utf-8"), "do not overwrite\n")


if __name__ == "__main__":
    unittest.main()
