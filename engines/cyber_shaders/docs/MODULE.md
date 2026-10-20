# cyber_shaders — module contract

**Kind:** graphics
**Layer:** 2
**Order:** 21
**Status:** planned
**Owner:** @TheCyberKiddd

## Purpose

Shader language, lexer, parser, type checker, IR, HLSL/SPIR-V/MSL code generation, reflection, and permutation cache.

## Public API

`include/cyber/shaders/`

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

## Non-goals

- Direct DXIL generation in the first pass.
- Material parameter UI.

## Tests

Golden compile tests, expected-error tests, fuzz tests, and reflection layout tests.

## Budgets

Incremental shader compile within 1 s on the target machine; cache hits avoid recompilation.

## Roadmap

Phases 16, 17.
