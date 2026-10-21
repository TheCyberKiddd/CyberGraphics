# AGENTS.md

CyberGraphics is the from-scratch renderer for **Cyber Engine**, a serious
commercial C++20 game engine for Windows. This repository is renderer-only: it
becomes a Cyber Engine submodule or installed CMake package and does not contain
gameplay, editor, or CyberUI code.

## Project shape

All runtime code lives in `engines/<module>/`, with a written contract in
`docs/MODULE.md`, public headers under `include/cyber/<short>/`, private code
under `src/`, and tests under `tests/`.

- Foundation: `cyber_core`, `cyber_math`, `cyber_memory`.
- Services: `cyber_platform`, `cyber_jobs`, `cyber_perf`, `cyber_stability`.
- Graphics: `cyber_rhi`, `cyber_shaders`, `cyber_assets`, `cyber_scene`,
  `cyber_renderer`.
- Products: `cyber_graphics`, `cyber_software_renderer`.
- `include/cybergraphics/` is the stable C ABI and stays empty until Phase 25.
- `engines/modules.json` is the source of truth for layers and dependencies.
  `engines/MODULES.md` is generated from it; do not hand-edit the generated file.

## Read first

At the start of every session:

1. Read this file.
2. Read `docs/ROADMAP.md`.
3. Read `docs/HANDOFF.md` if it exists.
4. Read `docs/graphics/CYBERGRAPHICS_ROADMAP.md` and
   `docs/graphics/CYBERGRAPHICS_BUILD_GUIDE.md` before any rendering work.
5. Read the current module contract, design note, report, and ADR for the phase.

Those two graphics documents are binding specifications, not suggestions.
Follow every rule and phase in them. If the roadmap or `docs/ROADMAP.md` is
missing, stop and ask the owner before phase work. If a request conflicts with
the roadmap, this file, or a module contract, stop and ask the owner.

## Hard rules

- Runtime code is C++20 with exceptions and RTTI disabled.
- Runtime dependencies are the C++ standard library, OS headers, and driver
  headers only. No third-party rendering or runtime libraries.
- CyberGraphics is built from scratch with its own RHI. Only `cyber_rhi` may
  include D3D12, Vulkan, or Metal headers. Engine code never calls Vulkan,
  OpenGL, or D3D directly.
- No global mutable state. Contexts, allocators, and devices are passed
  explicitly. GPU and scene objects use generation-counted handles.
- Math is right-handed, +Y up, column vectors, column-major matrices, 0..1
  reverse-Z, radians, meters, and linear RGB.
- The stable API is C-only, versioned, additive, and lives only in
  `include/cybergraphics/`. Do not freeze it before Phase 25.
- The project is proprietary until Phase 43. Third-party code must be MIT, BSD,
  or Apache-2.0; never GPL. Ask the owner before adding any dependency.
- No copied engine or renderer source. Record papers, specifications, and talks
  in reading notes; implement the ideas yourself.
- Do not add editor, CyberUI, Dear ImGui, Qt, or gameplay code to this repo.
- Generated build trees, cooked output, captures, and large binaries are not
  committed. Add an ADR and external store for any large test asset.

## Workflow

- Work one phase at a time in roadmap order. Finish, test, demonstrate, and
  report a phase before starting the next. Do not skip phases.
- Before code, write the design note and any ADR the phase needs. Update the
  module contract before changing a public interface.
- Keep changes modular and small. Do not rewrite working code to make a new
  feature fit; extend the owning module. Do not re-read huge files; search and
  read only the relevant sections.
- Every public function gets a test. Bug fixes get a regression test that fails
  before the fix. Parsers and shader input are treated as hostile.
- A phase is complete only when debug, release, and relevant sanitizer builds
  pass; structure, layer, and formatting checks pass; and the package consumer
  builds when a public target or header changed.
- Tick the phase checkbox in `docs/ROADMAP.md` only after the evidence exists.
- Write `docs/<area>/PHASE_N.md` with what was built, API examples, tests, and
  known issues. Keep `docs/design/phase-NN.md` and
  `docs/reports/phase-NN.md` in sync with the established layout.
- Update `CHANGELOG.md`, `README.md`, and the relevant module docs when status or
  integration targets change.
- If a request conflicts with a rule, stop and ask the owner instead of
  improvising.

## Code style

- Follow `docs/coding-standard.md` and the formatter. 4 spaces, 100 columns,
  Allman braces.
- Types and functions use PascalCase. Variables and parameters use camelCase.
  Members use `m_`, constants use `k`, macros use `CG_`, files use snake_case.
- Use `#pragma once`. Public headers are self-contained, have no
  `using namespace`, and include everything they use.
- Raw pointers and references are non-owning. Use explicit allocators and arenas
  in runtime code. `std::unique_ptr` is for tools and non-hot paths; avoid
  `std::shared_ptr` in renderer hot paths.
- Use `Result<T, E>` / `Status<E>` for recoverable errors, `CG_ASSERT` for
  programmer errors, and `CG_LOG` for diagnostics. Never throw from runtime
  code and never print to stdout from a module.
- Document every public API with Doxygen comments directly above the
  declaration, including parameters, return value, ownership, and failure
  behavior.
- Use fixed-width integer types at boundaries and serialized formats. Check
  overflow instead of relying on wraparound.
- Run `.\scripts\format.ps1` before committing. One logical change per commit,
  imperative mood, short scope prefix such as `rhi:` or `docs:`.

## Handoff

When the owner says "handoff", or the context is nearly full, write
`docs/HANDOFF.md` in under 80 lines containing:

- current state and the last verified commit;
- decisions made and why;
- files changed;
- half-done work and known gaps;
- the exact next command or next step.

Keep it factual. A handoff without a reproducible next step is incomplete.
