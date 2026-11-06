#include <cyber/renderer/frame_stream.h>

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
using cyber::memory::Span;

[[nodiscard]] Error StreamError(ErrorCode code, const char* message) noexcept
{
    return Error{code, message};
}

[[nodiscard]] bool IsSupportedBinding(rhi::BindingType type) noexcept
{
    return type == rhi::BindingType::UniformBuffer || type == rhi::BindingType::StorageBuffer ||
           type == rhi::BindingType::ReadOnlyStorageBuffer;
}

[[nodiscard]] bool AlignUp(u64 value, u64 alignment, u64& aligned) noexcept
{
    if (alignment == 0)
    {
        return false;
    }
    const u64 remainder = value % alignment;
    const u64 padding = remainder == 0 ? 0 : alignment - remainder;
    if (value > std::numeric_limits<u64>::max() - padding)
    {
        return false;
    }
    aligned = value + padding;
    return true;
}

[[nodiscard]] u64 GreatestCommonDivisor(u64 lhs, u64 rhs) noexcept
{
    while (rhs != 0)
    {
        const u64 remainder = lhs % rhs;
        lhs = rhs;
        rhs = remainder;
    }
    return lhs;
}

}  // namespace

FrameBufferStream::~FrameBufferStream()
{
    (void)Destroy();
}

FrameBufferStream::FrameBufferStream(FrameBufferStream&& other) noexcept
{
    MoveFrom(other);
}

FrameBufferStream& FrameBufferStream::operator=(FrameBufferStream&& other) noexcept
{
    if (this != &other)
    {
        (void)Destroy();
        MoveFrom(other);
    }
    return *this;
}

Result<FrameBufferStream, Error>
FrameBufferStream::Create(rhi::Device& device, memory::Allocator& allocator,
                          const FrameBufferStreamDesc& desc) noexcept
{
    if (!device.IsValid() || desc.framesInFlight == 0 ||
        desc.framesInFlight > rhi::kMaxFramesInFlight || desc.capacityBytes == 0 ||
        desc.maxAllocationsPerFrame == 0 || !IsSupportedBinding(desc.bindingType) ||
        (desc.bindingType == rhi::BindingType::UniformBuffer && desc.elementStride != 0) ||
        (desc.bindingType != rhi::BindingType::UniformBuffer && desc.elementStride % 4 != 0))
    {
        return Result<FrameBufferStream, Error>::Err(
            StreamError(ErrorCode::InvalidArgument, "frame stream: invalid descriptor"));
    }
    if (desc.capacityBytes > device.Capabilities().limits.maxBufferBytes ||
        desc.maxAllocationsPerFrame > std::numeric_limits<cyber::core::usize>::max() /
                                          (desc.framesInFlight * sizeof(UploadRange)))
    {
        return Result<FrameBufferStream, Error>::Err(
            StreamError(ErrorCode::OutOfRange, "frame stream: capacity exceeds limits"));
    }
    if (desc.bindingType != rhi::BindingType::UniformBuffer && desc.elementStride != 0 &&
        desc.capacityBytes % desc.elementStride != 0)
    {
        return Result<FrameBufferStream, Error>::Err(StreamError(
            ErrorCode::InvalidArgument, "frame stream: capacity is not element aligned"));
    }

    FrameBufferStream stream{};
    stream.m_device = &device;
    stream.m_allocator = &allocator;
    stream.m_capacityBytes = desc.capacityBytes;
    stream.m_maxAllocationsPerFrame = desc.maxAllocationsPerFrame;
    stream.m_framesInFlight = desc.framesInFlight;
    stream.m_bindingType = desc.bindingType;
    stream.m_readState = desc.bindingType == rhi::BindingType::UniformBuffer
                             ? rhi::ResourceState::ConstantBuffer
                             : (desc.bindingType == rhi::BindingType::StorageBuffer
                                    ? rhi::ResourceState::ShaderWrite
                                    : rhi::ResourceState::ShaderRead);
    const cyber::core::usize rangeCount =
        static_cast<cyber::core::usize>(desc.framesInFlight) * desc.maxAllocationsPerFrame;
    stream.m_ranges = static_cast<UploadRange*>(
        allocator.Allocate(rangeCount * sizeof(UploadRange), alignof(UploadRange)));
    if (stream.m_ranges == nullptr)
    {
        stream.Reset();
        return Result<FrameBufferStream, Error>::Err(
            StreamError(ErrorCode::Internal, "frame stream: upload metadata allocation failed"));
    }

    const rhi::BufferUsage bindingUsage = desc.bindingType == rhi::BindingType::UniformBuffer
                                              ? rhi::BufferUsage::Uniform
                                              : rhi::BufferUsage::Storage;
    for (u32 slotIndex = 0; slotIndex < desc.framesInFlight; ++slotIndex)
    {
        rhi::BufferDesc stagingDesc{};
        stagingDesc.usage = rhi::BufferUsage::HostVisible | rhi::BufferUsage::CopySource;
        stagingDesc.sizeBytes = desc.capacityBytes;
        stagingDesc.label = "renderer frame stream staging";
        auto staging = device.CreateBuffer(stagingDesc);
        if (!staging)
        {
            const Error error = staging.Error();
            (void)stream.Destroy();
            return Result<FrameBufferStream, Error>::Err(error);
        }
        stream.m_slots[slotIndex].staging = staging.Value();

        rhi::BufferDesc destinationDesc{};
        destinationDesc.usage = bindingUsage | rhi::BufferUsage::CopyDestination;
        destinationDesc.sizeBytes = desc.capacityBytes;
        destinationDesc.elementStride = desc.elementStride;
        destinationDesc.label = "renderer frame stream destination";
        auto destination = device.CreateBuffer(destinationDesc);
        if (!destination)
        {
            const Error error = destination.Error();
            (void)stream.Destroy();
            return Result<FrameBufferStream, Error>::Err(error);
        }
        stream.m_slots[slotIndex].destination = destination.Value();
    }
    return Result<FrameBufferStream, Error>::Ok(std::move(stream));
}

