# cyber_assets

**Status:** planned · **Layer:** 2 · **Kind:** graphics

Importers and decoders for glTF, OBJ, PNG, JPEG, DDS and KTX; cooked formats, asset database, and cooking.

The binding contract for this module lives in [docs/MODULE.md](docs/MODULE.md).
Read it before adding code or changing a public header.

## Layout

- `include/cyber/assets/` — public headers, self-contained.
- `src/` — private implementation.
- `tests/` — unit tests owned by this module.

The folder exists from day one so the architecture is navigable. A CMake
target is added only when the module's phase starts.

## Roadmap

Phases 9, 24, 33.
