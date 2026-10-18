#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Build the packed T9 dictionary header described in dictionary/README.md#packed-dictionary-format.

Python 3 standard library only. Input lines are `word<TAB>zipf` (a space also
separates); blank lines and lines starting with '#' are ignored.
"""

import argparse
import hashlib
import math
import re
import sys
from collections import Counter
from itertools import groupby
from pathlib import Path

GENERATOR_VERSION = "build_t9_dictionary 2"
MAX_WORD_LEN = 24
MAX_EXACT = 10  # T9_MAX_EXACT in keymap/t9_dictionary.h
SCORE_MAX = 63
DEFAULT_BLOCK_SIZE = 64
SHARED_BITS = 5
LEN_BITS = 5
DIGIT_BITS = 3
LETTER_BITS = 2
SCORE_BITS = 6
DIGIT_LETTERS = {2: "abc", 3: "def", 4: "ghi", 5: "jkl", 6: "mno", 7: "pqrs", 8: "tuv", 9: "wxyz"}
LETTER_DIGIT = {letter: digit for digit, group in DIGIT_LETTERS.items() for letter in group}
LETTER_INDEX = {letter: group.index(letter) for group in DIGIT_LETTERS.values() for letter in group}
LOWERCASE_WORD = re.compile(r"[a-z]+")


def fail(message: str) -> None:
    sys.exit(f"build_t9_dictionary: error: {message}")


def quantize(zipf: float) -> int:
    """floor(x + 0.5) rather than round() so C and awk agree on halves."""
    return min(max(math.floor(zipf * 8 + 0.5), 0), SCORE_MAX)


def word_digits(word: str) -> tuple[int, ...]:
    return tuple(LETTER_DIGIT[letter] for letter in word)


def sort_key(entry: tuple[str, int]) -> tuple:
    word, score = entry
    return (word_digits(word), -score, word)


def keep_order_key(entry: tuple[str, int]) -> tuple:
    """Budget filters keep the head of this order: drop lowest score first, ties by word descending."""
    word, score = entry
    return (-score, word)


def parse_rows(path: Path) -> list[tuple[str, float, int]]:
    rows = []
    for line_number, raw in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        fields = line.split("\t") if "\t" in line else line.split()
        if len(fields) < 2:
            fail(f"{path}:{line_number}: expected word and zipf, got {raw!r}")
        try:
            zipf = float(fields[1])
        except ValueError:
            fail(f"{path}:{line_number}: zipf is not a number: {fields[1]!r}")
        if not math.isfinite(zipf):
            fail(f"{path}:{line_number}: zipf must be finite: {fields[1]!r}")
        rows.append((fields[0].strip(), zipf, line_number))
    return rows


def rejection_reason(word: str, seen: set[str]) -> str | None:
    if not LOWERCASE_WORD.fullmatch(word):
        return "not lowercase a-z"
    if len(word) > MAX_WORD_LEN:
        return f"longer than {MAX_WORD_LEN}"
    if word in seen:
        return "duplicate (first occurrence kept)"
    return None


def filter_rows(rows: list[tuple[str, float, int]]) -> tuple[list[tuple[str, float]], Counter, dict[str, str]]:
    seen: set[str] = set()
    kept, reasons, rejected_words = [], Counter(), {}
    for word, zipf, _ in rows:
        reason = rejection_reason(word, seen)
        if reason:
            reasons[reason] += 1
            rejected_words.setdefault(word, reason)
        else:
            seen.add(word)
            kept.append((word, zipf))
    return kept, reasons, rejected_words


def shared_prefix_len(a: tuple[int, ...], b: tuple[int, ...]) -> int:
    return next((i for i, (x, y) in enumerate(zip(a, b)) if x != y), min(len(a), len(b)))


def entry_layout(entries: list[tuple[str, int]], block_size: int) -> list[int]:
    """`shared` field of every sorted entry."""
    sequences = [word_digits(word) for word, _ in entries]
    return [0 if i % block_size == 0 else shared_prefix_len(sequences[i - 1], seq) for i, seq in enumerate(sequences)]


def entry_bits(word: str, shared: int) -> int:
    return SHARED_BITS + LEN_BITS + DIGIT_BITS * (len(word) - shared) + LETTER_BITS * len(word) + SCORE_BITS


def block_count(word_count: int, block_size: int) -> int:
    return -(-word_count // block_size)


def data_bytes(total_bits: int, blocks: int) -> int:
    return -(-total_bits // 8) + 4 * (blocks + 1) + blocks


def measure(entries: list[tuple[str, int]], block_size: int) -> int:
    sorted_entries = sorted(entries, key=sort_key)
    shared = entry_layout(sorted_entries, block_size)
    total_bits = sum(entry_bits(word, s) for (word, _), s in zip(sorted_entries, shared))
    return data_bytes(total_bits, block_count(len(entries), block_size))


def cap_exact(ranked: list[tuple[str, int]], max_exact: int) -> list[tuple[str, int]]:
    """Keep the `max_exact` best-ranked words of every digit sequence; `ranked` is in keep order."""
    by_sequence = sorted(ranked, key=sort_key)
    kept = {
        entry for _, group in groupby(by_sequence, key=lambda e: word_digits(e[0])) for entry in list(group)[:max_exact]
    }
    return [entry for entry in ranked if entry in kept]


def fit_budget(ranked: list[tuple[str, int]], max_bytes: int, block_size: int) -> int:
    """Largest prefix of `ranked` whose data fits; size is monotone apart from block
    restart effects, so the binary search result is walked down until it fits."""
    if not ranked or measure(ranked, block_size) <= max_bytes:
        return len(ranked)
    low, high = 0, len(ranked)
    while high - low > 1:
        middle = (low + high) // 2
        low, high = (middle, high) if measure(ranked[:middle], block_size) <= max_bytes else (low, middle)
    keep = low
    while keep > 0 and measure(ranked[:keep], block_size) > max_bytes:
        keep -= 1
    return keep


def encode(entries: list[tuple[str, int]], block_size: int) -> tuple[bytes, list[int], list[int], int]:
    """MSB-first bitstream, block bit offsets (plus total) and block max scores."""
    shared = entry_layout(entries, block_size)

    def entry_bit_string(word: str, score: int, shared_digits: int) -> str:
        digits = word_digits(word)
        fields = [(shared_digits, SHARED_BITS), (len(word), LEN_BITS)]
        fields += [(digit - 2, DIGIT_BITS) for digit in digits[shared_digits:]]
        fields += [(LETTER_INDEX[letter], LETTER_BITS) for letter in word]
        fields += [(score, SCORE_BITS)]
        return "".join(format(value, f"0{width}b") for value, width in fields)

    chunks = [entry_bit_string(word, score, s) for (word, score), s in zip(entries, shared)]
    starts = [0]
    for chunk in chunks:
        starts.append(starts[-1] + len(chunk))
    total_bits = starts[-1]
    offsets = starts[::block_size][: block_count(len(entries), block_size)] + [total_bits]
    max_scores = [max(score for _, score in entries[i : i + block_size]) for i in range(0, len(entries), block_size)]
    stream = "".join(chunks)
    stream += "0" * (-len(stream) % 8)
    packed = bytes(int(stream[i : i + 8], 2) for i in range(0, len(stream), 8))
    return packed, offsets, max_scores, total_bits


def decode(packed: bytes, word_count: int, block_size: int) -> tuple[list[tuple[str, int]], list[int]]:
    """Reference decoder: sequential walk of the bitstream, independent of the encoder."""
    stream = "".join(format(byte, "08b") for byte in packed)
    position = 0

    def read(width: int) -> int:
        nonlocal position
        value = int(stream[position : position + width], 2)
        position += width
        return value

    entries, block_starts, sequence = [], [], []
    for index in range(word_count):
        if index % block_size == 0:
            block_starts.append(position)
        shared, length = read(SHARED_BITS), read(LEN_BITS)
        if index % block_size == 0 and shared:
            fail(f"self-check: block-first entry {index} has shared {shared}")
        sequence = sequence[:shared] + [read(DIGIT_BITS) + 2 for _ in range(length - shared)]
        word = "".join(DIGIT_LETTERS[digit][read(LETTER_BITS)] for digit in sequence)
        entries.append((word, read(SCORE_BITS)))
    return entries, block_starts + [position]


def self_check(entries, packed, offsets, max_scores, total_bits, block_size) -> None:
    decoded, decoded_offsets = decode(packed, len(entries), block_size)
    mismatches = [i for i, (a, b) in enumerate(zip(decoded, entries)) if a != b]
    if mismatches or len(decoded) != len(entries):
        first = mismatches[0] if mismatches else min(len(decoded), len(entries))
        fail(f"self-check: entry {first} decodes differently ({len(mismatches)} mismatches)")
    if decoded_offsets != offsets or offsets[-1] != total_bits:
        fail("self-check: block offsets differ from a sequential decode")
    if sorted(decoded, key=sort_key) != decoded:
        fail("self-check: decoded entries are not in documented dictionary order")
    expected_max = [max(score for _, score in decoded[i : i + block_size]) for i in range(0, len(decoded), block_size)]
    if expected_max != max_scores:
        fail("self-check: block max scores differ from a full decode")
    if any(b <= a for a, b in zip(offsets, offsets[1:])):
        fail("self-check: block offsets are not strictly increasing")


def c_array(values: list[int], value_format: str, per_line: int) -> str:
    lines = [
        ", ".join(value_format.format(v) for v in values[i : i + per_line]) for i in range(0, len(values), per_line)
    ]
    return "\n".join(f"    {line}," for line in lines)


def option_text(value) -> str:
    return "none" if value is None else str(value)


def render_packed(meta: dict, packed: bytes, offsets: list[int], max_scores: list[int]) -> str:
    stages = "\n".join(f" *   {name}: {count}" for name, count in meta["stages"])
    rejected = "\n".join(f" *   {reason}: {count}" for reason, count in sorted(meta["rejected"].items())) or " *   none"
    return f"""/* generated by {GENERATOR_VERSION}; do not edit. Format: dictionary/README.md#packed-dictionary-format
 * input: {meta["input_name"]}
 * input sha256: {meta["sha256"]}
 * budget: --max-bytes {option_text(meta["max_bytes"])}, --max-words {option_text(meta["max_words"])}, --min-zipf {option_text(meta["min_zipf"])}, --max-exact {meta["max_exact"]}, --block-size {meta["block_size"]}
 * words after each stage:
{stages}
 * rejected:
{rejected}
 * bitstream: {meta["total_bits"]} bits, {meta["bits_per_word"]:.2f} bits/word; data bytes: {meta["data_bytes"]}
 */
