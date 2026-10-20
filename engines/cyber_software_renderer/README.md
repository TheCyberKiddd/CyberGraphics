# cyber_software_renderer

**Status:** planned · **Layer:** 4 · **Kind:** product

CPU reference renderer: framebuffer, rasterization, depth, textures, shaders, golden images, and the second plugin.

The binding contract for this module lives in [docs/MODULE.md](docs/MODULE.md).
Read it before adding code or changing a public header.

## Layout

- `include/cyber/software/` — public headers, self-contained.
- `src/` — private implementation.
- `tests/` — unit tests owned by this module.

The folder exists from day one so the architecture is navigable. A CMake
target is added only when the module's phase starts.

## Roadmap

Phases 6, 7, 8, 9, 25.
