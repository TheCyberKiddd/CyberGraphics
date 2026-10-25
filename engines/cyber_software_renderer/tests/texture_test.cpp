#include <cyber/memory/heap_allocator.h>
#include <cyber/software/texture.h>
#include <cyber/test/test.h>

#include <cmath>
#include <utility>

using namespace cyber::software;
using namespace cyber::memory;
using cyber::core::f32;
using cyber::core::i32;
using cyber::core::u32;
using cyber::math::Color;

namespace
{

bool MakeTexture(HeapAllocator& heap, i32 width, i32 height, TextureFormat format, bool mips,
                 Texture& out)
{
    TextureDesc desc{};
    desc.width = width;
    desc.height = height;
    desc.format = format;
    desc.generateMips = mips;
    auto created = Texture::Create(desc, heap);
    if (!created)
    {
        return false;
    }
    out = std::move(created.Value());
    return true;
}

const Color kRed{1.0f, 0.0f, 0.0f, 1.0f};
const Color kGreen{0.0f, 1.0f, 0.0f, 1.0f};
const Color kBlue{0.0f, 0.0f, 1.0f, 1.0f};
const Color kWhite{1.0f, 1.0f, 1.0f, 1.0f};

void FillCheckerboard(Texture& texture)
{
    texture.SetTexel(0, 0, 0, kRed);
    texture.SetTexel(0, 1, 0, kGreen);
    texture.SetTexel(0, 0, 1, kBlue);
    texture.SetTexel(0, 1, 1, kWhite);
}

}  // namespace

CG_TEST(software_texture_descriptor_and_mips)
{
    HeapAllocator heap;

    TextureDesc invalid{};
    invalid.width = 0;
    invalid.height = 4;
    CG_EXPECT_FALSE(static_cast<bool>(Texture::Create(invalid, heap)));

    invalid.width = 4;
    invalid.format = static_cast<TextureFormat>(99);
    CG_EXPECT_FALSE(static_cast<bool>(Texture::Create(invalid, heap)));

    Texture texture;
    CG_REQUIRE(MakeTexture(heap, 4, 4, TextureFormat::RGBA8Unorm, true, texture));
    CG_EXPECT_TRUE(texture.IsValid());
    CG_EXPECT_EQ(texture.MipLevels(), u32{3});
    CG_EXPECT_EQ(texture.MipWidth(0), 4);
    CG_EXPECT_EQ(texture.MipWidth(1), 2);
    CG_EXPECT_EQ(texture.MipWidth(2), 1);
    CG_EXPECT_EQ(texture.MipHeight(2), 1);

    Texture flat;
    CG_REQUIRE(MakeTexture(heap, 4, 2, TextureFormat::RGBA8Unorm, false, flat));
    CG_EXPECT_EQ(flat.MipLevels(), u32{1});
    CG_EXPECT_EQ(flat.MipWidth(0), 4);
    CG_EXPECT_EQ(flat.MipHeight(0), 2);
}

CG_TEST(software_texture_texel_round_trip)
{
    HeapAllocator heap;
    Texture texture;
    CG_REQUIRE(MakeTexture(heap, 2, 2, TextureFormat::RGBA32F, false, texture));

    texture.SetTexel(0, 0, 0, Color{0.25f, 0.5f, 0.75f, 0.5f});
    const Color stored = texture.GetTexel(0, 0, 0);
    CG_EXPECT_NEAR(stored.r, 0.25f, 1.0e-6f);
    CG_EXPECT_NEAR(stored.g, 0.5f, 1.0e-6f);
    CG_EXPECT_NEAR(stored.b, 0.75f, 1.0e-6f);
    CG_EXPECT_NEAR(stored.a, 0.5f, 1.0e-6f);

    // Out-of-range writes are ignored and reads return transparent black.
    texture.SetTexel(0, 5, 0, kRed);
    // Store the colour first: binding a reference to a member of a returned
    // temporary would dangle after the full expression.
    const Color outOfBounds = texture.GetTexel(0, 5, 0);
    const Color outOfLevel = texture.GetTexel(9, 0, 0);
    CG_EXPECT_EQ(outOfBounds.r, 0.0f);
    CG_EXPECT_EQ(outOfLevel.r, 0.0f);

    // RGBA8 quantizes to unorm.
    Texture unorm;
    CG_REQUIRE(MakeTexture(heap, 1, 1, TextureFormat::RGBA8Unorm, false, unorm));
    unorm.SetTexel(0, 0, 0, Color{0.5f, 1.0f, 0.0f, 1.0f});
    CG_EXPECT_NEAR(unorm.GetTexel(0, 0, 0).r, 128.0f / 255.0f, 1.0e-6f);
}

