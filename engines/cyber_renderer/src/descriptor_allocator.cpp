#include <cyber/renderer/descriptor_allocator.h>

#include <limits>
#include <utility>

namespace cyber::renderer
{
namespace
{

using cyber::core::Error;
using cyber::core::ErrorCode;
using cyber::core::Result;
using cyber::core::Status;

[[nodiscard]] Error AllocatorError(ErrorCode code, const char* message) noexcept
{
    return Error{code, message};
}

[[nodiscard]] bool ValidFrame(const rhi::FrameRing& ring, const rhi::FrameSlot& frame,
                              u32 expectedFrames) noexcept
{
    return ring.IsValid() && ring.FramesInFlight() == expectedFrames && frame.valid &&
           frame.slot < expectedFrames && frame.frameIndex < ring.FrameIndex() &&
           frame.frameIndex % expectedFrames == frame.slot &&
           ring.SignalValueForFrame(frame.frameIndex) == frame.signalValue;
}

}  // namespace

PersistentDescriptorAllocator::~PersistentDescriptorAllocator()
{
    Destroy();
}

PersistentDescriptorAllocator::PersistentDescriptorAllocator(
    PersistentDescriptorAllocator&& other) noexcept
{
    MoveFrom(other);
}

PersistentDescriptorAllocator&
PersistentDescriptorAllocator::operator=(PersistentDescriptorAllocator&& other) noexcept
{
    if (this != &other)
    {
        Destroy();
        MoveFrom(other);
    }
    return *this;
}

Status<Error> PersistentDescriptorAllocator::Initialize(u32 capacity,
                                                        memory::Allocator& allocator) noexcept
{
    if (m_capacity != 0 || capacity == 0)
    {
        return Status<Error>::Err(
            AllocatorError(ErrorCode::InvalidArgument, "descriptor allocator: invalid capacity"));
    }
    if (capacity > std::numeric_limits<cyber::core::usize>::max() / sizeof(u32))
    {
        return Status<Error>::Err(
            AllocatorError(ErrorCode::OutOfRange, "descriptor allocator: capacity overflow"));
    }
    u32* freeIndices = static_cast<u32*>(allocator.Allocate(capacity * sizeof(u32), alignof(u32)));
    u32* generations = static_cast<u32*>(allocator.Allocate(capacity * sizeof(u32), alignof(u32)));
    bool* allocated =
        static_cast<bool*>(allocator.Allocate(capacity * sizeof(bool), alignof(bool)));
    if (freeIndices == nullptr || generations == nullptr || allocated == nullptr)
    {
        if (freeIndices != nullptr)
        {
            allocator.Deallocate(freeIndices);
        }
        if (generations != nullptr)
        {
            allocator.Deallocate(generations);
        }
        if (allocated != nullptr)
        {
            allocator.Deallocate(allocated);
        }
        return Status<Error>::Err(AllocatorError(
            ErrorCode::Internal, "descriptor allocator: metadata allocation failed"));
    }
    for (u32 index = 0; index < capacity; ++index)
    {
        freeIndices[index] = capacity - index - 1;
        generations[index] = 1;
        allocated[index] = false;
    }
    m_allocator = &allocator;
    m_freeIndices = freeIndices;
    m_generations = generations;
    m_allocated = allocated;
    m_capacity = capacity;
    m_freeCount = capacity;
    return Status<Error>::Ok();
}

Result<PersistentDescriptor, Error> PersistentDescriptorAllocator::Allocate() noexcept
{
    if (m_capacity == 0 || m_freeCount == 0)
    {
        return Result<PersistentDescriptor, Error>::Err(
            AllocatorError(ErrorCode::OutOfRange, "descriptor allocator: capacity exhausted"));
    }
    const u32 index = m_freeIndices[--m_freeCount];
    m_allocated[index] = true;
    return Result<PersistentDescriptor, Error>::Ok(
        PersistentDescriptor{true, index, m_generations[index]});
}

Status<Error> PersistentDescriptorAllocator::Free(PersistentDescriptor descriptor) noexcept
{
    if (!IsLive(descriptor))
    {
        return Status<Error>::Err(
            AllocatorError(ErrorCode::InvalidArgument, "descriptor allocator: stale handle"));
    }
    m_allocated[descriptor.index] = false;
    ++m_generations[descriptor.index];
    if (m_generations[descriptor.index] == 0)
    {
        ++m_generations[descriptor.index];
    }
    m_freeIndices[m_freeCount++] = descriptor.index;
    return Status<Error>::Ok();
}

bool PersistentDescriptorAllocator::IsLive(PersistentDescriptor descriptor) const noexcept
{
    return m_capacity != 0 && descriptor.valid && descriptor.index < m_capacity &&
           m_allocated[descriptor.index] &&
           m_generations[descriptor.index] == descriptor.generation;
}

void PersistentDescriptorAllocator::Destroy() noexcept
{
    if (m_allocator != nullptr)
    {
        m_allocator->Deallocate(m_freeIndices);
        m_allocator->Deallocate(m_generations);
        m_allocator->Deallocate(m_allocated);
    }
    m_allocator = nullptr;
    m_freeIndices = nullptr;
    m_generations = nullptr;
    m_allocated = nullptr;
    m_capacity = 0;
    m_freeCount = 0;
}

void PersistentDescriptorAllocator::MoveFrom(PersistentDescriptorAllocator& other) noexcept
{
    m_allocator = other.m_allocator;
    m_freeIndices = other.m_freeIndices;
    m_generations = other.m_generations;
    m_allocated = other.m_allocated;
    m_capacity = other.m_capacity;
    m_freeCount = other.m_freeCount;
    other.m_allocator = nullptr;
    other.m_freeIndices = nullptr;
    other.m_generations = nullptr;
    other.m_allocated = nullptr;
    other.m_capacity = 0;
    other.m_freeCount = 0;
}

TransientDescriptorAllocator::~TransientDescriptorAllocator()
{
    Destroy();
}

TransientDescriptorAllocator::TransientDescriptorAllocator(
    TransientDescriptorAllocator&& other) noexcept
{
    MoveFrom(other);
}

TransientDescriptorAllocator&
TransientDescriptorAllocator::operator=(TransientDescriptorAllocator&& other) noexcept
{
    if (this != &other)
    {
        Destroy();
        MoveFrom(other);
    }
    return *this;
}

Status<Error> TransientDescriptorAllocator::Initialize(u32 framesInFlight, u32 capacityPerFrame,
                                                       memory::Allocator& allocator) noexcept
{
    if (m_slots != nullptr || framesInFlight == 0 || framesInFlight > rhi::kMaxFramesInFlight ||
        capacityPerFrame == 0)
    {
        return Status<Error>::Err(
            AllocatorError(ErrorCode::InvalidArgument, "transient descriptors: invalid shape"));
    }
    Slot* slots =
        static_cast<Slot*>(allocator.Allocate(framesInFlight * sizeof(Slot), alignof(Slot)));
    if (slots == nullptr)
    {
        return Status<Error>::Err(AllocatorError(
            ErrorCode::Internal, "transient descriptors: metadata allocation failed"));
    }
    for (u32 index = 0; index < framesInFlight; ++index)
    {
        std::construct_at(slots + index);
    }
    m_allocator = &allocator;
    m_slots = slots;
    m_framesInFlight = framesInFlight;
    m_capacityPerFrame = capacityPerFrame;
    return Status<Error>::Ok();
}

Status<Error> TransientDescriptorAllocator::BeginFrame(const rhi::FrameRing& ring,
                                                       const rhi::FrameSlot& frame) noexcept
{
    if (m_slots == nullptr || !ValidFrame(ring, frame, m_framesInFlight) ||
        (m_slots[frame.slot].hasFrame && frame.frameIndex <= m_slots[frame.slot].frameIndex))
    {
        return Status<Error>::Err(AllocatorError(ErrorCode::InvalidArgument,
                                                 "transient descriptors: invalid frame token"));
    }
    Slot& slot = m_slots[frame.slot];
    slot.cursor = 0;
    slot.frameIndex = frame.frameIndex;
    slot.hasFrame = true;
    m_activeSlot = frame.slot;
    m_activeFrameIndex = frame.frameIndex;
    return Status<Error>::Ok();
}

Result<TransientDescriptorRange, Error> TransientDescriptorAllocator::Allocate(u32 count) noexcept
{
    if (m_slots == nullptr || m_activeSlot >= m_framesInFlight || count == 0)
    {
        return Result<TransientDescriptorRange, Error>::Err(AllocatorError(
            ErrorCode::InvalidArgument, "transient descriptors: no active frame or zero count"));
    }
    Slot& slot = m_slots[m_activeSlot];
    if (slot.frameIndex != m_activeFrameIndex || count > m_capacityPerFrame - slot.cursor)
    {
        return Result<TransientDescriptorRange, Error>::Err(AllocatorError(
            ErrorCode::OutOfRange, "transient descriptors: frame capacity exhausted"));
    }
    const TransientDescriptorRange range{true, slot.cursor, count, m_activeSlot,
                                         m_activeFrameIndex};
    slot.cursor += count;
    return Result<TransientDescriptorRange, Error>::Ok(range);
}

void TransientDescriptorAllocator::Destroy() noexcept
{
    if (m_slots != nullptr)
    {
        for (u32 index = 0; index < m_framesInFlight; ++index)
        {
            std::destroy_at(m_slots + index);
        }
        m_allocator->Deallocate(m_slots);
    }
    m_allocator = nullptr;
    m_slots = nullptr;
    m_framesInFlight = 0;
    m_capacityPerFrame = 0;
    m_activeSlot = 0xFFFFFFFFU;
    m_activeFrameIndex = 0;
}

void TransientDescriptorAllocator::MoveFrom(TransientDescriptorAllocator& other) noexcept
{
    m_allocator = other.m_allocator;
    m_slots = other.m_slots;
    m_framesInFlight = other.m_framesInFlight;
    m_capacityPerFrame = other.m_capacityPerFrame;
    m_activeSlot = other.m_activeSlot;
    m_activeFrameIndex = other.m_activeFrameIndex;
    other.m_allocator = nullptr;
    other.m_slots = nullptr;
    other.m_framesInFlight = 0;
    other.m_capacityPerFrame = 0;
    other.m_activeSlot = 0xFFFFFFFFU;
    other.m_activeFrameIndex = 0;
}

}  // namespace cyber::renderer
