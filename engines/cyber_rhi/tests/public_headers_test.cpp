#include <cyber/rhi/rhi.h>
#include <cyber/test/test.h>

#include <type_traits>

using namespace cyber::rhi;

CG_TEST(rhi_public_headers)
{
    // Every header under include/cyber/rhi/ compiles on its own; this test exists
    // so a missing include in one of them is a build failure rather than a
    // surprise in a consumer.
    static_assert(!std::is_copy_constructible_v<Device>);
    static_assert(!std::is_copy_assignable_v<Device>);
    static_assert(std::is_move_constructible_v<Device>);
    static_assert(std::is_move_assignable_v<Device>);
    static_assert(std::is_trivially_copyable_v<Capabilities>);
    static_assert(std::is_trivially_copyable_v<AdapterInfo>);
    static_assert(std::is_trivially_copyable_v<Features>);
    static_assert(std::is_trivially_copyable_v<BufferHandle>);
    static_assert(sizeof(BufferHandle) == 8);
    static_assert(sizeof(TextureHandle) == 8);

    // A default device owns nothing and reports so.
    Device device;
    CG_EXPECT_FALSE(device.IsValid());
    CG_EXPECT_STREQ(device.BackendName(), "none");
    CG_EXPECT_FALSE(device.Queue(QueueKind::Graphics).IsValid());
    CG_EXPECT_EQ(device.Capabilities().limits.maxTexture2D, u32{0});
    CG_EXPECT_FALSE(device.Capabilities().features.Has(Feature::Compute));
    CG_EXPECT_FALSE(device.WaitIdle());

    // The null backend is always available and always the same object.
    const Backend* backend = NullBackend();
    CG_REQUIRE(backend != nullptr);
    CG_EXPECT_EQ(backend, NullBackend());
    CG_EXPECT_EQ(backend->kind, BackendKind::Null);
    CG_EXPECT_STREQ(backend->name, "null");
    CG_EXPECT_TRUE(backend->interface != nullptr);
    CG_EXPECT_TRUE(backend->createDevice != nullptr);
    CG_EXPECT_TRUE(backend->destroyDevice != nullptr);
    CG_EXPECT_TRUE(backend->enumerateAdapters != nullptr);
}
