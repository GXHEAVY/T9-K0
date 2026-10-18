#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Download pinned word sources, verify them, and write dictionary/words_en.tsv.

Frequencies: wordfreq 'large' English list. Allow-list: SCOWL en_US (english + american
spelling categories; words, proper-names and upper sub-categories) up to --scowl-size.
Downloads are untrusted data: only read, never executed.
"""

import argparse
import datetime
import hashlib
import importlib.metadata
import io
import json
import math
import re
import sys
import tarfile
import tomllib
import urllib.request
import zipfile
from collections import Counter
from pathlib import Path

import wordfreq

VERSION = "fetch_sources 1.1.0"
DICT_DIR = Path(__file__).resolve().parent
SOURCES_DIR = DICT_DIR / "sources"
LOCK_PATH = DICT_DIR / "sources.lock"

WORDFREQ_VERSION = "3.1.1"
WORDFREQ_WHEEL = f"wordfreq-{WORDFREQ_VERSION}-py3-none-any.whl"
WORDFREQ_LANG = "en"
WORDFREQ_LIST = "large"
WORDFREQ_DATA = f"wordfreq/data/{WORDFREQ_LIST}_{WORDFREQ_LANG}.msgpack.gz"

SCOWL_VERSION = "2020.12.07"
SCOWL_URL = (
    f"https://sourceforge.net/projects/wordlist/files/SCOWL/{SCOWL_VERSION}/scowl-{SCOWL_VERSION}.tar.gz/download"
)
SCOWL_ROOT = f"scowl-{SCOWL_VERSION}"
SCOWL_SIZES = (10, 20, 35, 40, 50, 55, 60, 70, 80, 95)
SCOWL_SPELLINGS = ("english", "american")
SCOWL_KINDS = ("words", "proper-names", "upper")

MAX_WORD_LEN = 24
WORD_RE = re.compile(rf"^[a-z]{{1,{MAX_WORD_LEN}}}$")
CONTRACTION_RE = re.compile(r"^([a-z]+)'([a-z]+)$")
REPORT_TOP_REMOVED = 300
REPORT_TOP_KEPT_VIA = 50


def sha256_bytes(data):
    return hashlib.sha256(data).hexdigest()


def http_get(url):
    request = urllib.request.Request(url, headers={"User-Agent": VERSION})
    with urllib.request.urlopen(request, timeout=120) as response:
        return response.read()


def fetch_cached(path, url, expected_sha):
    """Return file bytes, downloading only when missing or stale; verify when a hash is pinned."""
    cached = path.read_bytes() if path.exists() else None
    data = (
        cached
        if cached is not None and (expected_sha is None or sha256_bytes(cached) == expected_sha)
        else http_get(url)
    )
    if expected_sha is not None and sha256_bytes(data) != expected_sha:
        sys.exit(f"sha256 mismatch for {path.name}: got {sha256_bytes(data)}, lock has {expected_sha}")
    if data is not cached:
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
    return data


def pypi_wheel_record():
    meta = json.loads(http_get(f"https://pypi.org/pypi/wordfreq/{WORDFREQ_VERSION}/json"))
    wheel = next(u for u in meta["urls"] if u["filename"] == WORDFREQ_WHEEL)
    return {"filename": wheel["filename"], "url": wheel["url"], "sha256": wheel["digests"]["sha256"]}


def scowl_file_names(max_size):
    return [
        f"{spelling}-{kind}.{size}"
        for kind in SCOWL_KINDS
        for spelling in SCOWL_SPELLINGS
        for size in SCOWL_SIZES
        if size <= max_size
    ]


def toml_value(value):
    return (
        "[\n" + "".join(f"  {json.dumps(v)},\n" for v in value) + "]" if isinstance(value, list) else json.dumps(value)
    )


def render_lock(sections):
    return "".join(
        f"[{name}]\n" + "".join(f"{k} = {toml_value(v)}\n" for k, v in body.items()) + "\n"
        for name, body in sections.items()
    )


def read_lock():
    if not LOCK_PATH.exists():
        sys.exit(f"{LOCK_PATH} missing: run once with --write-lock to pin the downloads")
    return tomllib.loads(LOCK_PATH.read_text())


def wheel_members(wheel_bytes):
    with zipfile.ZipFile(io.BytesIO(wheel_bytes)) as wheel:
        names = [n for n in wheel.namelist() if n.endswith((".dist-info/LICENSE.txt", ".dist-info/METADATA"))]
        return {n: wheel.read(n) for n in names + [WORDFREQ_DATA]}


def extract_scowl(tar_bytes, destination):
    """Extract only plain files under final/ plus the licence/readme files, with tarfile's data filter."""
    wanted = re.compile(rf"^{re.escape(SCOWL_ROOT)}/(final/[a-z_0-9-]+\.\d+|Copyright|README|VERSION)$")
    with tarfile.open(fileobj=io.BytesIO(tar_bytes), mode="r:gz") as archive:
        members = [m for m in archive.getmembers() if m.isfile() and wanted.match(m.name)]
        archive.extractall(destination, members=members, filter="data")
    return destination / SCOWL_ROOT