#pragma once

#include <stdint.h>

#define T9_DICT_WORD_COUNT {meta["word_count"]}
#define T9_DICT_BLOCK_SIZE {meta["block_size"]}
#define T9_DICT_BLOCK_COUNT {len(max_scores)}
#define T9_DICT_DATA_BYTES {meta["data_bytes"]}

static const uint8_t t9_dict_bits[] = {{
{c_array(list(packed), "0x{:02x}", 16)}
}};

static const uint32_t t9_dict_block_offsets[T9_DICT_BLOCK_COUNT + 1] = {{
{c_array(offsets, "{}", 8)}
}};

static const uint8_t t9_dict_block_max_score[T9_DICT_BLOCK_COUNT] = {{
{c_array(max_scores, "{}", 16)}
}};
"""


def render_simple(input_name: str, entries: list[tuple[str, int]]) -> str:
    rows = "".join(f'{{"{word}", {score}}},\n' for word, score in entries)
    return f"/* generated from {input_name} by {GENERATOR_VERSION}; do not edit */\n{rows}"


def exact_groups(entries: list[tuple[str, int]]) -> dict[tuple[int, ...], list[tuple[str, int]]]:
    return {digits: list(group) for digits, group in groupby(entries, key=lambda e: word_digits(e[0]))}


def digits_text(digits: tuple[int, ...]) -> str:
    return "".join(map(str, digits))


def check_words_section(path: Path, entries, groups, rejected_words: dict[str, str], input_words: set[str]) -> str:
    words = [
        line.strip().split()[0]
        for line in path.read_text(encoding="utf-8").splitlines()
        if line.strip() and not line.lstrip().startswith("#")
    ]
    kept = {word for word, _ in entries}

    def status(word: str) -> str:
        if word in kept:
            group = groups[word_digits(word)]
            rank = next(i for i, (w, _) in enumerate(group) if w == word) + 1
            return f"| {word} | {digits_text(word_digits(word))} | {rank} of {len(group)} |"
        reason = rejected_words.get(word) or (
            "dropped by --min-zipf/--max-words/--max-bytes" if word in input_words else "absent from input"
        )
        return f"| {word} | | missing: {reason} |"

    collisions = sorted({word_digits(w) for w in words if w in kept and len(groups[word_digits(w)]) > 1})
    collision_rows = [
        f"| {digits_text(d)} | {len(groups[d])} | " + ", ".join(f"{w} ({s})" for w, s in groups[d][:3]) + " |"
        for d in collisions
    ]
    present = sum(word in kept for word in words)
    return "\n".join(
        [
            f"## Check words: {path}",
            "",
            f"{present} of {len(words)} present.",
            "",
            "| word | digits | rank |",
            "|---|---|---|",
        ]
        + [status(word) for word in words]
        + ["", "### Collision sequences (top 3)", "", "| digits | exact words | top 3 (score) |", "|---|---|---|"]
        + collision_rows
        + [""]
    )


def histogram_rows(counter: Counter, label: str) -> list[str]:
    return [f"| {label} | words |", "|---|---|"] + [f"| {key} | {counter[key]} |" for key in sorted(counter)]


def render_report(meta: dict, entries, groups, check_section: str | None) -> str:
    index_bytes = meta["data_bytes"] - -(-meta["total_bits"] // 8)
    longest = max(entries, key=lambda e: (len(e[0]), e[1]))[0]
    lines = [
        "# T9 dictionary report",
        "",
        f"Generated by {GENERATOR_VERSION} from `{meta['input_name']}` (sha256 `{meta['sha256']}`).",
        "",
        "## Filters",
        "",
        "| stage | words |",
        "|---|---|",
        *[f"| {name} | {count} |" for name, count in meta["stages"]],
        "",
        "| rejected | words |",
        "|---|---|",
        *([f"| {reason} | {count} |" for reason, count in sorted(meta["rejected"].items())] or ["| none | 0 |"]),
        "",
        "## Size",
        "",
        "| item | value |",
        "|---|---|",
        f"| words | {meta['word_count']} |",
        f"| bitstream | {-(-meta['total_bits'] // 8)} B ({meta['total_bits']} bits, {meta['bits_per_word']:.2f} bits/word) |",
        f"| index (offsets + max scores) | {index_bytes} B |",
        f"| T9_DICT_DATA_BYTES | {meta['data_bytes']} B (--max-bytes {option_text(meta['max_bytes'])}) |",
        f"| bytes per word | {meta['data_bytes'] / meta['word_count']:.3f} |",
        f"| block size / blocks | {meta['block_size']} / {meta['block_count']} |",
        f"| digit sequences | {len(groups)} |",
        f"| largest exact collision | {max(len(g) for g in groups.values())} ({digits_text(max(groups, key=lambda d: len(groups[d])))}) |",
        f"| longest word | {longest} ({len(longest)}) |",
        "",
        "## Word length histogram",
        "",
        *histogram_rows(Counter(len(word) for word, _ in entries), "length"),
        "",
        "## Score histogram (score // 8 = zipf bucket)",
        "",
        *histogram_rows(Counter(score // 8 for _, score in entries), "zipf"),
        "",
    ]
    return "\n".join(lines) + ("\n" + check_section if check_section else "")


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", required=True, type=Path)
    parser.add_argument("--packed", type=Path, help="generated packed header")
    parser.add_argument("--simple", type=Path, help='optional {"word", score}, table')
    parser.add_argument("--max-bytes", type=int, help="limit on T9_DICT_DATA_BYTES")
    parser.add_argument("--max-words", type=int)
    parser.add_argument("--min-zipf", type=float)
    parser.add_argument(
        "--max-exact", type=int, default=MAX_EXACT, help="exact words kept per digit sequence (T9_MAX_EXACT)"
    )
    parser.add_argument("--block-size", type=int, default=DEFAULT_BLOCK_SIZE)
    parser.add_argument("--check-words", type=Path)
    parser.add_argument("--report", type=Path)
    parser.add_argument("--quiet", action="store_true")
    args = parser.parse_args(argv)
    if not 1 <= args.block_size <= 65535:
        parser.error("--block-size must be 1..65535")
    if args.max_exact < 1:
        parser.error("--max-exact must be at least 1")
    if args.max_words is not None and args.max_words < 1:
        parser.error("--max-words must be at least 1")
    if args.max_bytes is not None and args.max_bytes < 1:
        parser.error("--max-bytes must be at least 1")
    if args.min_zipf is not None and not math.isfinite(args.min_zipf):
        parser.error("--min-zipf must be finite")
    return args


def main(argv: list[str]) -> None:
    args = parse_args(argv)
    raw = args.input.read_bytes()
    rows = parse_rows(args.input)
    valid, rejected, rejected_words = filter_rows(rows)
    after_min_zipf = [(w, z) for w, z in valid if args.min_zipf is None or z >= args.min_zipf]
    ranked = sorted(((w, quantize(z)) for w, z in after_min_zipf), key=keep_order_key)
    after_max_words = ranked[: args.max_words] if args.max_words is not None else ranked
    after_max_exact = cap_exact(after_max_words, args.max_exact)
    budget_kept = (
        after_max_exact[: fit_budget(after_max_exact, args.max_bytes, args.block_size)]
        if args.max_bytes is not None
        else after_max_exact
    )
    entries = sorted(budget_kept, key=sort_key)
    if not entries:
        fail("no words left after filtering")

    packed, offsets, max_scores, total_bits = encode(entries, args.block_size)
    self_check(entries, packed, offsets, max_scores, total_bits, args.block_size)

    groups = exact_groups(entries)
    meta = {
        "input_name": args.input.as_posix(),
        "sha256": hashlib.sha256(raw).hexdigest(),
        "max_bytes": args.max_bytes,
        "max_words": args.max_words,
        "min_zipf": args.min_zipf,
        "max_exact": args.max_exact,
        "stages": [
            ("input rows", len(rows)),
            ("valid lowercase a-z, unique", len(valid)),
            ("after --min-zipf", len(after_min_zipf)),
            ("after --max-words", len(after_max_words)),
            ("after --max-exact", len(after_max_exact)),
            ("after --max-bytes", len(entries)),
        ],
        "rejected": dict(rejected),
        "word_count": len(entries),
        "block_size": args.block_size,
        "block_count": len(max_scores),
        "total_bits": total_bits,
        "bits_per_word": total_bits / len(entries),
        "data_bytes": data_bytes(total_bits, len(max_scores)),
    }
    if args.max_bytes is not None and meta["data_bytes"] > args.max_bytes:
        fail(f"data bytes {meta['data_bytes']} exceed --max-bytes {args.max_bytes}")

    if args.packed:
        args.packed.write_text(render_packed(meta, packed, offsets, max_scores), encoding="utf-8")
    if args.simple:
        args.simple.write_text(render_simple(meta["input_name"], entries), encoding="utf-8")
    check_section = (
        check_words_section(args.check_words, entries, groups, rejected_words, {word for word, _ in valid})
        if args.check_words
        else None
    )
    if args.report:
        args.report.write_text(render_report(meta, entries, groups, check_section), encoding="utf-8")
    if not args.quiet:
        print(
            f"{meta['word_count']} words, {len(groups)} sequences, {meta['block_count']} blocks of {args.block_size}, "
            f"{total_bits} bits ({meta['bits_per_word']:.2f}/word), {meta['data_bytes']} data bytes "
            f"({meta['data_bytes'] / meta['word_count']:.3f} B/word)"
        )
        if rejected:
            print("rejected: " + ", ".join(f"{reason}: {count}" for reason, count in sorted(rejected.items())))
        if check_section:
            print(check_section)


if __name__ == "__main__":
    main(sys.argv[1:])
