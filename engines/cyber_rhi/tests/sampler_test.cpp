#include <cyber/memory/heap_allocator.h>
#include <cyber/rhi/backend.h>
#include <cyber/rhi/device.h>
#include <cyber/rhi/sampler.h>
#include <cyber/test/test.h>

#include <limits>
#include <utility>

using namespace cyber::rhi;
using cyber::core::ErrorCode;
using cyber::core::u32;
using cyber::memory::HeapAllocator;

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

}  // namespace

CG_TEST(rhi_sampler_defaults_and_lifetime)
{
    HeapAllocator heap;
    {
        Device device;
        CG_REQUIRE(OpenDevice(device, heap));

        // A zeroed descriptor is valid: trilinear, repeat, isotropic, no
        // comparison. A sampler has no required field, unlike a buffer.
        auto created = device.CreateSampler(SamplerDesc{});
        CG_REQUIRE(static_cast<bool>(created));
        const SamplerHandle sampler = created.Value();
        CG_EXPECT_TRUE(sampler.IsValid());

        CG_EXPECT_TRUE(device.DestroySampler(sampler));
        CG_EXPECT_FALSE(device.DestroySampler(sampler));
    }
    CG_EXPECT_EQ(heap.Stats().used, cyber::core::usize{0});
}

CG_TEST(rhi_sampler_anisotropy)
{
    HeapAllocator heap;
    Device device;
    CG_REQUIRE(OpenDevice(device, heap));
    const u32 maxAnisotropy = device.Capabilities().limits.maxSamplerAnisotropy;
    CG_EXPECT_TRUE(maxAnisotropy >= 1);

    SamplerDesc desc{};
    desc.maxAnisotropy = maxAnisotropy;
    CG_EXPECT_TRUE(static_cast<bool>(device.CreateSampler(desc)));

    // Zero and above the limit are rejected; the null backend reports both.
    desc.maxAnisotropy = 0;
    auto created = device.CreateSampler(desc);
    CG_EXPECT_FALSE(created);
    CG_EXPECT_EQ(created.Error().code, ErrorCode::InvalidArgument);

    desc.maxAnisotropy = maxAnisotropy + 1;
    created = device.CreateSampler(desc);
    CG_EXPECT_FALSE(created);
    CG_EXPECT_EQ(created.Error().code, ErrorCode::InvalidArgument);

    // Anisotropy without linear filtering on all three axes is meaningless.
    desc.maxAnisotropy = 2;
    desc.magFilter = FilterMode::Nearest;
    created = device.CreateSampler(desc);
    CG_EXPECT_FALSE(created);
    CG_EXPECT_EQ(created.Error().code, ErrorCode::InvalidArgument);

    desc.magFilter = FilterMode::Linear;
    desc.mipmapFilter = FilterMode::Nearest;
    created = device.CreateSampler(desc);
    CG_EXPECT_FALSE(created);
    CG_EXPECT_EQ(created.Error().code, ErrorCode::InvalidArgument);

    desc.mipmapFilter = FilterMode::Linear;
    CG_EXPECT_TRUE(static_cast<bool>(device.CreateSampler(desc)));
}

