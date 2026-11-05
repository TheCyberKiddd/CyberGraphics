# cyber_shaders — module contract

**Kind:** graphics
**Layer:** 2
**Order:** 21
**Status:** active
**Owner:** @TheCyberKiddd

## Purpose

Phase 16's typed shader language, lexer, parser, intermediate representation,
diagnostics, and HLSL source generator. Reflection, permutations, and cache are
Phase 17 work.

## Public API

`include/cyber/shaders/`

`compiler.h` defines `Stage`, `Diagnostic`, `CompiledShader`, and
`Compile(StringView sourceName, StringView source, Allocator&)`. Compilation
returns allocator-owned HLSL text or a diagnostic that borrows the source name
and includes a one-based line and column. The compiler does not depend on
`cyber_rhi`; callers pass its generated text to the RHI shader API.

Only headers under this path are public. Everything under `src/` is private.
Public headers must compile on their own and must not include another module's
private headers.

## Dependencies

Allowed: cyber_core, cyber_math, cyber_memory, cyber_platform, cyber_jobs

Forbidden: cyber_renderer, cyber_graphics

Dependencies are one-way. A module may depend only on modules with a lower
`order` value, or on a declared contract such as `cybergraphics_api`.

## Invariants

- Compiler errors carry file, line, and column.
- The compiler never crashes on malformed input; parsers are fuzz-tested.
- Reflection is the single source of truth for bindings.
- All compiler allocations use the caller-provided allocator.
- Phase 16 emits HLSL text only; it does not compile platform bytecode.

## Non-goals

- Direct DXIL generation in the first pass.
- Material parameter UI.

## Tests

Golden HLSL output tests, expected-error tests, malformed-input tests, and a
D3D12 cube comparison that consumes generated vertex and pixel HLSL.

## Budgets

Incremental shader compile within 1 s on the target machine; cache hits avoid recompilation.

## Roadmap

Phases 16, 17.
