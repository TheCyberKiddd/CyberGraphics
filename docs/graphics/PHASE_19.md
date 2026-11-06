# Phase 19 — Constant streaming and bindless resources

## Delivered

- Added validated RHI dynamic offsets for uniform, writable-storage, and
  read-only-storage buffers, mutable bind-group arrays, and separate resource
  and sampler array limits.
- Added the renderer's fence-safe per-frame upload stream, persistent
  generation-counted and transient descriptor allocators, and capability-gated
  bindless texture and buffer tables with caller-provided fallback resources.
- Added `samples/08_frame_streaming` to compare 10,000 unique-transform draws
  through the dynamic-offset and bindless paths. It now selects the first
  enumerated D3D12 adapter by default; `--warp` explicitly selects WARP.

## API example

After creating a stream and obtaining a safe slot from `FrameRing`, copy
per-frame data into the stream, record its upload before its consumers, and
signal the frame slot on submission:

```cpp
auto frame = ring.BeginFrame(device);
if (!frame)
{
    return false;
}

if (auto status = stream.BeginFrame(ring, frame.Value()); !status)
{
    return false;
}

auto allocation = stream.Allocate(transformBytes);
if (!allocation)
{
    return false;
}

if (!commandList.Begin() || !stream.RecordUploads(commandList))
{
    return false;
}
```

For dynamic bindings, pass one aligned offset per dynamic layout entry to
`CommandList::SetBindGroup`. For bindless access, allocate a persistent table
index and call `BindlessResourceTable::SyncFrame` only after `FrameRing` returns
the safe slot. On adapters without `Feature::BindlessResources`, use
conventional bindings; do not create or assume a bindless table.

## Evidence

On Windows 10 Pro 22H2 with an Intel Core i5-6400 and Intel HD Graphics 530,
Debug, Release, and Debug-ASan each pass **99/99** tests. The Release sample ran
on the physical Intel adapter in three independent ten-frame batches. The
bindless path's p95 end-to-end frame completion was **11,594 µs, 8,630 µs, and
7,854 µs**, all below the **16,667 µs** budget. The maximum batch p95 CPU
record-plus-submit was **9,729 µs**. The dynamic-offset comparison is
substantially slower on this driver and is reported as a limitation rather than
hidden.

Installed-package and source-tree consumers, structure, layer, formatting, and
whitespace checks pass. Full command lines and per-batch timing distributions
are recorded in the [Phase 19 report](../reports/phase-19.md); design decisions
are in [ADR 0022](../adr/0022-frame-streaming-and-bindless-resources.md).

## Boundaries

Bindless is optional and capability-gated; the conventional dynamic-offset
path remains available for compatibility and unsupported adapters. The local
performance result is for one Intel adapter and driver, not a cross-vendor
hardware matrix. Phase 20 may proceed, but must not assume bindless support.
