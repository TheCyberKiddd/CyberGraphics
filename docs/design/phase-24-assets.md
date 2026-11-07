# Phase 24 — Asset import and cooked render assets

Date: 2026-11-07
Status: implemented

## What this phase builds

An allocator-backed `cyber_assets` module for importing glTF 2.0, OBJ, PNG,
baseline JPEG, DDS, and KTX2; validating canonical meshes, textures, and
materials; writing versioned cooked assets; tracking assets and dependencies;
reusing verified cook output; and reloading cooked assets without losing the
last working version on failure.

The offline `cyber_cooker` executable imports content and writes cooked files
and a JSON database manifest. The runtime `AssetLibrary` loads cooked files and
uses the platform file watcher to detect changes.

## What it deliberately leaves out

- GPU upload, residency, streaming policy, and renderer ownership.
- Editor import UI, texture transcoding, and block-compression authoring.
- Animation playback, skinning evaluation, and morph-target evaluation.
- Signing or encrypting cooked files.
- Third-party parser or decoder libraries.

## Interfaces and ownership

`cyber_assets` is a Layer 2 module ordered before `cyber_scene`. It depends on
`cyber_core`, `cyber_math`, `cyber_memory`, and `cyber_platform` only. Import
results own data through the caller's allocator; parser inputs are borrowed for
the call. `CookedAsset` exposes zero-copy views into caller-owned bytes, while
`CookedFile` owns the bytes for disk-loaded assets.

The module does not depend on the RHI or jobs system. Import and cooking remain
CPU-only, and renderer-facing GPU work stays in later phases.

## Design and invariants

- Parsers validate offsets, lengths, dimensions, counts, and format limits
  before using input-derived values.
- Asset IDs are derived from normalized virtual paths, so content edits do not
  change identity.
- Source hashes and import-settings hashes participate in cook-cache
  validation; a cache hit is used only after the referenced cooked file and
  checksum are revalidated.
- Cooked mesh, texture, material, and scene payloads use the
  [cooked asset specification](../formats-cooked.md): explicit little-endian
  headers, a version, a checksum, and 16-byte-aligned sections.
- Output is deterministic for the same source bytes and settings.
- A failed hot reload leaves the last valid asset mounted.

## Test plan and evidence

Parser unit tests cover JSON/glTF, OBJ, PNG, JPEG, DDS, and KTX2. A deterministic
mutation corpus exercises each in-memory parser and cooked-file validation
under the normal test and sanitizer runners. Cooker integration tests cover
clean output, all supported fixture formats, cache reuse, source-name
collisions, and manifest relationships. The Release benchmark compares source
imports/decodes against cooked loads.

See the [Phase 24 report](../reports/phase-24.md) for exact commands, results,
and measured benchmark data.

## Sources

- Khronos, [glTF 2.0 specification](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html).
- W3C, [PNG specification](https://www.w3.org/TR/png-3/).
- ITU-T, [Recommendation T.81 (JPEG)](https://www.itu.int/rec/T-REC-T.81).
- IETF, [RFC 1951: DEFLATE](https://www.rfc-editor.org/rfc/rfc1951).
- Khronos, [KTX 2.0 specification](https://registry.khronos.org/KTX/specs/2.0/ktxspec.v2.html).
- Microsoft, [DDS programming guide](https://learn.microsoft.com/en-us/windows/win32/direct3ddds/dx-graphics-dds).
