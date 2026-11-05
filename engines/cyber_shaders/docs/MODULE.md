# cyber_shaders — module contract

**Kind:** graphics
**Layer:** 2
**Order:** 21
**Status:** active
**Owner:** @TheCyberKiddd

## Purpose

The typed Cyber shader language and HLSL compiler, including compiler-owned
reflection, keyword permutations, checksummed disk cache, job-system compilation,
and file watching.

## Public API

`include/cyber/shaders/`

`compiler.h`, `reflection.h`, `permutation.h`, `cache.h`, and `library.h` expose
the compiler, compiler-owned reflection, stable permutation keys, an on-disk
shader cache, and the asynchronous `ShaderLibrary`. `CompileHlsl` returns
allocator-owned HLSL plus reflection or a diagnostic borrowing the source name
and containing a one-based line and column. `cyber_shaders` has no RHI
dependency; callers pass generated HLSL and reflection to `cyber_renderer`.

Only headers under this path are public. Everything under `src/` is private.
Public headers must compile on their own and must not include another module's
private headers.

## Dependencies

Allowed: cyber_core, cyber_memory, cyber_platform, cyber_jobs

Forbidden: cyber_renderer, cyber_graphics

Dependencies are one-way. A module may depend only on modules with a lower
`order` value, or on a declared contract such as `cybergraphics_api`.

## Invariants

- Compiler errors carry file, line, and column.
- The compiler never crashes on malformed input; parsers are fuzz-tested.
- Reflection is produced from the typed source and is the single source of
  truth for bindings and constant-buffer packing.
- All compiler allocations use the caller-provided allocator.
- Cache payloads and shader source are treated as hostile input.
- Failed hot reloads retain the last successfully published shader version.
- Worker allocations are serialized through a private allocator adapter.
- The module emits HLSL text only; it does not compile platform bytecode.

## Non-goals

- Direct DXIL generation in the first pass.
- Material parameter UI.

## Tests

Golden HLSL and reflection tests, permutation and cache corruption tests,
parallel library and hot-reload tests, and a D3D12 sample using generated HLSL.

## Budgets

Shader source changes publish within 1 s on the target machine; cache hits avoid
front-end recompilation.

## Roadmap

Phases 16, 17.
