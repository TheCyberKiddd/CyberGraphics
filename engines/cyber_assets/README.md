# cyber_assets

**Status:** active · **Layer:** 2 · **Kind:** graphics

Importers for glTF, OBJ, PNG, JPEG, DDS and KTX2; versioned cooked formats; a
stable asset database; offline cooking and hot reload. See the
[Phase 24 summary](../../docs/graphics/PHASE_24.md).

The binding contract for this module lives in [docs/MODULE.md](docs/MODULE.md).
Read it before adding code or changing a public header.

## Layout

- `include/cyber/assets/` — public headers, self-contained.
- `src/` — private implementation, including the DEFLATE and JPEG entropy
  decoders.
- `tests/` — unit, fuzz, and benchmark tests owned by this module.

## Pipeline

```text
source bytes ──► importer ──► canonical asset ──► cooker ──► .cooked file
                                                             (versioned,
                                                              checksummed,
                                                              16-byte aligned)
```

`AssetId` is derived from the normalized virtual path, so a reference survives
content edits. The database records source and cooked hashes, byte sizes,
import settings, and dependency edges; the cook cache compares those values
and revalidates the cooked payload instead of trusting timestamps or the
manifest alone.

## Roadmap

Phases 9, 24, 33.
