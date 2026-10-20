# Requirements

This note is the project-level statement of what CyberGraphics must do and what
it must never do. Module contracts refine it; they do not override it. When the
roadmap and this note disagree, stop and resolve the conflict before writing
code.

## 1. Purpose

CyberGraphics is Cyber Engine's own renderer. It must render real-time scenes on
the GPU, provide a CPU reference renderer for correctness, and expose a stable
plugin interface to the engine. It is built from engine-grade modules with
explicit contracts, not from a single application-sized codebase.

## 2. Scope

In scope:

- a rendering hardware interface (RHI) with a D3D12 backend first, then Vulkan
  and Metal;
- a renderer with a render graph, materials, lighting, shadows, post-processing,
  and modern anti-aliasing;
- a software renderer that implements the same product API;
- a shader compiler and reflection pipeline;
- an asset pipeline for meshes, textures, and materials;
- platform, job, memory, profiling, and stability services needed by the
  renderer;
- developer tools for testing, cooking, capturing, and inspecting frames;
- runnable samples and a stub host that proves the plugin boundary.

Out of scope for this repository:

- gameplay, physics, audio, animation, networking, and scripting;
- a full editor or UI framework beyond the renderer's own overlay;
- a general-purpose engine runtime that duplicates Cyber Engine;
- third-party rendering libraries or copied engine source.

## 3. Functional requirements

| ID | Requirement | Owner modules | Roadmap |
|---|---|---|---|
| FR-1 | The repository is split into independently buildable modules with a one-way dependency law. | all | 1 |
| FR-2 | A module may depend only on modules with a lower `order`, or on the declared `cybergraphics_api` contract. | all | 1 |
| FR-3 | The RHI hides D3D12, Vulkan, and Metal behind one API; no other module includes backend headers. | `cyber_rhi` | 10–15 |
| FR-4 | The first GPU backend is D3D12 and runs on Windows 10/11 with feature level 12_1. | `cyber_rhi` | 10–15 |
| FR-5 | A null RHI backend validates every public operation and runs on machines without a GPU. | `cyber_rhi` | 12 |
| FR-6 | The renderer records frames through a render graph with explicit resource lifetimes and barriers. | `cyber_renderer` | 20–21 |
| FR-7 | Materials, lighting, shadows, and post-processing are renderer features, not RHI features. | `cyber_renderer` | 18–37 |
| FR-8 | The software renderer implements the same product API and produces a reference image for GPU comparisons. | `cyber_software_renderer` | 6–9, 25 |
| FR-9 | The shader toolchain compiles the project shader language to HLSL first, then to SPIR-V and MSL, and emits reflection. | `cyber_shaders`, `cgc` | 16–17 |
| FR-10 | The asset pipeline imports glTF, images, and materials; cooked data is versioned, checksummed, and memory-mappable. | `cyber_assets`, `cooker` | 9, 24, 33 |
| FR-11 | The product ABI is a versioned C interface with opaque handles, capability negotiation, and error codes. | `cybergraphics_api` | 25 |
| FR-12 | Both GPU and software renderers load through the same plugin entry point and pass one conformance suite. | products | 25 |
| FR-13 | Platform code owns windows, input events, and timing; it does not own rendering. | `cyber_platform` | 5 |
| FR-14 | Jobs have explicit dependencies, support work stealing, and never require a job to wait on work that cannot run. | `cyber_jobs` | 5 |
| FR-15 | Memory ownership is explicit: arenas, pools, free lists, and frame rings; handles are generation-checked. | `cyber_memory` | 4 |
| FR-16 | Profiling exposes CPU and GPU timings, counters, and frame markers without changing renderer behavior when disabled. | `cyber_perf` | 5, 15, 36 |
| FR-17 | Stability services provide crash handling, validation, and device-lost recovery; failures produce a report. | `cyber_stability` | 2, 15, 40 |
| FR-18 | Tools (test runner, shader compiler, cooker, capture) use public module APIs and are never linked into shipping targets. | `tools/*` | 2, 16, 24, 39 |
| FR-19 | Samples and the host app run the same scenes on the GPU and software paths and compare them. | `samples/*` | 14, 25 |
| FR-20 | Every phase produces a runnable artifact, a test, a phase report, and any necessary ADR. | all | 1–44 |

## 4. Non-functional requirements

