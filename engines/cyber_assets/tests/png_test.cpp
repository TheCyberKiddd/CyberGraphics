#include <cyber/assets/image.h>
#include <cyber/memory/heap_allocator.h>
#include <cyber/platform/filesystem.h>
#include <cyber/test/test.h>

#include <cstring>
#include <string>

namespace
{

using cyber::assets::DecodeImage;
using cyber::assets::DecodePng;
using cyber::assets::IdentifyImage;
using cyber::assets::ImageFileFormat;
using cyber::core::Byte;
using cyber::core::ErrorCode;
using cyber::memory::DynamicArray;
using cyber::memory::HeapAllocator;
using cyber::memory::Span;

[[nodiscard]] std::string FixturePath(const char* name)
{
    return std::string(CYBER_ASSET_FIXTURES) + "/" + name;
}

[[nodiscard]] bool ReadFixture(const char* name, HeapAllocator& allocator, DynamicArray<Byte>& out)
{
    const std::string path = FixturePath(name);
    auto file = cyber::platform::ReadFile(path.c_str(), allocator);
    if (file.IsError())
    {
        return false;
    }
    out = std::move(file.Value());
    return true;
}

[[nodiscard]] bool PixelEquals(const cyber::assets::Image& image, cyber::core::u32 x,
                               cyber::core::u32 y, int r, int g, int b, int a)
{
    Byte pixel[4] = {};
    image.GetPixel(x, y, pixel);
    return std::to_integer<cyber::core::u8>(pixel[0]) == r &&
           std::to_integer<cyber::core::u8>(pixel[1]) == g &&
           std::to_integer<cyber::core::u8>(pixel[2]) == b &&
           std::to_integer<cyber::core::u8>(pixel[3]) == a;
}

CG_TEST(png_decodes_rgba_fixture)
{
    HeapAllocator allocator;
    DynamicArray<Byte> bytes(allocator);
    CG_REQUIRE(ReadFixture("png_rgba.png", allocator, bytes));

    CG_EXPECT_EQ(static_cast<int>(IdentifyImage(Span<const Byte>(bytes.Data(), bytes.Size()))),
                 static_cast<int>(ImageFileFormat::Png));

    auto decoded = DecodePng(Span<const Byte>(bytes.Data(), bytes.Size()), allocator);
    CG_REQUIRE(decoded.IsOk());
    const auto& image = decoded.Value();
    CG_EXPECT_EQ(image.Width(), 8u);
    CG_EXPECT_EQ(image.Height(), 8u);
    CG_EXPECT_EQ(image.SizeBytes(), static_cast<cyber::core::usize>(8 * 8 * 4));

    CG_EXPECT_TRUE(PixelEquals(image, 0, 0, 0, 0, 0, 255));
    CG_EXPECT_TRUE(PixelEquals(image, 2, 1, 64, 32, 48, 239));
    CG_EXPECT_TRUE(PixelEquals(image, 7, 7, 224, 224, 224, 199));
}

CG_TEST(png_decodes_adam7_interlaced_fixture)
{
    HeapAllocator allocator;
    DynamicArray<Byte> bytes(allocator);
    CG_REQUIRE(ReadFixture("png_interlaced.png", allocator, bytes));

    auto decoded = DecodePng(Span<const Byte>(bytes.Data(), bytes.Size()), allocator);
    CG_REQUIRE(decoded.IsOk());
    const auto& image = decoded.Value();
    CG_EXPECT_EQ(image.Width(), 8u);
    CG_EXPECT_EQ(image.Height(), 8u);
    // Every pixel must match the non-interlaced file exactly.
    CG_EXPECT_TRUE(PixelEquals(image, 0, 0, 0, 0, 0, 255));
    CG_EXPECT_TRUE(PixelEquals(image, 2, 1, 64, 32, 48, 239));
    CG_EXPECT_TRUE(PixelEquals(image, 7, 7, 224, 224, 224, 199));
    CG_EXPECT_TRUE(PixelEquals(image, 1, 6, 32, 192, 112, 247));
}

CG_TEST(png_decodes_palette_with_transparency)
{
    HeapAllocator allocator;
    DynamicArray<Byte> bytes(allocator);
    CG_REQUIRE(ReadFixture("png_palette.png", allocator, bytes));

    auto decoded = DecodePng(Span<const Byte>(bytes.Data(), bytes.Size()), allocator);
    CG_REQUIRE(decoded.IsOk());
    const auto& image = decoded.Value();
    CG_EXPECT_EQ(image.Width(), 4u);
    CG_EXPECT_TRUE(PixelEquals(image, 0, 0, 255, 0, 0, 255));
    CG_EXPECT_TRUE(PixelEquals(image, 1, 0, 0, 255, 0, 255));
    CG_EXPECT_TRUE(PixelEquals(image, 2, 0, 0, 0, 255, 255));
    CG_EXPECT_TRUE(PixelEquals(image, 3, 0, 255, 255, 255, 0));
}

CG_TEST(png_decodes_sixteen_bit_grayscale)
{
    HeapAllocator allocator;
    DynamicArray<Byte> bytes(allocator);
    CG_REQUIRE(ReadFixture("png_gray16.png", allocator, bytes));

    auto decoded = DecodePng(Span<const Byte>(bytes.Data(), bytes.Size()), allocator);
    CG_REQUIRE(decoded.IsOk());
    const auto& image = decoded.Value();
    CG_EXPECT_TRUE(PixelEquals(image, 0, 0, 0, 0, 0, 255));
    CG_EXPECT_TRUE(PixelEquals(image, 1, 0, 17, 17, 17, 255));
    CG_EXPECT_TRUE(PixelEquals(image, 2, 1, 102, 102, 102, 255));
    CG_EXPECT_TRUE(PixelEquals(image, 3, 3, 255, 255, 255, 255));
}

CG_TEST(png_rejects_corrupt_and_truncated_files)
{
    HeapAllocator allocator;
    DynamicArray<Byte> bytes(allocator);
    CG_REQUIRE(ReadFixture("png_rgba.png", allocator, bytes));

    // Flip one byte inside the compressed data: the chunk CRC must catch it.
    DynamicArray<Byte> corrupt(allocator);
    {
        auto copy = cyber::platform::ReadFile(FixturePath("png_rgba.png").c_str(), allocator);
        CG_REQUIRE(copy.IsOk());
        corrupt = std::move(copy.Value());
    }
    corrupt[60] = static_cast<Byte>(std::to_integer<cyber::core::u8>(corrupt[60]) ^ 0x5a);
    auto badCrc = DecodePng(Span<const Byte>(corrupt.Data(), corrupt.Size()), allocator);
    CG_REQUIRE(badCrc.IsError());
    CG_EXPECT_EQ(static_cast<int>(badCrc.Error().code), static_cast<int>(ErrorCode::ParseError));

    for (cyber::core::usize size :
         {static_cast<cyber::core::usize>(0), static_cast<cyber::core::usize>(8),
          static_cast<cyber::core::usize>(40), bytes.Size() - 13})
    {
        auto truncated = DecodePng(Span<const Byte>(bytes.Data(), size), allocator);
        CG_REQUIRE(truncated.IsError());
    }

    // A valid signature with a bad zlib stream must not be accepted.
    constexpr char kBadSignature[] = "not a png at all";
    auto wrongSignature = DecodePng(
        Span<const Byte>(reinterpret_cast<const Byte*>(kBadSignature), sizeof(kBadSignature) - 1),
        allocator);
    CG_REQUIRE(wrongSignature.IsError());
}

CG_TEST(png_dispatches_through_the_image_sniffer)
{
    HeapAllocator allocator;
    DynamicArray<Byte> bytes(allocator);
    CG_REQUIRE(ReadFixture("png_rgba.png", allocator, bytes));
    auto decoded = DecodeImage(Span<const Byte>(bytes.Data(), bytes.Size()), allocator);
    CG_REQUIRE(decoded.IsOk());
    CG_EXPECT_EQ(decoded.Value().Width(), 8u);

    constexpr char kUnknown[] = "plain text";
    auto unknown = DecodeImage(
        Span<const Byte>(reinterpret_cast<const Byte*>(kUnknown), sizeof(kUnknown) - 1), allocator);
    CG_REQUIRE(unknown.IsError());
    CG_EXPECT_EQ(static_cast<int>(unknown.Error().code), static_cast<int>(ErrorCode::Unsupported));
}

}  // namespace
