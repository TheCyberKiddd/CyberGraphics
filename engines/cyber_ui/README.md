# cyber_ui

**Status:** planned · **Layer:** 3 · **Kind:** graphics

Immediate-mode UI, debug overlay, text, graphs, console variables, and the render graph viewer.

The binding contract for this module lives in [docs/MODULE.md](docs/MODULE.md).
Read it before adding code or changing a public header.

## Layout

- `include/cyber/ui/` — public headers, self-contained.
- `src/` — private implementation.
- `tests/` — unit tests owned by this module.

The folder exists from day one so the architecture is navigable. A CMake
target is added only when the module's phase starts.

## Roadmap

Phases 38.
