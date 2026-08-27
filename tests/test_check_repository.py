# SPDX-License-Identifier: MIT

from __future__ import annotations

import tempfile
import unittest
from pathlib import Path

from scripts.check_repository import scan_repository


class RepositoryPolicyTests(unittest.TestCase):
    def test_clean_repository_passes(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "README.md").write_text("English text only.\n", encoding="utf-8")
            self.assertEqual(scan_repository(root), [])

    def test_language_policy_rejects_disallowed_text_without_literal_fixture(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "README.md").write_text("prefix " + chr(0x0410) + " suffix\n", encoding="utf-8")
            issues = scan_repository(root)
            self.assertTrue(any("project language policy" in issue for issue in issues))

    def test_language_policy_rejects_disallowed_path(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            filename = "prefix-" + chr(0x0410) + ".md"
            (root / filename).write_text("English contents.\n", encoding="utf-8")
            issues = scan_repository(root)
            self.assertTrue(any("project language policy" in issue for issue in issues))

    def test_runtime_configuration_and_network_secret_are_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "configuration.yaml").write_text(
                "network" + "_key: [1, 2, 3]\n",
                encoding="utf-8",
            )
            issues = scan_repository(root)
            self.assertTrue(any("forbidden runtime" in issue for issue in issues))
            self.assertTrue(any("possible Zigbee network secret" in issue for issue in issues))

    def test_generated_network_key_example_is_allowed(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "example.yaml").write_text("network" + "_key: GENERATE\n", encoding="utf-8")
            self.assertEqual(scan_repository(root), [])

    def test_private_marker_in_binary_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            marker = ("/home/" + "skydev").encode("utf-8")
            (root / "firmware.bin").write_bytes(b"prefix\x00" + marker + b"\x00suffix")
            issues = scan_repository(root)
            self.assertTrue(any("binary contains private" in issue for issue in issues))


if __name__ == "__main__":
    unittest.main()