CG_TEST(software_texture_upload_and_mip_average)
{
    HeapAllocator heap;
    Texture texture;
    CG_REQUIRE(MakeTexture(heap, 2, 2, TextureFormat::RGBA32F, true, texture));
    const Color colors[4] = {kRed, kGreen, kBlue, kWhite};
    CG_REQUIRE(texture.Upload(Span<const Color>(colors, 4)));

    const Color average = texture.GetTexel(1, 0, 0);
    CG_EXPECT_NEAR(average.r, 0.5f, 1.0e-5f);
    CG_EXPECT_NEAR(average.g, 0.5f, 1.0e-5f);
    CG_EXPECT_NEAR(average.b, 0.5f, 1.0e-5f);

    // A wrong span size leaves the texture untouched.
    const Color shortUpload[2] = {kRed, kGreen};
    CG_EXPECT_FALSE(texture.Upload(Span<const Color>(shortUpload, 2)));
}

CG_TEST(software_texture_wrap_modes)
{
    HeapAllocator heap;
    Texture texture;
    CG_REQUIRE(MakeTexture(heap, 2, 2, TextureFormat::RGBA32F, false, texture));
    FillCheckerboard(texture);

    SamplerDesc nearest{};
    nearest.magFilter = FilterMode::Nearest;
    nearest.minFilter = FilterMode::Nearest;

    // Repeat: 1.25 maps to 0.25.
    SamplerDesc repeat = nearest;
    repeat.addressU = AddressMode::Repeat;
    const Color repeated = SampleTexture(texture, 1.25f, 0.25f, repeat);
    const Color direct = SampleTexture(texture, 0.25f, 0.25f, repeat);
    CG_EXPECT_NEAR(repeated.r, direct.r, 1.0e-5f);
    CG_EXPECT_NEAR(repeated.g, direct.g, 1.0e-5f);

    // Clamp to edge: a negative coordinate reads the first texel.
    SamplerDesc clamp = nearest;
    clamp.addressU = AddressMode::ClampToEdge;
    clamp.addressV = AddressMode::ClampToEdge;
    const Color clamped = SampleTexture(texture, -3.0f, -3.0f, clamp);
    CG_EXPECT_NEAR(clamped.r, kRed.r, 1.0e-5f);
    CG_EXPECT_NEAR(clamped.g, kRed.g, 1.0e-5f);

    // Mirrored repeat: 1.25 folds to 0.75, which is the right column.
    SamplerDesc mirrored = nearest;
    mirrored.addressU = AddressMode::MirroredRepeat;
    const Color mirroredColor = SampleTexture(texture, 1.25f, 0.25f, mirrored);
    CG_EXPECT_NEAR(mirroredColor.r, kGreen.r, 1.0e-5f);
    CG_EXPECT_NEAR(mirroredColor.g, kGreen.g, 1.0e-5f);
}

