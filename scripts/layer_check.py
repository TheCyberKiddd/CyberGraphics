#!/usr/bin/env python3
"""Validate the CyberGraphics module dependency law.

The manifest in engines/modules.json is the source of truth. This script checks:

  * module names, layers, orders, and statuses are valid
  * every dependency exists and points to a lower order (or a declared contract)
  * the dependency graph is acyclic
  * CMake module declarations agree with the manifest
  * source files only include public headers from declared dependencies
"""

from __future__ import annotations

import argparse
import json
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
MANIFEST = ROOT / "engines" / "modules.json"
VALID_STATUS = {"planned", "scaffolded", "active", "stable", "deprecated"}
VALID_KINDS = {"foundation", "platform", "service", "graphics", "product"}
CMAKE_KEYWORDS = {
    "NAME",
    "LAYER",
    "DEPENDS",
    "SOURCES",
    "PUBLIC_HEADERS",
    "PRIVATE_DEPS",
}


def load_manifest(path: pathlib.Path = MANIFEST) -> dict:
    with path.open("r", encoding="utf-8") as handle:
        return json.load(handle)


def module_by_name(manifest: dict) -> dict[str, dict]:
    return {module["name"]: module for module in manifest["modules"]}


def module_by_short(manifest: dict) -> dict[str, dict]:
    return {module["short"]: module for module in manifest["modules"]}


def contract_names(manifest: dict) -> set[str]:
    return {contract["name"] for contract in manifest.get("contracts", [])}


def validate_manifest(manifest: dict) -> list[str]:
    errors: list[str] = []
    modules = manifest.get("modules", [])
    names: set[str] = set()
    shorts: set[str] = set()
    orders: set[int] = set()

    for module in modules:
        name = module.get("name", "<unnamed>")
        if name in names:
            errors.append(f"duplicate module name: {name}")
        names.add(name)

        short = module.get("short", "")
        if not short:
            errors.append(f"{name}: missing short name")
        elif short in shorts:
            errors.append(f"{name}: duplicate short name {short}")
        shorts.add(short)

        if module.get("status") not in VALID_STATUS:
            errors.append(f"{name}: invalid status {module.get('status')!r}")
        if module.get("kind") not in VALID_KINDS:
            errors.append(f"{name}: invalid kind {module.get('kind')!r}")

        layer = module.get("layer")
        order = module.get("order")
        if not isinstance(layer, int) or not 0 <= layer <= 4:
            errors.append(f"{name}: layer must be an integer in 0..4")
        if not isinstance(order, int) or order < 0:
            errors.append(f"{name}: order must be a non-negative integer")
        elif order in orders:
            errors.append(f"{name}: duplicate order {order}")
        else:
            orders.add(order)

        for key in ("purpose", "public_api", "tests", "budgets"):
            if not str(module.get(key, "")).strip():
                errors.append(f"{name}: missing {key}")
        for key in ("invariants", "non_goals", "roadmap"):
            if not module.get(key):
                errors.append(f"{name}: missing {key}")

    contracts = contract_names(manifest)
    for module in modules:
        name = module["name"]
        for dep in module.get("depends", []):
            if dep == name:
                errors.append(f"{name}: cannot depend on itself")
            elif dep in contracts:
                continue
            elif dep not in names:
                errors.append(f"{name}: unknown dependency {dep}")
            else:
                dep_module = next(m for m in modules if m["name"] == dep)
                if dep_module["order"] >= module["order"]:
                    errors.append(
                        f"{name} (order {module['order']}) depends on {dep} "
                        f"(order {dep_module['order']}); dependencies must point downward"
                    )
        for forbidden in module.get("forbidden", []):
            if forbidden in module.get("depends", []):
                errors.append(f"{name}: dependency {forbidden} is also listed as forbidden")

    errors.extend(validate_acyclic(modules))
    return errors


def validate_acyclic(modules: list[dict]) -> list[str]:
    by_name = {module["name"]: module for module in modules}
    state: dict[str, int] = {}
    errors: list[str] = []

    def visit(name: str, stack: list[str]) -> None:
        if state.get(name) == 1:
            cycle = " -> ".join(stack + [name])
            errors.append(f"dependency cycle: {cycle}")
            return
        if state.get(name) == 2:
            return
        state[name] = 1
        for dep in by_name[name].get("depends", []):
            if dep in by_name:
                visit(dep, stack + [name])
        state[name] = 2

    for module in modules:
        visit(module["name"], [])
    return errors


