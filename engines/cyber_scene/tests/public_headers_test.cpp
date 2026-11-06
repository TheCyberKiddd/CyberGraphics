#include <cyber/scene/mesh.h>
#include <cyber/scene/scene.h>
#include <cyber/test/test.h>

#include <type_traits>

CG_TEST(scene_public_header_contract)
{
    static_assert(!std::is_copy_constructible_v<cyber::scene::Scene>);
    static_assert(std::is_move_constructible_v<cyber::scene::Scene>);
    static_assert(!std::is_copy_constructible_v<cyber::scene::SceneCommandWriter>);
    static_assert(std::is_move_constructible_v<cyber::scene::SceneCommandWriter>);
    static_assert(!std::is_copy_constructible_v<cyber::scene::VisibilityList>);
    static_assert(std::is_move_constructible_v<cyber::scene::VisibilityList>);
    static_assert(std::is_trivially_copyable_v<cyber::scene::ProxyHandle>);
    static_assert(!std::is_copy_constructible_v<cyber::scene::Mesh>);
    static_assert(std::is_move_constructible_v<cyber::scene::Mesh>);
    static_assert(!std::is_copy_constructible_v<cyber::scene::InstanceBatchList>);
    static_assert(std::is_move_constructible_v<cyber::scene::InstanceBatchList>);
    CG_EXPECT_EQ(cyber::scene::kMaximumMeshLods, cyber::core::u32{4});
    static_assert(sizeof(cyber::scene::DrawIndexedIndirectArguments) == 20);
}
