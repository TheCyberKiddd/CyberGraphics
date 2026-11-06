# Phase 18 — Material system

## Delivered

- Immutable metallic-roughness material definitions validate names, ranges,
  render state, texture roles, and parameter schemas against shader reflection.
- Mutable material instances own reflected parameter bytes, uniform and staging
  buffers, and a per-material bind group while borrowing definitions, devices,
  allocators, and caller-owned texture views.
- Explicit graphics-queue uploads transition and copy dirty parameter blocks;
  repeated updates retain the staging-buffer copy-source state and restore the
  uniform buffer to its constant-buffer state.
- Deterministic opaque, masked, and transparent draw sorting and a bounded,
  source-located `.cybermat` v1 parser/writer are covered by renderer tests.
- The D3D12 `07_materials` sample creates and renders 100 instances, sorts 100
  draw keys, and validates a live parameter change on frame 20.
- Installed-package rules now export the shader and renderer targets and
  headers; package and source consumers compile against the renderer API.

## Evidence

On Windows 10 Pro 22H2 with an Intel Core i5-6400 and Intel HD Graphics 530:

- Debug, Release, and Debug-ASan each pass **98/98** tests.
- The D3D12 material self-test reports 100 definitions, 100 instances, 100
  sorted draws, and `live_parameter_change=passed` at frame 20.
- Release package consumer, source-tree consumer, structure, layer, formatting,
  and whitespace checks pass.

The full test and sample details are recorded in the
[Phase 18 report](../reports/phase-18.md). The implementation and evidence
are included in the Phase 18 commit and the master roadmap marks the phase
complete.

## Boundaries

This phase transports material data but does not implement full PBR lighting,
asset URI resolution, bindless resources, or frame-ring streaming. Those remain
in Phases 26, 24, and 19 respectively. Callers remain responsible for
submission ordering and fence-safe mutation or destruction.
