#include <cyber/assets/asset.h>
#include <cyber/assets/cooked.h>
#include <cyber/assets/gltf.h>
#include <cyber/assets/texture.h>
#include <cyber/memory/heap_allocator.h>
#include <cyber/platform/filesystem.h>
#include <cyber/test/test.h>

#include <cstring>
#include <string>

namespace
{

using cyber::assets::AssetIdFromPath;
using cyber::assets::AssetImportSettings;
using cyber::assets::AssetKind;
using cyber::assets::CookedAsset;
using cyber::assets::CookedFile;
using cyber::assets::CookedHeader;
using cyber::assets::CookMaterial;
using cyber::assets::CookMesh;
using cyber::assets::CookScene;
using cyber::assets::CookTexture;
using cyber::assets::ImportedScene;
using cyber::assets::ImportGltfFile;
using cyber::assets::kCookedFormatVersion;
using cyber::assets::kCookedMagic;
using cyber::assets::ParseDds;
using cyber::assets::TextureFormat;
using cyber::core::Byte;
using cyber::core::ErrorCode;
using cyber::core::f32;
using cyber::core::u32;
using cyber::core::u64;
using cyber::core::usize;
using cyber::memory::DynamicArray;
using cyber::memory::HeapAllocator;
using cyber::memory::Span;
using cyber::memory::StringView;

[[nodiscard]] std::string FixturePath(const char* name)
{
    return std::string(CYBER_ASSET_FIXTURES) + "/" + name;
}

[[nodiscard]] Span<const Byte> AsBytes(const DynamicArray<Byte>& bytes) noexcept
{
    return Span<const Byte>(bytes.Data(), bytes.Size());
}

[[nodiscard]] u32 ReadLe32(const Byte* data) noexcept
{
    return static_cast<u32>(std::to_integer<cyber::core::u8>(data[0])) |
           (static_cast<u32>(std::to_integer<cyber::core::u8>(data[1])) << 8u) |
           (static_cast<u32>(std::to_integer<cyber::core::u8>(data[2])) << 16u) |
           (static_cast<u32>(std::to_integer<cyber::core::u8>(data[3])) << 24u);
}

CG_TEST(cooked_mesh_round_trips_through_the_container)
{
    HeapAllocator allocator;
    auto scene = ImportGltfFile(FixturePath("two_quads.gltf").c_str(), allocator);
    CG_REQUIRE(scene.IsOk());

    const ImportedScene& imported = scene.Value();
    CG_REQUIRE(imported.meshes.Size() == 1);
    const cyber::assets::MeshAsset& mesh = imported.meshes[0];
    const cyber::assets::AssetId id = AssetIdFromPath(StringView("meshes/quad.cymesh"));

    auto cooked = CookMesh(mesh, id, 0x1122334455667788ull, AssetImportSettings{}, allocator);
    CG_REQUIRE(cooked.IsOk());
    const DynamicArray<Byte>& bytes = cooked.Value();
    CG_REQUIRE(bytes.Size() > sizeof(CookedHeader));

    // Header fields are readable without trusting the struct layout.
    CG_EXPECT_EQ(ReadLe32(bytes.Data()), kCookedMagic);
    CG_EXPECT_EQ(static_cast<int>(bytes[4]) | (static_cast<int>(bytes[5]) << 8),
                 static_cast<int>(kCookedFormatVersion));

    auto parsed = CookedAsset::FromBytes(AsBytes(bytes));
    CG_REQUIRE(parsed.IsOk());
    const CookedAsset& asset = parsed.Value();
    CG_EXPECT_EQ(static_cast<int>(asset.Kind()), static_cast<int>(AssetKind::Mesh));
    CG_EXPECT_EQ(asset.Id(), id);
    CG_EXPECT_EQ(asset.SourceHash(), static_cast<u64>(0x1122334455667788ull));
    CG_EXPECT_EQ(asset.Header().headerBytes, 64u);

    const cyber::assets::CookedMeshView& view = asset.AsMesh();
    CG_EXPECT_EQ(view.vertices.Size(), mesh.vertices.Size());
    CG_EXPECT_EQ(view.indices.Size(), mesh.indices.Size());
    CG_EXPECT_EQ(view.submeshes.Size(), mesh.submeshes.Size());
    CG_EXPECT_NEAR(view.info.boundsMinimum.x, mesh.bounds.Min().x, 1e-5f);
    CG_EXPECT_NEAR(view.info.boundsMaximum.z, mesh.bounds.Max().z, 1e-5f);

    // Zero copy: the vertex span points into the cooked bytes, not a copy.
    const Byte* payload = bytes.Data() + view.info.nameOffset;
    CG_EXPECT_TRUE(reinterpret_cast<const Byte*>(view.vertices.Data()) >= bytes.Data());
    CG_EXPECT_TRUE(reinterpret_cast<const Byte*>(view.vertices.Data()) +
                       view.vertices.Size() * sizeof(cyber::assets::MeshVertex) <=
                   bytes.Data() + bytes.Size());
    CG_EXPECT_TRUE(payload >= bytes.Data() && payload < bytes.Data() + bytes.Size());
    CG_EXPECT_NEAR(view.vertices[5].position.z, mesh.vertices[5].position.z, 1e-6f);
    CG_EXPECT_EQ(view.submeshes[1].materialIndex, mesh.submeshes[1].materialIndex);
    CG_EXPECT_EQ(
        StringView(reinterpret_cast<const char*>(view.strings.Data() + view.info.nameOffset),
                   view.info.nameLength),
        StringView("quad"));
}

CG_TEST(cooked_mesh_rejects_a_corrupt_checksum)
{
    HeapAllocator allocator;
    auto scene = ImportGltfFile(FixturePath("two_quads.gltf").c_str(), allocator);
    CG_REQUIRE(scene.IsOk());
    auto cooked = CookMesh(scene.Value().meshes[0], AssetIdFromPath(StringView("quad")), 1,
                           AssetImportSettings{}, allocator);
    CG_REQUIRE(cooked.IsOk());

    DynamicArray<Byte>& bytes = cooked.Value();
    CG_REQUIRE(bytes.Size() > 128);
    bytes[100] = static_cast<Byte>(std::to_integer<cyber::core::u8>(bytes[100]) ^ 0x5au);

    auto parsed = CookedAsset::FromBytes(AsBytes(bytes));
    CG_REQUIRE(parsed.IsError());
    CG_EXPECT_EQ(parsed.Error().code, ErrorCode::ParseError);
    CG_EXPECT_STREQ(parsed.Error().message, "cooked payload checksum does not match");
}

CG_TEST(cooked_rejects_truncated_and_foreign_files)
{
    HeapAllocator allocator;
    auto scene = ImportGltfFile(FixturePath("two_quads.gltf").c_str(), allocator);
    CG_REQUIRE(scene.IsOk());
    auto cooked = CookMesh(scene.Value().meshes[0], AssetIdFromPath(StringView("quad")), 1,
                           AssetImportSettings{}, allocator);
    CG_REQUIRE(cooked.IsOk());
    const DynamicArray<Byte>& bytes = cooked.Value();

    auto truncated = CookedAsset::FromBytes(Span<const Byte>(bytes.Data(), 32));
    CG_REQUIRE(truncated.IsError());
    CG_EXPECT_EQ(truncated.Error().code, ErrorCode::ParseError);
    CG_EXPECT_STREQ(truncated.Error().message, "cooked file is smaller than its header");

    DynamicArray<Byte> foreign(allocator);
    CG_REQUIRE(foreign.TryResize(128));
    std::memset(foreign.Data(), 0x7f, foreign.Size());
    auto rejected = CookedAsset::FromBytes(AsBytes(foreign));
    CG_REQUIRE(rejected.IsError());
    CG_EXPECT_STREQ(rejected.Error().message, "cooked magic does not match");
}

CG_TEST(cooked_texture_round_trips)
{
    HeapAllocator allocator;
    auto file = cyber::platform::ReadFile(FixturePath("texture_bc1.dds").c_str(), allocator);
    CG_REQUIRE(file.IsOk());
    auto texture = ParseDds(AsBytes(file.Value()), allocator);
    CG_REQUIRE(texture.IsOk());
    CG_REQUIRE(texture.Value().name.TryAssign(StringView("textures/checker")));

    auto cooked = CookTexture(texture.Value(), AssetIdFromPath(StringView("checker.cytex")), 7,
                              AssetImportSettings{}, allocator);
    CG_REQUIRE(cooked.IsOk());

    auto parsed = CookedAsset::FromBytes(AsBytes(cooked.Value()));
    CG_REQUIRE(parsed.IsOk());
    const auto& view = parsed.Value().AsTexture();
    CG_EXPECT_EQ(static_cast<int>(static_cast<TextureFormat>(view.info.format)),
                 static_cast<int>(TextureFormat::Bc1Rgba));
    CG_EXPECT_EQ(view.info.width, texture.Value().width);
    CG_EXPECT_EQ(view.info.height, texture.Value().height);
    CG_EXPECT_EQ(view.info.mipCount, texture.Value().mipCount);
    CG_EXPECT_EQ(view.info.cubemap, 0u);
    CG_EXPECT_EQ(view.levels.Size(), texture.Value().levels.Size());
    CG_EXPECT_EQ(view.data.Size(), texture.Value().data.Size());
    CG_EXPECT_TRUE(view.data.Data() >= cooked.Value().Data());
    CG_EXPECT_EQ(
        StringView(reinterpret_cast<const char*>(view.strings.Data() + view.info.nameOffset),
                   view.info.nameLength),
        StringView("textures/checker"));
}

CG_TEST(cooked_texture_file_loads_from_disk)
{
    HeapAllocator allocator;
    auto file = cyber::platform::ReadFile(FixturePath("texture_bc1.dds").c_str(), allocator);
    CG_REQUIRE(file.IsOk());
    auto texture = ParseDds(AsBytes(file.Value()), allocator);
    CG_REQUIRE(texture.IsOk());

    auto cooked = CookTexture(texture.Value(), AssetIdFromPath(StringView("disk-texture")), 7,
                              AssetImportSettings{}, allocator);
    CG_REQUIRE(cooked.IsOk());

    constexpr const char* path = "cyber_assets_cooked_texture_load.cytex";
    CG_REQUIRE(cyber::platform::WriteFile(path, AsBytes(cooked.Value())).IsOk());
    auto loaded = CookedFile::Load(path, allocator);
    const auto removed = cyber::platform::DeleteFile(path);
    CG_REQUIRE(removed.IsOk());
    CG_REQUIRE(loaded.IsOk());
    CG_EXPECT_EQ(loaded.Value().Asset().Kind(), AssetKind::Texture);
    CG_EXPECT_EQ(loaded.Value().Asset().AsTexture().data.Size(), texture.Value().data.Size());
}

CG_TEST(cooked_material_preserves_every_slot)
{
    HeapAllocator allocator;
    auto scene = ImportGltfFile(FixturePath("two_quads.gltf").c_str(), allocator);
    CG_REQUIRE(scene.IsOk());
    const cyber::assets::MaterialAsset& material = scene.Value().materials[0];

    auto cooked = CookMaterial(material, AssetIdFromPath(StringView("checker.cymat")), 9,
                               AssetImportSettings{}, allocator);
    CG_REQUIRE(cooked.IsOk());

    auto parsed = CookedAsset::FromBytes(AsBytes(cooked.Value()));
    CG_REQUIRE(parsed.IsOk());
    const auto& view = parsed.Value().AsMaterial();
    CG_EXPECT_NEAR(view.record.baseColorFactor.w, 1.0f, 1e-6f);
    CG_EXPECT_NEAR(view.record.metallicFactor, 0.0f, 1e-6f);
    CG_EXPECT_NEAR(view.record.roughnessFactor, 0.8f, 1e-6f);
    CG_EXPECT_EQ(view.record.doubleSided, 1u);
    CG_REQUIRE(view.textures.Size() == cyber::assets::kCookedTextureSlots);
    CG_EXPECT_EQ(view.textures[0].image, material.baseColorTexture.image);
    CG_EXPECT_EQ(view.textures[4].image, material.emissiveTexture.image);
    CG_EXPECT_EQ(
        StringView(reinterpret_cast<const char*>(view.strings.Data() + view.record.nameOffset),
                   view.record.nameLength),
        StringView("checker"));
}

CG_TEST(cooked_scene_preserves_the_hierarchy)
{
    HeapAllocator allocator;
    auto scene = ImportGltfFile(FixturePath("two_quads.gltf").c_str(), allocator);
    CG_REQUIRE(scene.IsOk());
    const ImportedScene& imported = scene.Value();

    auto cooked = CookScene(imported, AssetIdFromPath(StringView("two_quads.cyscene")), 11,
                            AssetImportSettings{}, allocator, "two_quads.gltf");
    CG_REQUIRE(cooked.IsOk());

    auto parsed = CookedAsset::FromBytes(AsBytes(cooked.Value()));
    CG_REQUIRE(parsed.IsOk());
    const auto& view = parsed.Value().AsScene();
    CG_EXPECT_EQ(view.info.nodeCount, static_cast<cyber::core::u32>(imported.nodes.Size()));
    CG_EXPECT_EQ(view.info.instanceCount, static_cast<cyber::core::u32>(imported.instances.Size()));
    CG_REQUIRE(view.meshes.Size() == 1);
    CG_EXPECT_EQ(view.meshes[0], AssetIdFromPath(StringView("two_quads.gltf#mesh0")));
    CG_EXPECT_EQ(view.nodes[0].mesh, view.meshes[0]);
    CG_EXPECT_TRUE((view.nodes[0].flags & cyber::assets::kCookedNodeHasMesh) != 0);
    CG_EXPECT_NEAR(view.instances[0].transform[3].x, -1.5f, 1e-5f);
    CG_EXPECT_EQ(
        StringView(reinterpret_cast<const char*>(view.strings.Data() + view.nodes[1].nameOffset),
                   view.nodes[1].nameLength),
        StringView("right"));

    // A submesh only stores a material index, so the scene must ship the table
    // that index addresses.
    CG_REQUIRE(view.info.materialCount == imported.materials.Size());
    CG_REQUIRE(view.materials.Size() == imported.materials.Size());
    CG_REQUIRE(view.materialTextures.Size() ==
               view.materials.Size() * cyber::assets::kCookedTextureSlots);
    CG_EXPECT_NEAR(view.materials[0].metallicFactor, 0.0f, 1e-6f);
    CG_EXPECT_NEAR(view.materials[0].roughnessFactor, 0.8f, 1e-6f);
    CG_EXPECT_EQ(view.materialTextures[0].image, imported.materials[0].baseColorTexture.image);
    CG_EXPECT_EQ(StringView(reinterpret_cast<const char*>(view.strings.Data() +
                                                          view.materials[0].nameOffset),
                            view.materials[0].nameLength),
                 StringView("checker"));
}

CG_TEST(cooked_file_loads_from_disk_and_owns_its_bytes)
{
    HeapAllocator allocator;
    auto scene = ImportGltfFile(FixturePath("two_quads.gltf").c_str(), allocator);
    CG_REQUIRE(scene.IsOk());
    auto cooked = CookMesh(scene.Value().meshes[0], AssetIdFromPath(StringView("disk")), 3,
                           AssetImportSettings{}, allocator);
    CG_REQUIRE(cooked.IsOk());

    const char* path = "cyber_assets_cooked_test.bin";
    CG_REQUIRE(cyber::platform::WriteFile(path, AsBytes(cooked.Value())).IsOk());

    auto loaded = CookedFile::Load(path, allocator);
    (void)cyber::platform::DeleteFile(path);
    CG_REQUIRE(loaded.IsOk());
    CG_EXPECT_TRUE(loaded.Value().Asset().IsValid());
    CG_EXPECT_EQ(loaded.Value().Asset().AsMesh().vertices.Size(),
                 scene.Value().meshes[0].vertices.Size());
    CG_EXPECT_EQ(loaded.Value().Asset().AsMesh().indices[2], scene.Value().meshes[0].indices[2]);
}

}  // namespace
