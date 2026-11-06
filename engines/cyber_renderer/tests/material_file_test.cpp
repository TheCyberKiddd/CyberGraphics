#include <cyber/renderer/material.h>
#include <cyber/test/test.h>

#include <array>
#include <cstring>

using cyber::core::ErrorCode;
using cyber::core::u32;
using cyber::memory::Byte;
using cyber::memory::Span;
using cyber::memory::StringView;
using cyber::renderer::MaterialColorSpace;
using cyber::renderer::MaterialFile;
using cyber::renderer::MaterialQueue;
using cyber::renderer::MaterialValue;
using cyber::renderer::ParseMaterialFile;
using cyber::renderer::WriteMaterialFile;
using cyber::shaders::ValueType;

namespace
{

constexpr char kMaterialText[] =
    "# authored definition\n"
    "CYBERMAT 1\n"
    "name \"PaintedMetal\"\n"
    "model metallic_roughness\n"
    "shader \"shaders/metal_vertex.cgs\" \"shaders/metal_pixel.cgs\"\n"
    "queue opaque\n"
    "state cull back\n"
    "state blend off\n"
    "state depth_test on\n"
    "state depth_write on\n"
    "parameter \"baseColorFactor\" float4 default 1 1 1 1 range 0 0 0 0 1 1 1 1\n"
    "parameter \"roughnessFactor\" float default 1 range 0 1\n"
    "parameter \"alphaCutoff\" float default 0.5 range 0 2\n"
    "texture \"baseColorTexture\" default \"textures/paint\\\"base.png\" color_space srgb\n"
    "texture \"metallicRoughnessTexture\" default none color_space linear\n"
    "end\n";

}  // namespace

CG_TEST(renderer_material_file_parses_and_round_trips)
{
    auto parsed = ParseMaterialFile(StringView(kMaterialText, sizeof(kMaterialText) - 1));
    CG_REQUIRE(static_cast<bool>(parsed));
    const MaterialFile& file = parsed.Value();
    CG_EXPECT_EQ(StringView(file.name.data()), StringView("PaintedMetal"));
    CG_EXPECT_EQ(file.queue, MaterialQueue::Opaque);
    CG_EXPECT_EQ(file.parameterCount, u32{3});
    CG_EXPECT_EQ(file.textureSlotCount, u32{2});
    CG_EXPECT_EQ(file.parameters[0].defaultValue.type, ValueType::Float4);
    CG_EXPECT_EQ(file.parameters[1].defaultValue.components[0], 1.0F);
    CG_EXPECT_EQ(file.parameters[2].defaultValue.components[0], 0.5F);
    CG_EXPECT_EQ(file.textureSlots[0].colorSpace, MaterialColorSpace::Srgb);
    CG_EXPECT_EQ(
        StringView(file.textureSlots[0].defaultUri.data(), file.textureSlots[0].defaultUriBytes),
        StringView("textures/paint\"base.png"));

    std::array<Byte, cyber::renderer::kMaxMaterialFileBytes> output{};
    auto written = WriteMaterialFile(file, Span<Byte>(output.data(), output.size()));
    CG_REQUIRE(static_cast<bool>(written));
    const auto* text = reinterpret_cast<const char*>(output.data());
    auto reparsed = ParseMaterialFile(StringView(text, written.Value()));
    CG_REQUIRE(static_cast<bool>(reparsed));
    CG_EXPECT_EQ(StringView(reparsed.Value().name.data()), StringView(file.name.data()));
    CG_EXPECT_EQ(reparsed.Value().parameterCount, file.parameterCount);
    CG_EXPECT_EQ(reparsed.Value().textureSlotCount, file.textureSlotCount);
    CG_EXPECT_EQ(reparsed.Value().parameters[0].defaultValue.components[2],
                 file.parameters[0].defaultValue.components[2]);
    CG_EXPECT_EQ(reparsed.Value().textureSlots[0].colorSpace, file.textureSlots[0].colorSpace);
}

