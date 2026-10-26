#include <cyber/memory/heap_allocator.h>
#include <cyber/memory/span.h>
#include <cyber/rhi/backend.h>
#include <cyber/rhi/buffer.h>
#include <cyber/rhi/device.h>
#include <cyber/test/test.h>

#include <utility>

using namespace cyber::rhi;
using cyber::core::ErrorCode;
using cyber::core::u32;
using cyber::core::u64;
using cyber::memory::Byte;
using cyber::memory::HeapAllocator;
using cyber::memory::Span;

namespace
{

bool OpenDevice(Device& out, HeapAllocator& heap) noexcept
{
    DeviceDesc desc{};
    desc.backend = NullBackend();
    auto created = Device::Create(desc, heap);
    if (!created)
    {
        return false;
    }
    out = std::move(created.Value());
    return true;
}

constexpr Byte B(unsigned value) noexcept
{
    return static_cast<Byte>(value);
}

BufferDesc MakeDesc(BufferUsage usage, u64 sizeBytes, u32 elementStride = 0) noexcept
{
    BufferDesc desc{};
    desc.usage = usage;
    desc.sizeBytes = sizeBytes;
    desc.elementStride = elementStride;
    desc.label = "test buffer";
    return desc;
}

}  // namespace

CG_TEST(rhi_buffer_create_destroy_and_info)
{
    HeapAllocator heap;
    {
        Device device;
        CG_REQUIRE(OpenDevice(device, heap));

        auto created = device.CreateBuffer(
            MakeDesc(BufferUsage::Vertex | BufferUsage::CopyDestination, 1024, 32));
        CG_REQUIRE(static_cast<bool>(created));
        const BufferHandle buffer = created.Value();
        CG_EXPECT_TRUE(buffer.IsValid());

        const BufferInfo info = device.BufferInfoOf(buffer);
        CG_EXPECT_TRUE(info.valid);
        CG_EXPECT_EQ(info.sizeBytes, u64{1024});
        CG_EXPECT_EQ(info.elementStride, u32{32});
        CG_EXPECT_EQ(info.usage, BufferUsage::Vertex | BufferUsage::CopyDestination);
        CG_EXPECT_EQ(device.BufferStateOf(buffer), ResourceState::Undefined);

        CG_EXPECT_TRUE(device.DestroyBuffer(buffer));
        // A destroyed buffer has no info, no state, and cannot be destroyed twice.
        CG_EXPECT_FALSE(device.BufferInfoOf(buffer).valid);
        CG_EXPECT_EQ(device.BufferStateOf(buffer), ResourceState::Undefined);
        const auto destroyedAgain = device.DestroyBuffer(buffer);
        CG_EXPECT_FALSE(destroyedAgain);
        CG_EXPECT_EQ(destroyedAgain.Error().code, ErrorCode::InvalidArgument);
    }
    CG_EXPECT_EQ(heap.Stats().used, cyber::core::usize{0});
}

CG_TEST(rhi_buffer_invalid_descriptors)
{
    HeapAllocator heap;
    Device device;
    CG_REQUIRE(OpenDevice(device, heap));
    const Limits limits = device.Capabilities().limits;

    const auto noUsage = device.CreateBuffer(MakeDesc(BufferUsage::None, 256));
    CG_EXPECT_FALSE(noUsage);
    CG_EXPECT_EQ(noUsage.Error().code, ErrorCode::InvalidArgument);

    const auto zeroSize = device.CreateBuffer(MakeDesc(BufferUsage::Vertex, 0));
    CG_EXPECT_FALSE(zeroSize);
    CG_EXPECT_EQ(zeroSize.Error().code, ErrorCode::InvalidArgument);

    // Upload and readback memory cannot be both, and neither can be bound to a
    // pipeline: that is the D3D12 heap rule, not a simplification.
    const auto bothHostKinds =
        device.CreateBuffer(MakeDesc(BufferUsage::HostVisible | BufferUsage::Readback, 256));
    CG_EXPECT_FALSE(bothHostKinds);
    CG_EXPECT_EQ(bothHostKinds.Error().code, ErrorCode::InvalidArgument);

    const auto hostVertex =
        device.CreateBuffer(MakeDesc(BufferUsage::HostVisible | BufferUsage::Vertex, 256));
    CG_EXPECT_FALSE(hostVertex);
    CG_EXPECT_EQ(hostVertex.Error().code, ErrorCode::InvalidArgument);

    const auto readbackIndex =
        device.CreateBuffer(MakeDesc(BufferUsage::Readback | BufferUsage::Index, 256));
    CG_EXPECT_FALSE(readbackIndex);
    CG_EXPECT_EQ(readbackIndex.Error().code, ErrorCode::InvalidArgument);

    // A structured stride is a multiple of four and fits the API limit.
    const auto badStride = device.CreateBuffer(MakeDesc(BufferUsage::Storage, 256, 6));
    CG_EXPECT_FALSE(badStride);
    CG_EXPECT_EQ(badStride.Error().code, ErrorCode::InvalidArgument);
    const auto hugeStride = device.CreateBuffer(MakeDesc(BufferUsage::Storage, 256, 2052));
    CG_EXPECT_FALSE(hugeStride);
    CG_EXPECT_EQ(hugeStride.Error().code, ErrorCode::InvalidArgument);

    // Above the capability limit is a distinct code so a caller that read the
    // limits can tell "too much" from "malformed".
    const auto tooLarge =
        device.CreateBuffer(MakeDesc(BufferUsage::Vertex, limits.maxBufferBytes + 1));
    CG_EXPECT_FALSE(tooLarge);
    CG_EXPECT_EQ(tooLarge.Error().code, ErrorCode::OutOfRange);

    // The limit itself is accepted.
    BufferDesc atLimit = MakeDesc(BufferUsage::Vertex, limits.maxBufferBytes);
    atLimit.elementStride = 0;
    const auto accepted = device.CreateBuffer(atLimit);
    CG_EXPECT_TRUE(static_cast<bool>(accepted));
    if (accepted)
    {
        CG_EXPECT_TRUE(device.DestroyBuffer(accepted.Value()));
    }
}

