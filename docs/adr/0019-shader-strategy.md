# 0019 — A typed Cyber shader language that emits HLSL

- **Status:** accepted
- **Date:** 2026-11-05
- **Deciders:** @TheCyberKiddd
- **Related:** Phase 16, [design note](../design/phase-16-shader-language.md),
  [cyber_shaders contract](../../engines/cyber_shaders/docs/MODULE.md),
  [ADR 0017](0017-d3d12-backend.md)

## Context

The D3D12 backend currently accepts HLSL text and compiles it with the Windows
SDK. Phase 16 requires the cube shader to come from CyberGraphics' own typed
language, while later backends must not make every shader asset depend on
D3D12 syntax. Writing DXIL directly would duplicate a large, evolving compiler
target, and emitting SPIR-V or MSL is not needed to prove the first shader path.

## Decision

1. Define a small typed source language in `cyber_shaders`, scoped to the
   existing textured cube's vertex and pixel stages.
2. Lex, parse, type-check, and lower that source into an explicit typed IR
   before emitting deterministic HLSL.
3. Keep `cyber_shaders` independent of `cyber_rhi`; it owns no device, backend,
   or platform compiler state. Its output is allocator-owned text.
4. Pass generated HLSL through the existing RHI shader creation path. On
   Windows, D3D12 continues to use the Windows SDK `D3DCompile`.
5. Keep diagnostics tied to the original source name and one-based line and
   column. Reject malformed or unsupported input rather than emitting fallback
   shader code.
6. Bound Phase 16 to language and HLSL generation. Reflection, permutations,
   cache, and hot reload remain Phase 17; materials remain Phase 18.

## Consequences

### Positive

- The first renderer-facing shader uses CyberGraphics-owned syntax and
  type-checking without requiring a new runtime dependency.
- D3D12 keeps using its existing, SDK-provided HLSL compiler.
- A later backend can receive a new code generator without changing the source
  grammar or the typed IR.
- The generated cube shader exercises the same backend pipeline and image
  comparison as the current inline HLSL.

### Negative

- The first language is intentionally narrow and cannot express general shader
  programs; it must grow only when a later sample requires a feature.
- HLSL remains a backend dependency for the first implementation and does not
  provide Vulkan or Metal shader output by itself.
- Maintaining a parser, type checker, IR, and code generator is additional
  project-owned work, including hostile-input tests.

## Alternatives considered

| Alternative | Reason not selected |
|---|---|
| HLSL as the source language | It would not satisfy the roadmap's requirement that a shader in the project's own language compile to HLSL. |
| Write a compiler backend for each graphics API immediately | It adds DXIL, SPIR-V, and MSL work before there is a renderer feature or second backend that needs it. |
| Generate DXIL directly | DXIL is a complex compiler target and unnecessary while the Windows SDK compiler is available. |
| Add a third-party shader toolchain | The project forbids third-party runtime/rendering dependencies and requires owner approval for dependencies. |

## Follow-up

- `cyber_shaders` owns the language, compiler API, diagnostics, and tests.
- The Phase 14 cube comparison consumes generated vertex and pixel HLSL.
- Phase 17 may extend the IR with reflection and asynchronous compilation; it
  must not silently broaden the v1 grammar.
