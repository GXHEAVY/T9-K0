#!/usr/bin/env python3
"""Prepare pinned tools and QMK without treating an existing directory as success.

SPDX-License-Identifier: GPL-2.0-or-later
"""

import argparse
import json
import subprocess
import sys
from pathlib import Path


def run(*args: str, capture: bool = False) -> str:
    result = subprocess.run(args, check=True, text=True, stdout=subprocess.PIPE if capture else None)
    return result.stdout.strip() if capture else ""


def ensure_venv(path: Path, requirements: list[str]) -> None:
    python = path / "bin/python"
    if not python.exists():
        run(sys.executable, "-m", "venv", str(path))
    if subprocess.run(
        [str(python), "-m", "pip", "--version"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL
    ).returncode:
        run(str(python), "-m", "ensurepip", "--upgrade")
    pins = dict(line.split("==", 1) for line in requirements)
    probe = subprocess.run(
        [
            str(python),
            "-c",
            "import importlib.metadata as m, json, sys; "
            "assert all(m.version(k) == v for k, v in json.loads(sys.argv[1]).items())",
            json.dumps(pins),
        ],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    healthy = (
        probe.returncode == 0
        and subprocess.run(
            [str(python), "-m", "pip", "check"],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        ).returncode
        == 0
    )
    if not healthy:
        run(str(python), "-m", "pip", "install", "--disable-pip-version-check", *requirements)
        run(str(python), "-m", "pip", "check")


def ensure_qmk(path: Path, repo: str, commit: str, submodules: list[str]) -> None:
    if not (path / ".git").exists():
        if path.exists() and any(path.iterdir()):
            raise ValueError(f"{path} is nonempty and is not a Git checkout; inspect it before retrying")
        path.mkdir(parents=True, exist_ok=True)
        run("git", "init", str(path))
    origin = subprocess.run(
        ["git", "-C", str(path), "remote", "get-url", "origin"],
        capture_output=True,
        text=True,
    )
    if origin.returncode:
        run("git", "-C", str(path), "remote", "add", "origin", repo)
    elif origin.stdout.strip() != repo:
        raise ValueError(f"{path}: origin is {origin.stdout.strip()}, expected {repo}; inspect the checkout")
    head = subprocess.run(
        ["git", "-C", str(path), "rev-parse", "--verify", "HEAD"],
        capture_output=True,
        text=True,
    )
    if head.returncode:
        run("git", "-C", str(path), "fetch", "--depth", "1", "origin", commit)
        run("git", "-C", str(path), "checkout", "--detach", commit)
    elif head.stdout.strip() != commit:
        raise ValueError(
            f"{path}: HEAD is {head.stdout.strip()}, expected {commit}; preserve your work before switching"
        )
    if run("git", "-C", str(path), "status", "--porcelain", "--untracked-files=no", capture=True):
        raise ValueError(f"{path} has tracked changes; inspect them before building a pinned release")
    if submodules:
        status = run("git", "-C", str(path), "submodule", "status", "--", *submodules, capture=True)
        if any(line.startswith(("-", "+", "U")) for line in status.splitlines()):
            run("git", "-C", str(path), "submodule", "update", "--init", "--depth", "1", "--", *submodules)


def ensure_link(link: Path, source: Path) -> None:
    if link.is_symlink():
        if link.resolve() == source.resolve():
            return
        link.unlink()
    elif link.exists():
        raise ValueError(f"{link} is not a symlink; preserve its contents before linking this keymap")
    link.parent.mkdir(parents=True, exist_ok=True)
    link.symlink_to(source.resolve(), target_is_directory=True)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    venv = commands.add_parser("venv")
    venv.add_argument("path", type=Path)
    pins = venv.add_mutually_exclusive_group(required=True)
    pins.add_argument("--package")
    pins.add_argument("--requirements", type=Path)
    qmk = commands.add_parser("qmk")
    qmk.add_argument("path", type=Path)
    qmk.add_argument("--repo", required=True)
    qmk.add_argument("--commit", required=True)
    qmk.add_argument("--submodules", nargs="*", default=[])
    qmk.add_argument("--link", type=Path, required=True)
    qmk.add_argument("--source", type=Path, required=True)
    args = parser.parse_args()
    try:
        if args.command == "venv":
            requirements = (
                [args.package]
                if args.package
                else [
                    line.strip()
                    for line in args.requirements.read_text().splitlines()
                    if line.strip() and not line.lstrip().startswith("#")
                ]
            )
            ensure_venv(args.path, requirements)
        else:
            ensure_qmk(args.path, args.repo, args.commit, args.submodules)
            ensure_link(args.link, args.source)
    except (ValueError, subprocess.CalledProcessError) as error:
        parser.exit(1, f"setup: {error}\n")


if __name__ == "__main__":
    main()
