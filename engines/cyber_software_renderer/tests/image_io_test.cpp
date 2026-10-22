#include <cyber/memory/heap_allocator.h>
#include <cyber/platform/filesystem.h>
#include <cyber/software/image_io.h>
#include <cyber/test/test.h>

#include <cstring>
#include <utility>

using namespace cyber::software;
using namespace cyber::memory;
using cyber::math::Color;

namespace
{

using ByteArray = DynamicArray<Byte>;

bool FailsWith(const cyber::core::Status<cyber::core::Error>& status, cyber::core::ErrorCode code)
{
    return !status && status.Error().code == code;
}

bool BytesEqual(const ByteArray& bytes, const void* expected, usize expectedSize)
{
    return bytes.Size() == expectedSize && std::memcmp(bytes.Data(), expected, expectedSize) == 0;
}

}  // namespace

CG_TEST(software_image_ppm_exact_bytes)
{
    HeapAllocator heap;

    FramebufferDesc desc{};
    desc.width = 2;
    desc.height = 1;
    auto created = Framebuffer::Create(desc, heap);
    CG_REQUIRE(static_cast<bool>(created));
    Framebuffer framebuffer = std::move(created.Value());
    framebuffer.SetPixel(0, 0, Color{1.0f, 0.0f, 0.0f, 1.0f});
    framebuffer.SetPixel(1, 0, Color{0.0f, 1.0f, 0.0f, 1.0f});

    ImageWriteDesc writeDesc{};
    writeDesc.format = ImageFormat::Ppm;
    auto encoded = EncodeImage(framebuffer, writeDesc, heap);
    CG_REQUIRE(static_cast<bool>(encoded));

    const u8 expected[] = {'P', '6',  '\n', '2', ' ', '1', '\n', '2', '5',
                           '5', '\n', 255,  0,   0,   0,   255,  0};
    CG_EXPECT_TRUE(BytesEqual(encoded.Value(), expected, sizeof(expected)));

    writeDesc.encodeSrgb = false;
    auto linear = EncodeImage(framebuffer, writeDesc, heap);
    CG_REQUIRE(static_cast<bool>(linear));
    CG_EXPECT_TRUE(BytesEqual(linear.Value(), expected, sizeof(expected)));

    framebuffer.SetPixel(0, 0, Color{0.5f, 0.5f, 0.5f, 1.0f});
    const u8 srgbGray[] = {'P', '6',  '\n', '2', ' ', '1', '\n', '2', '5',
                           '5', '\n', 188,  188, 188, 0,   255,  0};
    auto srgbEncoded = EncodeImage(framebuffer, ImageWriteDesc{}, heap);
    CG_REQUIRE(static_cast<bool>(srgbEncoded));
    CG_EXPECT_TRUE(BytesEqual(srgbEncoded.Value(), srgbGray, sizeof(srgbGray)));

    const u8 linearGray[] = {'P', '6',  '\n', '2', ' ', '1', '\n', '2', '5',
                             '5', '\n', 128,  128, 128, 0,   255,  0};
    auto linearEncoded = EncodeImage(framebuffer, writeDesc, heap);
    CG_REQUIRE(static_cast<bool>(linearEncoded));
    CG_EXPECT_TRUE(BytesEqual(linearEncoded.Value(), linearGray, sizeof(linearGray)));
}

CG_TEST(software_image_tga_exact_bytes)
{
    HeapAllocator heap;

    FramebufferDesc desc{};
    desc.width = 2;
    desc.height = 1;
    auto created = Framebuffer::Create(desc, heap);
    CG_REQUIRE(static_cast<bool>(created));
    Framebuffer framebuffer = std::move(created.Value());
    framebuffer.SetPixel(0, 0, Color{1.0f, 0.0f, 0.0f, 1.0f});
    framebuffer.SetPixel(1, 0, Color{0.0f, 1.0f, 0.0f, 1.0f});

    ImageWriteDesc writeDesc{};
    writeDesc.format = ImageFormat::Tga;
    auto encoded = EncodeImage(framebuffer, writeDesc, heap);
    CG_REQUIRE(static_cast<bool>(encoded));

    CG_EXPECT_EQ(encoded.Value().Size(), usize{18 + 6});
    const u8* bytes = reinterpret_cast<const u8*>(encoded.Value().Data());
    CG_EXPECT_EQ(bytes[0], u8{0});   // No image ID.
    CG_EXPECT_EQ(bytes[1], u8{0});   // No color map.
    CG_EXPECT_EQ(bytes[2], u8{2});   // Uncompressed true color.
    CG_EXPECT_EQ(bytes[12], u8{2});  // Width low byte.
    CG_EXPECT_EQ(bytes[13], u8{0});
    CG_EXPECT_EQ(bytes[14], u8{1});  // Height low byte.
    CG_EXPECT_EQ(bytes[15], u8{0});
    CG_EXPECT_EQ(bytes[16], u8{24});
    CG_EXPECT_EQ(bytes[17], u8{0x20});  // Top-down origin.
    CG_EXPECT_EQ(bytes[18], u8{0});     // Blue first.
    CG_EXPECT_EQ(bytes[19], u8{0});
    CG_EXPECT_EQ(bytes[20], u8{255});
    CG_EXPECT_EQ(bytes[21], u8{0});
    CG_EXPECT_EQ(bytes[22], u8{255});
    CG_EXPECT_EQ(bytes[23], u8{0});
}

