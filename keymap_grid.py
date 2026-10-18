"""Render the per-layer key grids in KEYMAP.md.

Each grid sits between `<!-- grid:NAME -->` and `<!-- /grid -->` markers and is
rewritten from the LAYERS table below. Edit a layer here, run `make keymap-doc`.

SPDX-License-Identifier: GPL-2.0-or-later
"""

import re
import sys
from pathlib import Path

CELL_WIDTH = 9
LINES_PER_ROW = 3

# Physical rows of LAYOUT_tenkey_27, left to right. "+" and "Ent" are 2u tall
# (they continue into the next row), "0" is 2u wide.
ROWS = [
    ["knob", "Esc", "Del", "Tab", "Bksp"],
    ["M1", "Num", "/", "*", "-"],
    ["M2", "7", "8", "9", "+"],
    ["M3", "4", "5", "6", "+"],
    ["M4", "1", "2", "3", "Ent"],
    ["Fn", "0", ".", "Ent"],
]
TALL_KEYS = {"+", "Ent"}
WIDE_KEYS = {"0"}

# Per layer: key -> action lines under the legend. Tall keys may use up to five
# lines, the rest two. Missing keys render blank.
LAYERS = {
    "t9": {
        "knob": ["cand/wd", "p: mute"],
        "Esc": ["cancel", "word"],
        "Del": ["Delete"],
        "Tab": ["Tab"],
        "Bksp": ["delete", "word"],
        "M1": ["tap:MC1", "h:chords"],
        "Num": ["mode", "h: NumLk"],
        "/": ["tap:caps", "h:chords"],
        "*": ["next", "cand"],
        "-": ["delete", "last"],
        "M2": ["macro"],
        "7": ["1", ".,?!'-"],
        "8": ["2 abc"],
        "9": ["3 def"],
        "+": ["h: spell", "word"],
        "M3": ["macro"],
        "4": ["4 ghi"],
        "5": ["5 jkl"],
        "6": ["6 mno"],
        "M4": ["macro"],
        "1": ["7 pqrs"],
        "2": ["8 tuv"],
        "3": ["9 wxyz"],
        "Ent": ["complete", "word", "else", "Enter"],
        "Fn": ["hold: FN"],
        "0": ["accept + space", "hold: 0"],
        ".": ["accept", "no space"],
    },
    "multitap": {
        "knob": ["<L/R>", "p: mute"],
        "Esc": ["cancel", "word"],
        "Del": ["Delete"],
        "Tab": ["Tab"],
        "Bksp": ["delete", "word"],
        "M1": ["tap:MC1", "h:chords"],
        "Num": ["mode", "h: NumLk"],
        "/": ["tap:caps", "h:chords"],
        "*": ["next", "punct"],
        "-": ["delete", "last"],
        "M2": ["macro"],
        "7": ["1", ".,?!'-"],
        "8": ["2 abc"],
        "9": ["3 def"],
        "+": ["next", "letter"],
        "M3": ["macro"],
        "4": ["4 ghi"],
        "5": ["5 jkl"],
        "6": ["6 mno"],
        "M4": ["macro"],
        "1": ["7 pqrs"],
        "2": ["8 tuv"],
        "3": ["9 wxyz"],
        "Ent": ["Enter"],
        "Fn": ["hold: FN"],
        "0": ["accept + space", "hold: 0"],
        ".": ["accept", "no space"],
    },
    "slash": {
        "knob": ["<Up/Dn>", "p: quit"],
        "Esc": ["undo", "C+Z"],
        "Del": ["cut", "C+X"],
        "Tab": ["copy", "C+C"],
        "Bksp": ["paste", "C+V"],
        "M1": ["(none)"],
        "Num": ["previous", "mode"],
        "/": ["held"],
        "*": ["back tab", "ShiftTab"],
        "-": ["Tab"],
        "M2": ["(none)"],
        "7": ["(none)"],
        "8": ["(none)"],
        "9": ["(none)"],
        "+": ["switch", "app", "Alt/Cmd", "+Tab"],
        "M3": ["(none)"],
        "4": ["(none)"],
        "5": ["(none)"],
        "6": ["(none)"],
        "M4": ["(none)"],
        "1": ["(none)"],
        "2": ["(none)"],
        "3": ["(none)"],
        "Ent": ["VS Code", "terminal", "Ctrl+`"],
        "Fn": ["(none)"],
        "0": ["(none)"],
        ".": ["close", "C+W"],
    },
    "fn": {
        "knob": ["<bright>", "p: RGB"],
        "Esc": ["BT 1"],
        "Del": ["BT 2"],
        "Tab": ["BT 3"],
        "Bksp": ["2.4 GHz"],
        "M1": ["BOOT", "(DFU)"],
        "Num": ["effect", "next"],
        "/": ["bright +"],
        "*": ["hue +"],
        "-": ["(base)"],
        "M2": ["(base)"],
        "7": ["effect", "prev"],
        "8": ["bright -"],
        "9": ["hue -"],
        "+": ["(base)"],
        "M3": ["(base)"],
        "4": ["sat +"],
        "5": ["speed +"],
        "6": ["prev", "track"],
        "M4": ["(base)"],
        "1": ["sat -"],
        "2": ["speed -"],
        "3": ["play/", "pause"],
        "Ent": ["word", "complete", "on/off"],
        "Fn": ["(held)"],
        "0": ["RGB on/off"],
        ".": ["next", "track"],
    },
}


