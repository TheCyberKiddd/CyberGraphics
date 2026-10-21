# cyber_memory

**Status:** active · **Layer:** 0 · **Kind:** foundation

Explicit allocation, frame-scoped arenas, diagnostics, containers, strings, and
generation-counted handles.

The binding contract for this module lives in [docs/MODULE.md](docs/MODULE.md).
Read it before adding code or changing a public header.

## Layout

- `include/cyber/memory/` — public headers, self-contained.
- `src/` — private implementation.
- `tests/` — unit tests owned by this module.

The folder existed before its phase started so the architecture stayed
navigable. The `cyber_memory` target became active in Phase 4.

## Allocation model

`Allocator` is a non-throwing interface with explicit alignment, ownership
queries, and accounting. `HeapAllocator` is the root. Linear, stack, pool,
free-list, ring, frame, and tracking allocators cover bulk loading, temporary
work, fixed-size objects, variable-size reuse, transient frame data, and leak
diagnostics. Every runtime allocation must pass through the interface so the
tracking report is complete.

Containers take an allocator by reference. `DynamicArray` and `String` grow by
doubling, `HashMap` uses open addressing with tombstones, and `SlotMap` recycles
slots with generation-checked handles. Copy is deleted where ownership would be
ambiguous; move transfers it.

## Roadmap

Phase 4.
