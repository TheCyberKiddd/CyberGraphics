#include <cyber/assets/asset.h>
#include <cyber/assets/asset_database.h>
#include <cyber/core/types.h>
#include <cyber/memory/dynamic_array.h>
#include <cyber/memory/heap_allocator.h>
#include <cyber/platform/filesystem.h>
#include <cyber/test/test.h>

#include <cstring>

namespace
{

using cyber::assets::AssetDatabase;
using cyber::assets::AssetId;
using cyber::assets::AssetImportSettings;
using cyber::assets::AssetRecord;
using cyber::assets::CookCacheEntry;
using cyber::assets::kAssetDatabaseVersion;
using cyber::assets::SourceFormat;
using cyber::core::Byte;
using cyber::core::ErrorCode;
using cyber::core::u32;
using cyber::core::u64;
using cyber::core::usize;
using cyber::memory::DynamicArray;
using cyber::memory::HeapAllocator;
using cyber::memory::Span;
using cyber::memory::StringView;

[[nodiscard]] bool Same(StringView text, const char* expected) noexcept
{
    return text.Equals(StringView(expected));
}

[[nodiscard]] bool WriteText(const char* path, const char* text) noexcept
{
    return cyber::platform::WriteFile(
               path, Span<const Byte>(reinterpret_cast<const Byte*>(text), std::strlen(text)))
        .IsOk();
}

[[nodiscard]] AssetId Register(AssetDatabase& database, const char* path) noexcept
{
    const auto registered = database.Register(StringView(path));
    if (registered.IsError())
    {
        // CG_REQUIRE cannot be used in a value-returning helper, so report the
        // failure and let the dependent assertions fail with a clear trail.
        ::cyber::test::Fail("asset failed to register", __FILE__, __LINE__);
        return cyber::assets::kInvalidAssetId;
    }
    return registered.Value();
}

CG_TEST(asset_database_normalizes_virtual_paths)
{
    HeapAllocator allocator;
    AssetDatabase database(allocator);

    const auto path = database.Register(StringView("./textures\\\\Albedo.png"));
    CG_REQUIRE(path.IsOk());
    const AssetRecord* record = database.Find(path.Value());
    CG_REQUIRE(record != nullptr);
    CG_EXPECT_TRUE(Same(record->virtualPath.View(), "textures/Albedo.png"));
    CG_EXPECT_EQ(static_cast<int>(record->format), static_cast<int>(SourceFormat::Png));
    CG_EXPECT_EQ(database.Count(), static_cast<usize>(1));

    // The same logical path, spelled differently, resolves to the existing record.
    const auto repeated = database.Register(StringView("textures//albedo.PNG"));
    CG_REQUIRE(repeated.IsOk());
    CG_EXPECT_EQ(repeated.Value(), path.Value());
    CG_EXPECT_EQ(database.Count(), static_cast<usize>(1));

    CG_EXPECT_TRUE(database.Find(StringView("TEXTURES/albedo.png")) != nullptr);
    CG_EXPECT_TRUE(database.Find(StringView("textures/missing.png")) == nullptr);

    // A path may never escape the content root.
    CG_EXPECT_TRUE(database.Register(StringView("../outside.png")).IsError());
    CG_EXPECT_TRUE(database.Register(StringView("meshes/../../outside.png")).IsError());
    CG_EXPECT_TRUE(database.Register(StringView("   ")).IsError());
    CG_EXPECT_TRUE(database.Register(StringView("notes.txt")).IsError());
    CG_EXPECT_EQ(database.Count(), static_cast<usize>(1));
}

CG_TEST(asset_database_rejects_conflicting_formats)
{
    HeapAllocator allocator;
    AssetDatabase database(allocator);

    const auto first = database.Register(StringView("scene.gltf"));
    CG_REQUIRE(first.IsOk());
    CG_EXPECT_EQ(static_cast<int>(database.Find(first.Value())->kind),
                 static_cast<int>(cyber::assets::AssetKind::Scene));

    const auto conflicting = database.Register(StringView("scene.gltf"), SourceFormat::Obj);
    CG_REQUIRE(conflicting.IsError());
    CG_EXPECT_EQ(static_cast<int>(conflicting.Error().code),
                 static_cast<int>(ErrorCode::InvalidArgument));
    CG_EXPECT_TRUE(conflicting.Error().message != nullptr);

    CG_EXPECT_TRUE(database.Register(StringView("mesh.obj"), SourceFormat::Unknown).IsError());
}

CG_TEST(asset_database_orders_dependencies_and_rejects_cycles)
{
    HeapAllocator allocator;
    AssetDatabase database(allocator);

    const AssetId scene = Register(database, "level.gltf");
    const AssetId mesh = Register(database, "meshes/quad.obj");
    const AssetId texture = Register(database, "textures/quad.png");

    CG_REQUIRE(database.AddDependency(scene, mesh).IsOk());
    CG_REQUIRE(database.AddDependency(mesh, texture).IsOk());
    // Repeating an edge is a no-op, not an error.
    CG_REQUIRE(database.AddDependency(scene, mesh).IsOk());
    CG_EXPECT_EQ(database.Find(scene)->dependencies.Size(), static_cast<usize>(1));

    CG_EXPECT_TRUE(database.DependsOn(scene, mesh));
    CG_EXPECT_TRUE(database.DependsOn(scene, texture));
    CG_EXPECT_FALSE(database.DependsOn(texture, mesh));
    CG_EXPECT_FALSE(database.DependsOn(scene, scene));

    // Direct and transitive cycles are rejected.
    CG_EXPECT_TRUE(database.AddDependency(texture, scene).IsError());
    CG_EXPECT_TRUE(database.AddDependency(mesh, mesh).IsError());
    CG_EXPECT_EQ(database.Find(mesh)->dependencies.Size(), static_cast<usize>(1));

    // A missing asset is a caller bug, not a silent skip.
    CG_EXPECT_TRUE(database.AddDependency(scene, 12345).IsError());

    DynamicArray<AssetId> order(allocator);
    CG_REQUIRE(database.CookOrder(order));
    CG_REQUIRE(order.Size() == 3);
    usize textureIndex = order.Size();
    usize meshIndex = order.Size();
    usize sceneIndex = order.Size();
    for (usize index = 0; index < order.Size(); ++index)
    {
        if (order[index] == texture)
        {
            textureIndex = index;
        }
        else if (order[index] == mesh)
        {
            meshIndex = index;
        }
        else if (order[index] == scene)
        {
            sceneIndex = index;
        }
    }
    CG_EXPECT_TRUE(textureIndex < meshIndex);
    CG_EXPECT_TRUE(meshIndex < sceneIndex);
    // Registration order is preserved between unrelated assets.
    CG_EXPECT_EQ(order[0], texture);

    // Replacing the dependency list drops the old edge.
    const AssetId otherTexture = Register(database, "textures/other.png");
    const AssetId replacement[] = {otherTexture};
    CG_REQUIRE(database.SetDependencies(mesh, Span<const AssetId>(replacement, 1)).IsOk());
    CG_EXPECT_FALSE(database.DependsOn(mesh, texture));
    CG_EXPECT_TRUE(database.DependsOn(mesh, otherTexture));
    CG_EXPECT_FALSE(database.DependsOn(scene, texture));
}

CG_TEST(asset_database_caches_by_source_and_settings)
{
    HeapAllocator allocator;
    AssetDatabase database(allocator);

    const AssetId scene = Register(database, "level.gltf");
    const AssetId mesh = Register(database, "meshes/quad.obj");
    const AssetId texture = Register(database, "textures/quad.png");
    CG_REQUIRE(database.AddDependency(scene, mesh).IsOk());
    CG_REQUIRE(database.AddDependency(mesh, texture).IsOk());

    AssetImportSettings settings{};
    CookCacheEntry entry{};
    CG_EXPECT_FALSE(database.TryGetCooked(texture, 0x1234, settings, entry));

    CG_REQUIRE(
        database.StoreCooked(texture, "cooked/textures/quad.cytex", 0x1234, settings, 7).IsOk());
    CG_REQUIRE(database.StoreCooked(mesh, "cooked/meshes/quad.cymesh", 0x1234, settings, 8).IsOk());
    CG_REQUIRE(database.StoreCooked(scene, "cooked/level.cyscene", 0x1234, settings, 9).IsOk());

    CG_REQUIRE(database.TryGetCooked(texture, 0x1234, settings, entry));
    CG_EXPECT_EQ(entry.sourceHash, static_cast<u64>(0x1234));
    CG_EXPECT_EQ(entry.checksum, static_cast<u64>(7));

    // A byte change misses; a settings change misses for the same bytes.
    CG_EXPECT_FALSE(database.TryGetCooked(texture, 0x1235, settings, entry));
    AssetImportSettings flipped = settings;
    flipped.flipTexCoordV = true;
    CG_EXPECT_FALSE(database.TryGetCooked(texture, 0x1234, flipped, entry));

    // Invalidating a dependency also invalidates every consumer, transitively.
    database.Invalidate(texture);
    CG_EXPECT_FALSE(database.Find(texture)->IsCooked());
    CG_EXPECT_FALSE(database.Find(mesh)->IsCooked());
    CG_EXPECT_FALSE(database.Find(scene)->IsCooked());

    CG_REQUIRE(
        database.StoreCooked(texture, "cooked/textures/quad.cytex", 0x1234, settings, 7).IsOk());
    database.Invalidate(12345);
    CG_EXPECT_TRUE(database.Find(texture)->IsCooked());

    CG_EXPECT_TRUE(database.StoreCooked(12345, "x", 1, settings, 1).IsError());
    CG_EXPECT_TRUE(database.StoreCooked(texture, "", 1, settings, 1).IsError());
}

CG_TEST(asset_database_manifest_round_trips)
{
    HeapAllocator allocator;
    AssetDatabase database(allocator);

    const AssetId scene = Register(database, "level.gltf");
    const AssetId mesh = Register(database, "meshes/quad.obj");
    const AssetId texture = Register(database, "textures/quad.png");
    CG_REQUIRE(database.AddDependency(scene, mesh).IsOk());
    CG_REQUIRE(database.AddDependency(mesh, texture).IsOk());

    const AssetImportSettings settings{};
    CG_REQUIRE(
        database.StoreCooked(texture, "cooked/textures/quad.cytex", 0xabc, settings, 11).IsOk());
    CG_REQUIRE(database.StoreCooked(mesh, "cooked/meshes/quad.cymesh", 0xdef, settings, 12).IsOk());
    CG_REQUIRE(database.StoreCooked(scene, "cooked/level.cyscene", 0x99, settings, 13).IsOk());

    const char* firstPath = "cyber_assets_database_test.json";
    const char* secondPath = "cyber_assets_database_test_copy.json";
    CG_REQUIRE(database.Save(firstPath).IsOk());
    CG_REQUIRE(database.Save(secondPath).IsOk());

    // Two saves of the same database are byte-identical, so manifests can be
    // diffed and cached by content hash.
    auto first = cyber::platform::ReadFile(firstPath, allocator);
    auto second = cyber::platform::ReadFile(secondPath, allocator);
    CG_REQUIRE(first.IsOk());
    CG_REQUIRE(second.IsOk());
    CG_EXPECT_EQ(first.Value().Size(), second.Value().Size());
    CG_EXPECT_TRUE(first.Value().Size() > 0);
    CG_EXPECT_TRUE(std::memcmp(first.Value().Data(), second.Value().Data(), first.Value().Size()) ==
                   0);

    AssetDatabase loaded(allocator);
    CG_REQUIRE(loaded.Load(firstPath).IsOk());
    CG_EXPECT_EQ(loaded.Count(), static_cast<usize>(3));

    const AssetRecord* textureRecord = loaded.Find(StringView("textures/quad.png"));
    CG_REQUIRE(textureRecord != nullptr);
    CG_EXPECT_EQ(textureRecord->id, texture);
    CG_EXPECT_EQ(static_cast<int>(textureRecord->format), static_cast<int>(SourceFormat::Png));
    CG_EXPECT_EQ(textureRecord->sourceHash, static_cast<u64>(0xabc));
    CG_EXPECT_EQ(textureRecord->cookedChecksum, static_cast<u64>(11));
    CG_EXPECT_TRUE(Same(textureRecord->cookedPath.View(), "cooked/textures/quad.cytex"));

    const AssetRecord* meshRecord = loaded.Find(StringView("meshes/quad.obj"));
    CG_REQUIRE(meshRecord != nullptr);
    CG_REQUIRE(meshRecord->dependencies.Size() == 1);
    CG_EXPECT_EQ(meshRecord->dependencies[0], texture);

    const AssetRecord* sceneRecord = loaded.Find(scene);
    CG_REQUIRE(sceneRecord != nullptr);
    CG_REQUIRE(sceneRecord->dependencies.Size() == 1);
    CG_EXPECT_EQ(sceneRecord->dependencies[0], mesh);

    CookCacheEntry entry{};
    CG_EXPECT_TRUE(loaded.TryGetCooked(texture, 0xabc, settings, entry));
    CG_EXPECT_EQ(entry.checksum, static_cast<u64>(11));

    DynamicArray<AssetId> order(allocator);
    CG_REQUIRE(loaded.CookOrder(order));
    CG_EXPECT_EQ(order.Size(), static_cast<usize>(3));
    CG_EXPECT_EQ(order[0], texture);

    (void)cyber::platform::DeleteFile(firstPath);
    (void)cyber::platform::DeleteFile(secondPath);
}

CG_TEST(asset_database_rejects_broken_manifests_without_data_loss)
{
    HeapAllocator allocator;
    AssetDatabase database(allocator);
    (void)Register(database, "keep.obj");
    CG_REQUIRE(database.Load("cyber_assets_database_missing.json").IsError());

    const char* path = "cyber_assets_database_broken.json";
    struct Case
    {
        const char* text;
        ErrorCode code;
    };
    const Case cases[] = {
        {"", ErrorCode::ParseError},
        {"{}", ErrorCode::ParseError},
        {R"({"format":"other","version":1,"assets":[]})", ErrorCode::ParseError},
        {R"({"format":"cyberassetdb","version":2,"assets":[]})", ErrorCode::Unsupported},
        {R"({"format":"cyberassetdb","version":1,"assets":{}})", ErrorCode::ParseError},
        {R"({"format":"cyberassetdb","version":1,"assets":[{"path":"a.obj","format":"nope"}]})",
         ErrorCode::ParseError},
        {R"({"format":"cyberassetdb","version":1,"assets":[{"path":"a.obj","format":"obj","sourceHash":"12"}]})",
         ErrorCode::ParseError},
        {R"({"format":"cyberassetdb","version":1,"assets":[{"path":"a.obj","format":"obj","dependencies":["missing.png"]}]})",
         ErrorCode::ParseError},
        {R"({"format":"cyberassetdb","version":1,"assets":[{"path":"a.obj","format":"obj","dependencies":["a.obj"]}]})",
         ErrorCode::InvalidArgument},
        {R"({"format":"cyberassetdb","version":1,"assets":[{"path":"../escape.obj","format":"obj"}]})",
         ErrorCode::InvalidArgument},
    };

    for (const Case& testCase : cases)
    {
        CG_REQUIRE(WriteText(path, testCase.text));
        const auto status = database.Load(path);
        CG_REQUIRE(status.IsError());
        CG_EXPECT_EQ(static_cast<int>(status.Error().code), static_cast<int>(testCase.code));
        CG_EXPECT_TRUE(status.Error().message != nullptr);
        // A failed load never disturbs the database that was already open.
        CG_EXPECT_EQ(database.Count(), static_cast<usize>(1));
        CG_EXPECT_TRUE(database.Find(StringView("keep.obj")) != nullptr);
    }
    (void)cyber::platform::DeleteFile(path);

    CG_EXPECT_TRUE(WriteText("cyber_assets_database_ok.json",
                             R"({"format":"cyberassetdb","version":1,"assets":[]})"));
    CG_REQUIRE(database.Load("cyber_assets_database_ok.json").IsOk());
    CG_EXPECT_EQ(database.Count(), static_cast<usize>(0));
    (void)cyber::platform::DeleteFile("cyber_assets_database_ok.json");
}

CG_TEST(asset_database_clear_keeps_the_allocator)
{
    HeapAllocator allocator;
    AssetDatabase database(allocator);
    (void)Register(database, "a.obj");
    (void)Register(database, "b.png");
    CG_EXPECT_EQ(database.Count(), static_cast<usize>(2));
    database.Clear();
    CG_EXPECT_EQ(database.Count(), static_cast<usize>(0));
    const auto again = database.Register(StringView("c.obj"));
    CG_REQUIRE(again.IsOk());
    CG_EXPECT_EQ(database.Count(), static_cast<usize>(1));
}

}  // namespace
