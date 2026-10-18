"""Malformed dictionary inputs must fail clearly before writing an output file.

SPDX-License-Identifier: GPL-2.0-or-later
"""

import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

PACKER = Path(__file__).resolve().parents[1] / "dictionary/build_t9_dictionary.py"


class DictionaryCliTests(unittest.TestCase):
    def test_invalid_scores_and_limits(self):
        cases = [
            ("nan", [], "zipf must be finite"),
            ("inf", [], "zipf must be finite"),
            ("-inf", [], "zipf must be finite"),
            ("2.0", ["--max-words", "-1"], "--max-words must be at least 1"),
            ("2.0", ["--max-bytes", "-1"], "--max-bytes must be at least 1"),
            ("2.0", ["--min-zipf", "nan"], "--min-zipf must be finite"),
        ]
        with tempfile.TemporaryDirectory(prefix="t9-dictionary-test-") as directory:
            source, output = Path(directory) / "words.tsv", Path(directory) / "packed.h"
            for score, options, message in cases:
                with self.subTest(score=score, options=options):
                    source.write_text(f"hello\t{score}\n")
                    result = subprocess.run(
                        [sys.executable, str(PACKER), "--input", str(source), "--packed", str(output), *options],
                        capture_output=True,
                        text=True,
                    )
                    self.assertNotEqual(result.returncode, 0)
                    self.assertIn(message, result.stderr)
                    self.assertNotIn("Traceback", result.stderr)
                    self.assertFalse(output.exists())


if __name__ == "__main__":
    unittest.main()