Status<Error> FrameBufferStream::BeginFrame(const rhi::FrameRing& ring,
                                            const rhi::FrameSlot& frame) noexcept
{
    if (!IsValid() || !ring.IsValid() || ring.FramesInFlight() != m_framesInFlight || m_poisoned ||
        !frame.valid || frame.slot >= m_framesInFlight || frame.frameIndex >= ring.FrameIndex() ||
        frame.frameIndex % m_framesInFlight != frame.slot ||
        ring.SignalValueForFrame(frame.frameIndex) != frame.signalValue)
    {
        return Status<Error>::Err(
            StreamError(ErrorCode::InvalidArgument, "frame stream: invalid frame token"));
    }
    Slot& slot = m_slots[frame.slot];
    if (slot.rangeCount != 0 || (slot.hasFrame && frame.frameIndex <= slot.frameIndex))
    {
        return Status<Error>::Err(StreamError(
            ErrorCode::InvalidArgument, "frame stream: frame slot has pending or stale data"));
    }
    slot.cursor = 0;
    slot.frameIndex = frame.frameIndex;
    slot.hasFrame = true;
    m_activeSlot = frame.slot;
    m_activeFrameIndex = frame.frameIndex;
    return Status<Error>::Ok();
}

Result<FrameBufferAllocation, Error>
FrameBufferStream::Allocate(Span<const memory::Byte> bytes) noexcept
{
    if (!IsValid() || m_activeSlot >= m_framesInFlight || bytes.Data() == nullptr || bytes.Empty())
    {
        return Result<FrameBufferAllocation, Error>::Err(
            StreamError(ErrorCode::InvalidArgument, "frame stream: no active frame or empty data"));
    }
    Slot& slot = m_slots[m_activeSlot];
    if (slot.frameIndex != m_activeFrameIndex || slot.rangeCount >= m_maxAllocationsPerFrame)
    {
        return Result<FrameBufferAllocation, Error>::Err(
            StreamError(ErrorCode::OutOfRange, "frame stream: allocation count exceeded"));
    }

    const rhi::Limits& limits = m_device->Capabilities().limits;
    u64 alignment = m_bindingType == rhi::BindingType::UniformBuffer
                        ? limits.minUniformBufferOffsetAlignment
                        : limits.minStorageBufferOffsetAlignment;
    const rhi::BufferInfo info = m_device->BufferInfoOf(slot.destination);
    if (info.elementStride != 0 && bytes.Size() % info.elementStride != 0)
    {
        return Result<FrameBufferAllocation, Error>::Err(
            StreamError(ErrorCode::InvalidArgument, "frame stream: data is not element aligned"));
    }
    if (info.elementStride != 0)
    {
        const u64 divisor = GreatestCommonDivisor(alignment, info.elementStride);
        const u64 factor = alignment / divisor;
        if (factor > std::numeric_limits<u64>::max() / info.elementStride)
        {
            return Result<FrameBufferAllocation, Error>::Err(
                StreamError(ErrorCode::OutOfRange, "frame stream: alignment overflows"));
        }
        alignment = factor * info.elementStride;
    }
    u64 offset = 0;
    if (!AlignUp(slot.cursor, alignment, offset) || offset > m_capacityBytes ||
        bytes.Size() > m_capacityBytes - offset)
    {
        return Result<FrameBufferAllocation, Error>::Err(
            StreamError(ErrorCode::OutOfRange, "frame stream: frame buffer capacity exceeded"));
    }
    const Status<Error> written = m_device->WriteBuffer(
        slot.staging, offset, Span<const memory::Byte>(bytes.Data(), bytes.Size()));
    if (!written)
    {
        return Result<FrameBufferAllocation, Error>::Err(written.Error());
    }
    const u32 rangeIndex = m_activeSlot * m_maxAllocationsPerFrame + slot.rangeCount;
    m_ranges[rangeIndex] = UploadRange{offset, static_cast<u64>(bytes.Size())};
    ++slot.rangeCount;
    slot.cursor = offset + bytes.Size();

    FrameBufferAllocation allocation{};
    allocation.valid = true;
    allocation.buffer = slot.destination;
    allocation.offsetBytes = offset;
    allocation.sizeBytes = bytes.Size();
    allocation.frameIndex = m_activeFrameIndex;
    return Result<FrameBufferAllocation, Error>::Ok(allocation);
}

