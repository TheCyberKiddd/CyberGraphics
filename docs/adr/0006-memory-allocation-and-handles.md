# ADR 0006: Memory allocation, containers, and handle lifetime

Date: 2026-10-21
Status: accepted

## Context

CyberGraphics needs deterministic allocation for frame data, assets, and later
GPU resource tables. The standard library is allowed, but its default allocator
hides failures and ownership. The runtime has exceptions and RTTI disabled, and
the renderer must be able to report leaks by tag and reject stale resource
handles. Phase 4 establishes these contracts before platform, RHI, and scene
code depend on them.

## Decision

1. Every runtime allocation goes through `cyber::memory::Allocator`. The
   interface is non-throwing, returns `nullptr` on failure, and reports capacity,
   used, peak, and allocation count.
2. The root allocator is `HeapAllocator`, a `std::malloc`-backed aligned allocator
   used for tools, diagnostics, and backing stores. Owning allocators either take
   a caller-provided buffer or allocate their backing store through another
   allocator.
3. Provide linear, stack, pool, free-list, ring, frame, and tracking allocators.
   Bump allocators reclaim through `Reset` or stack markers. The free-list
   allocator uses 64-byte boundary-tag headers, first-fit placement, and
   address-order coalescing. The tracking allocator uses intrusive 64-byte
   headers and reports leaks through a callback without allocating.
4. Containers take an allocator by reference. They are non-copyable and movable.
   Methods that can fail return `bool` or a pointer. Failed capacity growth leaves
   the old allocation intact. `DynamicArray` and `String` double capacity with a
   minimum of 8 and reject overflow.
5. `HashMap` uses open addressing with linear probing, tombstones, power-of-two
   capacity, and a 70% load threshold. Keys and values are default constructible
   and move assignable.
6. `SlotMap` stores values in dense slots and recycles slots through a free list.
   Handles carry a 32-bit index and a 32-bit generation. Removing or clearing a
   slot bumps its generation and skips zero. `Get` validates both fields, so a
   stale handle never resolves.
7. Public headers under `include/cyber/memory/` are self-contained and do not
   include private headers. Alignment must be a power of two; allocation
   requests that cannot satisfy the requested alignment return `nullptr`.

## Consequences

- Allocation failure is explicit and testable; callers cannot rely on exceptions.
- The leak report is complete only if runtime code uses the interface. Direct
  `new`/`malloc` outside a root allocator is a review defect.
- The free-list allocator trades memory overhead for simple, predictable
  coalescing. Small allocations should use the pool allocator.
- Tracking payloads are limited to 64-byte alignment. Over-aligned types need a
  dedicated allocator.
- Containers do not support element types that throw or are neither default
  constructible nor move assignable. This is acceptable for runtime data and is
  enforced by documentation and tests.
- A stale handle becomes a detectable error instead of silent corruption. The RHI
  will use the same index-plus-generation rule for GPU resources.
- Copying containers is a compile error; ownership is transferred by move. This
  prevents accidental deep copies in hot paths.

## Alternatives

- `std::pmr` and polymorphic standard containers: less control over metadata,
  failure reporting, and handle semantics.
- Reference-counted allocations: shared ownership obscures frame and device
  lifetimes, and atomic reference counts are unnecessary in the single-owner
  model.
- Robin Hood or Swiss-table hashing: deferred until profiling shows linear
  probing is insufficient.
- Swap-remove slot maps: invalidate handles on removal and do not provide stable
  resource identities.

## Follow-up

Phase 5 adds platform virtual memory and thread-local/frame ownership. Phase 10
adds asset budgets. The RHI adopts `Handle<Tag>` for GPU resources in Phase 13.
