# Phase 4 — Memory and containers

Date: 2026-10-21
Status: implemented
Related: ADR 0006, [cyber_memory contract](../../engines/cyber_memory/docs/MODULE.md)

## What this phase builds

`cyber_memory` becomes the third active foundation module. It provides:

- one explicit allocator interface and a root heap allocator;
- linear, stack, pool, free-list, ring, frame, and tracking allocators;
- `Span`, `StringView`, `String`, `DynamicArray`, `FixedArray`, an
  open-addressing `HashMap`, and a generation-checked `SlotMap`;
- generation-counted handles that later GPU resources can reuse.

No allocator owns global state. Every owning allocator is either given a buffer
or allocates its backing store through another allocator.

## What it deliberately leaves out

- OS virtual-memory reservation, huge pages, and guard pages. Those belong to
  `cyber_platform`.
- Thread-local allocators. The job system and frame ownership arrive in Phase 5.
- Concurrent containers. A container is single-writer; callers synchronize.
- GPU memory. `cyber_rhi` owns that policy.
- A general-purpose replacement for every standard-library container. The
  project containers exist where allocation control, handles, or predictable
  layout matters.

## Interfaces it touches

- New public target `cyber::memory`, depending only on `cyber::core`.
- Installed package headers under `include/cyber/memory/`.
- The package and source consumers link `cyber::memory` and exercise an
  allocator, a dynamic array, a hash map, and a handle.
- `CMakeLists.txt`, `cmake/CyberPackage.cmake`, `engines/modules.json`, and the
  generated `engines/MODULES.md` gain the active module.

The stable C ABI in `include/cybergraphics/` is untouched.

## Design

### Allocator contract

`Allocator` is a small virtual interface:

```cpp
void* Allocate(usize size, usize alignment) noexcept;
void  Deallocate(void* pointer) noexcept;
void  Reset() noexcept;
bool  Owns(const void* pointer) const noexcept;
AllocatorStats Stats() const noexcept;
```

`Allocate(0, ...)` returns `nullptr`. Alignment must be a power of two and at
least `alignof(void*)`; an unsatisfiable request returns `nullptr`. `Deallocate`
accepts `nullptr` and does nothing. Bump allocators treat deallocation as a
no-op and reclaim through `Reset` or a stack marker. The allocator never throws.
`AllocatorStats` reports capacity, currently used bytes, peak used bytes, and
successful allocation count.

`HeapAllocator` is the only root allocator. It uses `std::malloc` plus manual
alignment so it works on Windows and POSIX without platform headers. It is a
service for tools and allocator backing, not a hot-path allocation strategy.

### Allocator choices

- **Linear**: bump pointer, no per-allocation metadata, deallocation is a no-op.
  Best for level loading and immutable build data.
- **Stack**: linear plus `Mark()`/`FreeTo(marker)` for strict LIFO temporary
  work.
- **Pool**: fixed-size blocks with an intrusive free list. Constant-time
  allocate/free, no fragmentation within the block size.
- **Free list**: variable-size blocks with 64-byte boundary-tag headers,
  first-fit allocation, and address-order coalescing. Overhead is 64 bytes per
  allocation and the minimum block is 64 bytes.
- **Ring**: bump allocation with wraparound and a used counter. It never
  overwrites live data; `Reset` starts a new pass.
- **Frame**: a linear allocator with `BeginFrame()`; the renderer resets it once
  per frame.
- **Tracking**: intrusive 64-byte headers and an allocation list. It forwards
  storage to an upstream allocator and records the tag, size, and pointer. The
  leak report walks the list and invokes a caller callback, so reporting never
  allocates.

All allocators document their overhead in the public header. No allocator
reserves memory behind the caller's back.

### Containers

- `Span<T>` is a non-owning pointer and size. It does not allocate.
- `StringView` is a non-owning UTF-8 byte view with hashing and prefix/suffix
  helpers. It does not own or null-terminate.
- `String` owns a null-terminated byte buffer from an allocator. Copy is
  deleted; move transfers ownership.
- `DynamicArray<T>` grows by doubling with a minimum capacity of 8, checks
  overflow, and uses `std::construct_at`/`std::destroy_at`. Copy is deleted;
  move transfers the allocation.
