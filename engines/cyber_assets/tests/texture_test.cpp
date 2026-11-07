#include <cyber/assets/texture.h>
#include <cyber/memory/heap_allocator.h>
#include <cyber/platform/filesystem.h>
#include <cyber/test/test.h>

#include <string>

namespace
{

using cyber::assets::ParseDds;
using cyber::assets::ParseKtx2;
using cyber::assets::TextureFormat;
using cyber::core::Byte;
using cyber::core::ErrorCode;
using cyber::core::u8;
using cyber::memory::DynamicArray;
using cyber::memory::HeapAllocator;
using cyber::memory::Span;

[[nodiscard]] bool LoadFixture(const char* name, HeapAllocator& allocator, DynamicArray<Byte>& out)
{
    const std::string path = std::string(CYBER_ASSET_FIXTURES) + "/" + name;
    auto file = cyber::platform::ReadFile(path.c_str(), allocator);
    if (file.IsError())
    {
        return false;
    }
    out = std::move(file.Value());
    return true;
}

void WriteLe32(DynamicArray<Byte>& bytes, cyber::core::usize offset, cyber::core::u32 value)
{
    bytes[offset] = static_cast<Byte>(value & 0xffu);
    bytes[offset + 1] = static_cast<Byte>((value >> 8u) & 0xffu);
    bytes[offset + 2] = static_cast<Byte>((value >> 16u) & 0xffu);
    bytes[offset + 3] = static_cast<Byte>((value >> 24u) & 0xffu);
}

}  // namespace

CG_TEST(dds_reads_a_two_level_bc1_texture)
{
    HeapAllocator allocator;
    DynamicArray<Byte> bytes(allocator);
    CG_REQUIRE(LoadFixture("texture_bc1.dds", allocator, bytes));

    auto parsed = ParseDds(Span<const Byte>(bytes.Data(), bytes.Size()), allocator);
    CG_REQUIRE(parsed.IsOk());
    const auto& texture = parsed.Value();

    CG_EXPECT_EQ(static_cast<int>(texture.format), static_cast<int>(TextureFormat::Bc1Rgba));
    CG_EXPECT_EQ(texture.width, 4u);
    CG_EXPECT_EQ(texture.height, 4u);
    CG_EXPECT_EQ(texture.mipCount, 2u);
    CG_EXPECT_EQ(texture.layerCount, 1u);
    CG_EXPECT_FALSE(texture.cubemap);
    CG_REQUIRE(texture.levels.Size() == 2);

    CG_EXPECT_EQ(texture.levels[0].size, static_cast<cyber::core::u64>(8));
    CG_EXPECT_EQ(texture.levels[0].rowPitch, 8u);
    CG_EXPECT_EQ(texture.levels[1].size, static_cast<cyber::core::u64>(8));
    CG_EXPECT_EQ(texture.levels[1].width, 2u);
    CG_EXPECT_EQ(texture.levels[1].height, 2u);
    CG_EXPECT_EQ(texture.data.Size(), static_cast<cyber::core::usize>(16));

    // The first block stores red as colour0 and blue as colour1 (RGB565).
    CG_EXPECT_EQ(std::to_integer<u8>(texture.data[0]), 0x00u);
    CG_EXPECT_EQ(std::to_integer<u8>(texture.data[1]), 0xf8u);
}

CG_TEST(dds_rejects_invalid_headers_and_payloads)
{
    HeapAllocator allocator;

    {
        DynamicArray<Byte> bytes(allocator);
        CG_REQUIRE(LoadFixture("texture_bc1.dds", allocator, bytes));
        WriteLe32(bytes, 0, 0x20534444u ^ 1u);
        auto parsed = ParseDds(Span<const Byte>(bytes.Data(), bytes.Size()), allocator);
        CG_REQUIRE(parsed.IsError());
        CG_EXPECT_EQ(static_cast<int>(parsed.Error().code),
                     static_cast<int>(ErrorCode::ParseError));
    }
    {
        DynamicArray<Byte> bytes(allocator);
        CG_REQUIRE(LoadFixture("texture_bc1.dds", allocator, bytes));
        WriteLe32(bytes, 4, 123u);  // dwSize must be 124.
        auto parsed = ParseDds(Span<const Byte>(bytes.Data(), bytes.Size()), allocator);
        CG_REQUIRE(parsed.IsError());
    }
    {
        DynamicArray<Byte> bytes(allocator);
        CG_REQUIRE(LoadFixture("texture_bc1.dds", allocator, bytes));
        WriteLe32(bytes, 4 + 72 + 8, 0x31545844u ^ 3u);  // Unknown FourCC.
        auto parsed = ParseDds(Span<const Byte>(bytes.Data(), bytes.Size()), allocator);
        CG_REQUIRE(parsed.IsError());
        CG_EXPECT_EQ(static_cast<int>(parsed.Error().code),
                     static_cast<int>(ErrorCode::Unsupported));
    }
    {
        DynamicArray<Byte> bytes(allocator);
        CG_REQUIRE(LoadFixture("texture_bc1.dds", allocator, bytes));
        auto truncated = ParseDds(Span<const Byte>(bytes.Data(), bytes.Size() - 4), allocator);
        CG_REQUIRE(truncated.IsError());
    }

    auto empty = ParseDds(Span<const Byte>(), allocator);
    CG_REQUIRE(empty.IsError());
}

