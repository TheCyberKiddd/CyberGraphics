# cyber_assets — module contract

**Kind:** graphics
**Layer:** 2
**Order:** 22
**Status:** active
**Owner:** @TheCyberKiddd

## Purpose

Importers and decoders for glTF, OBJ, PNG, JPEG, DDS and KTX2; versioned cooked
formats for meshes, textures, materials, and scenes; an asset database with
stable IDs; and offline cooking with hot reload.

## Public API

`include/cyber/assets/`

Only headers under this path are public. Everything under `src/` is private.
Public headers must compile on their own and must not include another module's
private headers.

- `asset.h` — asset kinds, source formats, path-derived IDs, import settings,
  and the shared diagnostic type.
- `json.h` — bounded JSON document and parser.
- `mesh_data.h` — canonical imported mesh, validation, bounds, tangents.
- `image.h` — PNG and JPEG decoding to straight-alpha RGBA8 in the encoded
  source color space, plus image sniffing.
- `texture.h` — DDS and KTX2 containers with block-compressed payloads.
- `material.h` — metallic-roughness material data.
- `gltf.h` — glTF 2.0 (`.gltf` and `.glb`) import.
- `obj.h` — OBJ import.
- `cooked.h` — versioned, checksummed cooked containers and zero-copy views.
- `asset_database.h` — asset records, dependencies, import settings, cook cache.
- `asset_library.h` — cooking, loading, watching, and hot reload.

## Dependencies

Allowed: cyber_core, cyber_math, cyber_memory, cyber_platform

Forbidden: cyber_renderer, cyber_graphics

Dependencies are one-way. A module may depend only on modules with a lower
`order` value, or on a declared contract such as `cybergraphics_api`.

The implementation uses `cyber_core` (diagnostics, hashing), `cyber_math`
(vectors and bounds), `cyber_memory` (allocators, arrays, strings), and
`cyber_platform` (file I/O, memory mapping, the polling file watcher). It does
not depend on `cyber_rhi`, `cyber_jobs`, or `cyber_scene`.

## Invariants

- Every parser treats input as hostile and bounds-checks before use.
- Cooked files are versioned, checksummed, and memory-mappable.
- No global asset state.
- No third-party decoder: PNG, DEFLATE, JPEG, DDS, KTX2, glTF, and OBJ are
  parsed by this module.
- Cooked output is deterministic for identical source bytes and settings.
- A failed reload never replaces a working asset.

## Non-goals

- Residency and streaming policy (cyber_renderer).
- Editor import UI.
- GPU upload, transcoding, or block-compression authoring.
- Animation, skinning playback, and morph targets beyond storing their vertex
  attributes.
- Encrypting or signing cooked files.

## Tests

- `assets_json`, `assets_png`, `assets_jpeg`, `assets_texture`, `assets_obj`,
  `assets_gltf`, `assets_cooked`, `assets_database`, `assets_library`,
  `assets_fuzz`, and `assets_public_headers` cover the public API.
- `assets_fuzz` applies a bounded deterministic mutation corpus to each
  in-memory parser and cooked-file validation; sanitizer runs catch memory and
  undefined-behavior failures.
- `cyber_assets_bench` imports source assets and compares repeated source
  imports/decodes with repeated cooked loads in Release.

## Budgets

Cooked loads measurably faster than source. The bounded mutation corpus runs
with the test suite, and parsers do not allocate from unvalidated lengths.

## Roadmap

Phases 9, 24, 33.
