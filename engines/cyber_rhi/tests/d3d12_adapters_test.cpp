#include <cyber/memory/heap_allocator.h>
#include <cyber/memory/span.h>
#include <cyber/rhi/backend.h>
#include <cyber/rhi/d3d12.h>
#include <cyber/rhi/device.h>
#include <cyber/test/test.h>

using namespace cyber::rhi;
using cyber::core::u32;
using cyber::core::u64;
using cyber::memory::HeapAllocator;
using cyber::memory::Span;

namespace
{

// Microsoft's PCI vendor id. Every WARP adapter reports it and no hardware
// vendor does.
inline constexpr u32 kMicrosoftVendorId = 0x1414;

// Largest adapter list any sane machine reports. The test asserts the count fits
// this so a driver that enumerates without end is a failure, not a hang.
inline constexpr u32 kMaxAdapters = 32;

[[nodiscard]] u32 CountAdapters(const Backend& backend) noexcept
{
    // An empty span is the documented way to ask for the count.
    return backend.enumerateAdapters(nullptr, 0);
}

}  // namespace

CG_TEST(d3d12_backend_presence_matches_the_build)
{
    // The header's compile-time query and the entry point must agree, or a
    // consumer would take a null backend pointer after being told the API is
    // available.
    const Backend* backend = D3D12Backend();
    CG_EXPECT_EQ(IsD3D12CompiledIn(), backend != nullptr);
}

CG_TEST(d3d12_backend_describes_itself)
{
    const Backend* backend = D3D12Backend();
    CG_REQUIRE(backend != nullptr);

    CG_EXPECT_TRUE(backend->kind == BackendKind::D3D12);
    CG_EXPECT_STREQ(backend->name, "d3d12");
    CG_EXPECT_EQ(backend->apiVersion, u32{1200});
    CG_EXPECT_TRUE(backend->enumerateAdapters != nullptr);

    // Enumerating adapters must not require a device, so the backend answers
    // before any device exists.
    CG_EXPECT_TRUE(CountAdapters(*backend) >= 1);
}

CG_TEST(d3d12_backend_enumerates_a_hardware_adapter)
{
    const Backend* backend = D3D12Backend();
    CG_REQUIRE(backend != nullptr);

    AdapterInfo adapters[kMaxAdapters]{};
    const u32 count = EnumerateAdapters(*backend, Span<AdapterInfo>(adapters, kMaxAdapters));
    CG_REQUIRE(count >= 1);
    CG_REQUIRE(count <= kMaxAdapters);
    CG_EXPECT_EQ(count, CountAdapters(*backend));

    for (u32 index = 0; index < count; ++index)
    {
        const AdapterInfo& adapter = adapters[index];
        CG_EXPECT_TRUE(adapter.backend == BackendKind::D3D12);
        CG_EXPECT_EQ(adapter.apiVersion, u32{1200});

        // The name is null-terminated inside its fixed buffer, which is the one
        // property a caller logs and a formatter prints.
        CG_EXPECT_TRUE(adapter.name[kAdapterNameBytes - 1] == '\0');
        CG_EXPECT_TRUE(adapter.name[0] != '\0');
        CG_EXPECT_FALSE(cyber::test::CStringEquals(adapter.name, ""));
    }

    // The last entry is WARP: it is the adapter CI can always rely on, and
    // putting it last means "first adapter" is a real GPU on a real machine.
    CG_EXPECT_EQ(adapters[count - 1].vendorId, kMicrosoftVendorId);

    // A hardware adapter before it, when the machine has one, carries a
    // non-Microsoft vendor id and reports memory.
    if (count > 1)
    {
        CG_EXPECT_NE(adapters[0].vendorId, kMicrosoftVendorId);
        CG_EXPECT_TRUE(adapters[0].dedicatedVideoMemoryBytes > 0);
    }
}

CG_TEST(d3d12_backend_honours_a_short_span)
{
    const Backend* backend = D3D12Backend();
    CG_REQUIRE(backend != nullptr);

    const u32 total = CountAdapters(*backend);
    CG_REQUIRE(total >= 1);

    // One entry of room: the call reports the full count but writes only what
    // fits, so a caller can size its buffer from the first answer.
    AdapterInfo single{};
    single.vendorId = 0xDEADBEEF;
    const u32 reported = backend->enumerateAdapters(&single, 1);
    CG_EXPECT_EQ(reported, total);
    CG_EXPECT_NE(single.vendorId, u32{0xDEADBEEF});
    CG_EXPECT_TRUE(single.name[0] != '\0');

    // Zero capacity writes nothing and still reports the count.
    AdapterInfo untouched{};
    untouched.vendorId = 0xDEADBEEF;
    CG_EXPECT_EQ(backend->enumerateAdapters(&untouched, 0), total);
    CG_EXPECT_EQ(untouched.vendorId, u32{0xDEADBEEF});
}

CG_TEST(d3d12_backend_is_the_same_object_every_call)
{
    const Backend* first = D3D12Backend();
    const Backend* second = D3D12Backend();
    CG_REQUIRE(first != nullptr);
    CG_EXPECT_TRUE(first == second);
}

CG_TEST(d3d12_backend_reports_no_device_until_one_is_implemented)
{
    // This increment ships adapter enumeration only. Creating a device has to
    // fail with a reported error rather than a crash or a half-initialized
    // device, and that contract is what this case pins: when the device lands,
    // this case is replaced by the conformance suite, not deleted.
    const Backend* backend = D3D12Backend();
    CG_REQUIRE(backend != nullptr);
    CG_EXPECT_TRUE(backend->interface == nullptr);
    CG_EXPECT_TRUE(backend->createDevice == nullptr);

    HeapAllocator heap;
    DeviceDesc desc{};
    desc.backend = backend;
    const auto created = Device::Create(desc, heap);
    CG_EXPECT_FALSE(created);
    if (!created)
    {
        // `InvalidArgument`, because the table it was handed is incomplete: the
        // device layer rejects that before any backend code runs.
        CG_EXPECT_EQ(created.Error().code, cyber::core::ErrorCode::InvalidArgument);
    }
    CG_EXPECT_EQ(heap.Stats().used, u64{0});
}
