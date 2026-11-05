#include "gpu_allocator.h"

#if defined(CYBER_RHI_HAS_D3D12)

#include "d3d12_internal.h"

#include <cyber/core/log.h>
#include <cyber/memory/alignment.h>
#include <cyber/rhi/memory.h>

#include <cstdio>
#include <d3d12.h>

namespace cyber::rhi::d3d12
{

using cyber::core::Error;
using cyber::core::ErrorCode;
using cyber::core::usize;
using cyber::memory::AlignUp;
using cyber::memory::IsPowerOfTwo;

namespace
{

// The heap type a memory class is served from. Buffers only in increment 1;
// textures join in increment 2, when they can declare a lifetime.
[[nodiscard]] D3D12_HEAP_TYPE HeapTypeOf(MemoryClassType class_) noexcept
{
    switch (class_)
    {
    case MemoryClassType::Upload:
        return D3D12_HEAP_TYPE_UPLOAD;
    case MemoryClassType::Readback:
        return D3D12_HEAP_TYPE_READBACK;
    case MemoryClassType::DeviceLocal:
    case MemoryClassType::Count:
    default:
        break;
    }
    return D3D12_HEAP_TYPE_DEFAULT;
}

// The class's own words, so a heap's name reads exactly the way `MemoryStats`'
// report and a capture's hierarchy do.
[[nodiscard]] const char* ClassTextOf(MemoryClassType class_) noexcept
{
    switch (class_)
    {
    case MemoryClassType::Upload:
        return "upload";
    case MemoryClassType::Readback:
        return "readback";
    case MemoryClassType::DeviceLocal:
    case MemoryClassType::Count:
    default:
        break;
    }
    return "device-local";
}

// Names one heap after its class and size. Captures and leak reports read
// these back, and a heap whose name says what serves it is one a caller can
// reason about without opening the allocator's internal state.
void NameHeap(ID3D12Heap* heap, MemoryClassType class_, u64 sizeBytes, bool dedicated) noexcept
{
    if (heap == nullptr)
    {
        return;
    }
    char text[72]{};
    if (dedicated)
    {
        std::snprintf(text, sizeof(text), "gpu heap %s (%.1f MiB) dedicated", ClassTextOf(class_),
                      static_cast<double>(sizeBytes) / (1024.0 * 1024.0));
    }
    else
    {
        std::snprintf(text, sizeof(text), "gpu heap %s (%.1f MiB)", ClassTextOf(class_),
                      static_cast<double>(sizeBytes) / (1024.0 * 1024.0));
    }
    detail::NameObject(heap, text);
}

// First entry whose offset is at or above `offset`. Every list is kept sorted
// by offset, which is what makes this a binary search rather than a scan and
// what turns the merge on free into a neighbour check.
template <typename T>
usize OffsetLowerBound(const DynamicArray<T>& entries, u64 offset) noexcept
{
    usize begin = 0;
    usize end = entries.Size();
    while (begin < end)
    {
        const usize middle = begin + (end - begin) / 2;
        if (entries[middle].offset < offset)
        {
            begin = middle + 1;
        }
        else
        {
            end = middle;
        }
    }
    return begin;
}

// Shifts every entry from `index` to the end one slot up, making a hole. The
// entries are trivially copyable, so the shift is an assignment after a growth.
template <typename T>
bool OpenHoleAt(DynamicArray<T>& entries, usize index) noexcept
{
    if (!entries.TryResize(entries.Size() + 1))
    {
        return false;
    }
    for (usize i = entries.Size() - 1; i > index; --i)
    {
        entries[i] = entries[i - 1];
    }
    return true;
}

// Closes the hole at `index`: every entry after it moves one slot down.
template <typename T>
void RemoveSortedAt(DynamicArray<T>& entries, usize index) noexcept
{
    const usize size = entries.Size();
    for (usize i = index + 1; i < size; ++i)
    {
        entries[i - 1] = entries[i];
    }
    (void)entries.TryResize(size - 1);
}

// Records a reservation into a heap's list, kept sorted by offset. A caller
// that fails here has not carved anything yet and reports the failure whole.
[[nodiscard]] bool RecordReservation(SubHeap& sub, const SubReservation& reservation) noexcept
{
    const usize slot = OffsetLowerBound(sub.reservations, reservation.offset);
    if (!OpenHoleAt(sub.reservations, slot))
    {
        return false;
    }
    sub.reservations[slot] = reservation;
    return true;
}

// Removes the entry at `index` from a `DynamicArray` whose elements are
// trivially copyable, without preserving order: the last entry moves into the
// hole. Order does not matter for a dedicated-heap list, which is only walked.
template <typename T>
void SwapRemoveAt(DynamicArray<T>& array, usize index) noexcept
{
    const usize size = array.Size();
    if (index + 1 < size)
    {
        array[index] = array[size - 1];
    }
    (void)array.TryResize(size - 1);
}

}  // namespace

GpuHeapAllocator::GpuHeapAllocator(memory::Allocator& allocator) noexcept
    : m_allocator(&allocator),
      m_classes{ClassState(allocator), ClassState(allocator), ClassState(allocator)},
      m_dedicated(allocator)
{
}

void GpuHeapAllocator::Initialize(ID3D12Device* device) noexcept
{
    CG_ASSERT(m_device == nullptr);
    CG_ASSERT(device != nullptr);
    m_device = device;
}

void GpuHeapAllocator::Shutdown() noexcept
{
    CG_ASSERT(m_device != nullptr);
    for (u32 classIndex = 0; classIndex < static_cast<u32>(MemoryClassType::Count); ++classIndex)
    {
        ClassState& classState = m_classes[classIndex];
        for (u32 heapIndex = 0; heapIndex < classState.heaps.Size(); ++heapIndex)
        {
            SubHeap& sub = classState.heaps[heapIndex];
            if (sub.heap != nullptr)
            {
                sub.heap->Release();
                sub.heap = nullptr;
            }
        }
        classState.heaps.Clear();
    }
    for (u32 index = 0; index < m_dedicated.Size(); ++index)
    {
        DedicatedHeap& dedicated = m_dedicated[index];
        if (dedicated.heap != nullptr)
        {
            dedicated.heap->Release();
            dedicated.heap = nullptr;
        }
    }
    m_dedicated.Clear();
    m_device = nullptr;
}

bool GpuHeapAllocator::OpenSubHeap(ClassState& classState, MemoryClassType class_,
                                   u64 heapBytes) noexcept
{
    D3D12_HEAP_DESC heap{};
    heap.SizeInBytes = heapBytes;
    heap.Properties.Type = HeapTypeOf(class_);
    heap.Properties.CreationNodeMask = 1;
    heap.Properties.VisibleNodeMask = 1;
    // Default heap alignment, the same 64 KiB requirement buffers and ordinary
    // textures carry, so every placement this allocator computes is legal.
    heap.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
    heap.Flags = D3D12_HEAP_FLAG_NONE;

    ID3D12Heap* api = nullptr;
    const HRESULT result =
        m_device->CreateHeap(&heap, __uuidof(ID3D12Heap), reinterpret_cast<void**>(&api));
    if (FAILED(result) || api == nullptr)
    {
        CG_LOG(Error, Rhi,
               "d3d12 allocator: CreateHeap failed class=%u size=%llu alignment=%llu "
               "hr=0x%08lX removal=0x%08lX",
               static_cast<u32>(class_), static_cast<unsigned long long>(heapBytes),
               static_cast<unsigned long long>(heap.Alignment), static_cast<unsigned long>(result),
               static_cast<unsigned long>(m_device->GetDeviceRemovedReason()));
        return false;
    }

    // The `SubHeap` is constructed with the backing allocator, and the one
    // free range covering a fresh heap follows.
    if (!classState.heaps.TryEmplaceBack(*m_allocator))
    {
        api->Release();
        return false;
    }
    SubHeap& sub = classState.heaps.Back();
    if (!sub.freeRanges.TryResize(1))
    {
        classState.heaps.PopBack();
        api->Release();
        return false;
    }
    sub.heap = api;
    sub.sizeBytes = heapBytes;
    sub.usedBytes = 0;
    sub.freeRanges[0] = SubAllocation{0, heapBytes};
    NameHeap(api, class_, heapBytes, false);
    return true;
}

Status<Error> GpuHeapAllocator::Allocate(MemoryClassType class_, u64 sizeBytes, u64 alignmentBytes,
                                         ID3D12Heap** outHeap, u64* outOffset,
                                         bool* outDedicated) noexcept
{
    CG_ASSERT(outHeap != nullptr && outOffset != nullptr && outDedicated != nullptr);
    if (outHeap == nullptr || outOffset == nullptr || outDedicated == nullptr ||
        class_ >= MemoryClassType::Count)
    {
        return Status<Error>::Err(
            Error{ErrorCode::InvalidArgument, "d3d12 backend: allocator call is invalid"});
    }
    if (sizeBytes == 0 || !IsPowerOfTwo(alignmentBytes))
    {
        return Status<Error>::Err(
            Error{ErrorCode::InvalidArgument,
                  "d3d12 backend: allocator needs a non-zero size and a power-of-two alignment"});
    }

    const u64 rounded = AlignUp(sizeBytes, alignmentBytes);
    ClassState& classState = m_classes[static_cast<u8>(class_)];

    // First fit across the class's heaps, first fit within each heap's
    // offset-sorted free list. First fit rather than best fit: the
    // fragmentation statistics exist so a caller can see when it stops being
    // good enough (ADR 0018, decision 1).
    for (u32 heapIndex = 0; heapIndex < classState.heaps.Size(); ++heapIndex)
    {
        SubHeap& sub = classState.heaps[heapIndex];
        const usize rangeIndices = sub.freeRanges.Size();
        for (usize rangeIndex = 0; rangeIndex < rangeIndices; ++rangeIndex)
        {
            const SubAllocation& range = sub.freeRanges[rangeIndex];
            const u64 aligned = AlignUp(range.offset, alignmentBytes);
            const u64 rangeEnd = range.offset + range.sizeBytes;
            if (aligned < range.offset || aligned > rangeEnd || rounded > rangeEnd - aligned)
            {
                continue;
            }

            // The kept head [range.offset, aligned) is a free gap, which
            // goes back into the list as free space; the waste statistic
            // counts only the padding inside reservations.
            const u64 headSize = aligned - range.offset;
            const u64 tailOffset = aligned + rounded;
            const u64 tailSize = rangeEnd - tailOffset;

            // The reservation is recorded before the free list is touched, so
            // a bookkeeping failure cannot leave a carved range with no owner.
            if (!RecordReservation(sub, SubReservation{aligned, rounded, rounded - sizeBytes}))
            {
                return Status<Error>::Err(
                    Error{ErrorCode::Internal,
                          "d3d12 backend: allocator reservation list failed to grow"});
            }
            if (!OpenHoleAt(sub.freeRanges, rangeIndex))
            {
                RemoveSortedAt(sub.reservations, OffsetLowerBound(sub.reservations, aligned));
                return Status<Error>::Err(Error{
                    ErrorCode::Internal, "d3d12 backend: allocator free list failed to grow"});
            }
            // The hole holds two slots, and both must be rewritten now: a
            // degenerate head or tail leaves a stale copy of the range behind,
            // which would hand out overlapping offsets and wrap the waste
            // statistic into noise.
            if (headSize == 0 && tailSize == 0)
            {
                // The reservation covers the whole range: both slots go.
                RemoveSortedAt(sub.freeRanges, rangeIndex);
                RemoveSortedAt(sub.freeRanges, rangeIndex);
            }
            else if (headSize == 0)
            {
                // Tail only: drop the extra slot, then overwrite the range,
                // which the hole shift duplicated.
                RemoveSortedAt(sub.freeRanges, rangeIndex + 1);
                sub.freeRanges[rangeIndex] = SubAllocation{tailOffset, tailSize};
            }
            else
            {
                if (tailSize > 0)
                {
                    sub.freeRanges[rangeIndex + 1] = SubAllocation{tailOffset, tailSize};
                }
                else
                {
                    RemoveSortedAt(sub.freeRanges, rangeIndex + 1);
                }
                sub.freeRanges[rangeIndex] = SubAllocation{range.offset, headSize};
            }

            sub.usedBytes += rounded;
            *outHeap = sub.heap;
            *outOffset = aligned;
            *outDedicated = false;
            return Status<Error>::Ok();
        }
    }

    // No existing range served the request. The class's next heap — whose size
    // doubles from 8 MiB and stops at 256 MiB — takes the request when one can
    // hold it; otherwise a heap exactly the allocation's size is opened for it,
    // which is the dedicated path and today's per-resource behaviour.
    u64 next = classState.nextHeapBytes;
    if (next < rounded)
    {
        next = rounded;
    }
    if (next <= kLargestHeapBytes && OpenSubHeap(classState, class_, next))
    {
        if (classState.nextHeapBytes < kLargestHeapBytes)
        {
            const u64 doubled = classState.nextHeapBytes * 2;
            classState.nextHeapBytes = doubled < kLargestHeapBytes ? doubled : kLargestHeapBytes;
        }
        SubHeap& sub = classState.heaps.Back();
        // The fresh heap's whole range was just filled at index 0; carve the
        // reservation out of it. A sub-heap opened smaller than the request is
        // not possible here: `next` was raised to hold the request.
        if (!RecordReservation(sub, SubReservation{0, rounded, rounded - sizeBytes}))
        {
            sub.heap->Release();
            classState.heaps.PopBack();
            return Status<Error>::Err(Error{
                ErrorCode::Internal, "d3d12 backend: allocator reservation list failed to grow"});
        }
        sub.usedBytes = rounded;
        sub.freeRanges[0] = SubAllocation{rounded, sub.sizeBytes - rounded};
        *outHeap = sub.heap;
        *outOffset = 0;
        *outDedicated = false;
        return Status<Error>::Ok();
    }

    // Dedicated path: an oversized request, or every sub-heap opening failed.
    D3D12_HEAP_DESC heap{};
    heap.SizeInBytes = rounded;
    heap.Properties.Type = HeapTypeOf(class_);
    heap.Properties.CreationNodeMask = 1;
    heap.Properties.VisibleNodeMask = 1;
    heap.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
    heap.Flags = D3D12_HEAP_FLAG_NONE;

    ID3D12Heap* dedicatedApi = nullptr;
    const HRESULT result =
        m_device->CreateHeap(&heap, __uuidof(ID3D12Heap), reinterpret_cast<void**>(&dedicatedApi));
    if (FAILED(result) || dedicatedApi == nullptr)
    {
        CG_LOG(Error, Rhi,
               "d3d12 allocator: dedicated CreateHeap failed class=%u size=%llu "
               "alignment=%llu hr=0x%08lX removal=0x%08lX",
               static_cast<u32>(class_), static_cast<unsigned long long>(rounded),
               static_cast<unsigned long long>(heap.Alignment), static_cast<unsigned long>(result),
               static_cast<unsigned long>(m_device->GetDeviceRemovedReason()));
        return Status<Error>::Err(Error{
            ErrorCode::Internal, "d3d12 backend: no heap could be created for the allocation"});
    }
    DedicatedHeap dedicated{};
    dedicated.heap = dedicatedApi;
    dedicated.class_ = class_;
    dedicated.sizeBytes = rounded;
    NameHeap(dedicatedApi, class_, rounded, true);
    if (!m_dedicated.TryPushBack(dedicated))
    {
        dedicatedApi->Release();
        return Status<Error>::Err(
            Error{ErrorCode::Internal, "d3d12 backend: allocator bookkeeping failed to grow"});
    }
    *outHeap = dedicatedApi;
    *outOffset = 0;
    *outDedicated = true;
    return Status<Error>::Ok();
}

void GpuHeapAllocator::Deallocate(MemoryClassType class_, ID3D12Heap* heap, u64 offset,
                                  u64 sizeBytes, bool dedicated) noexcept
{
    CG_ASSERT(class_ < MemoryClassType::Count);
    CG_ASSERT(heap != nullptr);
    if (class_ >= MemoryClassType::Count || heap == nullptr)
    {
        return;
    }
    ClassState& classState = m_classes[static_cast<u8>(class_)];

    if (dedicated)
    {
        // A dedicated reservation releases its heap: there is no free range to
        // return, because none was ever carved out of it.
        for (usize index = 0; index < m_dedicated.Size(); ++index)
        {
            const DedicatedHeap& entry = m_dedicated[index];
            if (entry.heap == heap)
            {
                entry.heap->Release();
                SwapRemoveAt(m_dedicated, index);
                return;
            }
        }
        CG_ASSERT(false);
        return;
    }

    for (u32 heapIndex = 0; heapIndex < classState.heaps.Size(); ++heapIndex)
    {
        SubHeap& sub = classState.heaps[heapIndex];
        if (sub.heap != heap)
        {
            continue;
        }

        // Retire the reservation record: the caller's size is a claim, the
        // recorded one is the answer, and the two have to agree.
        const usize record = OffsetLowerBound(sub.reservations, offset);
        CG_ASSERT(record < sub.reservations.Size() && sub.reservations[record].offset == offset &&
                  sub.reservations[record].sizeBytes == sizeBytes);
        if (record < sub.reservations.Size() && sub.reservations[record].offset == offset)
        {
            RemoveSortedAt(sub.reservations, record);
        }

        const u64 end = offset + sizeBytes;
        const usize candidate = OffsetLowerBound(sub.freeRanges, offset);

        // A free range adjoining the freed one on either side merges with it,
        // so a fragmentation pattern that fits back together stays together.
        const bool mergesPrevious =
            candidate > 0 &&
            sub.freeRanges[candidate - 1].offset + sub.freeRanges[candidate - 1].sizeBytes ==
                offset;
        const bool mergesNext =
            candidate < sub.freeRanges.Size() && sub.freeRanges[candidate].offset == end;

        if (mergesPrevious && mergesNext)
        {
            sub.freeRanges[candidate - 1].sizeBytes +=
                sizeBytes + sub.freeRanges[candidate].sizeBytes;
            RemoveSortedAt(sub.freeRanges, candidate);
        }
        else if (mergesPrevious)
        {
            sub.freeRanges[candidate - 1].sizeBytes += sizeBytes;
        }
        else if (mergesNext)
        {
            sub.freeRanges[candidate].offset = offset;
            sub.freeRanges[candidate].sizeBytes += sizeBytes;
        }
        else
        {
            if (!OpenHoleAt(sub.freeRanges, candidate))
            {
                // A free that cannot be recorded is a leak in the reporting,
                // which the statistics will read as used space. That is the
                // honest degradation: the bytes are not lost, the report lies.
                return;
            }
            sub.freeRanges[candidate] = SubAllocation{offset, sizeBytes};
        }
        sub.usedBytes -= sizeBytes;
        return;
    }
    CG_ASSERT(false);
}

MemoryStats GpuHeapAllocator::Statistics() const noexcept
{
    MemoryStats stats{};
    for (u32 classIndex = 0; classIndex < static_cast<u32>(MemoryClassType::Count); ++classIndex)
    {
        MemoryHeapStats* out =
            classIndex == static_cast<u32>(MemoryClassType::DeviceLocal)
                ? &stats.deviceLocal
                : (classIndex == static_cast<u32>(MemoryClassType::Upload) ? &stats.upload
                                                                           : &stats.readback);
        const ClassState& classState = m_classes[classIndex];

        out->blockCount = static_cast<u32>(classState.heaps.Size());
        for (u32 heapIndex = 0; heapIndex < classState.heaps.Size(); ++heapIndex)
        {
            const SubHeap& sub = classState.heaps[heapIndex];
            out->reservedBytes += sub.sizeBytes;
            u64 largest = 0;
            for (u32 rangeIndex = 0; rangeIndex < sub.freeRanges.Size(); ++rangeIndex)
            {
                const SubAllocation& range = sub.freeRanges[rangeIndex];
                if (range.sizeBytes > largest)
                {
                    largest = range.sizeBytes;
                }
            }
            out->usedBytes += sub.usedBytes;
            if (largest > out->largestFreeBytes)
            {
                out->largestFreeBytes = largest;
            }
            // The waste statistic is the padding inside the live
            // reservations: head gaps are free-list entries, so they report as
            // free space, and the two add up to the reserved bytes.
            for (u32 recordIndex = 0; recordIndex < sub.reservations.Size(); ++recordIndex)
            {
                out->wasteBytes += sub.reservations[recordIndex].paddingBytes;
            }
        }

        // Dedicated heaps: reserved and used in full, since nothing of theirs
        // is ever carved or freed.
        for (u32 index = 0; index < m_dedicated.Size(); ++index)
        {
            const DedicatedHeap& dedicated = m_dedicated[index];
            if (static_cast<u32>(dedicated.class_) == classIndex)
            {
                ++out->blockCount;
                out->reservedBytes += dedicated.sizeBytes;
                out->usedBytes += dedicated.sizeBytes;
            }
        }
    }
    return stats;
}

}  // namespace cyber::rhi::d3d12

#endif