CG_TEST(ktx2_reads_bc1_levels)
{
    HeapAllocator allocator;
    DynamicArray<Byte> bytes(allocator);
    CG_REQUIRE(LoadFixture("texture_bc1.ktx2", allocator, bytes));

    auto parsed = ParseKtx2(Span<const Byte>(bytes.Data(), bytes.Size()), allocator);
    CG_REQUIRE(parsed.IsOk());
    const auto& texture = parsed.Value();

    CG_EXPECT_EQ(static_cast<int>(texture.format), static_cast<int>(TextureFormat::Bc1Rgba));
    CG_EXPECT_EQ(texture.width, 8u);
    CG_EXPECT_EQ(texture.height, 8u);
    CG_EXPECT_EQ(texture.mipCount, 2u);
    CG_EXPECT_EQ(texture.layerCount, 1u);
    CG_REQUIRE(texture.levels.Size() == 2);
    CG_EXPECT_EQ(texture.levels[0].size, static_cast<cyber::core::u64>(32));
    CG_EXPECT_EQ(texture.levels[0].rowPitch, 16u);
    CG_EXPECT_EQ(texture.levels[1].size, static_cast<cyber::core::u64>(8));
    CG_EXPECT_EQ(texture.levels[1].width, 4u);
    CG_EXPECT_EQ(texture.levels[1].height, 4u);
}

CG_TEST(ktx2_rejects_invalid_containers)
{
    HeapAllocator allocator;

    {
        DynamicArray<Byte> bytes(allocator);
        CG_REQUIRE(LoadFixture("texture_bc1.ktx2", allocator, bytes));
        bytes[0] = static_cast<Byte>(0x00);
        auto parsed = ParseKtx2(Span<const Byte>(bytes.Data(), bytes.Size()), allocator);
        CG_REQUIRE(parsed.IsError());
    }
    {
        DynamicArray<Byte> bytes(allocator);
        CG_REQUIRE(LoadFixture("texture_bc1.ktx2", allocator, bytes));
        WriteLe32(bytes, 44, 1u);  // supercompressionScheme = zstd.
        auto parsed = ParseKtx2(Span<const Byte>(bytes.Data(), bytes.Size()), allocator);
        CG_REQUIRE(parsed.IsError());
        CG_EXPECT_EQ(static_cast<int>(parsed.Error().code),
                     static_cast<int>(ErrorCode::Unsupported));
    }
    {
        DynamicArray<Byte> bytes(allocator);
        CG_REQUIRE(LoadFixture("texture_bc1.ktx2", allocator, bytes));
        WriteLe32(bytes, 12, 999u);  // Unknown vkFormat.
        auto parsed = ParseKtx2(Span<const Byte>(bytes.Data(), bytes.Size()), allocator);
        CG_REQUIRE(parsed.IsError());
    }
    {
        DynamicArray<Byte> bytes(allocator);
        CG_REQUIRE(LoadFixture("texture_bc1.ktx2", allocator, bytes));
        // Corrupt the first level's byteLength so it no longer matches a BC1 mip.
        bytes[80 + 8] = static_cast<Byte>(8);
        auto parsed = ParseKtx2(Span<const Byte>(bytes.Data(), bytes.Size()), allocator);
        CG_REQUIRE(parsed.IsError());
    }
    {
        DynamicArray<Byte> bytes(allocator);
        CG_REQUIRE(LoadFixture("texture_bc1.ktx2", allocator, bytes));
        auto parsed = ParseKtx2(Span<const Byte>(bytes.Data(), bytes.Size() - 8), allocator);
        CG_REQUIRE(parsed.IsError());
    }
}

CG_TEST(texture_level_sizing_matches_the_formats)
{
    cyber::core::u32 rowPitch = 0;
    auto bc7 = cyber::assets::TextureLevelSize(TextureFormat::Bc7Rgba, 8, 8, rowPitch);
    CG_REQUIRE(bc7.IsOk());
    CG_EXPECT_EQ(bc7.Value(), static_cast<cyber::core::u64>(64));
    CG_EXPECT_EQ(rowPitch, 32u);

    auto rgba = cyber::assets::TextureLevelSize(TextureFormat::Rgba8Unorm, 5, 3, rowPitch);
    CG_REQUIRE(rgba.IsOk());
    CG_EXPECT_EQ(rgba.Value(), static_cast<cyber::core::u64>(60));
    CG_EXPECT_EQ(rowPitch, 20u);

    auto unknown = cyber::assets::TextureLevelSize(TextureFormat::Unknown, 4, 4, rowPitch);
    CG_REQUIRE(unknown.IsError());
}