- `FixedArray<T, N>` stores up to `N` values inline and never allocates.
- `HashMap<K, V>` uses open addressing, linear probing, power-of-two capacity,
  and tombstones. It grows at 70% load. Keys and values must be default
  constructible and move assignable. Copy is deleted; move transfers the table.
- `SlotMap<T, Tag>` stores values in a dense slot array and recycles free slots.
  `Handle<Tag>` carries a 32-bit index and generation. Removing a slot bumps its
  generation and skips zero, so a stale handle never resolves.

Container methods that can fail return `bool` or a pointer. No method throws.
A failed reserve leaves the old allocation and contents intact.

### Growth policy

`DynamicArray` and `String` choose

```text
newCapacity = max(required, max(8, capacity * 2))
```

and reject the request if the byte count overflows. `HashMap` chooses the
smallest power of two whose 70% load is at least the requested element count.
`SlotMap` grows its slot array through `DynamicArray` and its free list through
`DynamicArray<u32>`.

### Handle lifetime

`Handle<Tag>::index == kInvalidHandleIndex` means invalid. A live slot always
has generation `>= 1`. `SlotMap::Get` checks both index range and generation.
Clear and remove bump every retired generation. Generation wrap skips zero.

## Alternatives considered

- **Placement-new-only, no allocator interface**: rejected because the leak
  report and frame ownership need a common boundary.
- **`std::pmr`**: rejected because the runtime wants explicit allocators and
  the project must control allocation metadata and diagnostics.
- **Separate metadata allocation for tracking**: rejected because it hides a
  second allocation path and can fail separately from the payload.
- **Robin Hood hashing**: deferred. Linear probing with tombstones is simpler to
  verify and fast enough for the first pass.
- **Dense slot map with swap-remove**: rejected because stable handles are the
  point. Free-slot reuse with generations is more predictable.
- **Failing assertions on allocation failure**: rejected. Recoverable
  allocation failure returns `nullptr` or `false`.

## Risks

- The free-list allocator is the most error-prone part. Boundary tags, coalescing,
  and alignment need direct tests with adjacent frees and small remainders.
- Containers assume no exceptions in runtime code. A throwing element type is a
  programmer error; tests use non-throwing types.
- `StringView` and map keys are non-owning. A dangling view is a caller bug;
  documentation and tests make the rule explicit.
- Hash-map tombstones can accumulate under repeated insert/remove. Rehash on
  growth and clear on reset bound the damage in the first pass.
- The tracking allocator's 64-byte header limits payload alignment to 64 bytes.
  Over-aligned types must use a different allocator.

## Test plan

`memory_allocator_test`:

- heap alignment and stats;
- linear alignment, exhaustion, reset, ownership;
- stack marks and out-of-order rejection;
- pool allocate/exhaust/free/reuse/reset;
- free-list small and large allocations, coalescing, alignment, reset, and a\n  deterministic 128-block randomized allocate/free stress run;
- ring wrap and live-data refusal;
- frame reset;
- tracking tags, deallocation, deliberate leak report.

`memory_container_test`:

- span construction, subviews, and const conversion;
- string view comparison, prefix/suffix, find, hash;
- dynamic array growth, move, non-trivial lifetime, overflow refusal;
- fixed array push/pop/clear;
- string append, reserve, move, null termination;
- hash map insert/find/update/remove/grow/tombstones/move;
- slot map insert/get/remove/stale handle/clear/reuse.

`memory_public_headers_test` compiles every public header together and checks
the documented layouts and handle size.

All tests run in `debug`, `release`, `shipping`, and `debug-asan`. Ubuntu CI adds
UBSan. The deliberate-leak test proves the report catches exactly one leak.

## Sources

- Naughty Dog, "Parallelizing the Naughty Dog Engine Using Fibers" (frame
  allocation patterns and explicit memory ownership).
- ISO C++20: `std::construct_at`, `std::destroy_at`, `std::malloc`.
- Intel and Microsoft alignment guidance for power-of-two allocators.
- Open-addressing hash table notes: linear probing, tombstones, load factor.
- Generation-counted handle practice from entity/component systems and GPU
  resource tables.
