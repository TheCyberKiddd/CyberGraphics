#include <cyber/assets/image.h>
#include <cyber/memory/heap_allocator.h>
#include <cyber/platform/filesystem.h>
#include <cyber/test/test.h>

#include <string>

namespace
{

using cyber::assets::DecodeJpeg;
using cyber::core::Byte;
using cyber::core::ErrorCode;
using cyber::memory::DynamicArray;
using cyber::memory::HeapAllocator;
using cyber::memory::Span;

[[nodiscard]] bool ReadFixture(const char* name, HeapAllocator& allocator, DynamicArray<Byte>& out)
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

[[nodiscard]] int Sample(const cyber::assets::Image& image, cyber::core::u32 x, cyber::core::u32 y,
                         int channel)
{
    Byte pixel[4] = {};
    image.GetPixel(x, y, pixel);
    return std::to_integer<cyber::core::u8>(pixel[channel]);
}

void CheckPixel(const cyber::assets::Image& image, cyber::core::u32 x, cyber::core::u32 y, int r,
                int g, int b, int tolerance)
{
    CG_EXPECT_NEAR(Sample(image, x, y, 0), r, tolerance);
    CG_EXPECT_NEAR(Sample(image, x, y, 1), g, tolerance);
    CG_EXPECT_NEAR(Sample(image, x, y, 2), b, tolerance);
    CG_EXPECT_EQ(Sample(image, x, y, 3), 255);
}

}  // namespace

CG_TEST(jpeg_decodes_444_reference_pixels)
{
    HeapAllocator allocator;
    DynamicArray<Byte> bytes(allocator);
    CG_REQUIRE(ReadFixture("jpeg_444.jpg", allocator, bytes));

    auto decoded = DecodeJpeg(Span<const Byte>(bytes.Data(), bytes.Size()), allocator);
    CG_REQUIRE(decoded.IsOk());
    const auto& image = decoded.Value();
    CG_EXPECT_EQ(image.Width(), 16u);
    CG_EXPECT_EQ(image.Height(), 16u);

    // Reference values were produced by libjpeg-turbo from the same committed
    // file; a small tolerance covers IDCT rounding differences.
    CheckPixel(image, 0, 0, 0, 0, 0, 2);
    CheckPixel(image, 3, 5, 48, 80, 17, 3);
    CheckPixel(image, 8, 8, 129, 128, 72, 3);
    CheckPixel(image, 15, 15, 241, 240, 254, 3);
}

CG_TEST(jpeg_decodes_420_chroma_subsampling)
{
    HeapAllocator allocator;
    DynamicArray<Byte> bytes(allocator);
    CG_REQUIRE(ReadFixture("jpeg_420.jpg", allocator, bytes));

    auto decoded = DecodeJpeg(Span<const Byte>(bytes.Data(), bytes.Size()), allocator);
    CG_REQUIRE(decoded.IsOk());
    const auto& image = decoded.Value();
    CG_EXPECT_EQ(image.Width(), 16u);

    CheckPixel(image, 0, 0, 1, 1, 0, 4);
    CheckPixel(image, 3, 5, 45, 81, 17, 4);
    CheckPixel(image, 8, 8, 128, 129, 72, 4);
    CheckPixel(image, 15, 15, 242, 239, 248, 4);
}

CG_TEST(jpeg_decodes_grayscale)
{
    HeapAllocator allocator;
    DynamicArray<Byte> bytes(allocator);
    CG_REQUIRE(ReadFixture("jpeg_gray.jpg", allocator, bytes));

    auto decoded = DecodeJpeg(Span<const Byte>(bytes.Data(), bytes.Size()), allocator);
    CG_REQUIRE(decoded.IsOk());
    const auto& image = decoded.Value();

    CG_EXPECT_NEAR(Sample(image, 0, 0, 0), 0, 2);
    CG_EXPECT_NEAR(Sample(image, 3, 5, 0), 63, 3);
    CG_EXPECT_NEAR(Sample(image, 8, 8, 0), 121, 3);
    CG_EXPECT_NEAR(Sample(image, 15, 15, 0), 241, 3);
    // Grayscale replicates into all three channels.
    CG_EXPECT_EQ(Sample(image, 8, 8, 0), Sample(image, 8, 8, 1));
    CG_EXPECT_EQ(Sample(image, 8, 8, 1), Sample(image, 8, 8, 2));
}

CG_TEST(jpeg_rejects_truncated_and_wrong_inputs)
{
    HeapAllocator allocator;
    DynamicArray<Byte> bytes(allocator);
    CG_REQUIRE(ReadFixture("jpeg_444.jpg", allocator, bytes));

    for (cyber::core::usize size :
         {static_cast<cyber::core::usize>(0), static_cast<cyber::core::usize>(2),
          static_cast<cyber::core::usize>(20), static_cast<cyber::core::usize>(bytes.Size() / 2),
          bytes.Size() - 2})
    {
        auto truncated = DecodeJpeg(Span<const Byte>(bytes.Data(), size), allocator);
        CG_REQUIRE(truncated.IsError());
    }

    // Change SOF0 to SOF2 (progressive) and confirm the honest rejection.
    DynamicArray<Byte> progressive(allocator);
    {
        auto copy = cyber::platform::ReadFile(
            (std::string(CYBER_ASSET_FIXTURES) + "/jpeg_444.jpg").c_str(), allocator);
        CG_REQUIRE(copy.IsOk());
        progressive = std::move(copy.Value());
    }
    bool patched = false;
    for (cyber::core::usize index = 0; index + 1 < progressive.Size(); ++index)
    {
        if (std::to_integer<cyber::core::u8>(progressive[index]) == 0xff &&
            std::to_integer<cyber::core::u8>(progressive[index + 1]) == 0xc0)
        {
            progressive[index + 1] = static_cast<Byte>(0xc2);
            patched = true;
            break;
        }
    }
    CG_REQUIRE(patched);
    auto unsupported =
        DecodeJpeg(Span<const Byte>(progressive.Data(), progressive.Size()), allocator);
    CG_REQUIRE(unsupported.IsError());
    CG_EXPECT_EQ(static_cast<int>(unsupported.Error().code),
                 static_cast<int>(ErrorCode::Unsupported));
}

CG_TEST(jpeg_rejects_garbage_after_a_valid_soi)
{
    HeapAllocator allocator;
    constexpr char kGarbage[] = "\xff\xd8garbage\xff\xd9";
    auto decoded = DecodeJpeg(
        Span<const Byte>(reinterpret_cast<const Byte*>(kGarbage), sizeof(kGarbage) - 1), allocator);
    CG_REQUIRE(decoded.IsError());
}
