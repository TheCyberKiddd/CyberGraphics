#pragma once

// Private GPU memory allocation for the Direct3D 12 backend.
// Phase 15 increment 1 (ADR 0018): sub-allocation inside large heaps.

#include <cyber/core/error.h>
#include <cyber/core/result.h>
#include <cyber/core/types.h>
#include <cyber/memory/allocator.h>
#include <cyber/memory/dynamic_array.h>
#include <cyber/rhi/memory.h>

struct ID3D12Device;
struct ID3D12Heap;

namespace cyber::rhi::d3d12
{

using cyber::core::Error;
using cyber::core::Status;
using cyber::core::u32;
using cyber::core::u64;
using cyber::core::u8;
using cyber::core::usize;
using cyber::memory::DynamicArray;

// Which of the three memory classes a resource belongs to. The classes map to
// the heap types the RHI names: device-local to `DEFAULT`, upload to `UPLOAD`,
// and readback to `READBACK`. One allocator serves one device, under the
// device's mutex.
enum class MemoryClassType : u8
{
    DeviceLocal = 0,
    Upload = 1,
    Readback = 2,
    Count = 3,
};

// The placement alignment the API requires for buffers and ordinary textures
// (`D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT`); multisampled textures need
// `kMsaaPlacementAlignment` (`D3D12_DEFAULT_MSAA_RESOURCE_PLACEMENT_ALIGNMENT`).
// The caller passes whichever its resource needs, and the allocator never
// assumes one.
inline constexpr u64 kDefaultPlacementAlignment = 64ULL * 1024ULL;
inline constexpr u64 kMsaaPlacementAlignment = 4ULL * 1024ULL * 1024ULL;

// One free range inside a sub-heap.
struct SubAllocation
{
    u64 offset = 0;
    u64 sizeBytes = 0;
};

// One live reservation: the rounded bytes the caller was served, and the
// alignment padding inside them. The padding is what the waste statistic
// reports, so it is bookkeeping the allocator owns rather than a derived
// number; the aliasing work of increment 2 extends this record, not the slot.
struct SubReservation
{
    u64 offset = 0;
    u64 sizeBytes = 0;
    u64 paddingBytes = 0;
};

// One heap a class has opened for sub-allocation, and what of it is free.
// Not default-constructible: the free list is allocator-owned from birth.
struct SubHeap
{
    explicit SubHeap(memory::Allocator& allocator) noexcept
        : freeRanges(allocator), reservations(allocator)
    {
    }

    SubHeap(const SubHeap&) = delete;
    SubHeap& operator=(const SubHeap&) = delete;
    // Moveable because `DynamicArray` moves its elements when it grows; a
    // moved-from heap carries the pointer through, and the source is dropped
    // from the table right after the move.
    SubHeap(SubHeap&& other) noexcept = default;
    SubHeap& operator=(SubHeap&& other) noexcept = default;

    ID3D12Heap* heap = nullptr;
    u64 sizeBytes = 0;
    u64 usedBytes = 0;
    // One entry per free range, kept sorted by offset. First fit finds the
    // first entry that fits, and the merge on free becomes a neighbour check,
    // so a freed range that touches another merges instead of fragmenting.
    DynamicArray<SubAllocation> freeRanges;
    // One entry per live reservation, kept sorted by offset, the free list's
    // complement. `Deallocate` needs it to retire the reservation's reported
    // size — the caller's rounded size is a claim this list is the answer to.
    DynamicArray<SubReservation> reservations;
};

// One memory class: the heaps it sub-allocates inside and the growth state.
struct ClassState
{
    explicit ClassState(memory::Allocator& allocator) noexcept : heaps(allocator) {}

    ClassState(const ClassState&) = delete;
    ClassState& operator=(const ClassState&) = delete;
    ClassState(ClassState&& other) noexcept = default;
    ClassState& operator=(ClassState&& other) noexcept = default;

