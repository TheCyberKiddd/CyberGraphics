# Samples

Each sample is runnable, built by the CMake presets, and covered by a test once
the relevant phase exists. A sample that is not tested will break.

| Folder | Purpose | Phase |
|---|---|---|
| `01_triangle/` | First RHI triangle. | 14 |
| `02_cube/` | Textured, rotating cube through the RHI. | 14 |
| `03_materials/` | Material definitions and instances. | 18 |
| `04_lighting/` | PBR sphere grid and light types. | 26 |
| `host_app/` | Stub Cyber Engine host that loads renderer plugins. | 25 |

Each sample has a README with the exact command to build and run it, and the
expected result.
