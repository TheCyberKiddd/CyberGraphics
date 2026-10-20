# cyber_shaders

**Status:** planned · **Layer:** 2 · **Kind:** graphics

Shader language, lexer, parser, type checker, IR, HLSL/SPIR-V/MSL code generation, reflection, and permutation cache.

The binding contract for this module lives in [docs/MODULE.md](docs/MODULE.md).
Read it before adding code or changing a public header.

## Layout

- `include/cyber/shaders/` — public headers, self-contained.
- `src/` — private implementation.
- `tests/` — unit tests owned by this module.

The folder exists from day one so the architecture is navigable. A CMake
target is added only when the module's phase starts.

## Roadmap

Phases 16, 17.
