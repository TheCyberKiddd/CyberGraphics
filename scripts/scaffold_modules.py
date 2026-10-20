#!/usr/bin/env python3
"""Create engine module folders and contract stubs from engines/modules.json.

This script is intentionally idempotent. It will not overwrite an existing
README.md or docs/MODULE.md unless --force is passed.
"""

from __future__ import annotations

import argparse
import json
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
MANIFEST = ROOT / "engines" / "modules.json"


def load_manifest() -> dict:
    with MANIFEST.open("r", encoding="utf-8") as handle:
        return json.load(handle)


def write_file(path: pathlib.Path, text: str, force: bool = False) -> None:
    if path.exists() and not force:
        return
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8", newline="\n")


def ensure_dir(path: pathlib.Path) -> None:
    path.mkdir(parents=True, exist_ok=True)
    keep = path / ".gitkeep"
    if not keep.exists():
        keep.write_text("", encoding="utf-8")


def format_list(items: list[str], empty: str = "None") -> str:
    return ", ".join(items) if items else empty


def module_readme(module: dict) -> str:
    phases = ", ".join(str(p) for p in module["roadmap"])
    return f"""# {module['name']}

**Status:** {module['status']} · **Layer:** {module['layer']} · **Kind:** {module['kind']}

{module['purpose']}

The binding contract for this module lives in [docs/MODULE.md](docs/MODULE.md).
Read it before adding code or changing a public header.

## Layout

- `include/cyber/{module['short']}/` — public headers, self-contained.
- `src/` — private implementation.
- `tests/` — unit tests owned by this module.

The folder exists from day one so the architecture is navigable. A CMake
target is added only when the module's phase starts.

## Roadmap

Phases {phases}.
"""


def module_contract(module: dict) -> str:
    deps = format_list(module["depends"])
    forbidden = format_list(module["forbidden"])
    invariants = "\n".join(f"- {item}" for item in module["invariants"])
    non_goals = "\n".join(f"- {item}" for item in module["non_goals"])
    phases = ", ".join(str(p) for p in module["roadmap"])
    return f"""# {module['name']} — module contract

**Kind:** {module['kind']}
**Layer:** {module['layer']}
**Order:** {module['order']}
**Status:** {module['status']}
**Owner:** @TheCyberKiddd

## Purpose

{module['purpose']}

## Public API

`{module['public_api']}`

Only headers under this path are public. Everything under `src/` is private.
Public headers must compile on their own and must not include another module's
private headers.

## Dependencies

Allowed: {deps}

Forbidden: {forbidden}

Dependencies are one-way. A module may depend only on modules with a lower
`order` value, or on a declared contract such as `cybergraphics_api`.

## Invariants

{invariants}

## Non-goals

{non_goals}

## Tests

{module['tests']}

## Budgets

{module['budgets']}

## Roadmap

Phases {phases}.
"""


def modules_index(manifest: dict) -> str:
    lines = [
        "# Engine modules",
        "",
        "This file is generated from [`modules.json`](modules.json). Do not edit it by hand.",
        "",
        "Each folder is an independently buildable module with a written contract in",
        "`docs/MODULE.md`. A folder may exist before its CMake target is created; that is",
        "deliberate, so the architecture is visible from the start.",
        "",
        "## Dependency law",
        "",
        "1. Dependencies point downward in `order`, never upward.",
        "2. A module may only depend on a module with a lower `order`, or on a declared",
        "   contract such as `cybergraphics_api`.",
        "3. No module includes another module's `src/`.",
        "4. `scripts/layer_check.py` enforces this in CI.",
        "",
        "## Modules",
        "",
        "| Module | Layer | Order | Kind | Status | Depends on | Roadmap |",
        "|---|---:|---:|---|---|---|---|",
    ]
    for module in manifest["modules"]:
        deps = "<br>".join(module["depends"]) if module["depends"] else "—"
        phases = ", ".join(str(p) for p in module["roadmap"])
        lines.append(
            f"| [`{module['name']}`]({module['name']}/) | {module['layer']} | "
            f"{module['order']} | {module['kind']} | {module['status']} | {deps} | {phases} |"
        )
    lines.extend(
        [
            "",
            "## Contracts",
            "",
            "| Contract | Public API | Purpose |",
            "|---|---|---|",
        ]
    )
    for contract in manifest.get("contracts", []):
        lines.append(
            f"| `{contract['name']}` | `{contract['public_api']}` | {contract['purpose']} |"
        )
    lines.extend(
        [
            "",
            "## Products vs. modules",
            "",
            "`cyber_graphics` and `cyber_software_renderer` are products: they implement the",
            "stable `cybergraphics_api` contract and are loaded as plugins. Every other entry",
            "is an engine service or library. Cyber Engine itself lives in its own repository",
            "and consumes this one through the stable API.",
            "",
        ]
    )
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--force", action="store_true", help="overwrite generated files")
    args = parser.parse_args()

    manifest = load_manifest()
    for module in manifest["modules"]:
        base = ROOT / "engines" / module["name"]
        ensure_dir(base / "include" / "cyber" / module["short"])
        ensure_dir(base / "src")
        ensure_dir(base / "tests")
        (base / "docs").mkdir(parents=True, exist_ok=True)
        write_file(base / "README.md", module_readme(module), args.force)
        write_file(base / "docs" / "MODULE.md", module_contract(module), args.force)

    write_file(ROOT / "engines" / "MODULES.md", modules_index(manifest), args.force)
    print(f"Scaffolded {len(manifest['modules'])} module folders.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