| ID | Requirement |
|---|---|
| NFR-1 | Runtime code uses only the C++ standard library and operating system or driver headers. No third-party runtime code. |
| NFR-2 | Runtime code is C++20, compiled with exceptions and RTTI disabled. |
| NFR-3 | Public module headers are self-contained, compile on their own, and do not leak implementation types. |
| NFR-4 | The stable product ABI is C-only, versioned, additive, and never exposes STL types or C++ objects. |
| NFR-5 | No global mutable state. Contexts and allocators are passed explicitly. |
| NFR-6 | A frame budget is set per phase and measured; performance claims name the machine, driver, and build. |
| NFR-7 | Resize, minimize, shutdown, device-lost, and out-of-memory are normal tested paths, not exceptional ones. |
| NFR-8 | Thread ownership is documented; no mutable object is shared without a published snapshot or a queue. |
| NFR-9 | Rendering is deterministic for a fixed scene, seed, resolution, and backend. Golden images use raw pixel comparison with a recorded tolerance. |
| NFR-10 | Parsers and the shader front end treat input as hostile and are fuzz-tested. |
| NFR-11 | Builds are warning-clean at `/W4` (MSVC) or `-Wall -Wextra` (clang) with warnings as errors in CI. |
| NFR-12 | A clean configure, build, and test must work on the CI images without manual setup. |
| NFR-13 | Documentation is written before code for each phase: reading notes, design note, ADR when needed, then report with evidence. |
| NFR-14 | The project remains proprietary until Phase 43; no public release or license change before that decision. |
| NFR-15 | The repository can live standalone and later become a Cyber Engine submodule without changing module APIs. |

## 5. Constraints

- **C-1:** Windows is the primary development platform; MSVC v143 / Windows SDK
  is the reference toolchain.
- **C-2:** CI runs Windows MSVC and Ubuntu clang builds. Linux also runs
  sanitizers.
- **C-3:** CMake 3.28 or newer, Ninja, CMake presets v6.
- **C-4:** Math is right-handed, +Y up, column vectors, column-major matrices,
  0..1 clip depth with reverse-Z, radians, meters, linear RGB. See
  [math-conventions.md](math-conventions.md).
- **C-5:** The local development GPU may lack ray tracing and mesh shaders; those
  features degrade cleanly and are not required for the first renderer.
- **C-6:** Assets used by tests are small and versioned; large binaries need an
  ADR and an external store.
- **C-7:** No copied engine code. Ideas come from specifications, papers, and
  talks, and sources are listed in reading notes and design notes.
- **C-8:** Git commits use the repository owner's identity; do not rewrite
  global git configuration.

## 6. Quality gates

A phase is complete only when all of the following are true:

1. The design note and reading notes exist.
2. The module contract is updated if a public interface changed.
3. The code builds in Debug and Release; Shipping where relevant.
4. Unit tests pass, plus the phase's integration, golden, fuzz, or stress tests.
5. `scripts/check_structure.py` and `scripts/layer_check.py` pass.
6. Sanitizer or validation runs pass where the phase requires them.
7. The phase report records the exact commands, machine, driver, and results.
8. The owner has reviewed the report and approved starting the next phase.

A green build alone is not a complete phase. A report without evidence is not a
complete phase.

## 7. Traceability

| Roadmap part | Modules | Required proof |
|---|---|---|
| Foundation (phases 1–5) | `cyber_core`, `cyber_math`, `cyber_memory`, `cyber_platform`, `cyber_jobs`, `cyber_perf`, `cyber_stability` | Unit tests, sanitizer runs, build checks. |
| Software renderer (6–9) | `cyber_software_renderer`, `cyber_assets` | Reference images and deterministic rasterization tests. |
| RHI (10–15) | `cyber_rhi`, `cyber_graphics`, `cyber_perf`, `cyber_stability` | D3D12 and null-backend tests, WARP CI, validation clean. |
| Shaders and materials (16–19) | `cyber_shaders`, `cyber_renderer` | Golden images, reflection tests, permutation cache tests. |
| Render graph and frame (20–23) | `cyber_renderer`, `cyber_scene` | Barrier validation, graph culling, resize and shutdown stress. |
| Assets and ABI (24–25) | `cyber_assets`, products, `include/cybergraphics` | Cook determinism, plugin conformance, version negotiation. |
| Lighting and effects (26–37) | `cyber_renderer` | Reference comparisons, white-furnace tests, performance budgets. |
| UI, capture, tooling (38–42) | `cyber_ui`, `cyber_capture`, `tools/*` | Overlay tests, capture replay, docs generation. |
| Release (43–44) | all | License decision, packaging, clean-machine verification. |

## 8. Open questions

These do not block Phase 1, but they need an owner decision before the phase
that depends on them:

- Which shader backend order after HLSL: SPIR-V first or MSL first? (Phase 16)
- Which GI approach is budgeted for the target hardware? (Phase 27–30)
- Which GPU runner is available for hardware CI beyond WARP? (Phase 14+)
- Which documentation generator is acceptable under the tooling policy?
  (Phase 42)
- When does the repository become a Cyber Engine submodule? (after Phase 43)

## 9. Change control

Changing a requirement needs a note in the phase report and, when it affects
architecture or the public contract, an ADR. Changing a module's dependency,
public API, or ownership rule is never a silent edit.