def find_call_block(text: str, call: str) -> str | None:
    start = text.find(call)
    if start < 0:
        return None
    open_paren = text.find("(", start + len(call))
    if open_paren < 0:
        return None
    depth = 0
    for index in range(open_paren, len(text)):
        char = text[index]
        if char == "(":
            depth += 1
        elif char == ")":
            depth -= 1
            if depth == 0:
                return text[open_paren + 1 : index]
    return None


def parse_module_depends(block: str) -> list[str]:
    tokens = block.split()
    deps: list[str] = []
    collecting = False
    for token in tokens:
        if token in CMAKE_KEYWORDS:
            collecting = token == "DEPENDS"
            continue
        if collecting:
            deps.append(token)
    return deps


def resolve_dep(dep: str, by_short: dict[str, dict], contracts: set[str]) -> str | None:
    if dep in contracts:
        return dep
    if dep.startswith("cyber_"):
        dep = dep[len("cyber_") :]
    module = by_short.get(dep)
    return module["name"] if module else None


def validate_cmake(manifest: dict) -> list[str]:
    errors: list[str] = []
    modules = module_by_name(manifest)
    by_short = module_by_short(manifest)
    contracts = contract_names(manifest)

    for name, module in modules.items():
        cmake = ROOT / "engines" / name / "CMakeLists.txt"
        if module["status"] == "planned":
            if cmake.exists():
                errors.append(f"{name}: planned module must not have a CMakeLists.txt yet")
            continue
        if not cmake.exists():
            errors.append(f"{name}: {module['status']} module is missing CMakeLists.txt")
            continue

        text = cmake.read_text(encoding="utf-8")
        block = find_call_block(text, "cyber_add_module")
        if block is None:
            errors.append(f"{name}: CMakeLists.txt does not call cyber_add_module")
            continue

        declared = set(module.get("depends", []))
        for dep in parse_module_depends(block):
            resolved = resolve_dep(dep, by_short, contracts)
            if resolved is None:
                errors.append(f"{name}: CMake DEPENDS unknown target {dep}")
            elif resolved not in declared:
                errors.append(
                    f"{name}: CMake DEPENDS {dep} but the contract does not declare {resolved}"
                )

        for match in re.finditer(r"target_link_libraries\s*\(([^)]*)\)", text):
            for alias in re.findall(r"cyber::([A-Za-z0-9_]+)", match.group(1)):
                resolved = resolve_dep(alias, by_short, contracts)
                if resolved is None:
                    errors.append(f"{name}: target_link_libraries uses unknown cyber::{alias}")
                elif resolved not in declared:
                    errors.append(
                        f"{name}: links cyber::{alias} but the contract does not declare {resolved}"
                    )

    return errors


def validate_includes(manifest: dict) -> list[str]:
    errors: list[str] = []
    modules = module_by_name(manifest)
    by_short = module_by_short(manifest)
    contracts = contract_names(manifest)
    include_re = re.compile(r'^\s*#\s*include\s*[<"](cyber/[A-Za-z0-9_]+)/')

    for name, module in modules.items():
        declared = set(module.get("depends", []))
        roots = [
            ROOT / "engines" / name / "include" / "cyber" / module["short"],
            ROOT / "engines" / name / "src",
        ]
        for root in roots:
            if not root.exists():
                continue
            for path in root.rglob("*"):
                if path.suffix not in {".h", ".hpp", ".inl", ".cpp", ".cc"}:
                    continue
                text = path.read_text(encoding="utf-8", errors="replace")
                for line in text.splitlines():
                    match = include_re.match(line)
                    if not match:
                        continue
                    root_name = match.group(1).split("/")[1]
                    if root_name == module["short"]:
                        continue
                    if root_name == "graphics" and "cybergraphics_api" in contracts:
                        resolved = "cybergraphics_api"
                    else:
                        target = by_short.get(root_name)
                        resolved = target["name"] if target else None
                    if resolved is None:
                        errors.append(
                            f"{name}: {path.relative_to(ROOT)} includes unknown module header {match.group(1)}"
                        )
                    elif resolved not in declared:
                        errors.append(
                            f"{name}: {path.relative_to(ROOT)} includes {match.group(1)} "
                            f"but does not declare {resolved}"
                        )
    return errors


def run_checks() -> list[str]:
    manifest = load_manifest()
    errors = validate_manifest(manifest)
    errors.extend(validate_cmake(manifest))
    errors.extend(validate_includes(manifest))
    return errors


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--quiet", action="store_true", help="only print failures")
    args = parser.parse_args()
    manifest = load_manifest()
    errors = run_checks()
    if errors:
        print("Layer check failed:", file=sys.stderr)
        for error in errors:
            print(f"  - {error}", file=sys.stderr)
        return 1
    if not args.quiet:
        print(f"Layer check passed: {len(manifest['modules'])} modules.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
