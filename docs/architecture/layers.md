# Layers and dependency law

The layer number is documentation. The **order** number is the enforceable
rule: a module may only depend on modules with a lower order.

## Layers

| Layer | Meaning | Modules |
|---:|---|---|
| 0 | Foundation: dependency-free primitives, maths, memory | `cyber_core`, `cyber_math`, `cyber_memory` |
| 1 | Platform and services: OS, scheduling, profiling, diagnostics | `cyber_platform`, `cyber_perf`, `cyber_jobs`, `cyber_stability` |
| 2 | GPU and data: hardware interface, shaders, assets, scene data | `cyber_rhi`, `cyber_shaders`, `cyber_assets`, `cyber_scene` |
| 3 | Renderer and presentation: graph, passes, UI, capture | `cyber_renderer`, `cyber_ui`, `cyber_capture` |
| 4 | Products: plugins implementing the stable API | `cyber_graphics`, `cyber_software_renderer` |

## Orders

| Order | Module |
|---:|---|
| 0 | `cyber_core` |
| 1 | `cyber_math` |
| 2 | `cyber_memory` |
| 10 | `cyber_platform` |
| 11 | `cyber_perf` |
| 12 | `cyber_jobs` |
| 13 | `cyber_stability` |
| 20 | `cyber_rhi` |
| 21 | `cyber_shaders` |
| 22 | `cyber_assets` |
| 23 | `cyber_scene` |
| 30 | `cyber_renderer` |
| 31 | `cyber_ui` |
| 32 | `cyber_capture` |
| 40 | `cyber_graphics` |
| 41 | `cyber_software_renderer` |

## Allowed and forbidden edges

Allowed examples:

- `cyber_math -> cyber_core`
- `cyber_jobs -> cyber_platform, cyber_memory, cyber_perf`
- `cyber_assets -> cyber_rhi` (for format identifiers)
- `cyber_renderer -> cyber_scene, cyber_rhi, cyber_shaders, cyber_assets`
- `cyber_software_renderer -> cybergraphics_api` (a contract, not a module)

Forbidden examples:

- `cyber_core -> cyber_platform`
- `cyber_platform -> cyber_rhi`
- `cyber_renderer -> cyber_graphics` (a product above it)
- `cyber_software_renderer -> cyber_rhi` (the CPU renderer must stay independent)
- Any module including `cyber_rhi/backends/d3d12/...` except `cyber_rhi` itself.

## What the checker enforces

`scripts/layer_check.py`:

- validates the manifest (unique names and orders, valid status and kind);
- rejects unknown dependencies and upward dependencies;
- detects cycles;
- checks `cyber_add_module(DEPENDS ...)` and `target_link_libraries(...)`
  against the declared contract;
- scans source includes and rejects undeclared cross-module includes.

`scripts/check_structure.py` additionally checks that every module has its
contract, docs, and expected directories, and that only active modules have
CMake targets.

## Adding a module

1. Add an entry to `engines/modules.json` with name, short name, layer, order,
   kind, status, purpose, public API, dependencies, invariants, non-goals,
   tests, budgets, and roadmap phases.
2. Run `python scripts/scaffold_modules.py` to create the folder, README, and
   contract stub.
3. Fill in the contract by hand; the generated text is a starting point.
4. Do not create `CMakeLists.txt` until the module's phase starts.
5. Run `python scripts/check_structure.py` and `python scripts/layer_check.py`.