def read_scowl_list(path):
    return {line.strip().lower() for line in path.read_text(encoding="latin-1").splitlines() if line.strip()}


def scowl_listings(final_dir):
    """Map each a-z word to every (size, file stem) that lists it, across all SCOWL categories, size ascending."""
    entries = sorted(
        (word, int(path.suffix[1:]), path.stem)
        for path in final_dir.iterdir()
        for word in read_scowl_list(path)
        if WORD_RE.match(word)
    )
    listings = {}
    for word, size, stem in entries:
        listings.setdefault(word, []).append((size, stem))
    return listings


def smallest_listing(listing):
    size = listing[0][0]
    return "/".join(stem for s, stem in listing if s == size) + f".{size}"


def removal_reason(listing):
    stems = {stem for _, stem in listing}
    kinds = {stem.split("-", 1)[1] for stem in stems}
    return (
        "not in any SCOWL list"
        if not listing
        else "en_US word/name above the size cut-off"
        if any(stem.split("-", 1)[0] in SCOWL_SPELLINGS and stem.split("-", 1)[1] in SCOWL_KINDS for stem in stems)
        else "abbreviation"
        if "abbreviations" in kinds
        else "contraction"
        if "contractions" in kinds
        else "special (hacker, roman numerals)"
        if any(stem.startswith("special-") for stem in stems)
        else "regional or variant spelling only"
    )


def to_zipf(frequency):
    return round(math.log10(frequency) + 9, 2)


def ranked(pairs):
    return sorted(pairs, key=lambda pair: (-pair[1], pair[0]))


def contraction_parts(frequencies, min_zipf):
    """both sides of every apostrophe token (don't -> don, t) with the token's zipf,
    so the engine can predict a contraction as word + apostrophe + word"""
    matches = ((CONTRACTION_RE.match(word), to_zipf(f)) for word, f in frequencies.items())
    sides = ((part, z) for m, z in matches if m and z >= min_zipf for part in m.groups() if WORD_RE.match(part))
    best = {}
    for part, z in sides:
        best[part] = max(z, best.get(part, 0.0))
    return best


def merge_parts(kept, parts):
    """add contraction parts missing from kept; raise the zipf of present ones"""
    merged = dict(kept)
    for part, z in parts.items():
        merged[part] = max(z, merged.get(part, 0.0))
    return ranked(merged.items())


def table(rows, header):
    return "\n".join(
        [f"| {' | '.join(header)} |", f"|{'---|' * len(header)}"] + [f"| {' | '.join(map(str, row))} |" for row in rows]
    )


