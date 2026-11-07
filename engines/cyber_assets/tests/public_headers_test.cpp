#include <cyber/assets/asset.h>
#include <cyber/assets/asset_database.h>
#include <cyber/assets/asset_library.h>
#include <cyber/assets/cooked.h>
#include <cyber/assets/gltf.h>
#include <cyber/assets/image.h>
#include <cyber/assets/json.h>
#include <cyber/assets/material.h>
#include <cyber/assets/mesh_data.h>
#include <cyber/assets/obj.h>
#include <cyber/assets/texture.h>
#include <cyber/test/test.h>

#include <type_traits>

namespace
{

/// Compiles every public header on its own and checks the documented
/// vocabulary is usable without any private include.
CG_TEST(assets_public_headers_expose_the_contract)
{
    static_assert(std::is_trivially_copyable_v<cyber::assets::MeshVertex>);
    static_assert(std::is_trivially_copyable_v<cyber::assets::ImportError>);
    static_assert(std::is_trivially_copyable_v<cyber::assets::AssetImportSettings>);
    static_assert(std::is_trivially_copyable_v<cyber::assets::CookedSectionEntry>);

    CG_EXPECT_STREQ(cyber::assets::AssetKindName(cyber::assets::AssetKind::Mesh), "mesh");
    CG_EXPECT_STREQ(cyber::assets::SourceFormatName(cyber::assets::SourceFormat::Gltf), "gltf");
    CG_EXPECT_TRUE(cyber::assets::IsMeshFormat(cyber::assets::SourceFormat::Obj));
    CG_EXPECT_TRUE(cyber::assets::IsTextureFormat(cyber::assets::SourceFormat::Png));
    CG_EXPECT_FALSE(cyber::assets::IsTextureFormat(cyber::assets::SourceFormat::Obj));
    CG_EXPECT_EQ(cyber::assets::kInvalidAssetId, static_cast<cyber::core::u64>(0));

    // Manifest round trips go through these two functions, so a rename in one
    // direction would silently turn into an unknown format.
    for (const char* name : {"gltf", "glb", "obj", "png", "jpeg", "dds", "ktx2", "cybermat"})
    {
        const cyber::assets::SourceFormat format =
            cyber::assets::SourceFormatFromName(cyber::memory::StringView(name));
        CG_EXPECT_TRUE(format != cyber::assets::SourceFormat::Unknown);
        CG_EXPECT_STREQ(cyber::assets::SourceFormatName(format), name);
    }
    CG_EXPECT_TRUE(cyber::assets::SourceFormatFromName(cyber::memory::StringView("webp")) ==
                   cyber::assets::SourceFormat::Unknown);
}

}  // namespace