CG_TEST(renderer_material_file_reports_malformed_records)
{
    constexpr const char* kInvalid[] = {
        "CYBERMAT 2\nname \"x\"\nmodel metallic_roughness\nshader \"v\" \"p\"\nqueue opaque\nend\n",
        "CYBERMAT 1\nname \"x\"\nmodel metallic_roughness\nshader \"v\" \"p\"\nqueue opaque\n"
        "parameter \"roughnessFactor\" float default nan range 0 1\nend\n",
        "CYBERMAT 1\nname \"x\"\nmodel metallic_roughness\nshader \"v\" \"p\"\nqueue opaque\n"
        "texture \"baseColorTexture\" default \"x\" color_space linear\nend\n",
        "CYBERMAT 1\nname \"x\"\nmodel metallic_roughness\nshader \"v\" \"p\"\nqueue opaque\n"
        "end\nname \"later\"\n",
        "CYBERMAT 1\nname \"x\"\nmodel metallic_roughness\nshader \"v\" \"p\"\nqueue opaque\n"
        "parameter \"roughnessFactor\" float default 1 range 0 1\n"
        "parameter \"roughnessFactor\" float default 1 range 0 1\nend\n",
        "CYBERMAT 1\nname \"x\\q\"\nmodel metallic_roughness\nshader \"v\" \"p\"\nqueue "
        "opaque\nend\n",
        "CYBERMAT 1\nname \"x\"\nmodel metallic_roughness\nshader \"v\" \"p\"\nqueue opaque\n"
        "parameter \"baseColorFactor\" float4 default 1 1 1 1 range 0 0 0 0 2 2 2 2\nend\n",
        "CYBERMAT 1\nname \"x\"\nmodel metallic_roughness\nshader \"v\" \"p\"\nqueue opaque\n"
        "parameter \"baseColorFactor\" float4 default 0.5 1 1 1 range 0 0 0 0 1 1 1 1\nend\n",
    };
    for (const char* text : kInvalid)
    {
        auto parsed = ParseMaterialFile(StringView(text));
        CG_REQUIRE(!parsed);
        CG_EXPECT_TRUE(parsed.Error().line > 0);
        CG_EXPECT_TRUE(parsed.Error().column > 0);
    }

    auto truncated = ParseMaterialFile(StringView("CYBERMAT 1\nname \"cut\""));
    CG_REQUIRE(!truncated);
    CG_EXPECT_EQ(truncated.Error().line, u32{2});

    std::array<char, cyber::renderer::kMaxMaterialFileRecordBytes + 1> longLine{};
    longLine.fill('x');
    auto overlongRecord = ParseMaterialFile(StringView(longLine.data(), longLine.size()));
    CG_REQUIRE(!overlongRecord);
    CG_EXPECT_EQ(overlongRecord.Error().error.code, ErrorCode::OutOfRange);
}

CG_TEST(renderer_material_file_enforces_bounds_and_output_capacity)
{
    std::array<char, cyber::renderer::kMaxMaterialFileBytes + 1> oversized{};
    oversized.fill(' ');
    auto tooLarge = ParseMaterialFile(StringView(oversized.data(), oversized.size()));
    CG_REQUIRE(!tooLarge);
    CG_EXPECT_EQ(tooLarge.Error().error.code, ErrorCode::OutOfRange);

    auto parsed = ParseMaterialFile(StringView(kMaterialText, sizeof(kMaterialText) - 1));
    CG_REQUIRE(static_cast<bool>(parsed));
    std::array<Byte, 1> tiny{};
    auto write = WriteMaterialFile(parsed.Value(), Span<Byte>(tiny.data(), tiny.size()));
    CG_REQUIRE(!write);
    CG_EXPECT_EQ(write.Error().code, ErrorCode::OutOfRange);

    MaterialFile invalid = parsed.Value();
    invalid.parameters[0].maximum.components[0] = -1.0F;
    std::array<Byte, cyber::renderer::kMaxMaterialFileBytes> output{};
    auto invalidWrite = WriteMaterialFile(invalid, Span<Byte>(output.data(), output.size()));
    CG_REQUIRE(!invalidWrite);
    CG_EXPECT_EQ(invalidWrite.Error().code, ErrorCode::InvalidArgument);

    MaterialFile unterminated = parsed.Value();
    unterminated.name.fill('x');
    auto unsafeName = WriteMaterialFile(unterminated, Span<Byte>(output.data(), output.size()));
    CG_REQUIRE(!unsafeName);
    CG_EXPECT_EQ(unsafeName.Error().code, ErrorCode::InvalidArgument);

    unterminated = parsed.Value();
    unterminated.textureSlots[0].defaultUriBytes = cyber::renderer::kMaxMaterialUriBytes + 1;
    auto unsafeUri = WriteMaterialFile(unterminated, Span<Byte>(output.data(), output.size()));
    CG_REQUIRE(!unsafeUri);
    CG_EXPECT_EQ(unsafeUri.Error().code, ErrorCode::InvalidArgument);
}