def render_tsv(kept, lock, args, counts):
    wf, sc = lock["wordfreq"], lock["scowl"]
    scowl_line = (
        f"# allow-list: SCOWL {sc['version']} en_US (english+american; words, proper-names, upper)"
        f" sizes <= {args.scowl_size}, sha256 {sc['sha256']}"
        if not args.no_scowl_filter
        else "# allow-list: none (--no-scowl-filter)"
    )
    header = [
        f"# generated by {VERSION}; do not edit, rerun `make fetch-sources`",
        "# format: word<TAB>zipf, sorted by zipf descending then word ascending",
        f"# frequencies: wordfreq {wf['version']} wordlist '{wf['wordlist']}' language '{wf['language']}',"
        f" wheel sha256 {wf['sha256']}",
        "#   wordfreq data licence: CC-BY-SA-4.0 (code Apache-2.0); see LICENSES.md for attribution",
        scowl_line,
        "#   SCOWL licence: permissive notice-preserving terms by Kevin Atkinson et al.; see LICENSES.md",
        f"# filters: ^[a-z]{{1,{MAX_WORD_LEN}}}$, zipf >= {args.min_zipf:.2f}, single letters {args.single_letters!r};"
        f" contraction parts {'added' if not args.no_contractions else 'not added'} ({counts['added_parts']} new words)",
        f"# counts: wordfreq tokens {counts['tokens']}, a-z tokens {counts['alpha']},"
        f" above min-zipf {counts['above_min']}, kept {len(kept)}",
        f"# sources fetched {lock['meta']['fetched']}; pins in dictionary/sources.lock",
    ]
    return "\n".join(header + [f"{word}\t{zipf:.2f}" for word, zipf in kept]) + "\n"


def render_report(args, lock, counts, removed, removed_all_count, listings, only_names, only_upper, added_parts):
    removed_rows = [
        (
            i + 1,
            word,
            f"{zipf:.2f}",
            removal_reason(listings.get(word, [])),
            smallest_listing(listings[word]) if word in listings else "-",
        )
        for i, (word, zipf) in enumerate(removed[:REPORT_TOP_REMOVED])
    ]
    reasons = Counter(removal_reason(listings.get(word, [])) for word, _ in removed)

    def via_rows(pairs):
        return [(i + 1, word, f"{zipf:.2f}") for i, (word, zipf) in enumerate(pairs[:REPORT_TOP_KEPT_VIA])]

    return (
        "\n\n".join(
            [
                "# SCOWL filter report",
                f"Generated by `{VERSION}` from wordfreq {lock['wordfreq']['version']} ('{WORDFREQ_LIST}', en) and "
                f"SCOWL {lock['scowl']['version']} en_US sizes <= {args.scowl_size}; min zipf {args.min_zipf:.2f}. "
                "Pins: `dictionary/sources.lock`.",
                table(
                    [
                        ("wordfreq tokens", counts["tokens"]),
                        (f"a-z tokens, length 1..{MAX_WORD_LEN}", counts["alpha"]),
                        ("a-z tokens in SCOWL allow-list (any zipf)", counts["alpha"] - removed_all_count),
                        ("a-z tokens removed by SCOWL filter (any zipf)", removed_all_count),
                        (f"a-z tokens with zipf >= {args.min_zipf:.2f}", counts["above_min"]),
                        (f"removed by SCOWL filter at zipf >= {args.min_zipf:.2f}", len(removed)),
                        (f"single letters dropped (kept: {args.single_letters})", counts["single_dropped"]),
                        ("contraction parts derived (don't -> don, t)", counts["contraction_parts"]),
                        ("contraction parts added as new words", counts["added_parts"]),
                        ("kept", counts["kept"]),
                        ("kept only via proper-names", len(only_names)),
                        ("kept only via upper", len(only_upper)),
                        ("SCOWL allow-list size (a-z)", counts["allow"]),
                    ],
                    ("measure", "count"),
                ),
                f"## Why tokens with zipf >= {args.min_zipf:.2f} were removed\n\n"
                "Reason from every SCOWL 2020.12.07 list (all spelling categories and sizes) containing the token, "
                "checked in this order: en_US word/name list above the size cut-off, abbreviation, contraction, "
                "special list, otherwise a regional (british/canadian/australian) or variant spelling.\n\n"
                + table(sorted(reasons.items(), key=lambda kv: (-kv[1], kv[0])), ("reason", "removed tokens")),
                f"## Top {REPORT_TOP_REMOVED} removed tokens by zipf\n\n"
                "Last column: SCOWL files at the smallest size that list the token.\n\n"
                + table(removed_rows, ("rank", "token", "zipf", "reason", "smallest SCOWL listing")),
                f"## Kept only via proper-names ({len(only_names)}), top {REPORT_TOP_KEPT_VIA}\n\n"
                + table(via_rows(only_names), ("rank", "token", "zipf")),
                f"## Kept only via upper ({len(only_upper)}), top {REPORT_TOP_KEPT_VIA}\n\n"
                + table(via_rows(only_upper), ("rank", "token", "zipf")),
                f"## Contraction parts added as new words ({len(added_parts)}), top {REPORT_TOP_KEPT_VIA}\n\n"
                "Both sides of apostrophe tokens with zipf >= min zipf, scored with the token's zipf. "
                "They bypass the SCOWL filter and the single-letter rule so that don + ' + t predicts \"don't\".\n\n"
                + table(via_rows(added_parts), ("rank", "token", "zipf")),
            ]
        )
        + "\n"
    )