Status<Error> FrameBufferStream::RecordUploads(rhi::CommandList& commandList) noexcept
{
    if (!IsValid() || m_activeSlot >= m_framesInFlight ||
        m_slots[m_activeSlot].frameIndex != m_activeFrameIndex ||
        commandList.Queue() != rhi::QueueKind::Graphics || !commandList.IsRecording())
    {
        return Status<Error>::Err(
            StreamError(ErrorCode::InvalidArgument, "frame stream: invalid upload recording"));
    }
    Slot& slot = m_slots[m_activeSlot];
    if (slot.rangeCount == 0)
    {
        return Status<Error>::Ok();
    }

    rhi::BufferBarrier initial[2]{};
    u32 barrierCount = 0;
    if (!slot.stagingReady)
    {
        initial[barrierCount++] = {slot.staging, rhi::ResourceState::Undefined,
                                   rhi::ResourceState::CopySource};
    }
    initial[barrierCount++] = {slot.destination, slot.destinationState,
                               rhi::ResourceState::CopyDestination};
    const Status<Error> initialBarrier =
        commandList.Barrier(Span<const rhi::BufferBarrier>(initial, barrierCount));
    if (!initialBarrier)
    {
        return initialBarrier;
    }

    for (u32 rangeIndex = 0; rangeIndex < slot.rangeCount; ++rangeIndex)
    {
        const UploadRange& range = m_ranges[m_activeSlot * m_maxAllocationsPerFrame + rangeIndex];
        const rhi::BufferCopy copy{slot.staging, range.offsetBytes, slot.destination,
                                   range.offsetBytes, range.sizeBytes};
        const Status<Error> copied = commandList.CopyBuffer(copy);
        if (!copied)
        {
            m_poisoned = true;
            return copied;
        }
    }
    const rhi::BufferBarrier final{slot.destination, rhi::ResourceState::CopyDestination,
                                   m_readState};
    const Status<Error> finalBarrier =
        commandList.Barrier(Span<const rhi::BufferBarrier>(&final, 1));
    if (!finalBarrier)
    {
        m_poisoned = true;
        return finalBarrier;
    }
    slot.destinationState = m_readState;
    slot.stagingReady = true;
    slot.rangeCount = 0;
    return Status<Error>::Ok();
}

