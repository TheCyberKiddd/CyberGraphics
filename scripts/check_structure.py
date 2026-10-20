#!/usr/bin/env python3
"""Check that the repository structure and module contracts are intact."""

from __future__ import annotations

import json
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))

import layer_check  # noqa: E402

REQUIRED_PATHS = [
    "CMakeLists.txt",
    "CMakePresets.json",
    "README.md",
    "LICENSE",
    "CHANGELOG.md",
    "CONTRIBUTING.md",
    "SECURITY.md",
    "THIRD_PARTY_NOTICES.md",
    ".clang-format",
    ".editorconfig",
    ".gitattributes",
    ".gitignore",
    "cmake",
    "cmake/CyberOptions.cmake",
    "cmake/CyberPackage.cmake",
    "cmake/CyberGraphicsConfig.cmake.in",
    "cmake/version_generated.h.in",
    "engines",
    "engines/MODULES.md",
    "engines/modules.json",
    "include/cybergraphics",
    "include/cybergraphics/README.md",
    "tools/test",
    "tools/cgc",
    "tools/cooker",
    "tools/capture",
    "tools/docgen",
    "tools/ci",
    "samples/host_app",
    "tests/integration",
    "tests/integration/package_consumer",
    "tests/integration/package_consumer/CMakeLists.txt",
    "tests/integration/package_consumer/main.cpp",
    "tests/integration/source_consumer",
    "tests/integration/source_consumer/CMakeLists.txt",
    "tests/golden",
    "tests/fuzz",
    "tests/stress",
    "tests/benchmarks",
    "tests/fixtures",
    "tests/images",
    "docs/README.md",
    "docs/math-conventions.md",
    "docs/coding-standard.md",
    "docs/glossary.md",
    "docs/architecture/overview.md",
    "docs/architecture/integration.md",
    "docs/architecture/layers.md",
    "docs/architecture/module-contract.md",
    "docs/architecture/threading.md",
    "docs/architecture/error-model.md",
    "docs/architecture/abi-policy.md",
    "docs/design/README.md",
    "docs/design/foundation-hardening.md",
    "docs/adr/0000-template.md",
    "docs/adr/0001-module-architecture.md",
    "docs/adr/0002-math-and-depth-conventions.md",
    "docs/adr/0003-integration-and-packaging.md",
    "docs/notes/README.md",
    "docs/reports/README.md",
    "docs/reports/foundation-hardening.md",
    "docs/specs/README.md",
    "docs/testing/README.md",
    "docs/perf/README.md",
    "scripts/build.ps1",
    "scripts/test.ps1",
    "scripts/format.ps1",
    "scripts/bootstrap.ps1",
    "scripts/layer_check.py",
    "scripts/check_structure.py",
    "scripts/check_format.py",
    "scripts/verify_package.ps1",
    "scripts/scaffold_modules.py",
    "third_party/README.md",
    ".github/workflows/ci.yml",
    ".github/dependabot.yml",
]

CONTRACT_HEADINGS = [
    "## Purpose",
    "## Public API",
    "## Dependencies",
    "## Invariants",
    "## Non-goals",
    "## Tests",
    "## Budgets",
    "## Roadmap",
]

FORBIDDEN_ROOT_PATHS = [
    "src",
    "include/cyber",
]


def check_required_paths(errors: list[str]) -> None:
    for relative in REQUIRED_PATHS:
        if not (ROOT / relative).exists():
            errors.append(f"missing required path: {relative}")
    for relative in FORBIDDEN_ROOT_PATHS:
        if (ROOT / relative).exists():
            errors.append(
                f"unexpected root path: {relative} (code belongs to a module under engines/)"
            )


def check_presets(errors: list[str]) -> None:
    path = ROOT / "CMakePresets.json"
    if not path.exists():
        return
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except json.JSONDecodeError as exc:
        errors.append(f"CMakePresets.json is not valid JSON: {exc}")
        return
    names = {preset.get("name") for preset in data.get("configurePresets", [])}
    for required in ("debug", "release", "shipping", "ci-windows", "ci-linux"):
        if required not in names:
            errors.append(f"CMakePresets.json is missing configure preset: {required}")


def check_modules(errors: list[str]) -> None:
    manifest = layer_check.load_manifest()
    for module in manifest["modules"]:
        name = module["name"]
        base = ROOT / "engines" / name
        for relative in (
            "",
            "README.md",
            "docs/MODULE.md",
            f"include/cyber/{module['short']}",
            "src",
            "tests",
        ):
            path = base / relative
            if not path.exists():
                errors.append(f"{name}: missing {relative or '.'}")

        contract = base / "docs" / "MODULE.md"
        if contract.exists():
            text = contract.read_text(encoding="utf-8")
            for heading in CONTRACT_HEADINGS:
                if heading not in text:
                    errors.append(f"{name}: contract is missing heading {heading!r}")
            if module["status"] not in text:
                errors.append(f"{name}: contract does not contain status {module['status']!r}")

        cmake = base / "CMakeLists.txt"
        if module["status"] == "planned" and cmake.exists():
            errors.append(f"{name}: planned module has a CMakeLists.txt")
        if module["status"] in {"scaffolded", "active", "stable"} and not cmake.exists():
            errors.append(f"{name}: {module['status']} module is missing CMakeLists.txt")

    core = ROOT / "engines" / "cyber_core"
    for relative in (
        "include/cyber/core/version.h",
        "src/version.cpp",
        "tests/version_test.cpp",
        "CMakeLists.txt",
    ):
        if not (core / relative).exists():
            errors.append(f"cyber_core: scaffold is missing {relative}")


def check_third_party(errors: list[str]) -> None:
    root = ROOT / "third_party"
    if not root.exists():
        return
    allowed = {root / "README.md"}
    for path in root.rglob("*"):
        if path.is_file() and path not in allowed:
            errors.append(
                f"third_party must stay empty by policy; found {path.relative_to(ROOT)}"
            )


def main() -> int:
    errors: list[str] = []
    check_required_paths(errors)
    check_presets(errors)
    check_modules(errors)
    check_third_party(errors)
    errors.extend(layer_check.run_checks())

    if errors:
        print("Structure check failed:", file=sys.stderr)
        for error in errors:
            print(f"  - {error}", file=sys.stderr)
        return 1

    manifest = layer_check.load_manifest()
    print(
        f"Structure check passed: {len(manifest['modules'])} modules, "
        f"{len(manifest.get('contracts', []))} contract(s)."
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