CG_TEST(rhi_sampler_lod_rules)
{
    HeapAllocator heap;
    Device device;
    CG_REQUIRE(OpenDevice(device, heap));

    SamplerDesc desc{};
    CG_EXPECT_TRUE(static_cast<bool>(device.CreateSampler(desc)));

    // The boundaries themselves are legal.
    desc = SamplerDesc{};
    desc.minLod = 0.0f;
    desc.maxLod = kMaxSamplerLod;
    desc.mipLodBias = kMaxMipLodBias;
    CG_EXPECT_TRUE(static_cast<bool>(device.CreateSampler(desc)));
    desc.mipLodBias = -kMaxMipLodBias;
    CG_EXPECT_TRUE(static_cast<bool>(device.CreateSampler(desc)));

    desc = SamplerDesc{};
    desc.minLod = 2.0f;
    desc.maxLod = 1.0f;
    auto created = device.CreateSampler(desc);
    CG_EXPECT_FALSE(created);
    CG_EXPECT_EQ(created.Error().code, ErrorCode::InvalidArgument);

    desc = SamplerDesc{};
    desc.minLod = -0.5f;
    created = device.CreateSampler(desc);
    CG_EXPECT_FALSE(created);
    CG_EXPECT_EQ(created.Error().code, ErrorCode::InvalidArgument);

    desc = SamplerDesc{};
    desc.maxLod = kMaxSamplerLod + 1.0f;
    created = device.CreateSampler(desc);
    CG_EXPECT_FALSE(created);
    CG_EXPECT_EQ(created.Error().code, ErrorCode::InvalidArgument);

    desc = SamplerDesc{};
    desc.mipLodBias = kMaxMipLodBias + 0.5f;
    created = device.CreateSampler(desc);
    CG_EXPECT_FALSE(created);
    CG_EXPECT_EQ(created.Error().code, ErrorCode::InvalidArgument);

    // A NaN compares false against both bounds, so it is rejected rather than
    // accepted by a one-sided test.
    desc = SamplerDesc{};
    desc.minLod = std::numeric_limits<float>::quiet_NaN();
    created = device.CreateSampler(desc);
    CG_EXPECT_FALSE(created);
    CG_EXPECT_EQ(created.Error().code, ErrorCode::InvalidArgument);

    desc = SamplerDesc{};
    desc.mipLodBias = std::numeric_limits<float>::quiet_NaN();
    created = device.CreateSampler(desc);
    CG_EXPECT_FALSE(created);
    CG_EXPECT_EQ(created.Error().code, ErrorCode::InvalidArgument);
}

CG_TEST(rhi_sampler_modes_and_compare)
{
    HeapAllocator heap;
    Device device;
    CG_REQUIRE(OpenDevice(device, heap));

    SamplerDesc desc{};
    desc.minFilter = FilterMode::Nearest;
    desc.magFilter = FilterMode::Linear;
    desc.mipmapFilter = FilterMode::Nearest;
    desc.addressU = AddressMode::ClampToBorder;
    desc.addressV = AddressMode::MirroredRepeat;
    desc.addressW = AddressMode::ClampToEdge;
    desc.borderColor = BorderColor::OpaqueWhite;
    CG_EXPECT_TRUE(static_cast<bool>(device.CreateSampler(desc)));

    desc.compareEnabled = true;
    desc.compareFunction = CompareFunction::LessEqual;
    CG_EXPECT_TRUE(static_cast<bool>(device.CreateSampler(desc)));

    // Values outside an enumeration are rejected rather than truncated into a
    // valid enumerator.
    desc = SamplerDesc{};
    desc.minFilter = static_cast<FilterMode>(7);
    auto created = device.CreateSampler(desc);
    CG_EXPECT_FALSE(created);
    CG_EXPECT_EQ(created.Error().code, ErrorCode::InvalidArgument);

    desc = SamplerDesc{};
    desc.addressW = static_cast<AddressMode>(9);
    created = device.CreateSampler(desc);
    CG_EXPECT_FALSE(created);
    CG_EXPECT_EQ(created.Error().code, ErrorCode::InvalidArgument);

    desc = SamplerDesc{};
    desc.borderColor = static_cast<BorderColor>(5);
    created = device.CreateSampler(desc);
    CG_EXPECT_FALSE(created);
    CG_EXPECT_EQ(created.Error().code, ErrorCode::InvalidArgument);

    desc = SamplerDesc{};
    desc.compareFunction = static_cast<CompareFunction>(12);
    created = device.CreateSampler(desc);
    CG_EXPECT_FALSE(created);
    CG_EXPECT_EQ(created.Error().code, ErrorCode::InvalidArgument);
}
