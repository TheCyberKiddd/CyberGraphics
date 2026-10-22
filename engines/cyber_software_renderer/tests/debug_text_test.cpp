#include <cyber/memory/heap_allocator.h>
#include <cyber/memory/string_view.h>
#include <cyber/software/debug_text.h>
#include <cyber/test/test.h>

#include <utility>

using namespace cyber::software;
using namespace cyber::memory;
using cyber::math::Color;

namespace
{

constexpr Color kBlack{0.0f, 0.0f, 0.0f, 1.0f};
constexpr Color kWhite{1.0f, 1.0f, 1.0f, 1.0f};

i32 CountLitPixels(const Framebuffer& framebuffer)
{
    i32 count = 0;
    for (i32 y = 0; y < framebuffer.Height(); ++y)
    {
        for (i32 x = 0; x < framebuffer.Width(); ++x)
        {
            if (framebuffer.GetPixel(x, y).r > 0.5f)
            {
                ++count;
            }
        }
    }
    return count;
}

Framebuffer MakeFramebuffer(HeapAllocator& heap, i32 width, i32 height, bool& ok)
{
    FramebufferDesc desc{};
    desc.width = width;
    desc.height = height;
    auto created = Framebuffer::Create(desc, heap);
    ok = static_cast<bool>(created);
    if (!ok)
    {
        return Framebuffer{};
    }
    return Framebuffer(std::move(created.Value()));
}

}  // namespace

CG_TEST(software_text_metrics)
{
    CG_EXPECT_TRUE(IsGlyphSupported('A'));
    CG_EXPECT_TRUE(IsGlyphSupported('Z'));
    CG_EXPECT_TRUE(IsGlyphSupported('0'));
    CG_EXPECT_TRUE(IsGlyphSupported('9'));
    CG_EXPECT_TRUE(IsGlyphSupported('.'));
    CG_EXPECT_TRUE(IsGlyphSupported(':'));
    CG_EXPECT_TRUE(IsGlyphSupported('-'));
    CG_EXPECT_TRUE(IsGlyphSupported(' '));
    CG_EXPECT_FALSE(IsGlyphSupported('a'));
    CG_EXPECT_FALSE(IsGlyphSupported('#'));
    CG_EXPECT_FALSE(IsGlyphSupported('\n'));

    const TextMetrics empty = MeasureText(StringView());
    CG_EXPECT_EQ(empty.width, i32{0});
    CG_EXPECT_EQ(empty.height, i32{0});

    const TextMetrics one = MeasureText(StringView("A"));
    CG_EXPECT_EQ(one.width, kGlyphWidth);
    CG_EXPECT_EQ(one.height, kGlyphHeight);

    const TextMetrics two = MeasureText(StringView("AB"));
    CG_EXPECT_EQ(two.width, kGlyphAdvance + kGlyphWidth);
    CG_EXPECT_EQ(two.height, i32{7});

    const TextMetrics scaled = MeasureText(StringView("ABC"), 2);
    CG_EXPECT_EQ(scaled.width, (2 * kGlyphAdvance + kGlyphWidth) * 2);
    CG_EXPECT_EQ(scaled.height, kGlyphHeight * 2);

    // A scale below one is treated as one.
    const TextMetrics clampedScale = MeasureText(StringView("A"), 0);
    CG_EXPECT_EQ(clampedScale.width, i32{5});
    CG_EXPECT_EQ(clampedScale.height, i32{7});
}

CG_TEST(software_text_every_glyph_draws)
{
    HeapAllocator heap;
    bool ok = false;
    Framebuffer framebuffer = MakeFramebuffer(heap, 8, 9, ok);
    CG_REQUIRE(ok);
    const char* supported = " 0123456789.:-ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    for (const char* cursor = supported; *cursor != '\0'; ++cursor)
    {
        framebuffer.Clear(kBlack);
        DrawText(framebuffer, 1, 1, StringView(cursor, 1), kWhite);
        const i32 lit = CountLitPixels(framebuffer);
        if (*cursor == ' ')
        {
            CG_EXPECT_EQ(lit, i32{0});
        }
        else
        {
            CG_EXPECT_TRUE(lit > 0);
        }
    }
}

CG_TEST(software_text_draws_and_clips)
{
    HeapAllocator heap;
    bool ok = false;
    Framebuffer framebuffer = MakeFramebuffer(heap, 12, 10, ok);
    CG_REQUIRE(ok);

    framebuffer.Clear(kBlack);
    DrawText(framebuffer, 0, 0, StringView("."), Color{0.0f, 0.0f, 1.0f, 1.0f});
    const Color dot = framebuffer.GetPixel(2, 5);
    CG_EXPECT_TRUE(dot.b > 0.5f);
    CG_EXPECT_TRUE(dot.r < 0.5f);
    CG_EXPECT_TRUE(framebuffer.GetPixel(0, 0).r < 0.5f);

    // Clip at every edge; the framebuffer's own bounds checking must hold.
    framebuffer.Clear(kBlack);
    DrawText(framebuffer, -4, -4, StringView("A"), kWhite);
    DrawText(framebuffer, 20, 20, StringView("B"), kWhite);
    DrawText(framebuffer, -100, 2, StringView("C"), kWhite);
    (void)CountLitPixels(framebuffer);

    // Left column of 'A' is visible after a negative offset.
    framebuffer.Clear(kBlack);
    DrawText(framebuffer, -4, 0, StringView("A"), kWhite);
    CG_EXPECT_TRUE(CountLitPixels(framebuffer) > 0);

    // Unsupported characters leave the image untouched.
    framebuffer.Clear(kBlack);
    DrawText(framebuffer, 0, 0, StringView("hello#"), kWhite);
    CG_EXPECT_EQ(CountLitPixels(framebuffer), i32{0});
}

CG_TEST(software_text_scale_multiplies_pixels)
{
    HeapAllocator heap;
    bool ok = false;
    Framebuffer single = MakeFramebuffer(heap, 8, 9, ok);
    CG_REQUIRE(ok);
    bool doubledOk = false;
    Framebuffer doubled = MakeFramebuffer(heap, 16, 18, doubledOk);
    CG_REQUIRE(doubledOk);

    single.Clear(kBlack);
    DrawText(single, 0, 0, StringView("A"), kWhite, 1);
    doubled.Clear(kBlack);
    DrawText(doubled, 0, 0, StringView("A"), kWhite, 2);

    const i32 singleCount = CountLitPixels(single);
    const i32 doubledCount = CountLitPixels(doubled);
    CG_EXPECT_TRUE(singleCount > 0);
    CG_EXPECT_EQ(doubledCount, singleCount * 4);
}

CG_TEST(software_text_empty_and_invalid_target)
{
    HeapAllocator heap;
    bool ok = false;
    Framebuffer framebuffer = MakeFramebuffer(heap, 8, 8, ok);
    CG_REQUIRE(ok);
    framebuffer.Clear(kBlack);
    DrawText(framebuffer, 0, 0, StringView(), kWhite);
    CG_EXPECT_EQ(CountLitPixels(framebuffer), i32{0});

    Framebuffer invalid;
    DrawText(invalid, 0, 0, StringView("A"), kWhite);
    CG_EXPECT_FALSE(invalid.IsValid());
}
