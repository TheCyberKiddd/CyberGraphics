#!/usr/bin/env python3
"""Check that all tracked C++ sources match .clang-format."""

from __future__ import annotations

import os
import pathlib
import shutil
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]

SOURCE_SUFFIXES = {".h", ".hpp", ".cpp", ".cc", ".cxx", ".inl"}
EXCLUDED_DIRECTORIES = {
    ".git",
    ".vs",
    "build",
    "install",
    "out",
    "third_party",
}


def find_clang_format() -> str | None:
    configured = os.environ.get("CLANG_FORMAT")
    if configured:
        return configured
    return shutil.which("clang-format")


def find_sources() -> list[pathlib.Path]:
    files: list[pathlib.Path] = []
    for path in ROOT.rglob("*"):
        if not path.is_file() or path.suffix not in SOURCE_SUFFIXES:
            continue
        relative = path.relative_to(ROOT)
        if any(part in EXCLUDED_DIRECTORIES for part in relative.parts):
            continue
        files.append(relative)
    return sorted(files)


def main() -> int:
    clang_format = find_clang_format()
    if not clang_format:
        print(
            "clang-format not found. Install it or set CLANG_FORMAT to the executable path.",
            file=sys.stderr,
        )
        return 2

    files = find_sources()
    if not files:
        print("Formatting check passed: no C++ sources.")
        return 0

    # Keep command lines below Windows limits as the codebase grows.
    chunk_size = 64
    for start in range(0, len(files), chunk_size):
        chunk = files[start : start + chunk_size]
        command = [
            clang_format,
            "--dry-run",
            "--Werror",
            "--style=file",
            *(str(path) for path in chunk),
        ]
        result = subprocess.run(command, cwd=ROOT, check=False)
        if result.returncode != 0:
            return result.returncode

    print(f"Formatting check passed: {len(files)} files.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