def parse_args():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--output", type=Path, default=DICT_DIR / "words_en.tsv")
    parser.add_argument("--report", type=Path, default=DICT_DIR / "report_sources.md")
    parser.add_argument("--min-zipf", type=float, default=2.0)
    parser.add_argument("--scowl-size", type=int, default=70, choices=SCOWL_SIZES)
    parser.add_argument("--no-scowl-filter", action="store_true")
    parser.add_argument(
        "--single-letters",
        default="ai",
        help="single-letter tokens kept as words (default: a and i; others arrive only as contraction parts)",
    )
    parser.add_argument(
        "--no-contractions",
        action="store_true",
        help="do not add both sides of apostrophe tokens (don't -> don, t) as words",
    )
    parser.add_argument("--write-lock", action="store_true", help="pin the current downloads in sources.lock")
    return parser.parse_args()


def main():
    args = parse_args()
    installed = importlib.metadata.version("wordfreq")
    if installed != WORDFREQ_VERSION:
        sys.exit(f"wordfreq {installed} installed, {WORDFREQ_VERSION} pinned: use .venv-dict from requirements.txt")

    pinned = None if args.write_lock else read_lock()
    wheel_record = pypi_wheel_record() if args.write_lock else pinned["wordfreq"]
    wheel_bytes = fetch_cached(SOURCES_DIR / WORDFREQ_WHEEL, wheel_record["url"], wheel_record["sha256"])
    scowl_bytes = fetch_cached(
        SOURCES_DIR / f"scowl-{SCOWL_VERSION}.tar.gz", SCOWL_URL, None if args.write_lock else pinned["scowl"]["sha256"]
    )

    members = wheel_members(wheel_bytes)
    wordfreq_dir = SOURCES_DIR / "wordfreq"
    wordfreq_dir.mkdir(parents=True, exist_ok=True)
    for name, data in members.items():
        if name != WORDFREQ_DATA:
            (wordfreq_dir / Path(name).name).write_bytes(data)
    installed_data = Path(wordfreq.__file__).parent / "data" / Path(WORDFREQ_DATA).name
    data_sha = sha256_bytes(members[WORDFREQ_DATA])
    if sha256_bytes(installed_data.read_bytes()) != data_sha:
        sys.exit(f"installed {installed_data} differs from {WORDFREQ_DATA} in the pinned wheel")

    scowl_dir = extract_scowl(scowl_bytes, SOURCES_DIR / "scowl")
    scowl_files = [name for name in scowl_file_names(args.scowl_size) if (scowl_dir / "final" / name).exists()]
    lock = {
        "meta": {
            "fetch_script_version": VERSION,
            "fetched": datetime.date.today().isoformat() if args.write_lock else pinned["meta"]["fetched"],
        },
        "wordfreq": {
            "version": WORDFREQ_VERSION,
            **wheel_record,
            "language": WORDFREQ_LANG,
            "wordlist": WORDFREQ_LIST,
            "data_file": WORDFREQ_DATA,
            "data_sha256": data_sha,
            "license_code": "Apache-2.0",
            "license_data": "CC-BY-SA-4.0",
        },
        "scowl": {
            "version": SCOWL_VERSION,
            "url": SCOWL_URL,
            "filename": f"scowl-{SCOWL_VERSION}.tar.gz",
            "sha256": sha256_bytes(scowl_bytes),
            "spelling": "en_US (english + american)",
            "max_size": args.scowl_size,
            "sub_categories": list(SCOWL_KINDS),
            "excluded": "abbreviations, contractions, variant_*, other regions, special-*; "
            "any entry with characters outside a-z after lowercasing",
            "files": [f"final/{name}" for name in scowl_files],
            "license": "SCOWL Copyright file (notice-preserving permissive terms)",
        },
    }
    if args.write_lock:
        LOCK_PATH.write_text(
            "# input pins for dictionary/fetch_sources.py; written by --write-lock\n\n"
            + render_lock(lock).rstrip()
            + "\n",
            encoding="utf-8",
        )
    elif pinned["wordfreq"]["data_sha256"] != data_sha:
        sys.exit("wordfreq data sha256 differs from sources.lock")

    sets = {
        kind: set().union(
            *(
                read_scowl_list(scowl_dir / "final" / name)
                for name in scowl_files
                if name.split("-", 1)[1].rsplit(".", 1)[0] == kind
            )
        )
        for kind in SCOWL_KINDS
    }
    allow = {w for w in set().union(*sets.values()) if WORD_RE.match(w)}

    frequencies = wordfreq.get_frequency_dict(WORDFREQ_LANG, wordlist=WORDFREQ_LIST)
    alpha = ranked((word, to_zipf(f)) for word, f in frequencies.items() if WORD_RE.match(word))
    above_min = [(w, z) for w, z in alpha if z >= args.min_zipf]
    single_ok = set(args.single_letters)
    plain = [(w, z) for w, z in above_min if len(w) > 1 or w in single_ok]
    allowed = plain if args.no_scowl_filter else [(w, z) for w, z in plain if w in allow]
    parts = {} if args.no_contractions else contraction_parts(frequencies, args.min_zipf)
    kept = merge_parts(allowed, parts)
    allowed_words = {w for w, _ in allowed}
    added_parts = ranked((w, z) for w, z in parts.items() if w not in allowed_words)
    removed = [(w, z) for w, z in above_min if w not in allow]
    removed_all_count = sum(1 for w, _ in alpha if w not in allow)
    names_only = sets["proper-names"] - sets["words"] - sets["upper"]
    upper_only = sets["upper"] - sets["words"] - sets["proper-names"]
    only_names = [(w, z) for w, z in kept if w in names_only]
    only_upper = [(w, z) for w, z in kept if w in upper_only]
    counts = {
        "tokens": len(frequencies),
        "alpha": len(alpha),
        "above_min": len(above_min),
        "single_dropped": len(above_min) - len(plain),
        "contraction_parts": len(parts),
        "added_parts": len(added_parts),
        "kept": len(kept),
        "allow": len(allow),
    }

    args.output.write_text(render_tsv(kept, lock, args, counts), encoding="utf-8")
    args.report.write_text(
        render_report(
            args,
            lock,
            counts,
            removed,
            removed_all_count,
            scowl_listings(scowl_dir / "final"),
            only_names,
            only_upper,
            added_parts,
        ),
        encoding="utf-8",
    )
    print(
        f"{VERSION}: kept {len(kept)} of {len(above_min)} a-z tokens with zipf >= {args.min_zipf:.2f} "
        f"({len(removed)} not in the SCOWL allow-list{', kept anyway' if args.no_scowl_filter else ''}) "
        f"-> {args.output}; report {args.report}"
    )


if __name__ == "__main__":
    main()