def legend(key):
    return "(knob)" if key == "knob" else f"[{key}]"


def key_lines(layer, key):
    lines = [legend(key), *layer.get(key, [])]
    height = LINES_PER_ROW * (2 if key in TALL_KEYS else 1)
    width = CELL_WIDTH * (2 if key in WIDE_KEYS else 1) + (1 if key in WIDE_KEYS else 0) - 1
    too_long = [line for line in lines if len(line) > width]
    if len(lines) > height or too_long:
        sys.exit(f"key {key!r} does not fit its cell: {lines}")
    return [f" {line:<{width}}" for line in lines + [""] * (height - len(lines))]


def cell_slice(layer, key, row_index):
    continues = key in TALL_KEYS and row_index > 0 and key in ROWS[row_index - 1]
    start = LINES_PER_ROW if continues else 0
    return key_lines(layer, key)[start : start + LINES_PER_ROW]


def separator(row_index):
    """Border below row_index (-1 = top edge); open where a tall key continues."""

    def segment(key):
        below = ROWS[row_index + 1] if 0 <= row_index < len(ROWS) - 1 else []
        width = CELL_WIDTH * (2 if key in WIDE_KEYS else 1) + (1 if key in WIDE_KEYS else 0)
        return " " * width if key in TALL_KEYS and key in below and row_index >= 0 else "-" * width

    keys = ROWS[max(row_index, 0)] if row_index < len(ROWS) - 1 else ROWS[-1]
    joined = "+".join(segment(key) for key in keys)
    return f"+{joined}" + ("|" if joined.endswith(" ") else "+")


def render(layer):
    def row_block(row_index, keys):
        cells = [cell_slice(layer, key, row_index) for key in keys]
        text = ["|" + "|".join(cell[line] for cell in cells) + "|" for line in range(LINES_PER_ROW)]
        return text + [separator(row_index)]

    blocks = [row_block(index, keys) for index, keys in enumerate(ROWS)]
    return "\n".join([separator(-1), *[line for block in blocks for line in block]])


def rewrite(markdown):
    def replace(match):
        name = match.group(1)
        if name not in LAYERS:
            sys.exit(f"unknown grid {name!r}; known: {', '.join(LAYERS)}")
        return f"<!-- grid:{name} -->\n```\n{render(LAYERS[name])}\n```\n<!-- /grid -->"

    return re.sub(r"<!-- grid:(\w+) -->.*?<!-- /grid -->", replace, markdown, flags=re.S)


if __name__ == "__main__":
    path = Path(sys.argv[1] if len(sys.argv) > 1 else "KEYMAP.md")
    path.write_text(rewrite(path.read_text()))
