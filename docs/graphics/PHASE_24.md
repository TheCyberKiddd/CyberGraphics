# Phase 24: Asset import and cooked render assets

**Status:** implementation and local validation complete.

`cyber_assets` now provides in-house JSON/glTF and OBJ import, PNG and baseline
JPEG decoding, DDS and KTX2 container readers, canonical mesh/texture/material
data, and versioned cooked mesh, texture, material, and scene assets. Its
database tracks stable path-derived IDs, hashes, import settings, dependencies,
and verified cache entries. `cyber_cooker` creates deterministic cooked
outputs, while `AssetLibrary` safely reloads them using the platform watcher.

The format contract is in [formats-cooked.md](../formats-cooked.md), the module
contract is in [cyber_assets/docs/MODULE.md](../../engines/cyber_assets/docs/MODULE.md),
and implementation decisions are in the [design note](../design/phase-24-assets.md).

See the [Phase 24 report](../reports/phase-24.md) for validation commands and
the Release source-versus-cooked benchmark.
