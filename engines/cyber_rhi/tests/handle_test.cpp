#include <cyber/rhi/handle.h>
#include <cyber/test/test.h>

#include <type_traits>

using namespace cyber::rhi;
using cyber::core::u32;
using cyber::core::u64;
using cyber::memory::Handle;

namespace
{

template <typename Tag>
Handle<Tag> MakeHandle(u32 index, u32 generation) noexcept
{
    Handle<Tag> handle;
    handle.index = index;
    handle.generation = generation;
    return handle;
}

}  // namespace

CG_TEST(rhi_handle_default_is_invalid)
{
    BufferHandle buffer;
    CG_EXPECT_FALSE(buffer.IsValid());
    CG_EXPECT_EQ(buffer.index, cyber::memory::kInvalidHandleIndex);
    CG_EXPECT_EQ(buffer.generation, u32{0});

    // Two default handles are equal, which is what makes a default member a
    // usable "no object" value.
    TextureHandle texture;
    CG_EXPECT_EQ(PackHandle(buffer), PackHandle(texture));

    buffer.Reset();
    CG_EXPECT_FALSE(buffer.IsValid());

    // A handle whose index is valid but whose generation is zero is still a real
    // handle: generation zero is what a default-constructed one has, and the
    // object table never issues it.
    const BufferHandle issued = MakeHandle<BufferTag>(0, 0);
    CG_EXPECT_TRUE(issued.IsValid());
}

CG_TEST(rhi_handle_equality_uses_both_fields)
{
    const BufferHandle first = MakeHandle<BufferTag>(3, 7);
    const BufferHandle same = MakeHandle<BufferTag>(3, 7);
    const BufferHandle otherGeneration = MakeHandle<BufferTag>(3, 8);
    const BufferHandle otherIndex = MakeHandle<BufferTag>(4, 7);

    CG_EXPECT_EQ(first, same);
    CG_EXPECT_NE(first, otherGeneration);
    CG_EXPECT_NE(first, otherIndex);

    // Equal index with a different generation is the stale-handle case: the slot
    // was reused, so the handles must not compare equal.
    CG_EXPECT_FALSE(otherGeneration == first);
}

CG_TEST(rhi_handle_pack_round_trip)
{
    const FenceHandle handle = MakeHandle<FenceTag>(0x12345678U, 0x9ABCDEF0U);
    const u64 packed = PackHandle(handle);
    CG_EXPECT_EQ((packed >> 32) & 0xFFFFFFFFULL, u64{0x12345678ULL});
    CG_EXPECT_EQ(packed & 0xFFFFFFFFULL, u64{0x9ABCDEF0ULL});

    const FenceHandle restored = UnpackHandle<FenceTag>(packed);
    CG_EXPECT_EQ(restored, handle);

    // The invalid handle round-trips too, so a log line for "no object" is
    // unambiguous.
    const FenceHandle none;
    CG_EXPECT_EQ(UnpackHandle<FenceTag>(PackHandle(none)), none);

    // Extreme values survive: the packing must not lose the top bit of either
    // field.
    const FenceHandle extremes = MakeHandle<FenceTag>(0xFFFFFFFFU, 0xFFFFFFFFU);
    CG_EXPECT_EQ(UnpackHandle<FenceTag>(PackHandle(extremes)), extremes);
}

CG_TEST(rhi_handle_tags_are_distinct_types)
{
    // The compiler is the test here: these lines must not compile.
    // BufferHandle -> TextureHandle and friends are distinct types with the same
    // layout, which is what catches a resource bound at the wrong slot.
    static_assert(!std::is_convertible_v<BufferHandle, TextureHandle>);
    static_assert(!std::is_convertible_v<TextureHandle, TextureViewHandle>);
    static_assert(!std::is_convertible_v<SamplerHandle, ShaderHandle>);
    static_assert(!std::is_convertible_v<FenceHandle, SwapChainHandle>);
    static_assert(std::is_same_v<QueueHandle, cyber::memory::Handle<QueueTag>>);
    static_assert(std::is_trivially_copyable_v<CommandListHandle>);
    static_assert(sizeof(BindGroupHandle) == 8);
}
