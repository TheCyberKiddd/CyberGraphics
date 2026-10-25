#include <cyber/memory/heap_allocator.h>
#include <cyber/memory/span.h>
#include <cyber/rhi/backend.h>
#include <cyber/rhi/capabilities.h>
#include <cyber/rhi/device.h>
#include <cyber/test/test.h>

#include <cstring>
#include <utility>

using namespace cyber::rhi;
using cyber::core::u32;
using cyber::core::u64;
using cyber::memory::Span;

namespace
{

bool IsPowerOfTwo(u32 value) noexcept
{
    return value != 0 && (value & (value - 1)) == 0;
}

}  // namespace

CG_TEST(rhi_capabilities_adapter_enumeration)
{
    const Backend* backend = NullBackend();
    CG_REQUIRE(backend != nullptr);

    AdapterInfo adapters[4] = {};
    const u32 written = EnumerateAdapters(*backend, Span<AdapterInfo>(adapters, 4));
    CG_EXPECT_EQ(written, u32{1});
    CG_EXPECT_EQ(adapters[0].backend, BackendKind::Null);
    CG_EXPECT_TRUE(std::strlen(adapters[0].name) > 0);
    CG_EXPECT_TRUE(std::strlen(adapters[0].name) < kAdapterNameBytes);
    CG_EXPECT_EQ(adapters[0].dedicatedVideoMemoryBytes, u64{0});
    CG_EXPECT_EQ(adapters[0].apiVersion, u32{0});

    // A zero-capacity span still reports the count so a caller can size its
    // buffer, and writes nothing.
    const u32 countOnly = EnumerateAdapters(*backend, Span<AdapterInfo>());
    CG_EXPECT_EQ(countOnly, u32{1});
}

CG_TEST(rhi_capabilities_features)
{
    Features features;
    CG_EXPECT_FALSE(features.Has(Feature::Compute));
    CG_EXPECT_EQ(features.bits, u32{0});

    features.Add(Feature::Compute);
    CG_EXPECT_TRUE(features.Has(Feature::Compute));
    CG_EXPECT_FALSE(features.Has(Feature::MeshShaders));

    // Adding twice is idempotent and removing is the inverse.
    features.Add(Feature::Compute);
    CG_EXPECT_EQ(features, Features::Of(Feature::Compute));
    features.Remove(Feature::Compute);
    CG_EXPECT_EQ(features, Features{});

    // The bits map to the enum value, which is what makes a persisted or logged
    // bitmask readable.
    constexpr Features compute = Features::Of(Feature::Compute);
    CG_EXPECT_EQ(compute.bits, u32{1} << static_cast<u32>(Feature::Compute));

    const Features combined =
        Features::Of(Feature::Compute) | Feature::IndirectDraw | Feature::TimelineFences;
    CG_EXPECT_TRUE(combined.Has(Feature::Compute));
    CG_EXPECT_TRUE(combined.Has(Feature::IndirectDraw));
    CG_EXPECT_TRUE(combined.Has(Feature::TimelineFences));
    CG_EXPECT_FALSE(combined.Has(Feature::MeshShaders));

    // Out-of-range features are never present and never corrupt the set.
    const u32 bitsBefore = combined.bits;
    CG_EXPECT_FALSE(combined.Has(Feature::Count));
    CG_EXPECT_FALSE(combined.Has(static_cast<Feature>(kFeatureCount + 5)));
    CG_EXPECT_EQ(combined.bits, bitsBefore);

    // Every enumerator has a distinct, readable name.
    u32 named = 0;
    for (u32 index = 0; index < kFeatureCount; ++index)
    {
        const Feature feature = static_cast<Feature>(index);
        if (feature == Feature::Count)
        {
            continue;
        }
        CG_EXPECT_TRUE(std::strcmp(FeatureName(feature), "unknown") != 0);
        ++named;
    }
    // The count sentinel's value is the number of real enumerators before it.
    CG_EXPECT_EQ(named, kFeatureCount);
    CG_EXPECT_STREQ(FeatureName(Feature::Count), "unknown");
}

CG_TEST(rhi_capabilities_limits_are_consistent)
{
    // The null backend's limits are what Phase 11 validation compares against, so
    // they must be internally consistent even where they are conservative.
    const Capabilities capabilities = []() -> Capabilities
    {
        DeviceDesc desc{};
        desc.backend = NullBackend();
        cyber::memory::HeapAllocator heap;
        auto created = Device::Create(desc, heap);
        if (!created)
        {
            return Capabilities{};
        }
        Device device = std::move(created.Value());
        return device.Capabilities();
    }();

    const Limits& limits = capabilities.limits;
    CG_EXPECT_TRUE(limits.maxTexture1D > 0);
    CG_EXPECT_TRUE(limits.maxTexture2D > 0);
    CG_EXPECT_TRUE(limits.maxTexture3D > 0);
    CG_EXPECT_TRUE(limits.maxTextureCube > 0);
    CG_EXPECT_TRUE(limits.maxTextureArrayLayers > 0);
    CG_EXPECT_TRUE(limits.maxColorAttachments > 0);
    CG_EXPECT_TRUE(limits.maxBindGroups > 0);
    CG_EXPECT_TRUE(limits.maxBindingsPerGroup > 0);
    CG_EXPECT_TRUE(limits.maxBufferBytes > 0);

    // 1D may exceed 2D; 2D is the ceiling for 3D and for a cube face.
    CG_EXPECT_TRUE(limits.maxTexture1D >= limits.maxTexture2D);
    CG_EXPECT_TRUE(limits.maxTexture3D <= limits.maxTexture2D);
    CG_EXPECT_TRUE(limits.maxTextureCube <= limits.maxTexture2D);

    // Buffer offset alignments are powers of two, because a backend rounds with a
    // mask rather than a division.
    CG_EXPECT_TRUE(IsPowerOfTwo(limits.minUniformBufferOffsetAlignment));
    CG_EXPECT_TRUE(IsPowerOfTwo(limits.minStorageBufferOffsetAlignment));

    // A validation backend accepts what it validates.
    CG_EXPECT_TRUE(capabilities.features.Has(Feature::Compute));
    CG_EXPECT_TRUE(capabilities.features.Has(Feature::TimelineFences));
    CG_EXPECT_EQ(capabilities.adapter.backend, BackendKind::Null);
}
