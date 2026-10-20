# cyber_assets — module contract

**Kind:** graphics
**Layer:** 2
**Order:** 22
**Status:** planned
**Owner:** @TheCyberKiddd

## Purpose

Importers and decoders for glTF, OBJ, PNG, JPEG, DDS and KTX; cooked formats, asset database, and cooking.

## Public API

`include/cyber/assets/`

Only headers under this path are public. Everything under `src/` is private.
Public headers must compile on their own and must not include another module's
private headers.

## Dependencies

Allowed: cyber_core, cyber_math, cyber_memory, cyber_platform, cyber_jobs, cyber_rhi

Forbidden: cyber_renderer, cyber_graphics

Dependencies are one-way. A module may depend only on modules with a lower
`order` value, or on a declared contract such as `cybergraphics_api`.

## Invariants

- Every parser treats input as hostile and bounds-checks before use.
- Cooked files are versioned, checksummed, and memory-mappable.
- No global asset state.

## Non-goals

- Residency and streaming policy (cyber_renderer).
- Editor import UI.

## Tests

Parser unit tests, fuzz tests, cook-and-load benchmark, and golden asset round-trip.

## Budgets

Cooked load measurably faster than source. Fuzz targets run on a schedule.

## Roadmap

Phases 9, 24, 33.
