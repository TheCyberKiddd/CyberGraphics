# 0001 — Repository and module architecture

- **Status:** accepted
- **Date:** 2026-10-20
- **Deciders:** CyberEngine (owner)
- **Related:** Phase 1, [requirements](../requirements.md),
  [architecture overview](../architecture/overview.md)

## Context

CyberGraphics is a from-scratch renderer built over 44 phases. It must remain
comprehensible as it grows from a version header to a GPU renderer, a software
reference renderer, a shader compiler, and an asset pipeline. It must also stay
independent of Cyber Engine until it is ready to become a submodule.

A conventional application layout (one `src/` tree, one library) is fast for the
first month and expensive for the next three years. The project needs boundaries
that a compiler and a checker can enforce, not a convention that reviewers must
remember.

## Decision

1. **Modules are the unit of ownership.** Every file lives in exactly one
   module under `engines/`. There is no root `src/`.
2. **Every module has a written contract** at
   `engines/<module>/docs/MODULE.md`, generated initially from
   `engines/modules.json` and then maintained by hand.
3. **Dependencies are ranked by `order`, not by layer.** A module may depend
   only on a module with a lower order. Layers are documentation.
4. **Only two modules are products:** `cyber_graphics` and
   `cyber_software_renderer`. They implement the stable contract and are not
   consumed by other modules. Everything else is a foundation, platform,
   service, graphics, or supporting module.
5. **The stable ABI lives outside the module tree**, in
   `include/cybergraphics/`. Module headers are internal C++ interfaces and are
   not binary-stable.
6. **The whole module tree exists from Phase 1**, but a module gets a CMake
   target only when its phase starts. Planned modules have folders and
   contracts, not empty targets.
7. **The dependency law is executable.** `scripts/layer_check.py` and
   `scripts/check_structure.py` run in CI and fail on upward dependencies,
   cycles, undeclared includes, missing contracts, and premature targets.
8. **Third-party runtime code is excluded by policy**, with `third_party/`
   kept empty and reviewed through an ADR if that ever changes.

## Consequences

### Positive

- A new contributor can find the owner of any behavior from the folder name.
- The checker catches architectural mistakes before the compiler sees them.
- Products can be swapped (GPU and software) behind one contract.
- The repository can be moved into Cyber Engine as a submodule without
  changing module APIs.
- Planned modules are visible early without pretending to be implemented.

### Negative

- More folders and CMake files than a small renderer needs at the start.
- Cross-module changes require touching contracts and, sometimes, the manifest.
- The `order` table must be maintained deliberately when a module is inserted.

### Neutral

- The module list will grow. Orders are spaced by ten to make insertion cheap.
- Some modules may merge if their contracts turn out to be inseparable; that
  requires an ADR, not a silent move.

## Alternatives considered

| Alternative | Why not |
|---|---|
| Single root `src/` and one library | Cannot enforce ownership; later extraction is a rewrite. |
| Header-only modules | Compile-time cost and hidden coupling; no place for private state. |
| Layer numbers as the only rule | Layers are coarse; several modules share a layer and still must not depend on each other. |
| Put the C ABI inside `engines/cyber_graphics/` | Ties the contract to one implementation and lets plugin details leak. |
| Create all CMake targets now | Empty targets create false dependencies and hide which modules are real. |
| Git submodules for every module | Overhead without isolation benefit at this scale; folders and checks are enough. |

## Follow-up

- Phase 2 adds `cyber_core`'s primitives and the project test runner.
- Each later phase adds its CMake target and updates its contract.
- Before Phase 43, review whether any module should split or merge, and record
  the decision.
