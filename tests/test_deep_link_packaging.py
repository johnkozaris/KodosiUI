from __future__ import annotations

import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class DeepLinkPackagingTests(unittest.TestCase):
    def test_desktop_registration_is_exact(self) -> None:
        desktop = (
            ROOT / "packaging/linux/com.kodosi.Kodosi.desktop"
        ).read_text(encoding="utf-8").splitlines()
        self.assertEqual(desktop.count("Exec=kodosi-qt %u"), 1)
        self.assertEqual(
            desktop.count("MimeType=x-scheme-handler/kodosi;"),
            1,
        )

    def test_all_packages_install_and_verify_registration(self) -> None:
        cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
        self.assertIn(
            "FILES packaging/linux/com.kodosi.Kodosi.desktop",
            cmake,
        )
        self.assertIn("desktop-file-utils", cmake)

        arch = (ROOT / "scripts/write-arch-package.sh").read_text(
            encoding="utf-8"
        )
        self.assertIn("depend = desktop-file-utils", arch)

        installed = (
            ROOT / "scripts/verify-installed-package-content.sh"
        ).read_text(encoding="utf-8")
        self.assertIn("Exec=kodosi-qt %u", installed)
        self.assertIn("MimeType=x-scheme-handler/kodosi;", installed)

        deb = (ROOT / "scripts/verify-linux-package.sh").read_text(
            encoding="utf-8"
        )
        self.assertIn("update-desktop-database", deb)
        self.assertIn("x-scheme-handler/kodosi", deb)


if __name__ == "__main__":
    unittest.main()