rhi::BindingType FrameBufferStream::BindingType() const noexcept
{
    return m_bindingType;
}

u64 FrameBufferStream::CapacityBytes() const noexcept
{
    return m_capacityBytes;
}

bool FrameBufferStream::IsValid() const noexcept
{
    return m_device != nullptr && m_ranges != nullptr && m_capacityBytes != 0 &&
           m_framesInFlight != 0 && !m_poisoned;
}

rhi::BufferHandle FrameBufferStream::DestinationBuffer(u32 slot) const noexcept
{
    return slot < m_framesInFlight ? m_slots[slot].destination : rhi::BufferHandle{};
}

Status<Error> FrameBufferStream::Destroy() noexcept
{
    Error firstError{};
    bool failed = false;
    if (m_device != nullptr)
    {
        for (u32 slotIndex = 0; slotIndex < m_framesInFlight; ++slotIndex)
        {
            Slot& slot = m_slots[slotIndex];
            if (slot.destination.IsValid())
            {
                const Status<Error> destroyed = m_device->DestroyBuffer(slot.destination);
                if (!destroyed && !failed)
                {
                    firstError = destroyed.Error();
                    failed = true;
                }
            }
            if (slot.staging.IsValid())
            {
                const Status<Error> destroyed = m_device->DestroyBuffer(slot.staging);
                if (!destroyed && !failed)
                {
                    firstError = destroyed.Error();
                    failed = true;
                }
            }
        }
    }
    Reset();
    return failed ? Status<Error>::Err(firstError) : Status<Error>::Ok();
}

void FrameBufferStream::MoveFrom(FrameBufferStream& other) noexcept
{
    m_device = other.m_device;
    m_allocator = other.m_allocator;
    m_ranges = other.m_ranges;
    m_capacityBytes = other.m_capacityBytes;
    m_maxAllocationsPerFrame = other.m_maxAllocationsPerFrame;
    m_framesInFlight = other.m_framesInFlight;
    m_activeSlot = other.m_activeSlot;
    m_activeFrameIndex = other.m_activeFrameIndex;
    m_poisoned = other.m_poisoned;
    m_bindingType = other.m_bindingType;
    m_readState = other.m_readState;
    m_slots = other.m_slots;
    other.m_device = nullptr;
    other.m_allocator = nullptr;
    other.m_ranges = nullptr;
    other.m_capacityBytes = 0;
    other.m_maxAllocationsPerFrame = 0;
    other.m_framesInFlight = 0;
    other.m_activeSlot = 0xFFFFFFFFU;
    other.m_activeFrameIndex = 0;
    other.m_poisoned = false;
    other.m_bindingType = rhi::BindingType::Count;
    other.m_readState = rhi::ResourceState::Undefined;
    other.m_slots = {};
}

void FrameBufferStream::Reset() noexcept
{
    if (m_ranges != nullptr && m_allocator != nullptr)
    {
        m_allocator->Deallocate(m_ranges);
    }
    m_device = nullptr;
    m_allocator = nullptr;
    m_ranges = nullptr;
    m_capacityBytes = 0;
    m_maxAllocationsPerFrame = 0;
    m_framesInFlight = 0;
    m_activeSlot = 0xFFFFFFFFU;
    m_activeFrameIndex = 0;
    m_poisoned = false;
    m_bindingType = rhi::BindingType::Count;
    m_readState = rhi::ResourceState::Undefined;
    m_slots = {};
}

}  // namespace cyber::renderer
