# Engine modules

This file is generated from [`modules.json`](modules.json). Do not edit it by hand.

Each folder is an independently buildable module with a written contract in
`docs/MODULE.md`. A folder may exist before its CMake target is created; that is
deliberate, so the architecture is visible from the start.

## Dependency law

1. Dependencies point downward in `order`, never upward.
2. A module may only depend on a module with a lower `order`, or on a declared
   contract such as `cybergraphics_api`.
3. No module includes another module's `src/`.
4. `scripts/layer_check.py` enforces this in CI.

## Modules

| Module | Layer | Order | Kind | Status | Depends on | Roadmap |
|---|---:|---:|---|---|---|---|
| [`cyber_core`](cyber_core/) | 0 | 0 | foundation | active | — | 2 |
| [`cyber_math`](cyber_math/) | 0 | 1 | foundation | active | cyber_core | 3 |
| [`cyber_memory`](cyber_memory/) | 0 | 2 | foundation | active | cyber_core | 4 |
| [`cyber_platform`](cyber_platform/) | 1 | 10 | platform | active | cyber_core<br>cyber_memory | 5 |
| [`cyber_perf`](cyber_perf/) | 1 | 11 | service | active | cyber_core<br>cyber_platform | 5, 15, 36 |
| [`cyber_jobs`](cyber_jobs/) | 1 | 12 | service | active | cyber_core<br>cyber_memory<br>cyber_platform<br>cyber_perf | 5 |
| [`cyber_stability`](cyber_stability/) | 1 | 13 | service | planned | cyber_core<br>cyber_memory<br>cyber_platform<br>cyber_perf | 2, 15, 40 |
| [`cyber_rhi`](cyber_rhi/) | 2 | 20 | graphics | planned | cyber_core<br>cyber_math<br>cyber_memory<br>cyber_platform<br>cyber_jobs<br>cyber_perf<br>cyber_stability | 10, 11, 12, 13, 14, 15, 34, 35 |
| [`cyber_shaders`](cyber_shaders/) | 2 | 21 | graphics | planned | cyber_core<br>cyber_math<br>cyber_memory<br>cyber_platform<br>cyber_jobs | 16, 17 |
| [`cyber_assets`](cyber_assets/) | 2 | 22 | graphics | planned | cyber_core<br>cyber_math<br>cyber_memory<br>cyber_platform<br>cyber_jobs<br>cyber_rhi | 9, 24, 33 |
| [`cyber_scene`](cyber_scene/) | 2 | 23 | graphics | planned | cyber_core<br>cyber_math<br>cyber_memory<br>cyber_jobs<br>cyber_assets | 22, 23 |
| [`cyber_renderer`](cyber_renderer/) | 3 | 30 | graphics | planned | cyber_core<br>cyber_math<br>cyber_memory<br>cyber_jobs<br>cyber_perf<br>cyber_stability<br>cyber_rhi<br>cyber_shaders<br>cyber_assets<br>cyber_scene | 18, 19, 20, 21, 22, 23, 26, 27, 28, 29, 30, 31, 32, 33, 37 |
| [`cyber_ui`](cyber_ui/) | 3 | 31 | graphics | planned | cyber_core<br>cyber_math<br>cyber_memory<br>cyber_perf<br>cybergraphics_api | 38 |
| [`cyber_capture`](cyber_capture/) | 3 | 32 | service | planned | cyber_core<br>cyber_memory<br>cyber_platform<br>cyber_rhi | 39 |
| [`cyber_graphics`](cyber_graphics/) | 4 | 40 | product | planned | cyber_core<br>cyber_math<br>cyber_memory<br>cyber_platform<br>cyber_jobs<br>cyber_perf<br>cyber_stability<br>cyber_rhi<br>cyber_shaders<br>cyber_assets<br>cyber_scene<br>cyber_renderer<br>cybergraphics_api | 14, 15, 25 |
| [`cyber_software_renderer`](cyber_software_renderer/) | 4 | 41 | product | active | cyber_core<br>cyber_math<br>cyber_memory<br>cyber_platform<br>cyber_jobs | 6, 7, 8, 9, 25 |

## Contracts

| Contract | Public API | Purpose |
|---|---|---|
| `cybergraphics_api` | `include/cybergraphics/` | Stable C ABI implemented by every renderer plugin. |

## Products vs. modules

`cyber_graphics` and `cyber_software_renderer` are products: they implement the
stable `cybergraphics_api` contract and are loaded as plugins. Every other entry
is an engine service or library. Cyber Engine itself lives in its own repository
and consumes this one through the stable API.
