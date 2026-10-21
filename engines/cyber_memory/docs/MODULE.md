# cyber_memory — module contract

**Kind:** foundation
**Layer:** 0
**Order:** 2
**Status:** active
**Owner:** @TheCyberKiddd

## Purpose

Explicit allocation, frame-scoped arenas, diagnostics, containers, strings, and
generation-counted handles. This module owns the allocation boundary that the
platform, RHI, scene, and renderer layers use.

## Public API

`include/cyber/memory/`

Only headers under this path are public. Everything under `src/` is private.
Public headers must compile on their own and must not include another module's
private headers.

| Header | Contents |
|---|---|
| `types.h` | Namespace aliases for `cyber_core` fixed-width types. |
| `alignment.h` | Power-of-two checks, `AlignUp`, alignment queries. |
| `allocator.h` | `Allocator`, `AllocatorStats`, default alignment. |
| `heap_allocator.h` | `HeapAllocator`, the `std::malloc`-backed root allocator. |
| `linear_allocator.h` | Bump allocator with reset and ownership queries. |
| `stack_allocator.h` | Linear allocator with LIFO markers. |
| `pool_allocator.h` | Fixed-block allocator with an intrusive free list. |
| `free_list_allocator.h` | Variable-size boundary-tag allocator with coalescing. |
| `ring_allocator.h` | Transient wraparound bump allocator. |
| `frame_allocator.h` | Linear allocator with `BeginFrame()`/`EndFrame()`. |
| `tracking_allocator.h` | Tagged allocations and a non-allocating leak report. |
| `span.h` | Non-owning `Span<T>`. |
| `string_view.h` | Non-owning UTF-8 byte view. |
| `string.h` | Owned null-terminated `String`. |
| `dynamic_array.h` | `DynamicArray<T>` with explicit allocator ownership. |
| `fixed_array.h` | Inline `FixedArray<T, N>`. |
| `hash_map.h` | Open-addressing `HashMap<K, V>`. |
| `handle.h` | `Handle<Tag>` with index and generation. |
| `slot_map.h` | Stable-handle `SlotMap<T, Tag>`. |
| `memory.h` | Umbrella include. |

### Allocator contract

`Allocator::Allocate(size, alignment)` returns `nullptr` on failure or for a
zero-size request. Alignment must be a non-zero power of two. `Deallocate` is
safe for `nullptr`; unknown pointers are ignored when the allocator can validate
them. Bump allocators reclaim through `Reset`, stack markers, or frame
boundaries. `Owns` reports whether a pointer lies in the allocator's backing
range. `Stats` reports capacity, payload used, peak payload used, and successful
allocation count.

The root `HeapAllocator` stores a 16-byte header plus alignment padding before
each payload. `Reset` is a no-op because the root cannot discover outstanding
blocks.

### Container rules

- `Span`, `StringView`, and `Handle` do not allocate.
- `String` and `DynamicArray` are non-copyable and movable; move transfers the
  allocation and leaves the source empty.
- `FixedArray` is inline, fixed-capacity, and copyable/movable when `T` is.
- `HashMap` is non-copyable and movable. Keys and values must be default
  constructible and move assignable. `StringView` keys are non-owning.
- `SlotMap` is non-copyable and movable. `T` must be default constructible and
  move assignable.
- Methods that can fail return `bool` or a pointer. A failed capacity growth
  leaves the old allocation and contents intact.
- `DynamicArray` and `String` double capacity with a minimum of 8 (16 for the
  first string allocation) and reject byte-count overflow.
- `HashMap` uses power-of-two capacity, linear probing, tombstones, and a 70%
  load threshold. Rehash also removes tombstones.
- `SlotMap` recycles free slots. Removing or clearing a slot increments its
  generation and skips zero, so a stale handle never resolves.

### Allocator overhead

| Allocator | Per-allocation overhead | Reclaim |
|---|---|---|
| `HeapAllocator` | 16-byte header plus up to alignment - 1 padding | `Deallocate` |
| `LinearAllocator` | none; padding is counted in used bytes | `Reset` |
| `StackAllocator` | none | `FreeTo(marker)` or `Reset` |
| `PoolAllocator` | none; blocks are padded to block alignment | `Deallocate` or `Reset` |
| `FreeListAllocator` | 64-byte boundary-tag header; 64-byte minimum block | `Deallocate` or `Reset` |
| `RingAllocator` | none; padding at wrap is wasted | `Reset` |
| `FrameAllocator` | none | `BeginFrame` |
| `TrackingAllocator` | 64-byte intrusive header | `Deallocate` or `Reset` |

`FreeListAllocator` and `TrackingAllocator` cap payload alignment at 64 bytes.
Over-aligned types need a dedicated allocator. `RingAllocator` may reuse the
oldest bytes after wraparound; pointers from before a wrap are invalid.

## Dependencies

Allowed: cyber_core

Forbidden: cyber_platform, cyber_rhi, cyber_renderer, cyber_graphics

Dependencies are one-way. A module may depend only on modules with a lower
`order` value, or on a declared contract such as `cybergraphics_api`.

## Invariants

- Every runtime allocation goes through the allocator interface.
- Allocators hold no global mutable state and are not thread-safe.
- Containers document copy and move rules and never throw.
- Handles carry an index and a generation; `Get` validates both.
- Public headers are self-contained and do not include private headers.
- The tracking allocator's leak path performs no allocation.

## Non-goals

- OS virtual-memory reservation, guard pages, and huge pages (`cyber_platform`).
- Thread-local allocators and concurrent containers (Phase 5).
- GPU memory allocation (`cyber_rhi`).
- A drop-in replacement for every standard container.
- Exception-based failure or throwing element types.

## Tests

- `memory_allocator`: heap alignment and stats; linear exhaustion and reset;
  stack markers; pool reuse and exhaustion; free-list coalescing, alignment,
  validation, and a 128-block randomized stress run; ring wrap; frame reset;
  tracking tags and a deliberate leak report.
- `memory_container`: span, string view, dynamic array growth/move, fixed array,
  string append/alias/move, hash map insert/update/remove/rehash/tombstones,
  string-keyed hash map, slot map stale handles and reuse, handle layout.
- `memory_public_headers`: every public header compiles together and documented
  layouts hold.

The suite runs in `debug`, `release`, `shipping`, and `debug-asan`; Ubuntu CI
adds UBSan.

## Budgets

Allocator overhead is documented above. No hidden global allocation. Container
growth is amortized doubling; hash-map growth rehashes at 70% load. The deliberate
leak test proves the report catches exactly one outstanding allocation.

## Roadmap

Phase 4.
