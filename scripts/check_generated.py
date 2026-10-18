#!/usr/bin/env python3
"""Check committed outputs without rewriting the working tree.

SPDX-License-Identifier: GPL-2.0-or-later
"""

import runpy
import subprocess
import sys
import tempfile
from pathlib import Path


def main() -> None:
    grid = runpy.run_path("keymap_grid.py")
    keymap = Path("KEYMAP.md").read_text(encoding="utf-8")
    if grid["rewrite"](keymap) != keymap:
        sys.exit("KEYMAP.md diagrams are stale; run make keymap-doc")
    with tempfile.TemporaryDirectory(prefix="t9-generated-") as directory:
        header = Path(directory) / "dictionary.h"
        subprocess.run(
            [
                sys.executable,
                "dictionary/build_t9_dictionary.py",
                "--input",
                "dictionary/words_en.tsv",
                "--packed",
                str(header),
                "--max-bytes",
                "150000",
                "--quiet",
            ],
            check=True,
        )
        if header.read_bytes() != Path("keymap/t9_dictionary_data.h").read_bytes():
            sys.exit("Packed dictionary is stale; run make dict")
    print("Keymap diagrams and packed dictionary match their generators.")


if __name__ == "__main__":
    main()
