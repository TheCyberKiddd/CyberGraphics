// Measures the cost the cooked format is supposed to remove: parsing text and
// compressed containers at load time. Every scenario compares the same asset in
// its source form against its cooked form.
//
// Run it in the release profile; debug timings measure the debugger, not the
// format.

#include <cyber/assets/asset.h>
#include <cyber/assets/cooked.h>
#include <cyber/assets/gltf.h>
#include <cyber/assets/image.h>
#include <cyber/assets/obj.h>
#include <cyber/assets/texture.h>
#include <cyber/memory/heap_allocator.h>
#include <cyber/platform/filesystem.h>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>

namespace
{

using cyber::assets::AssetIdFromPath;
using cyber::assets::AssetImportSettings;
using cyber::assets::AssetKind;
using cyber::assets::CookedAsset;
using cyber::assets::CookMesh;
using cyber::assets::CookScene;
using cyber::assets::CookTexture;
using cyber::assets::DecodePng;
using cyber::assets::ImportedScene;
using cyber::assets::ImportGltfFile;
using cyber::assets::ImportObj;
using cyber::assets::ImportStatus;
using cyber::assets::TextureFromImage;
using cyber::core::Byte;
using cyber::core::u32;
using cyber::core::u64;
using cyber::core::usize;
using cyber::memory::Allocator;
using cyber::memory::DynamicArray;
using cyber::memory::HeapAllocator;
using cyber::memory::Span;
using cyber::memory::StringView;

volatile usize g_sink = 0;

template <typename Fn>
[[nodiscard]] double MicrosPerOp(Fn&& operation, u32 iterations)
{
    const auto start = std::chrono::steady_clock::now();
    for (u32 index = 0; index < iterations; ++index)
    {
        operation();
    }
    const auto end = std::chrono::steady_clock::now();
    const double total = std::chrono::duration<double, std::micro>(end - start).count();
    return total / static_cast<double>(iterations);
}

[[nodiscard]] std::string FixturePath(const char* name)
{
    return std::string(CYBER_ASSET_FIXTURES) + "/" + name;
}

[[nodiscard]] bool ReadFixture(const char* name, Allocator& allocator, DynamicArray<Byte>& bytes)
{
    auto file = cyber::platform::ReadFile(FixturePath(name).c_str(), allocator);
    if (file.IsError())
    {
        std::fprintf(stderr, "cannot read fixture %s\n", name);
        return false;
    }
    bytes = std::move(file.Value());
    return true;
}

void Report(const char* label, double micros)
{
    std::printf("  %-26s %9.3f us/op\n", label, micros);
}

void Speedup(const char* label, double source, double cooked)
{
    std::printf("  %-26s %9.2fx\n", label, source / cooked);
}

void Progress(const char* label)
{
    std::fprintf(stderr, "benchmark: %s\n", label);
    std::fflush(stderr);
}

/// A grid OBJ built in memory, so the benchmark measures parsing rather than a
/// disk that may or may not be cached.
[[nodiscard]] std::string MakeGridObj(u32 cells)
{
    std::string text;
    text.reserve(static_cast<usize>(cells) * cells * 64);
    const u32 verticesPerSide = cells + 1;
    for (u32 y = 0; y < verticesPerSide; ++y)
    {
        for (u32 x = 0; x < verticesPerSide; ++x)
        {
            char line[64] = {};
            std::snprintf(line, sizeof(line), "v %.3f %.3f 0.0\n",
                          static_cast<double>(x) / static_cast<double>(cells),
                          static_cast<double>(y) / static_cast<double>(cells));
            text.append(line);
        }
    }
    text.append("vt 0.0 0.0\n");
    for (u32 y = 0; y < cells; ++y)
    {
        for (u32 x = 0; x < cells; ++x)
        {
            const u32 a = y * verticesPerSide + x + 1;
            const u32 b = a + 1;
            const u32 c = a + verticesPerSide;
            const u32 d = c + 1;
            char line[96] = {};
            std::snprintf(line, sizeof(line), "f %u %u %u %u\n", a, b, d, c);
            text.append(line);
        }
    }
    return text;
}

}  // namespace