CG_TEST(rhi_buffer_host_round_trip)
{
    HeapAllocator heap;
    Device device;
    CG_REQUIRE(OpenDevice(device, heap));

    auto created =
        device.CreateBuffer(MakeDesc(BufferUsage::HostVisible | BufferUsage::CopySource, 64));
    CG_REQUIRE(static_cast<bool>(created));
    const BufferHandle staging = created.Value();

    const Byte pattern[8] = {B(1), B(2), B(3), B(4), B(5), B(6), B(7), B(8)};
    CG_EXPECT_TRUE(device.WriteBuffer(staging, 16, Span<const Byte>(pattern, 8)));

    Byte read[8] = {};
    CG_EXPECT_TRUE(device.ReadBuffer(staging, 16, Span<Byte>(read, 8)));
    for (u32 index = 0; index < 8; ++index)
    {
        CG_EXPECT_EQ(read[index], pattern[index]);
    }
    CG_EXPECT_EQ(read[0], B(1));
    CG_EXPECT_EQ(read[7], B(8));

    // A window into the middle of what was written reads the matching bytes:
    // the copy lands at the offset, not at the start.
    Byte middle[4] = {};
    CG_EXPECT_TRUE(device.ReadBuffer(staging, 18, Span<Byte>(middle, 4)));
    CG_EXPECT_EQ(middle[0], B(3));
    CG_EXPECT_EQ(middle[1], B(4));
    CG_EXPECT_EQ(middle[2], B(5));
    CG_EXPECT_EQ(middle[3], B(6));

    // Overwriting part of it leaves the rest alone.
    const Byte overwrite[2] = {B(40), B(41)};
    CG_EXPECT_TRUE(device.WriteBuffer(staging, 20, Span<const Byte>(overwrite, 2)));
    Byte after[8] = {};
    CG_EXPECT_TRUE(device.ReadBuffer(staging, 16, Span<Byte>(after, 8)));
    CG_EXPECT_EQ(after[4], B(40));
    CG_EXPECT_EQ(after[5], B(41));
    CG_EXPECT_EQ(after[6], B(7));
    CG_EXPECT_EQ(after[7], B(8));

    // Writing to the last byte is legal; writing past it is not.
    const Byte tail = B(9);
    CG_EXPECT_TRUE(device.WriteBuffer(staging, 63, Span<const Byte>(&tail, 1)));
    const auto pastEnd = device.WriteBuffer(staging, 63, Span<const Byte>(&tail, 2));
    CG_EXPECT_FALSE(pastEnd);
    CG_EXPECT_EQ(pastEnd.Error().code, ErrorCode::OutOfRange);

    // An empty span at the end and an empty span with a null pointer are both
    // legal: a zero-length copy has nothing to check.
    CG_EXPECT_TRUE(device.WriteBuffer(staging, 64, Span<const Byte>()));
    CG_EXPECT_TRUE(device.WriteBuffer(staging, 64, Span<const Byte>(nullptr, 0)));
    const auto emptyPastEnd = device.WriteBuffer(staging, 65, Span<const Byte>());
    CG_EXPECT_FALSE(emptyPastEnd);
    CG_EXPECT_EQ(emptyPastEnd.Error().code, ErrorCode::OutOfRange);

    // A null source with a non-zero size is a caller bug, not an empty copy.
    const auto nullSource = device.WriteBuffer(staging, 0, Span<const Byte>(nullptr, 4));
    CG_EXPECT_FALSE(nullSource);
    CG_EXPECT_EQ(nullSource.Error().code, ErrorCode::InvalidArgument);

    // An offset that would overflow the range arithmetic is rejected, not
    // wrapped into a legal one.
    const auto overflow = device.WriteBuffer(staging, ~u64{0} - 3, Span<const Byte>(pattern, 8));
    CG_EXPECT_FALSE(overflow);
    CG_EXPECT_EQ(overflow.Error().code, ErrorCode::OutOfRange);
}