    // Heaps opened so far, in creation order.
    DynamicArray<SubHeap> heaps;
    // Size of the next heap this class opens: doubles from 8 MiB and stops at
    // 256 MiB, so a class that grows does so in few, large jumps.
    u64 nextHeapBytes = 8ULL * 1024ULL * 1024ULL;
};

// The GPU memory allocator of one device (ADR 0018, decision 1). A class opens
// heaps that grow by doubling from 8 MiB to 256 MiB; a request every sub-heap
// refuses gets a dedicated committed-sized heap, which is Phase 14's per-resource
// behaviour, so the allocator is a fast path and never a new failure mode. An
// allocation returns `(heap, offset, size)` for `CreatePlacedResource`; nothing
// is freed to the OS until `Shutdown` at device destruction, which is the policy
// that makes `Statistics()` a report rather than a snapshot.
//
// Thread safety: every call must hold the owning device's mutex. The allocator
// itself takes no lock, so the device decides the locking discipline once.
class GpuHeapAllocator
{
public:
    // `allocator` backs every bookkeeping array, so the allocator is
    // constructed with it, exactly once, and never moved or copied.
    explicit GpuHeapAllocator(memory::Allocator& allocator) noexcept;

    GpuHeapAllocator(const GpuHeapAllocator&) = delete;
    GpuHeapAllocator& operator=(const GpuHeapAllocator&) = delete;
    GpuHeapAllocator(GpuHeapAllocator&&) = delete;
    GpuHeapAllocator& operator=(GpuHeapAllocator&&) = delete;

    // Binds the allocator to `device`'s API surface. Must be called before any
    // other call, exactly once.
    void Initialize(ID3D12Device* device) noexcept;

    // Releases every heap. Call once, at device destruction, after the GPU has
    // finished every submission.
    void Shutdown() noexcept;

    // Reserves `sizeBytes` on `class_` and returns the heap and offset a
    // `CreatePlacedResource` call needs. `alignmentBytes` is the resource's own
    // placement requirement — `kDefaultPlacementAlignment` for buffers and
    // ordinary textures, `kMsaaPlacementAlignment` for multisampled ones; the
    // allocator rounds the reservation up to that alignment and also carves on
    // it, so a neighbour can never overlap.
    //
    // `outDedicated` reports that no sub-heap could serve the request and a
    // heap exactly its size was created for it; `Deallocate` needs the answer
    // to release the right thing.
    //
    // Returns `ErrorCode::Internal` when the API refused the heap or the
    // bookkeeping failed to grow. The allocator does not pre-fail on size:
    // what fits is the adapter's business, and a heap creation failure is the
    // honest report.
    [[nodiscard]] Status<Error> Allocate(MemoryClassType class_, u64 sizeBytes, u64 alignmentBytes,
                                         ID3D12Heap** outHeap, u64* outOffset,
                                         bool* outDedicated) noexcept;

    // Returns a reservation `Allocate` handed out. A dedicated reservation
    // releases its heap; a sub-allocated range returns to its heap's free list,
    // merging with a neighbour when they touch. Nothing is torn down below the
    // allocator either way.
    void Deallocate(MemoryClassType class_, ID3D12Heap* heap, u64 offset, u64 sizeBytes,
                    bool dedicated) noexcept;

    // The report the public `Device::MemoryStats()` reads. Must hold the device
    // mutex, like every other call.
    [[nodiscard]] MemoryStats Statistics() const noexcept;

private:
    static constexpr u64 kMiB = 1024ULL * 1024ULL;
    // ADR 0018: heaps grow by doubling from 8 MiB and stop at 256 MiB.
    static constexpr u64 kFirstHeapBytes = 8 * kMiB;
    static constexpr u64 kLargestHeapBytes = 256 * kMiB;

    // The dedicated path, inlined into `Allocate`: one heap exactly the
    // allocation's rounded size large, alignment met at offset 0 because
    // D3D12 heaps are 64 KiB aligned at minimum and a committed-size heap is
    // placed at 0.

    // Opens one sub-heap of `heapBytes` for `classState`, with its whole
    // range free. On failure the class state is left as it was.
    [[nodiscard]] bool OpenSubHeap(ClassState& classState, MemoryClassType class_,
                                   u64 heapBytes) noexcept;

    ID3D12Device* m_device = nullptr;
    memory::Allocator* m_allocator = nullptr;
    // Constructed per element because `DynamicArray` owns its allocator from
    // birth; the array cannot be re-constructed after.
    ClassState m_classes[static_cast<u8>(MemoryClassType::Count)];

    // Dedicated heaps, which no class state tracks because none of them is
    // ever sub-allocated or reused. Kept so the statistics include them and
    // `Shutdown` releases them with everything else.
    struct DedicatedHeap
    {
        ID3D12Heap* heap = nullptr;
        MemoryClassType class_ = MemoryClassType::DeviceLocal;
        u64 sizeBytes = 0;
    };
    DynamicArray<DedicatedHeap> m_dedicated;
};

}  // namespace cyber::rhi::d3d12