int main()
{
    HeapAllocator allocator;
    const AssetImportSettings settings{};

    // --- glTF: source import against cooked scene and mesh load --------------
    auto scene = ImportGltfFile(FixturePath("two_quads.gltf").c_str(), allocator);
    if (scene.IsError())
    {
        std::fprintf(stderr, "cannot import the glTF fixture\n");
        return 1;
    }
    const ImportedScene& imported = scene.Value();
    if (imported.meshes.Empty())
    {
        std::fprintf(stderr, "glTF fixture has no meshes\n");
        return 1;
    }

    auto cookedScene = CookScene(imported, AssetIdFromPath(StringView("bench.gltf")), 1, settings,
                                 allocator, "bench.gltf");
    auto cookedMesh = CookMesh(imported.meshes[0], AssetIdFromPath(StringView("bench.cymesh")), 1,
                               settings, allocator);
    if (cookedScene.IsError() || cookedMesh.IsError())
    {
        std::fprintf(stderr, "cannot cook the glTF fixture\n");
        return 1;
    }

    const char* scenePath = "cyber_assets_bench_scene.cyscene";
    const char* meshPath = "cyber_assets_bench_mesh.cymesh";
    if (cyber::platform::WriteFile(
            scenePath, Span<const Byte>(cookedScene.Value().Data(), cookedScene.Value().Size()))
            .IsError() ||
        cyber::platform::WriteFile(
            meshPath, Span<const Byte>(cookedMesh.Value().Data(), cookedMesh.Value().Size()))
            .IsError())
    {
        std::fprintf(stderr, "cannot write cooked benchmark payloads\n");
        return 1;
    }

    constexpr u32 kTimedIterations = 100;
    Progress("measuring glTF source import");
    const double sourceImport = MicrosPerOp(
        [&]
        {
            auto result = ImportGltfFile(FixturePath("two_quads.gltf").c_str(), allocator, {});
            if (result.IsOk())
            {
                g_sink += result.Value().TotalTriangleCount();
            }
        },
        kTimedIterations);
    Progress("measuring cooked scene load");
    const double cookedSceneLoad = MicrosPerOp(
        [&]
        {
            auto file = cyber::assets::CookedFile::Load(scenePath, allocator);
            if (file.IsOk())
            {
                g_sink += file.Value().Asset().AsScene().nodes.Size();
            }
        },
        kTimedIterations);
    Progress("measuring cooked mesh load");
    const double cookedMeshLoad = MicrosPerOp(
        [&]
        {
            auto file = cyber::assets::CookedFile::Load(meshPath, allocator);
            if (file.IsOk())
            {
                g_sink += file.Value().Asset().AsMesh().indices.Size();
            }
        },
        kTimedIterations);

    std::printf("glTF source vs cooked\n");
    Report("import two_quads.gltf", sourceImport);
    Report("load cooked scene", cookedSceneLoad);
    Report("load cooked mesh", cookedMeshLoad);
    Speedup("scene speedup", sourceImport, cookedSceneLoad);
    Speedup("mesh speedup", sourceImport, cookedMeshLoad);

    // --- PNG: decode against cooked texture load ----------------------------
    DynamicArray<Byte> pngBytes(allocator);
    if (!ReadFixture("png_rgba.png", allocator, pngBytes))
    {
        return 1;
    }
    auto decoded = DecodePng(Span<const Byte>(pngBytes.Data(), pngBytes.Size()), allocator);
    if (decoded.IsError())
    {
        std::fprintf(stderr, "cannot decode the PNG fixture\n");
        return 1;
    }
    auto texture = TextureFromImage(decoded.Value(), StringView("bench.png"), allocator);
    if (texture.IsError())
    {
        std::fprintf(stderr, "cannot convert the PNG fixture\n");
        return 1;
    }
    auto cookedTexture = CookTexture(texture.Value(), AssetIdFromPath(StringView("bench.cytex")), 1,
                                     settings, allocator);
    if (cookedTexture.IsError())
    {
        std::fprintf(stderr, "cannot cook the PNG fixture\n");
        return 1;
    }
    const char* texturePath = "cyber_assets_bench_texture.cytex";
    if (cyber::platform::WriteFile(texturePath, Span<const Byte>(cookedTexture.Value().Data(),
                                                                 cookedTexture.Value().Size()))
            .IsError())
    {
        std::fprintf(stderr, "cannot write the cooked texture\n");
        return 1;
    }

    Progress("measuring PNG source decode");
    const double pngReadDecode = MicrosPerOp(
        [&]
        {
            auto file = cyber::platform::ReadFile(FixturePath("png_rgba.png").c_str(), allocator);
            if (file.IsOk())
            {
                auto image = DecodePng(Span<const Byte>(file.Value().Data(), file.Value().Size()),
                                       allocator);
                if (image.IsOk())
                {
                    g_sink += image.Value().SizeBytes();
                }
            }
        },
        kTimedIterations);
    Progress("measuring cooked texture load");
    const double cookedTextureLoad = MicrosPerOp(
        [&]
        {
            auto file = cyber::assets::CookedFile::Load(texturePath, allocator);
            if (file.IsOk())
            {
                g_sink += file.Value().Asset().AsTexture().data.Size();
            }
        },
        kTimedIterations);

    std::printf("PNG source vs cooked\n");
    Report("read + decode png_rgba.png", pngReadDecode);
    Report("load cooked texture", cookedTextureLoad);
    Speedup("texture speedup", pngReadDecode, cookedTextureLoad);

    // --- Large synthetic OBJ: text parsing against a cooked mesh ------------
    const std::string objText = MakeGridObj(16);
    auto objImport =
        ImportObj(Span<const Byte>(reinterpret_cast<const Byte*>(objText.data()), objText.size()),
                  allocator, settings);
    if (objImport.IsError() || objImport.Value().mesh.vertices.Empty())
    {
        std::fprintf(stderr, "cannot import the generated OBJ\n");
        return 1;
    }
    const ImportStatus validated = objImport.Value().mesh.Validate();
    if (validated.IsError())
    {
        std::fprintf(stderr, "generated OBJ is not valid: %s\n", validated.Error().message);
        return 1;
    }
    auto bigCooked = CookMesh(objImport.Value().mesh, AssetIdFromPath(StringView("grid.cymesh")), 2,
                              settings, allocator);
    if (bigCooked.IsError())
    {
        std::fprintf(stderr, "cannot cook the generated OBJ\n");
        return 1;
    }

    constexpr u32 kBigIterations = 10;
    Progress("measuring OBJ source import");
    const double objParse = MicrosPerOp(
        [&]
        {
            auto parsed = ImportObj(
                Span<const Byte>(reinterpret_cast<const Byte*>(objText.data()), objText.size()),
                allocator, settings);
            if (parsed.IsOk())
            {
                g_sink += parsed.Value().mesh.vertices.Size();
            }
        },
        kBigIterations);
    Progress("measuring cooked mesh parse");
    const double bigCookedParse = MicrosPerOp(
        [&]
        {
            auto parsed = CookedAsset::FromBytes(
                Span<const Byte>(bigCooked.Value().Data(), bigCooked.Value().Size()));
            if (parsed.IsOk())
            {
                g_sink += parsed.Value().AsMesh().vertices.Size();
            }
        },
        kBigIterations);

    std::printf("16x16 OBJ (%zu vertices) vs cooked\n", objImport.Value().mesh.vertices.Size());
    Report("parse OBJ text", objParse);
    Report("parse cooked mesh", bigCookedParse);
    Speedup("grid speedup", objParse, bigCookedParse);

    (void)cyber::platform::DeleteFile(scenePath);
    (void)cyber::platform::DeleteFile(meshPath);
    (void)cyber::platform::DeleteFile(texturePath);
    Progress("benchmark complete");
    std::printf("sink %zu\n", static_cast<usize>(g_sink));
    return 0;
}
