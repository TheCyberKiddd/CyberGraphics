#include <cyber/assets/asset.h>
#include <cyber/assets/asset_library.h>
#include <cyber/assets/cooked.h>
#include <cyber/assets/gltf.h>
#include <cyber/core/types.h>
#include <cyber/memory/dynamic_array.h>
#include <cyber/memory/heap_allocator.h>
#include <cyber/platform/filesystem.h>
#include <cyber/test/test.h>

#include <string>

namespace
{

using cyber::assets::AssetIdFromPath;
using cyber::assets::AssetImportSettings;
using cyber::assets::AssetLibrary;
using cyber::assets::CookedAsset;
using cyber::assets::CookMesh;
using cyber::assets::ImportGltfFile;
using cyber::core::Byte;
using cyber::core::ErrorCode;
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

/// Cooks the fixture mesh with a chosen source hash so a reload is
/// distinguishable by the header alone. Returns false instead of asserting,
/// because CG_REQUIRE cannot be used in a value-returning helper.
[[nodiscard]] bool CookedPayload(HeapAllocator& allocator, u64 sourceHash, DynamicArray<Byte>& out)
{
    auto scene = ImportGltfFile(FixturePath("two_quads.gltf").c_str(), allocator);
    if (scene.IsError())
    {
        return false;
    }
    auto cooked = CookMesh(scene.Value().meshes[0], AssetIdFromPath(StringView("quad.cymesh")),
                           sourceHash, AssetImportSettings{}, allocator);
    if (cooked.IsError())
    {
        return false;
    }
    out = std::move(cooked.Value());
    return true;
}

[[nodiscard]] bool WriteBytes(const char* path, const DynamicArray<Byte>& bytes) noexcept
{
    return cyber::platform::WriteFile(path, Span<const Byte>(bytes.Data(), bytes.Size())).IsOk();
}

CG_TEST(asset_library_mounts_and_finds_cooked_files)
{
    HeapAllocator allocator;
    DynamicArray<Byte> payload(allocator);
    CG_REQUIRE(CookedPayload(allocator, 41, payload));
    const char* path = "cyber_assets_library_mount.cooked";
    CG_REQUIRE(WriteBytes(path, payload));

    AssetLibrary library(allocator);
    const auto mounted = library.Mount(StringView("meshes/quad.obj"), path);
    (void)cyber::platform::DeleteFile(path);
    CG_REQUIRE(mounted.IsOk());
    CG_EXPECT_EQ(mounted.Value(), 0u);
    CG_EXPECT_EQ(library.Count(), static_cast<usize>(1));
    CG_EXPECT_EQ(library.Generation(), static_cast<u64>(0));

    const CookedAsset* asset = library.Find(StringView("meshes/quad.obj"));
    CG_REQUIRE(asset != nullptr);
    CG_EXPECT_TRUE(asset->IsValid());
    CG_EXPECT_EQ(asset->SourceHash(), static_cast<u64>(41));
    CG_EXPECT_EQ(asset->AsMesh().vertices.Size(), static_cast<usize>(8));

    CG_EXPECT_TRUE(library.At(0) == asset);
    CG_EXPECT_TRUE(library.At(1) == nullptr);
    CG_EXPECT_TRUE(library.PathAt(0).Equals(StringView("meshes/quad.obj")));
    CG_EXPECT_TRUE(library.PathAt(1).Empty());
    CG_EXPECT_TRUE(library.Find(StringView("meshes/other.obj")) == nullptr);

    // Case and separators identify the same asset; a second mount is refused.
    const auto duplicate = library.Mount(StringView("MESHES/quad.OBJ"), path);
    CG_REQUIRE(duplicate.IsError());
    CG_EXPECT_EQ(static_cast<int>(duplicate.Error().code),
                 static_cast<int>(ErrorCode::AlreadyExists));

    AssetLibrary empty(allocator);
    CG_EXPECT_TRUE(empty.Mount(StringView("a.obj"), "definitely_missing.cooked").IsError());
    CG_EXPECT_TRUE(empty.Mount(StringView(), path).IsError());
    CG_EXPECT_TRUE(empty.Mount(StringView("a.obj"), "").IsError());
    CG_EXPECT_EQ(empty.Count(), static_cast<usize>(0));
}

CG_TEST(asset_library_reloads_after_the_file_changes)
{
    HeapAllocator allocator;
    const char* path = "cyber_assets_library_hot.cooked";
    DynamicArray<Byte> first(allocator);
    CG_REQUIRE(CookedPayload(allocator, 1, first));
    CG_REQUIRE(WriteBytes(path, first));

    AssetLibrary library(allocator);
    CG_REQUIRE(library.Mount(StringView("quad.obj"), path).IsOk());
    CG_REQUIRE(library.Find(StringView("quad.obj"))->SourceHash() == 1);

    // A delete followed by a fresh cook is what a rebuild produces: the watch
    // reports both steps and only the successful load counts as a reload.
    CG_REQUIRE(cyber::platform::DeleteFile(path).IsOk());
    const AssetLibrary::ReloadStats removed = library.Poll();
    CG_EXPECT_EQ(removed.failed, 1u);
    CG_EXPECT_EQ(removed.reloaded, 0u);
    CG_EXPECT_EQ(library.Generation(), static_cast<u64>(0));
    CG_EXPECT_EQ(library.Find(StringView("quad.obj"))->SourceHash(), static_cast<u64>(1));

    DynamicArray<Byte> second(allocator);
    CG_REQUIRE(CookedPayload(allocator, 2, second));
    CG_REQUIRE(WriteBytes(path, second));
    const AssetLibrary::ReloadStats rebuilt = library.Poll();
    CG_EXPECT_EQ(rebuilt.reloaded, 1u);
    CG_EXPECT_EQ(rebuilt.failed, 0u);
    CG_EXPECT_EQ(library.Generation(), static_cast<u64>(1));
    CG_EXPECT_EQ(library.Find(StringView("quad.obj"))->SourceHash(), static_cast<u64>(2));

    // A manual reload of an unchanged file is a successful, observable reload.
    CG_REQUIRE(library.Reload(StringView("QUAD.obj")).IsOk());
    CG_EXPECT_EQ(library.Generation(), static_cast<u64>(2));
    CG_EXPECT_TRUE(library.Reload(StringView("missing.obj")).IsError());
    (void)cyber::platform::DeleteFile(path);
}

CG_TEST(asset_library_keeps_the_last_good_payload)
{
    HeapAllocator allocator;
    const char* path = "cyber_assets_library_corrupt.cooked";
    DynamicArray<Byte> payload(allocator);
    CG_REQUIRE(CookedPayload(allocator, 7, payload));
    CG_REQUIRE(WriteBytes(path, payload));

    AssetLibrary library(allocator);
    CG_REQUIRE(library.Mount(StringView("quad.obj"), path).IsOk());

    const char garbage[] = "not a cooked file";
    CG_REQUIRE(
        cyber::platform::WriteFile(
            path, Span<const Byte>(reinterpret_cast<const Byte*>(garbage), sizeof(garbage) - 1))
            .IsOk());
    const auto reload = library.Reload(StringView("quad.obj"));
    (void)cyber::platform::DeleteFile(path);
    CG_REQUIRE(reload.IsError());
    CG_EXPECT_EQ(static_cast<int>(reload.Error().code), static_cast<int>(ErrorCode::ParseError));
    CG_EXPECT_EQ(library.Generation(), static_cast<u64>(0));

    const CookedAsset* asset = library.Find(StringView("quad.obj"));
    CG_REQUIRE(asset != nullptr);
    CG_EXPECT_EQ(asset->SourceHash(), static_cast<u64>(7));
    CG_EXPECT_EQ(asset->AsMesh().indices.Size(), static_cast<usize>(12));
}

CG_TEST(asset_library_clear_drops_mounted_assets)
{
    HeapAllocator allocator;
    const char* path = "cyber_assets_library_clear.cooked";
    DynamicArray<Byte> payload(allocator);
    CG_REQUIRE(CookedPayload(allocator, 3, payload));
    CG_REQUIRE(WriteBytes(path, payload));

    AssetLibrary library(allocator);
    CG_REQUIRE(library.Mount(StringView("a.obj"), path).IsOk());
    CG_REQUIRE(library.Mount(StringView("b.obj"), path).IsOk());
    CG_EXPECT_EQ(library.Count(), static_cast<usize>(2));

    library.Clear();
    (void)cyber::platform::DeleteFile(path);
    CG_EXPECT_EQ(library.Count(), static_cast<usize>(0));
    CG_EXPECT_TRUE(library.Find(StringView("a.obj")) == nullptr);
    const AssetLibrary::ReloadStats stats = library.Poll();
    CG_EXPECT_EQ(stats.reloaded, 0u);
    CG_EXPECT_EQ(stats.failed, 0u);
}

}  // namespace