CG_TEST(rhi_buffer_host_access_requires_host_usage)
{
    HeapAllocator heap;
    Device device;
    CG_REQUIRE(OpenDevice(device, heap));

    auto vertex = device.CreateBuffer(MakeDesc(BufferUsage::Vertex, 256));
    CG_REQUIRE(static_cast<bool>(vertex));
    const Byte data[4] = {B(1), B(2), B(3), B(4)};
    Byte out[4] = {};

    const auto written = device.WriteBuffer(vertex.Value(), 0, Span<const Byte>(data, 4));
    CG_EXPECT_FALSE(written);
    CG_EXPECT_EQ(written.Error().code, ErrorCode::InvalidArgument);
    const auto read = device.ReadBuffer(vertex.Value(), 0, Span<Byte>(out, 4));
    CG_EXPECT_FALSE(read);
    CG_EXPECT_EQ(read.Error().code, ErrorCode::InvalidArgument);

    // A readback buffer is written by the GPU, so the CPU cannot write it, but
    // it is readable.
    auto readback = device.CreateBuffer(MakeDesc(BufferUsage::Readback, 256));
    CG_REQUIRE(static_cast<bool>(readback));
    const auto writeReadback = device.WriteBuffer(readback.Value(), 0, Span<const Byte>(data, 4));
    CG_EXPECT_FALSE(writeReadback);
    CG_EXPECT_EQ(writeReadback.Error().code, ErrorCode::InvalidArgument);
    CG_EXPECT_TRUE(device.ReadBuffer(readback.Value(), 0, Span<Byte>(out, 4)));

    // Writes and reads on a stale handle are rejected before any range check.
    const BufferHandle stale = vertex.Value();
    CG_EXPECT_TRUE(device.DestroyBuffer(stale));
    const auto staleWrite = device.WriteBuffer(stale, 0, Span<const Byte>(data, 4));
    CG_EXPECT_FALSE(staleWrite);
    CG_EXPECT_EQ(staleWrite.Error().code, ErrorCode::InvalidArgument);
    const auto staleRead = device.ReadBuffer(stale, 0, Span<Byte>(out, 4));
    CG_EXPECT_FALSE(staleRead);
    CG_EXPECT_EQ(staleRead.Error().code, ErrorCode::InvalidArgument);
}

CG_TEST(rhi_buffer_handles_do_not_alias_after_reuse)
{
    HeapAllocator heap;
    Device device;
    CG_REQUIRE(OpenDevice(device, heap));

    auto first = device.CreateBuffer(MakeDesc(BufferUsage::Vertex, 64));
    CG_REQUIRE(static_cast<bool>(first));
    const BufferHandle oldHandle = first.Value();
    CG_EXPECT_TRUE(device.DestroyBuffer(oldHandle));

    // The slot is recycled. The old handle has the previous generation, so it
    // must not resolve even though the index is live again.
    auto second = device.CreateBuffer(MakeDesc(BufferUsage::Vertex, 128));
    CG_REQUIRE(static_cast<bool>(second));
    const BufferHandle newHandle = second.Value();
    CG_EXPECT_EQ(oldHandle.index, newHandle.index);
    CG_EXPECT_NE(oldHandle.generation, newHandle.generation);

    CG_EXPECT_FALSE(device.BufferInfoOf(oldHandle).valid);
    CG_EXPECT_TRUE(device.BufferInfoOf(newHandle).valid);
    CG_EXPECT_EQ(device.BufferInfoOf(newHandle).sizeBytes, u64{128});
    const auto destroyOld = device.DestroyBuffer(oldHandle);
    CG_EXPECT_FALSE(destroyOld);
    CG_EXPECT_EQ(destroyOld.Error().code, ErrorCode::InvalidArgument);
}

CG_TEST(rhi_buffer_device_teardown_releases_host_memory)
{
    HeapAllocator heap;
    {
        Device device;
        CG_REQUIRE(OpenDevice(device, heap));
        auto created =
            device.CreateBuffer(MakeDesc(BufferUsage::HostVisible | BufferUsage::CopySource, 4096));
        CG_REQUIRE(static_cast<bool>(created));
        CG_EXPECT_TRUE(device.BufferInfoOf(created.Value()).valid);
        // Intentionally leak the buffer: the device owns its storage and must
        // release it when the device dies.
    }
    CG_EXPECT_EQ(heap.Stats().used, cyber::core::usize{0});
}
