#include <cyber/memory/heap_allocator.h>
#include <cyber/platform/filesystem.h>
#include <cyber/software/golden_image.h>
#include <cyber/test/test.h>

#include <utility>

using namespace cyber::software;
using namespace cyber::memory;
using cyber::core::u64;
using cyber::math::Color;

namespace
{

bool CreateSquare(HeapAllocator& heap, cyber::core::i32 size, Framebuffer& out)
{
    FramebufferDesc desc{};
    desc.width = size;
    desc.height = size;
    desc.format = PixelFormat::RGBA32F;
    auto created = Framebuffer::Create(desc, heap);
    if (!created)
    {
        return false;
    }
    out = std::move(created.Value());
    return true;
}

}  // namespace

CG_TEST(software_golden_image_compare)
{
    HeapAllocator heap;
    Framebuffer actual;
    Framebuffer expected;
    CG_REQUIRE(CreateSquare(heap, 4, actual));
    CG_REQUIRE(CreateSquare(heap, 4, expected));

    actual.Clear(Color{0.2f, 0.4f, 0.6f, 1.0f});
    expected.Clear(Color{0.2f, 0.4f, 0.6f, 1.0f});

    GoldenImageResult result = CompareImages(actual, expected);
    CG_EXPECT_EQ(result.status, GoldenImageStatus::Ok);
    CG_EXPECT_EQ(result.differingPixels, u64{0});
    CG_EXPECT_EQ(result.maxChannelDelta, 0.0f);

    // One changed pixel reports exactly one differing pixel.
    expected.SetPixel(1, 2, Color{1.0f, 0.0f, 0.0f, 1.0f});
    result = CompareImages(actual, expected);
    CG_EXPECT_EQ(result.status, GoldenImageStatus::Mismatch);
    CG_EXPECT_EQ(result.differingPixels, u64{1});
    CG_EXPECT_TRUE(result.maxChannelDelta > 2.0f);

    // A loose tolerance accepts the change.
    GoldenImageDesc loose{};
    loose.tolerance = 1.0f;
    result = CompareImages(actual, expected, loose);
    CG_EXPECT_EQ(result.status, GoldenImageStatus::Ok);

    // A dimension mismatch is invalid rather than a mismatch.
    Framebuffer other;
    CG_REQUIRE(CreateSquare(heap, 2, other));
    result = CompareImages(actual, other);
    CG_EXPECT_EQ(result.status, GoldenImageStatus::InvalidArgument);

    // The difference image is written to disk.
    const char* path = "golden_diff_test.ppm";
    CG_REQUIRE(static_cast<bool>(WriteDifferenceImage(path, actual, expected, heap)));
    CG_EXPECT_TRUE(cyber::platform::Exists(path));
    (void)cyber::platform::DeleteFile(path);

    // An invalid target is rejected.
    Framebuffer invalid;
    CG_EXPECT_EQ(CompareImages(actual, invalid).status, GoldenImageStatus::InvalidArgument);
}