CG_TEST(software_image_encoding_is_deterministic)
{
    HeapAllocator heap;

    FramebufferDesc desc{};
    desc.width = 5;
    desc.height = 3;
    desc.format = PixelFormat::RGBA32F;
    auto created = Framebuffer::Create(desc, heap);
    CG_REQUIRE(static_cast<bool>(created));
    Framebuffer framebuffer = std::move(created.Value());
    for (i32 y = 0; y < 3; ++y)
    {
        for (i32 x = 0; x < 5; ++x)
        {
            framebuffer.SetPixel(
                x, y, Color{static_cast<f32>(x) / 4.0f, static_cast<f32>(y) / 2.0f, 0.25f, 1.0f});
        }
    }

    for (const ImageFormat format : {ImageFormat::Ppm, ImageFormat::Tga})
    {
        ImageWriteDesc writeDesc{};
        writeDesc.format = format;
        auto first = EncodeImage(framebuffer, writeDesc, heap);
        auto second = EncodeImage(framebuffer, writeDesc, heap);
        CG_REQUIRE(static_cast<bool>(first));
        CG_REQUIRE(static_cast<bool>(second));
        CG_EXPECT_EQ(first.Value().Size(), second.Value().Size());
        CG_EXPECT_TRUE(
            std::memcmp(first.Value().Data(), second.Value().Data(), first.Value().Size()) == 0);
    }
}

CG_TEST(software_image_write_and_read_back)
{
    HeapAllocator heap;

    FramebufferDesc desc{};
    desc.width = 3;
    desc.height = 2;
    auto created = Framebuffer::Create(desc, heap);
    CG_REQUIRE(static_cast<bool>(created));
    Framebuffer framebuffer = std::move(created.Value());
    framebuffer.Clear(Color{0.25f, 0.5f, 0.75f, 1.0f});
    framebuffer.SetPixel(2, 1, Color{1.0f, 0.0f, 0.0f, 1.0f});

    ImageWriteDesc ppmDesc{};
    ppmDesc.format = ImageFormat::Ppm;
    auto expectedPpm = EncodeImage(framebuffer, ppmDesc, heap);
    CG_REQUIRE(static_cast<bool>(expectedPpm));

    auto writePpm = WriteImage("cyber_phase6_test.ppm", framebuffer, heap, ppmDesc);
    CG_REQUIRE(static_cast<bool>(writePpm));
    auto readPpm = cyber::platform::ReadFile("cyber_phase6_test.ppm", heap);
    CG_REQUIRE(static_cast<bool>(readPpm));
    CG_EXPECT_EQ(readPpm.Value().Size(), expectedPpm.Value().Size());
    CG_EXPECT_TRUE(std::memcmp(readPpm.Value().Data(), expectedPpm.Value().Data(),
                               expectedPpm.Value().Size()) == 0);

    ImageWriteDesc tgaDesc{};
    tgaDesc.format = ImageFormat::Tga;
    auto expectedTga = EncodeImage(framebuffer, tgaDesc, heap);
    CG_REQUIRE(static_cast<bool>(expectedTga));
    auto writeTga = WriteImage("cyber_phase6_test.tga", framebuffer, heap, tgaDesc);
    CG_REQUIRE(static_cast<bool>(writeTga));
    auto readTga = cyber::platform::ReadFile("cyber_phase6_test.tga", heap);
    CG_REQUIRE(static_cast<bool>(readTga));
    CG_EXPECT_TRUE(std::memcmp(readTga.Value().Data(), expectedTga.Value().Data(),
                               expectedTga.Value().Size()) == 0);

    (void)cyber::platform::DeleteFile("cyber_phase6_test.ppm");
    (void)cyber::platform::DeleteFile("cyber_phase6_test.tga");
    CG_EXPECT_FALSE(cyber::platform::Exists("cyber_phase6_test.ppm"));
}

CG_TEST(software_image_rejects_invalid_input)
{
    HeapAllocator heap;

    Framebuffer invalid;
    auto invalidEncode = EncodeImage(invalid, ImageWriteDesc{}, heap);
    CG_EXPECT_FALSE(static_cast<bool>(invalidEncode));
    if (!invalidEncode)
    {
        CG_EXPECT_TRUE(invalidEncode.Error().code == cyber::core::ErrorCode::InvalidArgument);
    }

    auto invalidWrite = WriteImage("cyber_phase6_invalid.ppm", invalid, heap);
    CG_EXPECT_TRUE(FailsWith(invalidWrite, cyber::core::ErrorCode::InvalidArgument));

    FramebufferDesc desc{};
    desc.width = 2;
    desc.height = 2;
    auto created = Framebuffer::Create(desc, heap);
    CG_REQUIRE(static_cast<bool>(created));
    Framebuffer framebuffer = std::move(created.Value());

    auto emptyPath = WriteImage("", framebuffer, heap);
    CG_EXPECT_TRUE(FailsWith(emptyPath, cyber::core::ErrorCode::InvalidArgument));

    ImageWriteDesc unknown{};
    unknown.format = static_cast<ImageFormat>(17);
    auto unknownEncode = EncodeImage(framebuffer, unknown, heap);
    CG_EXPECT_FALSE(static_cast<bool>(unknownEncode));

    // The parent directory does not exist, so the platform write fails.
    auto missingDirectory = WriteImage("cyber_phase6_missing_dir/shot.ppm", framebuffer, heap);
    CG_EXPECT_FALSE(static_cast<bool>(missingDirectory));
}
