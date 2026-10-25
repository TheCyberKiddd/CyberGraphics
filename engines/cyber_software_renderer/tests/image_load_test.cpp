#include <cyber/memory/heap_allocator.h>
#include <cyber/software/image_io.h>
#include <cyber/test/test.h>

#include <cmath>
#include <cstring>
#include <utility>

using namespace cyber::software;
using namespace cyber::memory;
using cyber::core::i32;
using cyber::core::u8;
using cyber::core::usize;
using cyber::math::Color;

namespace
{

Span<const Byte> BytesOf(const char* data, usize size)
{
    return Span<const Byte>(reinterpret_cast<const Byte*>(data), size);
}

}  // namespace

CG_TEST(software_image_load_ppm_binary)
{
    HeapAllocator heap;
    const char ppm[] = "P6\n2 1\n255\n\xFF\x00\x00\x00\xFF\x00";
    auto decoded = ReadImageFromMemory(BytesOf(ppm, sizeof(ppm) - 1), heap);
    CG_REQUIRE(static_cast<bool>(decoded));
    Framebuffer image = std::move(decoded.Value());
    CG_EXPECT_EQ(image.Width(), 2);
    CG_EXPECT_EQ(image.Height(), 1);

    const Color first = image.GetPixel(0, 0);
    CG_EXPECT_NEAR(first.r, 1.0f, 2.0e-3f);
    CG_EXPECT_NEAR(first.g, 0.0f, 2.0e-3f);

    const Color second = image.GetPixel(1, 0);
    CG_EXPECT_NEAR(second.r, 0.0f, 2.0e-3f);
    CG_EXPECT_NEAR(second.g, 1.0f, 2.0e-3f);
}

CG_TEST(software_image_load_ppm_ascii_with_comment)
{
    HeapAllocator heap;
    const char ppm[] = "P3\n# a comment\n1 1\n255\n255 128 0\n";
    auto decoded = ReadImageFromMemory(BytesOf(ppm, sizeof(ppm) - 1), heap);
    CG_REQUIRE(static_cast<bool>(decoded));
    Framebuffer image = std::move(decoded.Value());
    CG_EXPECT_EQ(image.Width(), 1);

    const Color pixel = image.GetPixel(0, 0);
    CG_EXPECT_NEAR(pixel.r, 1.0f, 2.0e-3f);
    // 128/255 is stored as an sRGB code and decoded to linear.
    CG_EXPECT_NEAR(pixel.g, cyber::math::SrgbToLinear(128.0f / 255.0f), 2.0e-3f);
    CG_EXPECT_NEAR(pixel.b, 0.0f, 2.0e-3f);
}

CG_TEST(software_image_load_tga_true_color)
{
    HeapAllocator heap;
    u8 tga[21] = {};
    tga[2] = 2;  // Uncompressed true color.
    tga[12] = 1;
    tga[13] = 0;
    tga[14] = 1;
    tga[15] = 0;
    tga[16] = 24;
    tga[17] = 0x20;  // Top-down origin.
    tga[18] = 0;     // Blue.
    tga[19] = 0;     // Green.
    tga[20] = 255;   // Red.

    auto decoded = ReadImageFromMemory(
        Span<const Byte>(reinterpret_cast<const Byte*>(tga), sizeof(tga)), heap);
    CG_REQUIRE(static_cast<bool>(decoded));
    Framebuffer image = std::move(decoded.Value());
    CG_EXPECT_EQ(image.Width(), 1);
    CG_EXPECT_EQ(image.Height(), 1);
    const Color pixel = image.GetPixel(0, 0);
    CG_EXPECT_NEAR(pixel.r, 1.0f, 2.0e-3f);
    CG_EXPECT_NEAR(pixel.g, 0.0f, 2.0e-3f);
    CG_EXPECT_NEAR(pixel.b, 0.0f, 2.0e-3f);
}

CG_TEST(software_image_load_rejects_bad_input)
{
    HeapAllocator heap;

    // Zero dimensions are rejected.
    const char emptyPpm[] = "P6\n0 0\n255\n";
    CG_EXPECT_FALSE(
        static_cast<bool>(ReadImageFromMemory(BytesOf(emptyPpm, sizeof(emptyPpm) - 1), heap)));

    // A truncated payload is rejected instead of reading out of bounds.
    const char truncated[] = "P6\n4 4\n255\nAB";
    CG_EXPECT_FALSE(
        static_cast<bool>(ReadImageFromMemory(BytesOf(truncated, sizeof(truncated) - 1), heap)));

    // A header that is not an image is rejected.
    const char garbage[] = "this is not an image at all";
    CG_EXPECT_FALSE(
        static_cast<bool>(ReadImageFromMemory(BytesOf(garbage, sizeof(garbage) - 1), heap)));

    // A compressed TGA is unsupported.
    u8 compressed[19] = {};
    compressed[2] = 10;  // Run-length encoded true color.
    compressed[12] = 1;
    compressed[14] = 1;
    compressed[16] = 24;
    CG_EXPECT_FALSE(static_cast<bool>(ReadImageFromMemory(
        Span<const Byte>(reinterpret_cast<const Byte*>(compressed), sizeof(compressed)), heap)));
}

CG_TEST(software_image_load_round_trip)
{
    HeapAllocator heap;
    FramebufferDesc desc{};
    desc.width = 4;
    desc.height = 4;
    desc.format = PixelFormat::RGBA32F;
    auto created = Framebuffer::Create(desc, heap);
    CG_REQUIRE(static_cast<bool>(created));
    Framebuffer source = std::move(created.Value());
    source.Clear(Color{0.25f, 0.5f, 0.75f, 1.0f});

    ImageWriteDesc writeDesc{};
    writeDesc.format = ImageFormat::Ppm;
    writeDesc.encodeSrgb = true;
    auto encoded = EncodeImage(source, writeDesc, heap);
    CG_REQUIRE(static_cast<bool>(encoded));

    DynamicArray<Byte>& bytes = encoded.Value();
    auto decoded = ReadImageFromMemory(Span<const Byte>(bytes.Data(), bytes.Size()), heap);
    CG_REQUIRE(static_cast<bool>(decoded));
    Framebuffer restored = std::move(decoded.Value());
    CG_EXPECT_EQ(restored.Width(), 4);
    CG_EXPECT_EQ(restored.Height(), 4);

    const Color pixel = restored.GetPixel(2, 2);
    CG_EXPECT_NEAR(pixel.r, 0.25f, 5.0e-3f);
    CG_EXPECT_NEAR(pixel.g, 0.5f, 5.0e-3f);
    CG_EXPECT_NEAR(pixel.b, 0.75f, 5.0e-3f);
}
