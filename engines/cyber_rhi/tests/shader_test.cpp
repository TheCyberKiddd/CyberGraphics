#include <cyber/memory/heap_allocator.h>
#include <cyber/memory/span.h>
#include <cyber/rhi/backend.h>
#include <cyber/rhi/device.h>
#include <cyber/rhi/shader.h>
#include <cyber/test/test.h>

#include <cstring>
#include <utility>

using namespace cyber::rhi;
using cyber::core::ErrorCode;
using cyber::core::u64;
using cyber::memory::Byte;
using cyber::memory::HeapAllocator;
using cyber::memory::Span;

namespace
{

bool OpenDevice(Device& out, HeapAllocator& heap) noexcept
{
    DeviceDesc desc{};
    desc.backend = NullBackend();
    auto created = Device::Create(desc, heap);
    if (!created)
    {
        return false;
    }
    out = std::move(created.Value());
    return true;
}

constexpr const char kVertexSource[] =
    "float4 main(float3 position : TEXCOORD0) : SV_Position { return float4(position, 1); }";
constexpr const char kPixelSource[] = "float4 main() : SV_Target { return float4(1, 0, 0, 1); }";

Span<const Byte> AsBytes(const char* text) noexcept
{
    return Span<const Byte>(reinterpret_cast<const Byte*>(text), std::strlen(text));
}

ShaderDesc MakeShader(ShaderStage stage, const char* source) noexcept
{
    ShaderDesc desc{};
    desc.stage = stage;
    desc.format = ShaderBytecodeFormat::HlslSource;
    desc.code = AsBytes(source);
    desc.entryPoint = "main";
    desc.label = "test shader";
    return desc;
}

}  // namespace

CG_TEST(rhi_shader_create_and_info)
{
    HeapAllocator heap;
    {
        Device device;
        CG_REQUIRE(OpenDevice(device, heap));

        auto created = device.CreateShader(MakeShader(ShaderStage::Vertex, kVertexSource));
        CG_REQUIRE(static_cast<bool>(created));
        const ShaderHandle shader = created.Value();

        const ShaderInfo info = device.ShaderInfoOf(shader);
        CG_EXPECT_TRUE(info.valid);
        CG_EXPECT_EQ(info.stage, ShaderStage::Vertex);
        CG_EXPECT_EQ(info.format, ShaderBytecodeFormat::HlslSource);
        CG_EXPECT_EQ(info.codeSize, u64{std::strlen(kVertexSource)});

        // A binary format needs no entry point.
        ShaderDesc binary{};
        binary.stage = ShaderStage::Compute;
        binary.format = ShaderBytecodeFormat::SpirV;
        const Byte spirv[8] = {static_cast<Byte>(3), static_cast<Byte>(2), static_cast<Byte>(0x23),
                               static_cast<Byte>(0x07)};
        binary.code = Span<const Byte>(spirv, 8);
        auto compiled = device.CreateShader(binary);
        CG_EXPECT_TRUE(static_cast<bool>(compiled));
        if (compiled)
        {
            CG_EXPECT_EQ(device.ShaderInfoOf(compiled.Value()).codeSize, u64{8});
        }

        CG_EXPECT_TRUE(device.DestroyShader(shader));
        CG_EXPECT_FALSE(device.ShaderInfoOf(shader).valid);
        CG_EXPECT_FALSE(device.DestroyShader(shader));
    }
    CG_EXPECT_EQ(heap.Stats().used, cyber::core::usize{0});
}

CG_TEST(rhi_shader_invalid_descriptors)
{
    HeapAllocator heap;
    Device device;
    CG_REQUIRE(OpenDevice(device, heap));

    ShaderDesc desc = MakeShader(ShaderStage::Vertex, kVertexSource);
    desc.stage = ShaderStage::Count;
    auto created = device.CreateShader(desc);
    CG_EXPECT_FALSE(created);
    CG_EXPECT_EQ(created.Error().code, ErrorCode::InvalidArgument);

    desc = MakeShader(ShaderStage::Vertex, kVertexSource);
    desc.format = ShaderBytecodeFormat::Count;
    created = device.CreateShader(desc);
    CG_EXPECT_FALSE(created);
    CG_EXPECT_EQ(created.Error().code, ErrorCode::InvalidArgument);

    // An empty blob has nothing to compile.
    desc = MakeShader(ShaderStage::Vertex, kVertexSource);
    desc.code = Span<const Byte>();
    created = device.CreateShader(desc);
    CG_EXPECT_FALSE(created);
    CG_EXPECT_EQ(created.Error().code, ErrorCode::InvalidArgument);

    desc = MakeShader(ShaderStage::Vertex, kVertexSource);
    desc.code = Span<const Byte>(nullptr, 4);
    created = device.CreateShader(desc);
    CG_EXPECT_FALSE(created);
    CG_EXPECT_EQ(created.Error().code, ErrorCode::InvalidArgument);

    // A source format needs an entry point; an empty string is not one.
    desc = MakeShader(ShaderStage::Vertex, kVertexSource);
    desc.entryPoint = nullptr;
    created = device.CreateShader(desc);
    CG_EXPECT_FALSE(created);
    CG_EXPECT_EQ(created.Error().code, ErrorCode::InvalidArgument);

    desc.entryPoint = "";
    created = device.CreateShader(desc);
    CG_EXPECT_FALSE(created);
    CG_EXPECT_EQ(created.Error().code, ErrorCode::InvalidArgument);
}

CG_TEST(rhi_shader_handles_do_not_alias)
{
    HeapAllocator heap;
    Device device;
    CG_REQUIRE(OpenDevice(device, heap));

    auto first = device.CreateShader(MakeShader(ShaderStage::Pixel, kPixelSource));
    CG_REQUIRE(static_cast<bool>(first));
    const ShaderHandle oldHandle = first.Value();
    CG_EXPECT_TRUE(device.DestroyShader(oldHandle));

    auto second = device.CreateShader(MakeShader(ShaderStage::Pixel, kPixelSource));
    CG_REQUIRE(static_cast<bool>(second));
    CG_EXPECT_EQ(oldHandle.index, second.Value().index);
    CG_EXPECT_NE(oldHandle.generation, second.Value().generation);
    CG_EXPECT_FALSE(device.ShaderInfoOf(oldHandle).valid);
    CG_EXPECT_TRUE(device.ShaderInfoOf(second.Value()).valid);
    CG_EXPECT_FALSE(device.DestroyShader(oldHandle));
}
