#include <cyber/shaders/cache.h>
#include <cyber/shaders/compiler.h>
#include <cyber/shaders/library.h>
#include <cyber/shaders/permutation.h>
#include <cyber/shaders/reflection.h>
#include <cyber/test/test.h>

#include <type_traits>

using namespace cyber::shaders;

CG_TEST(shaders_public_headers)
{
    static_assert(std::is_trivially_copyable_v<Reflection>);
    static_assert(std::is_trivially_copyable_v<PermutationKey>);
    static_assert(std::is_trivially_copyable_v<ShaderCacheKey>);
    static_assert(!std::is_copy_constructible_v<ShaderLibrary>);
    static_assert(std::is_move_constructible_v<ShaderLibrary>);
    static_assert(!std::is_copy_constructible_v<ShaderCache>);
    static_assert(std::is_move_constructible_v<ShaderCache>);

    CG_EXPECT_EQ(ValueTypeSize(ValueType::Float4x4), u32{64});
    CG_EXPECT_EQ(ValueTypeSize(ValueType::Float3), u32{12});
    CG_EXPECT_STREQ(StageName(Stage::Compute), "compute");
    CG_EXPECT_STREQ(ValueTypeName(ValueType::Uint3), "uint3");
    CG_EXPECT_STREQ(ResourceKindName(ResourceKind::StorageTexture), "storage_texture");
    CG_EXPECT_EQ(PermutationKey{}.hash, kPermutationHashSeed);

    ShaderLibrary empty;
    CG_EXPECT_EQ(empty.ShaderCount(), u32{0});
    CG_EXPECT_TRUE(empty.Find(1, kPermutationHashSeed) == nullptr);
    ShaderEvent event{};
    CG_EXPECT_FALSE(empty.PollEvent(event));
}