CG_TEST(software_texture_filtering)
{
    HeapAllocator heap;
    Texture texture;
    CG_REQUIRE(MakeTexture(heap, 2, 2, TextureFormat::RGBA32F, false, texture));
    FillCheckerboard(texture);

    SamplerDesc bilinear{};
    bilinear.addressU = AddressMode::ClampToEdge;
    bilinear.addressV = AddressMode::ClampToEdge;
    bilinear.magFilter = FilterMode::Bilinear;
    bilinear.minFilter = FilterMode::Bilinear;

    // The centre of a 2x2 averages all four texels.
    const Color center = SampleTexture(texture, 0.5f, 0.5f, bilinear);
    CG_EXPECT_NEAR(center.r, 0.5f, 1.0e-5f);
    CG_EXPECT_NEAR(center.g, 0.5f, 1.0e-5f);
    CG_EXPECT_NEAR(center.b, 0.5f, 1.0e-5f);

    SamplerDesc nearest = bilinear;
    nearest.magFilter = FilterMode::Nearest;
    const Color nearestCenter = SampleTexture(texture, 0.25f, 0.25f, nearest);
    CG_EXPECT_NEAR(nearestCenter.r, kRed.r, 1.0e-5f);

    // A NaN coordinate saturates to the first texel instead of reading garbage.
    const Color nanColor = SampleTexture(texture, std::nanf(""), 0.0f, nearest);
    CG_EXPECT_TRUE(std::isfinite(nanColor.r));
}

CG_TEST(software_texture_trilinear)
{
    HeapAllocator heap;
    Texture texture;
    CG_REQUIRE(MakeTexture(heap, 4, 4, TextureFormat::RGBA32F, true, texture));
    for (i32 y = 0; y < 4; ++y)
    {
        for (i32 x = 0; x < 4; ++x)
        {
            texture.SetTexel(0, x, y, kRed);
        }
    }
    // Make every texel of level 1 green so a sample at the level's centre is
    // exactly green.
    for (i32 y = 0; y < texture.MipHeight(1); ++y)
    {
        for (i32 x = 0; x < texture.MipWidth(1); ++x)
        {
            texture.SetTexel(1, x, y, kGreen);
        }
    }

    SamplerDesc trilinear{};
    trilinear.addressU = AddressMode::ClampToEdge;
    trilinear.addressV = AddressMode::ClampToEdge;
    trilinear.mipFilter = FilterMode::Bilinear;
    trilinear.magFilter = FilterMode::Bilinear;
    trilinear.minFilter = FilterMode::Bilinear;

    const Color levelZero = SampleTextureLevel(texture, 0.5f, 0.5f, 0.0f, trilinear);
    CG_EXPECT_NEAR(levelZero.r, 1.0f, 1.0e-5f);
    CG_EXPECT_NEAR(levelZero.g, 0.0f, 1.0e-5f);

    const Color levelOne = SampleTextureLevel(texture, 0.5f, 0.5f, 1.0f, trilinear);
    CG_EXPECT_NEAR(levelOne.g, 1.0f, 1.0e-5f);

    const Color halfway = SampleTextureLevel(texture, 0.5f, 0.5f, 0.5f, trilinear);
    CG_EXPECT_NEAR(halfway.r, 0.5f, 1.0e-4f);
    CG_EXPECT_NEAR(halfway.g, 0.5f, 1.0e-4f);

    // A nearest mip filter snaps to the floor level.
    SamplerDesc nearestMip = trilinear;
    nearestMip.mipFilter = FilterMode::Nearest;
    const Color snapped = SampleTextureLevel(texture, 0.5f, 0.5f, 0.75f, nearestMip);
    CG_EXPECT_NEAR(snapped.g, 0.0f, 1.0e-5f);
}

CG_TEST(software_texture_allocator_balance)
{
    HeapAllocator heap;
    {
        TextureDesc desc{};
        desc.width = 8;
        desc.height = 8;
        auto created = Texture::Create(desc, heap);
        CG_REQUIRE(static_cast<bool>(created));
        Texture texture = std::move(created.Value());
        CG_EXPECT_TRUE(texture.DataSizeBytes() > 0);
        texture.SetTexel(0, 0, 0, kRed);
        Texture moved = std::move(texture);
        CG_EXPECT_TRUE(moved.IsValid());
        CG_EXPECT_FALSE(texture.IsValid());
    }
    CG_EXPECT_EQ(heap.Stats().used, cyber::core::usize{0});
}
