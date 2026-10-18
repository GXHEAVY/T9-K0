#!/usr/bin/env python3
"""Format hand-written source, excluding generated dictionaries and fixtures.

SPDX-License-Identifier: GPL-2.0-or-later
"""

import argparse
import subprocess
from pathlib import Path


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tools", type=Path, default=Path(".venv-dev/bin"))
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    generated = {"t9_dictionary_data.h", "t9_fixture_table.h", "t9_fixture_packed.h"}
    c_files = sorted(
        str(path)
        for root in (Path("keymap"), Path("host"))
        for path in root.rglob("*")
        if path.suffix in {".c", ".h"} and path.name not in generated and "build" not in path.parts
    )
    python_files = ["keymap_grid.py", "dictionary", "scripts", "tests"]
    commands = [
        [str(args.tools / "clang-format"), *(["--dry-run", "--Werror"] if args.check else ["-i"]), *c_files],
        [str(args.tools / "ruff"), "check", *([] if args.check else ["--fix"]), *python_files],
        [str(args.tools / "ruff"), "format", *(["--check"] if args.check else []), *python_files],
    ]
    failed = False
    for command in commands:
        failed |= subprocess.run(command).returncode != 0
    raise SystemExit(int(failed))


if __name__ == "__main__":
    main()
