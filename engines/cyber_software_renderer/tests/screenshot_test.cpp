#include <cyber/memory/heap_allocator.h>
#include <cyber/platform/filesystem.h>
#include <cyber/software/debug_text.h>
#include <cyber/software/framebuffer.h>
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

// Renders the same fixed-time scene the gradient sample uses: a linear-space
// two-axis gradient, an animated checkerboard, and the frame-time overlay. The
// animation time is a parameter so two runs at the same time must match byte for
// byte.
void RenderFixedFrame(Framebuffer& target, f32 seconds)
{
    const i32 width = target.Width();
    const i32 height = target.Height();
    const i32 checkerShift = static_cast<i32>(seconds * 4.0f);

    for (i32 y = 0; y < height; ++y)
    {
        for (i32 x = 0; x < width; ++x)
        {
            const f32 u = static_cast<f32>(x) / static_cast<f32>(width - 1 > 0 ? width - 1 : 1);
            const f32 v = static_cast<f32>(y) / static_cast<f32>(height - 1 > 0 ? height - 1 : 1);
            const bool dark = (((x / 24) + (y / 24) + checkerShift) & 1) != 0;
            const f32 checker = dark ? 0.18f : 0.82f;
            target.SetPixel(
                x, y, Color{checker * u, checker * v, checker * (1.0f - 0.5f * (u + v)), 1.0f});
        }
    }

    DrawText(target, 8, 8, "16.6 MS  60.1 FPS", Color{1.0f, 1.0f, 1.0f, 1.0f}, 2);
}

bool FailsWith(const cyber::core::Status<cyber::core::Error>& status, cyber::core::ErrorCode code)
{
    return !status && status.Error().code == code;
}

bool BytesEqual(const ByteArray& bytes, const void* expected, usize expectedSize)
{
    return bytes.Size() == expectedSize && std::memcmp(bytes.Data(), expected, expectedSize) == 0;
}

}  // namespace

CG_TEST(software_screenshot_same_frame_is_byte_identical)
{
    HeapAllocator heap;

    FramebufferDesc desc{};
    desc.width = 160;
    desc.height = 90;
    auto created = Framebuffer::Create(desc, heap);
    CG_REQUIRE(static_cast<bool>(created));
    Framebuffer framebuffer = std::move(created.Value());

    RenderFixedFrame(framebuffer, 1.25f);

    for (const ImageFormat format : {ImageFormat::Ppm, ImageFormat::Tga})
    {
        ImageWriteDesc writeDesc{};
        writeDesc.format = format;
        auto first = EncodeImage(framebuffer, writeDesc, heap);
        auto second = EncodeImage(framebuffer, writeDesc, heap);
        CG_REQUIRE(static_cast<bool>(first));
        CG_REQUIRE(static_cast<bool>(second));
        CG_EXPECT_EQ(first.Value().Size(), second.Value().Size());
        CG_EXPECT_TRUE(BytesEqual(first.Value(), second.Value().Data(), second.Value().Size()));
    }
}

CG_TEST(software_screenshot_renders_camera_ready_scene)
{
    HeapAllocator heap;

    FramebufferDesc desc{};
    desc.width = 96;
    desc.height = 64;
    auto created = Framebuffer::Create(desc, heap);
    CG_REQUIRE(static_cast<bool>(created));
    Framebuffer framebuffer = std::move(created.Value());

    RenderFixedFrame(framebuffer, 0.0f);

    // Corner pixels differ: the gradient is not a flat fill.
    const Color topLeft = framebuffer.GetPixel(0, 0);
    const Color bottomRight = framebuffer.GetPixel(95, 63);
    CG_EXPECT_TRUE(topLeft.r != bottomRight.r || topLeft.g != bottomRight.g);

    // The overlay wrote opaque pixels near the top-left corner.
    bool foundOpaque = false;
    for (i32 y = 8; y < 8 + kGlyphHeight * 2 && !foundOpaque; ++y)
    {
        for (i32 x = 8; x < 8 + 18 * kGlyphAdvance * 2; ++x)
        {
            if (framebuffer.GetPixel(x, y).r > 0.99f)
            {
                foundOpaque = true;
                break;
            }
        }
    }
    CG_EXPECT_TRUE(foundOpaque);
}

CG_TEST(software_screenshot_files_read_back_identically)
{
    HeapAllocator heap;

    FramebufferDesc desc{};
    desc.width = 32;
    desc.height = 24;
    auto created = Framebuffer::Create(desc, heap);
    CG_REQUIRE(static_cast<bool>(created));
    Framebuffer framebuffer = std::move(created.Value());
    RenderFixedFrame(framebuffer, 2.0f);

    ImageWriteDesc ppmDesc{};
    ppmDesc.format = ImageFormat::Ppm;
    auto expectedPpm = EncodeImage(framebuffer, ppmDesc, heap);
    CG_REQUIRE(static_cast<bool>(expectedPpm));
    CG_REQUIRE(static_cast<bool>(WriteImage("cyber_phase6_frame.ppm", framebuffer, heap, ppmDesc)));
    auto readPpm = cyber::platform::ReadFile("cyber_phase6_frame.ppm", heap);
    CG_REQUIRE(static_cast<bool>(readPpm));
    CG_EXPECT_TRUE(
        BytesEqual(readPpm.Value(), expectedPpm.Value().Data(), expectedPpm.Value().Size()));

    ImageWriteDesc tgaDesc{};
    tgaDesc.format = ImageFormat::Tga;
    auto expectedTga = EncodeImage(framebuffer, tgaDesc, heap);
    CG_REQUIRE(static_cast<bool>(expectedTga));
    CG_REQUIRE(static_cast<bool>(WriteImage("cyber_phase6_frame.tga", framebuffer, heap, tgaDesc)));
    auto readTga = cyber::platform::ReadFile("cyber_phase6_frame.tga", heap);
    CG_REQUIRE(static_cast<bool>(readTga));
    CG_EXPECT_TRUE(
        BytesEqual(readTga.Value(), expectedTga.Value().Data(), expectedTga.Value().Size()));

    (void)cyber::platform::DeleteFile("cyber_phase6_frame.ppm");
    (void)cyber::platform::DeleteFile("cyber_phase6_frame.tga");
}

CG_TEST(software_screenshot_rejects_invalid_scene)
{
    HeapAllocator heap;

    FramebufferDesc desc{};
    desc.width = 8;
    desc.height = 8;
    auto created = Framebuffer::Create(desc, heap);
    CG_REQUIRE(static_cast<bool>(created));
    Framebuffer framebuffer = std::move(created.Value());

    // An unknown format is rejected before any file is touched.
    ImageWriteDesc unknown{};
    unknown.format = static_cast<ImageFormat>(9);
    auto encoded = EncodeImage(framebuffer, unknown, heap);
    CG_EXPECT_FALSE(static_cast<bool>(encoded));

    auto badPath = WriteImage("", framebuffer, heap);
    CG_EXPECT_TRUE(FailsWith(badPath, cyber::core::ErrorCode::InvalidArgument));
}
