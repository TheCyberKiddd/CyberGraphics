# 0020 — Compiler-owned reflection, keyword permutations, and renderer-side hot pipelines

- **Status:** accepted
- **Date:** 2026-11-05
- **Deciders:** @TheCyberKiddd
- **Related:** Phase 17, [design note](../design/phase-17-shader-permutations.md),
  [ADR 0017](0017-d3d12-backend.md), [ADR 0019](0019-shader-strategy.md)

## Context

Phase 17 needs bindings derived from our compiler, shader keywords, an on-disk
cache, job-system compilation, asynchronous pipeline creation, and hot reload.
`cyber_shaders` (order 21) may not depend on `cyber_rhi` (order 20) by its
contract, and `cyber_rhi` cannot depend on the compiler without inverting the
module order. Something has to turn reflection into RHI layouts and pipelines.

## Decision

1. Reflection is produced by our own front end from the typed program, never by
   parsing HLSL or querying `ID3D12ShaderReflection`. The generated HLSL pins
   every constant with `packoffset` so the backend cannot disagree.
2. Permutations are boolean keywords declared in the source with `when`/`else`
   blocks, capped at 8 keywords (256 permutations) per shader. A permutation's
   identity is a 64-bit hash of its sorted enabled keyword names.
3. The cache, the job-system library, and file watching live in `cyber_shaders`
   and use only `cyber_core`, `cyber_memory`, `cyber_platform`, and
   `cyber_jobs`. The cache key is source hash, compiler version, permutation
   hash, and target; the full key is stored in every entry.
4. `cyber_renderer` becomes active in Phase 17 with two components that join
   the RHI and the compiler: reflected layout building and hot graphics
   pipelines swapped at a frame boundary. It links only the modules it uses
   (`cyber_core`, `cyber_memory`, `cyber_jobs`, `cyber_rhi`, `cyber_shaders`);
   `cyber_assets` and `cyber_scene` stay planned.
5. The reflected layout follows the D3D12 convention already in ADR 0017: space
   `S` is bind-group slot `S`, resources and samplers are ordinal in register
   order, and the `b0, space0` block is the push-constant range (at most 128
   bytes, the Vulkan guaranteed minimum).

## Consequences

### Positive

- Bindings have one source of truth; changing a shader cannot silently break a
  hand-written layout.
- The compiler and cache remain usable by offline tools without a GPU.
- Hot reload never blocks the render thread and never presents a frame with a
  half-swapped program.

### Negative

- The cache stores our compiler's output, not backend bytecode, so a warm start
  still runs `D3DCompile` inside `CreateShader`. Caching bytecode needs an
  additive RHI API to export compiled code; deferred and recorded as a gap.
- Only one constant buffer (the push-constant block) is bindable until Phase 19.
- Activating `cyber_renderer` this early fixes its first public headers; they
  are internal C++ API and remain free to change before Phase 25.

### Neutral

- The keyword limit is a compile-time constant and can be raised later without
  changing key hashing.

## Alternatives considered

| Alternative | Why not |
|---|---|
| Reflect with `ID3D12ShaderReflection` | Backend-specific, contradicts "reflection is the single source of truth", and is unavailable to a Vulkan or Metal backend. |
| Preprocessor `#if` permutations | Needs a macro preprocessor the language deliberately lacks; `when` blocks keep every branch parsed and diagnosable. |
| Put the RHI glue in `cyber_shaders` | Breaks the module order and the contract that the compiler has no RHI dependency. |
| Put the glue in the sample | The material system (Phase 18) needs the same code; samples must not own engine logic. |
| `ReadDirectoryChangesW` watcher | Same last-write signal, plus buffer-overflow loss; the existing polling watcher meets the one-second target. |

## Follow-up

- Phase 18 builds materials on `HotGraphicsPipeline` and reflected layouts.
- Phase 19 adds constant streaming and lifts the single-constant-buffer limit.
- An RHI bytecode export and a bytecode cache target are tracked as a gap.
</content>
