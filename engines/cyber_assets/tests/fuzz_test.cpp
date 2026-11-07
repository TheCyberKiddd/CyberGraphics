#include <cyber/assets/asset.h>
#include <cyber/assets/cooked.h>
#include <cyber/assets/gltf.h>
#include <cyber/assets/image.h>
#include <cyber/assets/json.h>
#include <cyber/assets/obj.h>
#include <cyber/assets/texture.h>
#include <cyber/memory/heap_allocator.h>
#include <cyber/memory/string_view.h>
#include <cyber/platform/filesystem.h>
#include <cyber/test/test.h>

#include <chrono>
#include <cstddef>
#include <string>
#include <vector>

namespace
{

using cyber::assets::AssetIdFromPath;
using cyber::assets::AssetImportSettings;
using cyber::assets::CookedAsset;
using cyber::assets::CookMesh;
using cyber::assets::DecodeImage;
using cyber::assets::DecodeJpeg;
using cyber::assets::DecodePng;
using cyber::assets::ImportGltf;
using cyber::assets::ImportObj;
using cyber::assets::JsonDocument;
using cyber::assets::ParseDds;
using cyber::assets::ParseKtx2;
using cyber::core::Byte;
using cyber::core::u32;
using cyber::core::u8;
using cyber::memory::Allocator;
using cyber::memory::DynamicArray;
using cyber::memory::HeapAllocator;
using cyber::memory::Span;
using cyber::memory::StringView;

constexpr std::chrono::milliseconds kFuzzDuration{100};

bool LoadFixture(const char* name, Allocator& allocator, DynamicArray<Byte>& bytes)
{
    const std::string path = std::string(CYBER_ASSET_FIXTURES) + "/" + name;
    auto file = cyber::platform::ReadFile(path.c_str(), allocator);
    if (file.IsError())
    {
        return false;
    }
    bytes = std::move(file.Value());
    return true;
}

template <typename Parser>
void FuzzMutations(Span<const Byte> seed, Parser&& parser)
{
    if (seed.Empty())
    {
        return;
    }

    const auto deadline = std::chrono::steady_clock::now() + kFuzzDuration;
    u32 iteration = 0;
    do
    {
        std::vector<Byte> mutated(seed.Data(), seed.Data() + seed.Size());
        if (iteration % 4 == 0)
        {
            mutated.resize((static_cast<std::size_t>(iteration) * 29u) % (mutated.size() + 1u));
        }
        else
        {
            const std::size_t offset = (static_cast<std::size_t>(iteration) * 131u +
                                        static_cast<std::size_t>(iteration / seed.Size()) * 17u) %
                                       mutated.size();
            const u8 value = std::to_integer<u8>(mutated[offset]);
            mutated[offset] = static_cast<Byte>(value ^ static_cast<u8>(1u << (iteration % 8)));
        }

        parser(Span<const Byte>(mutated.data(), mutated.size()));
        ++iteration;
    } while (std::chrono::steady_clock::now() < deadline);
}

}  // namespace

CG_TEST(assets_fuzz_mutates_every_parser_with_a_bounded_corpus)
{
    HeapAllocator allocator;
    const AssetImportSettings settings{};

    constexpr char kJsonSeed[] = R"({"asset":{"version":"2.0"},"nodes":[]})";
    constexpr char kGltfSeed[] =
        R"({"asset":{"version":"2.0"},"scene":0,"scenes":[{"nodes":[]}],"nodes":[]})";
    constexpr char kObjSeed[] = "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n";
    const Span<const Byte> jsonBytes(reinterpret_cast<const Byte*>(kJsonSeed),
                                     sizeof(kJsonSeed) - 1);
    const Span<const Byte> gltfBytes(reinterpret_cast<const Byte*>(kGltfSeed),
                                     sizeof(kGltfSeed) - 1);
    const Span<const Byte> objBytes(reinterpret_cast<const Byte*>(kObjSeed), sizeof(kObjSeed) - 1);

    FuzzMutations(jsonBytes,
                  [&allocator](Span<const Byte> bytes)
                  {
                      auto result = JsonDocument::Parse(
                          StringView(reinterpret_cast<const char*>(bytes.Data()), bytes.Size()),
                          allocator);
                      (void)result;
                  });
    FuzzMutations(gltfBytes,
                  [&allocator](Span<const Byte> bytes)
                  {
                      auto result = ImportGltf(bytes, "", allocator);
                      (void)result;
                  });
    FuzzMutations(objBytes,
                  [&allocator, &settings](Span<const Byte> bytes)
                  {
                      auto result = ImportObj(bytes, allocator, settings);
                      (void)result;
                  });

    DynamicArray<Byte> png(allocator);
    DynamicArray<Byte> jpeg(allocator);
    DynamicArray<Byte> dds(allocator);
    DynamicArray<Byte> ktx2(allocator);
    CG_REQUIRE(LoadFixture("png_rgba.png", allocator, png));
    CG_REQUIRE(LoadFixture("jpeg_420.jpg", allocator, jpeg));
    CG_REQUIRE(LoadFixture("texture_bc1.dds", allocator, dds));
    CG_REQUIRE(LoadFixture("texture_bc1.ktx2", allocator, ktx2));

    FuzzMutations(Span<const Byte>(png.Data(), png.Size()),
                  [&allocator](Span<const Byte> bytes)
                  {
                      auto result = DecodePng(bytes, allocator);
                      (void)result;
                  });
    FuzzMutations(Span<const Byte>(jpeg.Data(), jpeg.Size()),
                  [&allocator](Span<const Byte> bytes)
                  {
                      auto result = DecodeJpeg(bytes, allocator);
                      (void)result;
                  });
    FuzzMutations(Span<const Byte>(png.Data(), png.Size()),
                  [&allocator](Span<const Byte> bytes)
                  {
                      auto result = DecodeImage(bytes, allocator);
                      (void)result;
                  });
    FuzzMutations(Span<const Byte>(dds.Data(), dds.Size()),
                  [&allocator](Span<const Byte> bytes)
                  {
                      auto result = ParseDds(bytes, allocator);
                      (void)result;
                  });
    FuzzMutations(Span<const Byte>(ktx2.Data(), ktx2.Size()),
                  [&allocator](Span<const Byte> bytes)
                  {
                      auto result = ParseKtx2(bytes, allocator);
                      (void)result;
                  });

    auto imported = ImportObj(objBytes, allocator, settings);
    CG_REQUIRE(imported.IsOk());
    auto cooked = CookMesh(imported.Value().mesh, AssetIdFromPath(StringView("fuzz-triangle.obj")),
                           1, settings, allocator);
    CG_REQUIRE(cooked.IsOk());
    FuzzMutations(Span<const Byte>(cooked.Value().Data(), cooked.Value().Size()),
                  [](Span<const Byte> bytes)
                  {
                      auto result = CookedAsset::FromBytes(bytes);
                      (void)result;
                  });
}
