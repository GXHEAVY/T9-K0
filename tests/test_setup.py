"""Setup recovery and preservation of existing work, using local Git fixtures.

SPDX-License-Identifier: GPL-2.0-or-later
"""

import importlib.util
import subprocess
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

SPEC = importlib.util.spec_from_file_location("t9_setup", Path(__file__).resolve().parents[1] / "scripts/setup.py")
setup = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(setup)


class SetupTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="t9-setup-test-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.origin = self.root / "origin"
        self.origin.mkdir()
        self.git(self.origin, "init", "-q")
        (self.origin / "source.c").write_text("original\n")
        self.git(self.origin, "add", "source.c")
        self.git(
            self.origin,
            "-c",
            "user.name=Setup Test",
            "-c",
            "user.email=test@example.invalid",
            "commit",
            "-qm",
            "fixture",
        )
        self.commit = self.git(self.origin, "rev-parse", "HEAD")
        self.checkout = self.root / "qmk"

    @staticmethod
    def git(path, *args):
        return subprocess.check_output(["git", "-C", str(path), *args], text=True).strip()

    def prepare(self):
        setup.ensure_qmk(self.checkout, str(self.origin), self.commit, [])

    def test_empty_directory_and_repeated_setup(self):
        self.checkout.mkdir()
        self.prepare()
        # Repeating successful setup must not contact the remote.
        with patch.object(setup, "run", wraps=setup.run) as run:
            self.prepare()
        self.assertFalse(any("fetch" in call.args for call in run.call_args_list))
        self.assertEqual(self.git(self.checkout, "rev-parse", "HEAD"), self.commit)

    def test_interrupted_checkout_after_git_init(self):
        self.checkout.mkdir()
        self.git(self.checkout, "init", "-q")
        self.prepare()
        self.assertEqual((self.checkout / "source.c").read_text(), "original\n")

    def test_modified_checkout_is_preserved(self):
        self.prepare()
        source = self.checkout / "source.c"
        source.write_text("local work\n")
        with self.assertRaisesRegex(ValueError, "tracked changes"):
            self.prepare()
        self.assertEqual(source.read_text(), "local work\n")

    def test_wrong_commit_is_preserved(self):
        self.prepare()
        with self.assertRaisesRegex(ValueError, "HEAD is"):
            setup.ensure_qmk(self.checkout, str(self.origin), "0" * 40, [])
        self.assertEqual(self.git(self.checkout, "rev-parse", "HEAD"), self.commit)

    def test_non_repository_directory_is_preserved(self):
        self.checkout.mkdir()
        source = self.checkout / "notes.txt"
        source.write_text("local work")
        with self.assertRaisesRegex(ValueError, "nonempty"):
            self.prepare()
        self.assertEqual(source.read_text(), "local work")

    def test_wrong_origin_is_preserved(self):
        self.prepare()
        self.git(self.checkout, "remote", "set-url", "origin", "unexpected-origin")
        with self.assertRaisesRegex(ValueError, "origin is"):
            self.prepare()
        self.assertEqual(self.git(self.checkout, "remote", "get-url", "origin"), "unexpected-origin")

    def test_link_relocation_and_directory_protection(self):
        source = self.root / "keymap"
        source.mkdir()
        link = self.root / "link"
        link.symlink_to(self.root / "old-keymap")
        setup.ensure_link(link, source)
        setup.ensure_link(link, source)
        self.assertEqual(link.resolve(), source.resolve())
        link.unlink()
        link.mkdir()
        with self.assertRaisesRegex(ValueError, "not a symlink"):
            setup.ensure_link(link, source)
        self.assertTrue(link.is_dir())

    def test_empty_venv_directory_is_repaired(self):
        path = self.root / "venv"
        path.mkdir()
        setup.ensure_venv(path, [])
        setup.ensure_venv(path, [])
        subprocess.run([str(path / "bin/python"), "-m", "pip", "check"], check=True)

    def test_venv_interrupted_before_pip_is_repaired(self):
        path = self.root / "venv"
        setup.run(setup.sys.executable, "-m", "venv", "--without-pip", str(path))
        setup.ensure_venv(path, [])
        subprocess.run([str(path / "bin/python"), "-m", "pip", "check"], check=True)


if __name__ == "__main__":
    unittest.main()
