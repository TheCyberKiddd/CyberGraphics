// The Phase 14 comparison, in one process: the Direct3D 12 backend and the CPU
// reference renderer draw the same scene from the same description, and the two
// images are compared pixel by pixel.
//
// This test lives outside the modules because it is the only place that may
// link both renderers: `cyber_rhi` must not depend on the software renderer and
// the software renderer must not depend on the RHI. The scene description
// (geometry, texture, camera, and clear color) is written once, here, and handed
// to both, so the only thing the comparison can find is a difference in what the
// two rasterizers produce.

#include <cyber/math/mat4.h>
#include <cyber/math/vec2.h>
#include <cyber/math/vec3.h>
#include <cyber/math/vec4.h>
#include <cyber/memory/heap_allocator.h>
#include <cyber/memory/span.h>
#include <cyber/platform/filesystem.h>
#include <cyber/rhi/command_list.h>
#include <cyber/rhi/d3d12.h>
#include <cyber/rhi/device.h>
#include <cyber/rhi/pipeline.h>
#include <cyber/rhi/render_pass.h>
#include <cyber/rhi/sampler.h>
#include <cyber/rhi/shader.h>
#include <cyber/rhi/submission.h>
#include <cyber/rhi/texture.h>
#include <cyber/shaders/compiler.h>
#include <cyber/software/camera.h>
#include <cyber/software/depth_buffer.h>
#include <cyber/software/framebuffer.h>
#include <cyber/software/shader.h>
#include <cyber/software/texture.h>
#include <cyber/test/test.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <utility>

using cyber::core::f32;
using cyber::core::f64;
using cyber::core::i32;
using cyber::core::u32;
using cyber::core::u64;
using cyber::core::u8;
using cyber::math::Color;
using cyber::math::Mat4;
using cyber::math::Vec2;
using cyber::math::Vec3;
using cyber::math::Vec4;
using cyber::memory::Byte;
using cyber::memory::HeapAllocator;
using cyber::memory::Span;
using cyber::memory::StringView;
using namespace cyber::rhi;
namespace software = cyber::software;

namespace
{

// The size of the image the phase presents: the PPM the test writes, the PNG
// the report embeds, and the pair the comparison measures are all this size.
constexpr u32 kWidth = 512;
constexpr u32 kHeight = 512;
constexpr u32 kRowBytes = kWidth * 4;

// Supersampling is the project's antialiasing until a rasterizer grows
// multisample coverage (ADR 0012): both renderers draw the scene into a target
// `kSupersample` times larger per axis and box-filter it down, which smooths the
// silhouette and the texel boundaries at once and changes neither engine. ADR
// 0012 fixes the maximum at 4 — sixteen samples behind every output pixel.
constexpr u32 kSupersample = 4;
constexpr u32 kRenderWidth = kWidth * kSupersample;
constexpr u32 kRenderHeight = kHeight * kSupersample;
constexpr u32 kRenderRowBytes = kRenderWidth * 4;
// D3D12 requires a texture-to-buffer row pitch of at least the row's byte size
// and a multiple of 256; 8192 is both for a 2048-pixel RGBA8 row.
constexpr u32 kRowPitch = kRenderRowBytes;
constexpr u32 kReadbackBytes = kRowPitch * kRenderHeight;

constexpr u32 kTextureSize = 8;
constexpr u32 kCheckerBytes = kTextureSize * kTextureSize * 4;
// Alignment D3D12 requires between two rows of an uploaded texture.
constexpr u32 kUploadRowPitch = 256;

// The two checker colors, as the bytes both renderers end up storing. The
// reference renderer is handed these divided by 255, so the same numbers leave
// both paths whatever the rounding rule is.
constexpr u8 kLightTexel[4] = {217, 191, 140, 255};
constexpr u8 kDarkTexel[4] = {38, 46, 56, 255};

const Color kClearColor{0.05f, 0.05f, 0.08f, 1.0f};

// The recorded tolerance of the Phase 14 comparison. Two rasterizers agree
// inside every triangle and disagree where an edge falls: the software
// rasterizer snaps coverage to integer pixel centers with fixed-point
// arithmetic, the hardware does not, so silhouette and texel-boundary pixels
// may land on either side. The comparison measures the resolved images — the
// ones the phase presents — so a disagreement has to survive the box filter to
// count; a difference of one sample in sixteen shows up as a fraction of the
// edge color, not as the whole pixel. `kChannelTolerance` is the per-channel
// delta treated as equal, `kOverTolerancePercent` the share of pixels allowed
// past it, and `kMeanChannelTolerance` the average delta over the whole image.
constexpr u32 kChannelTolerance = 4;
constexpr u32 kOverTolerancePercent = 5;
constexpr f64 kMeanChannelTolerance = 6.0;

// One vertex of the cube, in the layout both renderers consume.
struct CubeVertex
{
    Vec3 position;
    Vec3 normal;
    Vec2 uv;
};

// The cube of the golden scenes: six faces, four corners each, two triangles
// per face.
u32 BuildCube(CubeVertex* out)
{
    const Vec3 corners[8] = {
        Vec3{-1.0f, -1.0f, -1.0f}, Vec3{1.0f, -1.0f, -1.0f}, Vec3{1.0f, 1.0f, -1.0f},
        Vec3{-1.0f, 1.0f, -1.0f},  Vec3{-1.0f, -1.0f, 1.0f}, Vec3{1.0f, -1.0f, 1.0f},
        Vec3{1.0f, 1.0f, 1.0f},    Vec3{-1.0f, 1.0f, 1.0f},
    };
    struct Face
    {
        Vec3 normal;
        u32 corners[4];
    };
    const Face faces[6] = {
        Face{Vec3{0.0f, 0.0f, 1.0f}, {4, 5, 6, 7}},  Face{Vec3{0.0f, 0.0f, -1.0f}, {1, 0, 3, 2}},
        Face{Vec3{-1.0f, 0.0f, 0.0f}, {0, 4, 7, 3}}, Face{Vec3{1.0f, 0.0f, 0.0f}, {5, 1, 2, 6}},
        Face{Vec3{0.0f, 1.0f, 0.0f}, {7, 6, 2, 3}},  Face{Vec3{0.0f, -1.0f, 0.0f}, {0, 1, 5, 4}},
    };
    const Vec2 uvs[4] = {Vec2{0.0f, 0.0f}, Vec2{1.0f, 0.0f}, Vec2{1.0f, 1.0f}, Vec2{0.0f, 1.0f}};

    u32 count = 0;
    for (const Face& face : faces)
    {
        const u32 order[2][3] = {{0, 1, 2}, {0, 2, 3}};
        for (const auto& triangle : order)
        {
            for (u32 corner = 0; corner < 3; ++corner)
            {
                const u32 slot = triangle[corner];
                out[count] = CubeVertex{corners[face.corners[slot]], face.normal, uvs[slot]};
                ++count;
            }
        }
    }
    return count;
}

// The checker board both renderers sample, written as the bytes that end up in
// the texture.
void BuildChecker(u8* out)
{
    for (u32 y = 0; y < kTextureSize; ++y)
    {
        for (u32 x = 0; x < kTextureSize; ++x)
        {
            const bool light = ((x / 2) + (y / 2)) % 2 == 0;
            const u8* texel = light ? kLightTexel : kDarkTexel;
            std::memcpy(out + (y * kTextureSize + x) * 4, texel, 4);
        }
    }
}

// The scene both renderers draw: an orbit camera on a cube turned by a fixed
// angle, with the checker board as the only shading.
struct Scene
{
    software::OrbitCamera camera;
    Mat4 model = Mat4::RotationY(0.4f);
    Mat4 modelViewProjection = Mat4::Identity();

    void Build() noexcept
    {
        camera.distance = 4.5f;
        camera.yawRadians = 0.6f;
        camera.pitchRadians = 0.4f;
        const f32 aspect = static_cast<f32>(kRenderWidth) / static_cast<f32>(kRenderHeight);
        modelViewProjection = software::OrbitCameraProjection(camera, aspect) *
                              software::OrbitCameraView(camera) * model;
    }
};

// What the reference renderer's cube shaders read.
struct CubeUniforms
{
    software::TransformShaderData transform;
    const software::Texture* texture = nullptr;
    software::SamplerDesc sampler;
};

void CubeVertexStage(const software::VertexInput& input, const void* userData,
                     software::VertexStageOutput& output) noexcept
{
    software::TransformVertexShader(input, &static_cast<const CubeUniforms*>(userData)->transform,
                                    output);
}

Color CubePixelStage(const software::Varyings& varyings, const void* userData) noexcept
{
    const auto* uniforms = static_cast<const CubeUniforms*>(userData);
    return software::SampleTexture(*uniforms->texture, varyings.uv.x, varyings.uv.y,
                                   uniforms->sampler);
}

// The cube drawn by the reference renderer, returned tightly packed.
bool RenderSoftwareCube(HeapAllocator& heap, u8* outPixels)
{
    software::FramebufferDesc colorDesc{};
    colorDesc.width = static_cast<i32>(kRenderWidth);
    colorDesc.height = static_cast<i32>(kRenderHeight);
    colorDesc.format = software::PixelFormat::RGBA8Unorm;
    auto color = software::Framebuffer::Create(colorDesc, heap);
    if (!color)
    {
        return false;
    }
    software::DepthBufferDesc depthDesc{};
    depthDesc.width = static_cast<i32>(kRenderWidth);
    depthDesc.height = static_cast<i32>(kRenderHeight);
    auto depth = software::DepthBuffer::Create(depthDesc, heap);
    if (!depth)
    {
        return false;
    }
    color.Value().Clear(kClearColor);
    depth.Value().Clear(0.0f);

    software::TextureDesc textureDesc{};
    textureDesc.width = static_cast<i32>(kTextureSize);
    textureDesc.height = static_cast<i32>(kTextureSize);
    textureDesc.format = software::TextureFormat::RGBA32F;
    textureDesc.generateMips = false;
    auto texture = software::Texture::Create(textureDesc, heap);
    if (!texture)
    {
        return false;
    }
    u8 checker[kCheckerBytes];
    BuildChecker(checker);
    for (u32 y = 0; y < kTextureSize; ++y)
    {
        for (u32 x = 0; x < kTextureSize; ++x)
        {
            const u8* texel = checker + (y * kTextureSize + x) * 4;
            texture.Value().SetTexel(
                0, static_cast<i32>(x), static_cast<i32>(y),
                Color{texel[0] / 255.0f, texel[1] / 255.0f, texel[2] / 255.0f, texel[3] / 255.0f});
        }
    }

    CubeVertex cube[36];
    software::VertexInput inputs[36];
    const u32 count = BuildCube(cube);
    for (u32 index = 0; index < count; ++index)
    {
        inputs[index].position = cube[index].position;
        inputs[index].normal = cube[index].normal;
        inputs[index].uv = cube[index].uv;
        inputs[index].color = Color{1.0f, 1.0f, 1.0f, 1.0f};
    }

    Scene scene;
    scene.Build();

    CubeUniforms uniforms{};
    uniforms.transform.modelViewProjection = scene.modelViewProjection;
    uniforms.transform.modelToWorld = scene.model;
    uniforms.transform.normalToWorld = scene.model;
    uniforms.texture = &texture.Value();
    uniforms.sampler.addressU = software::AddressMode::Repeat;
    uniforms.sampler.addressV = software::AddressMode::Repeat;
    uniforms.sampler.minFilter = software::FilterMode::Nearest;
    uniforms.sampler.magFilter = software::FilterMode::Nearest;
    uniforms.sampler.mipFilter = software::FilterMode::Nearest;

    software::ShaderDesc shader{};
    shader.pipeline.viewport = software::Viewport{0.0f, 0.0f, static_cast<f32>(kRenderWidth),
                                                  static_cast<f32>(kRenderHeight)};
    // Both sides draw every winding; the depth buffer decides which face wins.
    shader.pipeline.cullMode = software::CullMode::None;
    shader.vertexShader = &CubeVertexStage;
    shader.pixelShader = &CubePixelStage;
    shader.userData = &uniforms;

    if (!software::DrawShadedTriangles(shader, Span<const software::VertexInput>(inputs, count),
                                       color.Value(), depth.Value(), nullptr, heap, nullptr)
             .IsOk())
    {
        return false;
    }

    for (u32 y = 0; y < kRenderHeight; ++y)
    {
        std::memcpy(outPixels + y * kRenderRowBytes, color.Value().Row(static_cast<i32>(y)),
                    kRenderRowBytes);
    }
    return true;
}

// The triangle drawn by the reference renderer: three clip-space positions and
// one flat color, with the identity transform because the positions are already
// in clip space.
bool RenderSoftwareTriangle(HeapAllocator& heap, u8* outPixels, const Color& shading)
{
    software::FramebufferDesc colorDesc{};
    colorDesc.width = static_cast<i32>(kRenderWidth);
    colorDesc.height = static_cast<i32>(kRenderHeight);
    colorDesc.format = software::PixelFormat::RGBA8Unorm;
    auto color = software::Framebuffer::Create(colorDesc, heap);
    if (!color)
    {
        return false;
    }
    software::DepthBufferDesc depthDesc{};
    depthDesc.width = static_cast<i32>(kRenderWidth);
    depthDesc.height = static_cast<i32>(kRenderHeight);
    auto depth = software::DepthBuffer::Create(depthDesc, heap);
    if (!depth)
    {
        return false;
    }
    color.Value().Clear(kClearColor);
    depth.Value().Clear(0.0f);

    software::VertexInput vertices[3];
    vertices[0].position = Vec3{-0.9f, -0.9f, 0.5f};
    vertices[1].position = Vec3{0.9f, -0.9f, 0.5f};
    vertices[2].position = Vec3{0.0f, 0.9f, 0.5f};
    for (software::VertexInput& vertex : vertices)
    {
        vertex.color = shading;
    }

    software::TransformShaderData transform{};

    software::ShaderDesc shader{};
    shader.pipeline.viewport = software::Viewport{0.0f, 0.0f, static_cast<f32>(kRenderWidth),
                                                  static_cast<f32>(kRenderHeight)};
    shader.pipeline.cullMode = software::CullMode::None;
    shader.vertexShader = &software::TransformVertexShader;
    shader.pixelShader = [](const software::Varyings& varyings, const void*) noexcept
    { return varyings.color; };
    shader.userData = &transform;

    if (!software::DrawShadedTriangles(shader, Span<const software::VertexInput>(vertices, 3),
                                       color.Value(), depth.Value(), nullptr, heap, nullptr)
             .IsOk())
    {
        return false;
    }

    for (u32 y = 0; y < kRenderHeight; ++y)
    {
        std::memcpy(outPixels + y * kRenderRowBytes, color.Value().Row(static_cast<i32>(y)),
                    kRenderRowBytes);
    }
    return true;
}

// ---------------------------------------------------------------------------
// Direct3D 12
// ---------------------------------------------------------------------------

// The vertex format the cube pipeline declares: location 0 is the position,
// location 1 the texture coordinate, and the backend maps locations onto
// `TEXCOORD` semantics for the input assembler.
struct GpuVertex
{
    f32 position[3];
    f32 uv[2];
};
static_assert(sizeof(GpuVertex) == 20);

constexpr char kCubeVertexSource[] =
    "shader vertex Cube {\n"
    "    input float3 position @location(0);\n"
    "    input float2 uv @location(1);\n"
    "    output float4 clipPosition @builtin(position);\n"
    "    output float2 texcoord @location(0);\n"
    "    uniform float4x4 modelViewProjection @register(b0);\n"
    "    output.clipPosition = mul(modelViewProjection, float4(input.position, 1.0));\n"
    "    output.texcoord = input.uv;\n"
    "}\n";

constexpr char kCubePixelSource[] = "shader pixel Cube {\n"
                                    "    input float4 position @builtin(position);\n"
                                    "    input float2 uv @location(0);\n"
                                    "    texture2d albedo @register(t0, space0);\n"
                                    "    sampler albedoSampler @register(s0, space0);\n"
                                    "    return sample(albedo, albedoSampler, input.uv);\n"
                                    "}\n";

constexpr char kTriangleVertexSource[] = "struct Input\n"
                                         "{\n"
                                         "    float3 position : TEXCOORD0;\n"
                                         "};\n"
                                         "float4 main(Input input) : SV_Position\n"
                                         "{\n"
                                         "    return float4(input.position, 1.0);\n"
                                         "}\n";

constexpr char kTrianglePixelSource[] = "float4 main(float4 position : SV_Position) : SV_Target\n"
                                        "{\n"
                                        "    return float4(1.0, 0.5, 0.25, 1.0);\n"
                                        "}\n";

[[nodiscard]] Span<const Byte> Bytes(const char* text) noexcept
{
    return Span<const Byte>(reinterpret_cast<const Byte*>(text), std::strlen(text));
}

[[nodiscard]] Span<const Byte> Bytes(StringView text) noexcept
{
    return Span<const Byte>(reinterpret_cast<const Byte*>(text.Data()), text.Size());
}

[[nodiscard]] bool OpenD3D12Device(Device& out, HeapAllocator& heap) noexcept
{
    const Backend* backend = D3D12Backend();
    if (backend == nullptr)
    {
        return false;
    }
    const u32 count = backend->enumerateAdapters(nullptr, 0);
    if (count == 0)
    {
        return false;
    }
    DeviceDesc desc{};
    desc.backend = backend;
    // The last adapter is WARP, so the image is the same on every machine.
    desc.adapterIndex = count - 1;
    auto created = Device::Create(desc, heap);
    if (!created)
    {
        return false;
    }
    out = std::move(created.Value());
    return true;
}

struct Target
{
    TextureHandle texture;
    TextureViewHandle view;
};

[[nodiscard]] bool MakeTarget(Device& device, Format format, TextureUsage usage, Target& out)
{
    TextureDesc desc{};
    desc.format = format;
    desc.usage = usage;
    desc.width = kRenderWidth;
    desc.height = kRenderHeight;
    auto created = device.CreateTexture(desc);
    if (!created)
    {
        return false;
    }
    TextureViewDesc viewDesc{};
    viewDesc.texture = created.Value();
    viewDesc.format = format;
    // A depth-stencil target exposes its depth aspect, and the view rule
    // requires the description to name it.
    if (HasUsage(usage, TextureUsage::DepthStencil))
    {
        viewDesc.range.aspect = TextureAspect::Depth;
    }
    auto view = device.CreateTextureView(viewDesc);
    if (!view)
    {
        return false;
    }
    out.texture = created.Value();
    out.view = view.Value();
    return true;
}

[[nodiscard]] bool BeginFrame(CommandList& list, const Target& color, const Target& depth)
{
    const TextureBarrier barriers[] = {
        TextureBarrier{color.texture, SubresourceRange{}, ResourceState::Undefined,
                       ResourceState::RenderTarget},
        TextureBarrier{depth.texture, SubresourceRange{0, 0, 0, 0, TextureAspect::Depth},
                       ResourceState::Undefined, ResourceState::DepthWrite},
    };
    if (!list.Barrier(Span<const BufferBarrier>{}, Span<const TextureBarrier>(barriers, 2)).IsOk())
    {
        return false;
    }

    ColorAttachment colorAttachment{};
    colorAttachment.view = color.view;
    colorAttachment.loadOp = LoadOp::Clear;
    colorAttachment.storeOp = StoreOp::Store;
    colorAttachment.clearValue =
        ClearColorValue::Of(kClearColor.r, kClearColor.g, kClearColor.b, kClearColor.a);

    RenderPassDesc pass{};
    pass.colorAttachments = Span<const ColorAttachment>(&colorAttachment, 1);
    pass.hasDepthStencil = true;
    pass.depthStencil.view = depth.view;
    pass.depthStencil.depthLoadOp = LoadOp::Clear;
    pass.depthStencil.depthStoreOp = StoreOp::Store;
    pass.depthStencil.clearValue = ClearDepthStencilValue::Of(0.0f);
    if (!list.BeginRenderPass(pass).IsOk())
    {
        return false;
    }
    if (!list.SetViewport(
                 Viewport::Of(static_cast<f32>(kRenderWidth), static_cast<f32>(kRenderHeight)))
             .IsOk())
    {
        return false;
    }
    return list.SetScissor(ScissorRect{0, 0, kRenderWidth, kRenderHeight}).IsOk();
}

[[nodiscard]] bool ReadTarget(Device& device, BufferHandle readback, u8* outPixels)
{
    for (u32 y = 0; y < kRenderHeight; ++y)
    {
        const Status<Error> read = device.ReadBuffer(
            readback, static_cast<u64>(y) * kRowPitch,
            Span<Byte>(reinterpret_cast<Byte*>(outPixels) + y * kRenderRowBytes, kRenderRowBytes));
        if (!read.IsOk())
        {
            return false;
        }
    }
    return true;
}

// The colored triangle of the Phase 14 milestone, drawn on Direct3D 12.
bool RenderD3DTriangle(HeapAllocator& heap, u8* outPixels)
{
    Device device;
    if (!OpenD3D12Device(device, heap))
    {
        return false;
    }

    Target color;
    Target depth;
    if (!MakeTarget(device, Format::RGBA8Unorm,
                    TextureUsage::RenderTarget | TextureUsage::CopySource, color) ||
        !MakeTarget(device, Format::D32Float, TextureUsage::DepthStencil, depth))
    {
        return false;
    }

    const f32 positions[9] = {-0.9f, -0.9f, 0.5f, 0.9f, -0.9f, 0.5f, 0.0f, 0.9f, 0.5f};
    BufferDesc vertexDesc{};
    vertexDesc.usage = BufferUsage::Vertex | BufferUsage::CopyDestination;
    vertexDesc.sizeBytes = sizeof(positions);
    auto vertices = device.CreateBuffer(vertexDesc);
    if (!vertices)
    {
        return false;
    }

    BufferDesc stagingDesc{};
    stagingDesc.usage = BufferUsage::CopySource | BufferUsage::HostVisible;
    stagingDesc.sizeBytes = sizeof(positions);
    auto staging = device.CreateBuffer(stagingDesc);
    if (!staging)
    {
        return false;
    }
    if (!device
             .WriteBuffer(
                 staging.Value(), 0,
                 Span<const Byte>(reinterpret_cast<const Byte*>(positions), sizeof(positions)))
             .IsOk())
    {
        return false;
    }

    BufferDesc readbackDesc{};
    readbackDesc.usage = BufferUsage::Readback | BufferUsage::CopyDestination;
    readbackDesc.sizeBytes = kReadbackBytes;
    auto readback = device.CreateBuffer(readbackDesc);
    if (!readback)
    {
        return false;
    }

    ShaderDesc vertexShaderDesc{};
    vertexShaderDesc.stage = ShaderStage::Vertex;
    vertexShaderDesc.code = Bytes(kTriangleVertexSource);
    vertexShaderDesc.entryPoint = "main";
    auto vertexShader = device.CreateShader(vertexShaderDesc);
    if (!vertexShader)
    {
        return false;
    }
    ShaderDesc pixelShaderDesc{};
    pixelShaderDesc.stage = ShaderStage::Pixel;
    pixelShaderDesc.code = Bytes(kTrianglePixelSource);
    pixelShaderDesc.entryPoint = "main";
    auto pixelShader = device.CreateShader(pixelShaderDesc);
    if (!pixelShader)
    {
        return false;
    }

    // One used slot is the binding contract's minimum layout. The shader reads
    // nothing, so the group binds a real uniform range that nothing samples.
    const BindGroupLayoutEntry entry{0, BindingType::UniformBuffer,
                                     ShaderStageBit(ShaderStage::Pixel), 1};
    BindGroupLayoutDesc groupLayoutDesc{};
    groupLayoutDesc.entries = Span<const BindGroupLayoutEntry>(&entry, 1);
    auto groupLayout = device.CreateBindGroupLayout(groupLayoutDesc);
    if (!groupLayout)
    {
        return false;
    }
    BufferDesc constantsDesc{};
    constantsDesc.usage = BufferUsage::Uniform | BufferUsage::CopyDestination;
    constantsDesc.sizeBytes = 256;
    auto constants = device.CreateBuffer(constantsDesc);
    if (!constants)
    {
        return false;
    }
    BindingResource constantResource{};
    constantResource.type = BindingType::UniformBuffer;
    constantResource.buffer = constants.Value();
    constantResource.offset = 0;
    constantResource.size = 256;
    BindGroupDesc groupDesc{};
    groupDesc.layout = groupLayout.Value();
    groupDesc.resources = Span<const BindingResource>(&constantResource, 1);
    auto group = device.CreateBindGroup(groupDesc);
    if (!group)
    {
        return false;
    }

    PipelineLayoutDesc layoutDesc{};
    layoutDesc.groups[static_cast<u32>(BindGroupSlot::PerFrame)] = groupLayout.Value();
    auto layout = device.CreatePipelineLayout(layoutDesc);
    if (!layout)
    {
        return false;
    }

    ColorAttachmentState attachment{};
    attachment.format = Format::RGBA8Unorm;
    VertexBinding binding{};
    binding.strideBytes = sizeof(f32) * 3;
    binding.rate = VertexInputRate::PerVertex;
    VertexAttribute attribute{};
    attribute.location = 0;
    attribute.binding = 0;
    attribute.format = Format::R32G32B32Float;
    attribute.offsetBytes = 0;

    GraphicsPipelineDesc pipelineDesc{};
    pipelineDesc.layout = layout.Value();
    pipelineDesc.vertexShader = vertexShader.Value();
    pipelineDesc.pixelShader = pixelShader.Value();
    pipelineDesc.vertexLayout.bindings = Span<const VertexBinding>(&binding, 1);
    pipelineDesc.vertexLayout.attributes = Span<const VertexAttribute>(&attribute, 1);
    pipelineDesc.rasterizer.cullMode = CullMode::None;
    pipelineDesc.depthStencil.depthCompare = CompareFunction::GreaterEqual;
    pipelineDesc.depthStencilFormat = Format::D32Float;
    pipelineDesc.colorAttachments = Span<const ColorAttachmentState>(&attachment, 1);
    auto pipeline = device.CreateGraphicsPipeline(pipelineDesc);
    if (!pipeline)
    {
        return false;
    }

    FenceDesc fenceDesc{};
    auto fence = device.CreateFence(fenceDesc);
    if (!fence)
    {
        return false;
    }

    CommandListDesc listDesc{};
    auto createdList = device.CreateCommandList(listDesc);
    if (!createdList)
    {
        return false;
    }
    CommandList list = std::move(createdList.Value());
    if (!list.Begin().IsOk())
    {
        return false;
    }

    const BufferBarrier upload[] = {
        {staging.Value(), ResourceState::Undefined, ResourceState::CopySource},
        {vertices.Value(), ResourceState::Undefined, ResourceState::CopyDestination},
        {readback.Value(), ResourceState::Undefined, ResourceState::CopyDestination},
    };
    if (!list.Barrier(Span<const BufferBarrier>(upload, 3), Span<const TextureBarrier>{}).IsOk())
    {
        return false;
    }
    BufferCopy copy{};
    copy.source = staging.Value();
    copy.destination = vertices.Value();
    copy.sizeBytes = sizeof(positions);
    if (!list.CopyBuffer(copy).IsOk())
    {
        return false;
    }
    const BufferBarrier ready[] = {
        {vertices.Value(), ResourceState::CopyDestination, ResourceState::VertexBuffer},
    };
    if (!list.Barrier(Span<const BufferBarrier>(ready, 1), Span<const TextureBarrier>{}).IsOk())
    {
        return false;
    }

    if (!BeginFrame(list, color, depth))
    {
        return false;
    }
    if (!list.SetPipeline(pipeline.Value()).IsOk() ||
        !list.SetBindGroup(BindGroupSlot::PerFrame, group.Value()).IsOk() ||
        !list.SetVertexBuffer(0, vertices.Value()).IsOk() ||
        !list.Draw(DrawArguments{3, 1, 0, 0}).IsOk() || !list.EndRenderPass().IsOk())
    {
        return false;
    }

    const TextureBarrier colorReady[] = {
        TextureBarrier{color.texture, SubresourceRange{}, ResourceState::RenderTarget,
                       ResourceState::CopySource},
    };
    if (!list.Barrier(Span<const BufferBarrier>{}, Span<const TextureBarrier>(colorReady, 1))
             .IsOk())
    {
        return false;
    }
    BufferTextureCopy pixels{};
    pixels.buffer = readback.Value();
    pixels.bufferRowPitch = kRowPitch;
    pixels.texture = color.texture;
    pixels.width = kRenderWidth;
    pixels.height = kRenderHeight;
    if (!list.CopyTextureToBuffer(pixels).IsOk() || !list.End().IsOk())
    {
        return false;
    }

    SubmitDesc submit{};
    submit.queue = QueueKind::Graphics;
    const CommandListHandle handles[] = {list.Handle()};
    submit.commandLists = Span<const CommandListHandle>(handles, 1);
    FenceSignal signal{};
    signal.fence = fence.Value();
    signal.value = 1;
    submit.signals = Span<const FenceSignal>(&signal, 1);
    if (!device.Submit(submit).IsOk() || !device.WaitFence(fence.Value(), 1).IsOk())
    {
        return false;
    }
    return ReadTarget(device, readback.Value(), outPixels);
}

// The textured cube drawn on Direct3D 12: vertex upload, texture upload, a
// sampled texture behind a descriptor table, root constants for the transform,
// and a reverse-Z depth test.
u32 RenderD3DCube(HeapAllocator& heap, u8* outPixels)
{
    Device device;
    if (!OpenD3D12Device(device, heap))
    {
        return __LINE__;
    }

    CubeVertex cube[36];
    const u32 count = BuildCube(cube);
    GpuVertex gpu[36];
    for (u32 index = 0; index < count; ++index)
    {
        std::memcpy(gpu[index].position, &cube[index].position, sizeof(gpu[index].position));
        std::memcpy(gpu[index].uv, &cube[index].uv, sizeof(gpu[index].uv));
    }
    const u64 vertexBytes = static_cast<u64>(count) * sizeof(GpuVertex);

    u8 checker[kCheckerBytes];
    BuildChecker(checker);

    Target color;
    Target depth;
    if (!MakeTarget(device, Format::RGBA8Unorm,
                    TextureUsage::RenderTarget | TextureUsage::CopySource, color) ||
        !MakeTarget(device, Format::D32Float, TextureUsage::DepthStencil, depth))
    {
        return __LINE__;
    }

    TextureDesc textureDesc{};
    textureDesc.format = Format::RGBA8Unorm;
    textureDesc.usage = TextureUsage::Sampled | TextureUsage::CopyDestination;
    textureDesc.width = kTextureSize;
    textureDesc.height = kTextureSize;
    auto texture = device.CreateTexture(textureDesc);
    if (!texture)
    {
        return __LINE__;
    }
    TextureViewDesc textureViewDesc{};
    textureViewDesc.texture = texture.Value();
    textureViewDesc.format = Format::RGBA8Unorm;
    auto textureView = device.CreateTextureView(textureViewDesc);
    if (!textureView)
    {
        return __LINE__;
    }

    BufferDesc vertexDesc{};
    vertexDesc.usage = BufferUsage::Vertex | BufferUsage::CopyDestination;
    vertexDesc.sizeBytes = vertexBytes;
    auto vertices = device.CreateBuffer(vertexDesc);
    if (!vertices)
    {
        return __LINE__;
    }

    BufferDesc stagingDesc{};
    stagingDesc.usage = BufferUsage::CopySource | BufferUsage::HostVisible;
    // D3D12 aligns every uploaded texture row to 256 bytes and every placed
    // footprint to 512, so the checker board leads the staging buffer and the
    // vertices follow it at an aligned offset.
    stagingDesc.sizeBytes = kUploadRowPitch * kTextureSize + vertexBytes;
    auto staging = device.CreateBuffer(stagingDesc);
    if (!staging)
    {
        return __LINE__;
    }
    for (u32 y = 0; y < kTextureSize; ++y)
    {
        const u64 rowOffset = static_cast<u64>(y) * kUploadRowPitch;
        const Byte* row = reinterpret_cast<const Byte*>(checker + y * kTextureSize * 4);
        if (!device.WriteBuffer(staging.Value(), rowOffset, Span<const Byte>(row, kTextureSize * 4))
                 .IsOk())
        {
            return __LINE__;
        }
    }
    const u64 vertexOffset = kUploadRowPitch * kTextureSize;
    if (!device
             .WriteBuffer(staging.Value(), vertexOffset,
                          Span<const Byte>(reinterpret_cast<const Byte*>(gpu), vertexBytes))
             .IsOk())
    {
        return __LINE__;
    }

    BufferDesc readbackDesc{};
    readbackDesc.usage = BufferUsage::Readback | BufferUsage::CopyDestination;
    readbackDesc.sizeBytes = kReadbackBytes;
    auto readback = device.CreateBuffer(readbackDesc);
    if (!readback)
    {
        return __LINE__;
    }

    SamplerDesc samplerDesc{};
    samplerDesc.minFilter = FilterMode::Nearest;
    samplerDesc.magFilter = FilterMode::Nearest;
    samplerDesc.mipmapFilter = FilterMode::Nearest;
    samplerDesc.addressU = AddressMode::Repeat;
    samplerDesc.addressV = AddressMode::Repeat;
    auto sampler = device.CreateSampler(samplerDesc);
    if (!sampler)
    {
        return __LINE__;
    }

    BindGroupLayoutEntry layoutEntries[2]{};
    layoutEntries[0].binding = 0;
    layoutEntries[0].type = BindingType::SampledTexture;
    layoutEntries[0].visibility = ShaderStageBit(ShaderStage::Pixel);
    layoutEntries[1].binding = 1;
    layoutEntries[1].type = BindingType::Sampler;
    layoutEntries[1].visibility = ShaderStageBit(ShaderStage::Pixel);
    BindGroupLayoutDesc groupLayoutDesc{};
    groupLayoutDesc.entries = Span<const BindGroupLayoutEntry>(layoutEntries, 2);
    auto groupLayout = device.CreateBindGroupLayout(groupLayoutDesc);
    if (!groupLayout)
    {
        return __LINE__;
    }

    BindingResource resources[2]{};
    resources[0].type = BindingType::SampledTexture;
    resources[0].textureView = textureView.Value();
    resources[1].type = BindingType::Sampler;
    resources[1].sampler = sampler.Value();
    BindGroupDesc groupDesc{};
    groupDesc.layout = groupLayout.Value();
    groupDesc.resources = Span<const BindingResource>(resources, 2);
    auto group = device.CreateBindGroup(groupDesc);
    if (!group)
    {
        return __LINE__;
    }

    const PushConstantRange matrix{0, 64, ShaderStageBit(ShaderStage::Vertex)};
    PipelineLayoutDesc layoutDesc{};
    layoutDesc.groups[static_cast<u32>(BindGroupSlot::PerFrame)] = groupLayout.Value();
    layoutDesc.pushConstants = Span<const PushConstantRange>(&matrix, 1);
    auto layout = device.CreatePipelineLayout(layoutDesc);
    if (!layout)
    {
        return __LINE__;
    }

    auto generatedVertex = cyber::shaders::CompileHlsl("cube_vertex.cgs", kCubeVertexSource, heap);
    if (!generatedVertex)
    {
        return __LINE__;
    }
    auto generatedPixel = cyber::shaders::CompileHlsl("cube_pixel.cgs", kCubePixelSource, heap);
    if (!generatedPixel)
    {
        return __LINE__;
    }

    ShaderDesc vertexShaderDesc{};
    vertexShaderDesc.stage = ShaderStage::Vertex;
    vertexShaderDesc.code = Bytes(generatedVertex.Value().hlsl.View());
    vertexShaderDesc.entryPoint = "main";
    auto vertexShader = device.CreateShader(vertexShaderDesc);
    if (!vertexShader)
    {
        return __LINE__;
    }
    ShaderDesc pixelShaderDesc{};
    pixelShaderDesc.stage = ShaderStage::Pixel;
    pixelShaderDesc.code = Bytes(generatedPixel.Value().hlsl.View());
    pixelShaderDesc.entryPoint = "main";
    auto pixelShader = device.CreateShader(pixelShaderDesc);
    if (!pixelShader)
    {
        return __LINE__;
    }

    ColorAttachmentState attachment{};
    attachment.format = Format::RGBA8Unorm;
    VertexBinding binding{};
    binding.strideBytes = sizeof(GpuVertex);
    binding.rate = VertexInputRate::PerVertex;
    VertexAttribute attributes[2]{};
    attributes[0].location = 0;
    attributes[0].binding = 0;
    attributes[0].format = Format::R32G32B32Float;
    attributes[0].offsetBytes = 0;
    attributes[1].location = 1;
    attributes[1].binding = 0;
    attributes[1].format = Format::RG32Float;
    attributes[1].offsetBytes = 12;

    GraphicsPipelineDesc pipelineDesc{};
    pipelineDesc.layout = layout.Value();
    pipelineDesc.vertexShader = vertexShader.Value();
    pipelineDesc.pixelShader = pixelShader.Value();
    pipelineDesc.vertexLayout.bindings = Span<const VertexBinding>(&binding, 1);
    pipelineDesc.vertexLayout.attributes = Span<const VertexAttribute>(attributes, 2);
    pipelineDesc.rasterizer.cullMode = CullMode::None;
    // Reverse-Z: the projection puts the near plane at 1 and the far plane at 0,
    // which is the comparison the reference renderer tests against a cleared 0.
    pipelineDesc.depthStencil.depthCompare = CompareFunction::GreaterEqual;
    pipelineDesc.depthStencilFormat = Format::D32Float;
    pipelineDesc.colorAttachments = Span<const ColorAttachmentState>(&attachment, 1);
    auto pipeline = device.CreateGraphicsPipeline(pipelineDesc);
    if (!pipeline)
    {
        return __LINE__;
    }

    FenceDesc fenceDesc{};
    auto fence = device.CreateFence(fenceDesc);
    if (!fence)
    {
        return __LINE__;
    }

    CommandListDesc listDesc{};
    auto createdList = device.CreateCommandList(listDesc);
    if (!createdList)
    {
        return __LINE__;
    }
    CommandList list = std::move(createdList.Value());
    if (!list.Begin().IsOk())
    {
        return __LINE__;
    }

    const BufferBarrier upload[] = {
        {staging.Value(), ResourceState::Undefined, ResourceState::CopySource},
        {vertices.Value(), ResourceState::Undefined, ResourceState::CopyDestination},
        {readback.Value(), ResourceState::Undefined, ResourceState::CopyDestination},
    };
    const TextureBarrier uploadTexture[] = {
        TextureBarrier{texture.Value(), SubresourceRange{}, ResourceState::Undefined,
                       ResourceState::CopyDestination},
    };
    if (!list.Barrier(Span<const BufferBarrier>(upload, 3),
                      Span<const TextureBarrier>(uploadTexture, 1))
             .IsOk())
    {
        return __LINE__;
    }

    BufferCopy vertexCopy{};
    vertexCopy.source = staging.Value();
    vertexCopy.sourceOffset = vertexOffset;
    vertexCopy.destination = vertices.Value();
    vertexCopy.sizeBytes = vertexBytes;
    if (!list.CopyBuffer(vertexCopy).IsOk())
    {
        return __LINE__;
    }

    BufferTextureCopy uploadPixels{};
    uploadPixels.buffer = staging.Value();
    uploadPixels.bufferRowPitch = kUploadRowPitch;
    uploadPixels.texture = texture.Value();
    uploadPixels.width = kTextureSize;
    uploadPixels.height = kTextureSize;
    if (!list.CopyBufferToTexture(uploadPixels).IsOk())
    {
        return __LINE__;
    }

    const BufferBarrier ready[] = {
        {vertices.Value(), ResourceState::CopyDestination, ResourceState::VertexBuffer},
    };
    const TextureBarrier sampling[] = {
        TextureBarrier{texture.Value(), SubresourceRange{}, ResourceState::CopyDestination,
                       ResourceState::ShaderRead},
    };
    // The attachment transitions belong to BeginFrame alone: recording them
    // here as well would hand the backend an equal before/after state.
    if (!list.Barrier(Span<const BufferBarrier>(ready, 1), Span<const TextureBarrier>(sampling, 1))
             .IsOk())
    {
        return __LINE__;
    }

    if (!BeginFrame(list, color, depth))
    {
        return __LINE__;
    }

    Scene scene;
    scene.Build();
    const Byte* matrixBytes = reinterpret_cast<const Byte*>(&scene.modelViewProjection);
    if (!list.SetPipeline(pipeline.Value()).IsOk() ||
        !list.SetBindGroup(BindGroupSlot::PerFrame, group.Value()).IsOk() ||
        !list.PushConstants(ShaderStageBit(ShaderStage::Vertex), 0,
                            Span<const Byte>(matrixBytes, sizeof(Mat4)))
             .IsOk() ||
        !list.SetVertexBuffer(0, vertices.Value()).IsOk() ||
        !list.Draw(DrawArguments{count, 1, 0, 0}).IsOk() || !list.EndRenderPass().IsOk())
    {
        return __LINE__;
    }

    const TextureBarrier colorReady[] = {
        TextureBarrier{color.texture, SubresourceRange{}, ResourceState::RenderTarget,
                       ResourceState::CopySource},
    };
    if (!list.Barrier(Span<const BufferBarrier>{}, Span<const TextureBarrier>(colorReady, 1))
             .IsOk())
    {
        return __LINE__;
    }

    BufferTextureCopy pixels{};
    pixels.buffer = readback.Value();
    pixels.bufferRowPitch = kRowPitch;
    pixels.texture = color.texture;
    pixels.width = kRenderWidth;
    pixels.height = kRenderHeight;
    const Status<Error> copiedPixels = list.CopyTextureToBuffer(pixels);
    if (!copiedPixels.IsOk())
    {
        std::fprintf(stderr, "d3d12 cube: copy texture to buffer: %s\n",
                     copiedPixels.Error().message);
        return __LINE__;
    }
    const Status<Error> endedList = list.End();
    if (!endedList.IsOk())
    {
        std::fprintf(stderr, "d3d12 cube: end list: %s\n", endedList.Error().message);
        return __LINE__;
    }

    SubmitDesc submit{};
    submit.queue = QueueKind::Graphics;
    const CommandListHandle handles[] = {list.Handle()};
    submit.commandLists = Span<const CommandListHandle>(handles, 1);
    FenceSignal signal{};
    signal.fence = fence.Value();
    signal.value = 1;
    submit.signals = Span<const FenceSignal>(&signal, 1);
    if (!device.Submit(submit).IsOk() || !device.WaitFence(fence.Value(), 1).IsOk())
    {
        return __LINE__;
    }
    return ReadTarget(device, readback.Value(), outPixels) ? 0 : __LINE__;
}

// ---------------------------------------------------------------------------
// Comparison
// ---------------------------------------------------------------------------

// Box-filters one supersampled frame down to the image the phase presents:
// every output pixel becomes the rounded mean of the `kSupersample` squared
// samples that covered it. Both renderers go through this same resolve, so it
// cannot favour either side, and it is the antialiasing ADR 0012 places at the
// caller until a rasterizer grows multisample coverage.
void ResolveBox(const u8* rendered, u8* out) noexcept
{
    constexpr u32 kSamples = kSupersample * kSupersample;
    for (u32 y = 0; y < kHeight; ++y)
    {
        for (u32 x = 0; x < kWidth; ++x)
        {
            u32 sum[4] = {0, 0, 0, 0};
            for (u32 sampleY = 0; sampleY < kSupersample; ++sampleY)
            {
                const u64 first = (static_cast<u64>(y) * kSupersample + sampleY) * kRenderWidth +
                                  static_cast<u64>(x) * kSupersample;
                const u8* samples = rendered + first * 4;
                for (u32 sampleX = 0; sampleX < kSupersample; ++sampleX)
                {
                    for (u32 channel = 0; channel < 4; ++channel)
                    {
                        sum[channel] += samples[sampleX * 4 + channel];
                    }
                }
            }
            u8* pixel = out + (static_cast<u64>(y) * kWidth + x) * 4;
            for (u32 channel = 0; channel < 4; ++channel)
            {
                pixel[channel] = static_cast<u8>((sum[channel] + kSamples / 2) / kSamples);
            }
        }
    }
}

struct Difference
{
    u32 pixels = 0;
    u32 pixelsOverTolerance = 0;
    u32 worstChannelDelta = 0;
    f64 meanChannelDelta = 0.0;
};

[[nodiscard]] Difference Compare(const u8* reference, const u8* candidate) noexcept
{
    Difference result;
    u64 total = 0;
    const u64 pixels = static_cast<u64>(kWidth) * kHeight;
    for (u64 index = 0; index < pixels * 4; index += 4)
    {
        u32 worst = 0;
        for (u32 channel = 0; channel < 4; ++channel)
        {
            const u32 delta = reference[index + channel] > candidate[index + channel]
                                  ? reference[index + channel] - candidate[index + channel]
                                  : candidate[index + channel] - reference[index + channel];
            worst = delta > worst ? delta : worst;
            total += delta;
        }
        if (worst > result.worstChannelDelta)
        {
            result.worstChannelDelta = worst;
        }
        if (worst > kChannelTolerance)
        {
            ++result.pixelsOverTolerance;
        }
    }
    result.pixels = static_cast<u32>(pixels);
    result.meanChannelDelta = static_cast<f64>(total) / static_cast<f64>(pixels * 4);
    return result;
}

[[nodiscard]] bool WithinTolerance(const Difference& difference) noexcept
{
    const u64 allowed = (static_cast<u64>(difference.pixels) * kOverTolerancePercent) / 100;
    return difference.pixelsOverTolerance <= allowed &&
           difference.meanChannelDelta <= kMeanChannelTolerance;
}

// Reports a failure with the measurement the tolerance is read from, so a red
// run says how far off it was rather than only that it was off.
void Report(const char* name, const Difference& difference) noexcept
{
    const u64 percent =
        (static_cast<u64>(difference.pixelsOverTolerance) * 100) / difference.pixels;
    char message[256];
    std::snprintf(message, sizeof(message),
                  "%s: %u of %u pixels (%llu%%) differ by more than %u per channel, worst %u, "
                  "mean %.3f",
                  name, difference.pixelsOverTolerance, difference.pixels,
                  static_cast<unsigned long long>(percent), kChannelTolerance,
                  difference.worstChannelDelta, difference.meanChannelDelta);
    CG_FAIL(message);
}

// Writes one rendered image as the P6 the project's references use, into the
// build-tree directory this target names. The bytes are the ones the comparison
// just read, so the images and a green test cannot disagree about what was
// rendered, and the two of them together are the phase's evidence.
//
// The framebuffer the renderers filled stores linear bytes — that is the
// `PixelFormat::RGBA8Unorm` contract, no transfer function at write time. A P6
// PPM on the other hand is a display interchange format every viewer reads as
// sRGB-encoded, so exporting linear bytes as-is displays midtones far darker
// than they were rendered: against a dark clear color that reads as a muddy
// halo along every smoothed edge. The export therefore applies the engine's
// sRGB transfer function per channel — the same encode `PresentTarget` performs
// when a frame goes to a window — and channels re-encode identically on both
// sides, the comparison itself keeps measuring the stored linear bytes.
void WriteImage(const char* name, const u8* pixels) noexcept
{
    constexpr u32 kHeaderBytes = 32;
    constexpr u64 kImageBytes = static_cast<u64>(kWidth) * kHeight * 3;
    // Too large for the stack, and the test is single-threaded, which is the same
    // bargain the two image buffers in each test make.
    static u8 file[kHeaderBytes + kImageBytes];
    // A PPM's raster data starts at the first byte after the header's last
    // newline, so the header is written at the start and the pixels immediately
    // follow it; any gap would be read back as the first rows of the image.
    const int header = std::snprintf(reinterpret_cast<char*>(file), kHeaderBytes,
                                     "P6\n%u %u\n255\n", kWidth, kHeight);
    if (header <= 0 || static_cast<u32>(header) >= kHeaderBytes)
    {
        std::fprintf(stderr, "backend compare: cannot format the header of %s\n", name);
        return;
    }
    u64 cursor = static_cast<u64>(header);
    for (u32 y = 0; y < kHeight; ++y)
    {
        for (u32 x = 0; x < kWidth; ++x)
        {
            const u64 source = (static_cast<u64>(y) * kWidth + x) * 4;
            constexpr f32 kUnorm8ToLinear = 1.0f / 255.0f;
            file[cursor] =
                software::EncodeSrgb8(static_cast<f32>(pixels[source]) * kUnorm8ToLinear);
            file[cursor + 1] =
                software::EncodeSrgb8(static_cast<f32>(pixels[source + 1]) * kUnorm8ToLinear);
            file[cursor + 2] =
                software::EncodeSrgb8(static_cast<f32>(pixels[source + 2]) * kUnorm8ToLinear);
            cursor += 3;
        }
    }

    // The directory comes from the build system, so a clean tree does not have
    // it yet and the first image of the first run has to make it.
    if (!cyber::platform::IsDirectory(CYBER_BACKEND_COMPARE_IMAGE_DIR))
    {
        const Status<Error> created =
            cyber::platform::CreateDirectories(CYBER_BACKEND_COMPARE_IMAGE_DIR);
        if (!created)
        {
            std::fprintf(stderr, "backend compare: cannot create %s: %s\n",
                         CYBER_BACKEND_COMPARE_IMAGE_DIR, created.Error().message);
            return;
        }
    }

    char path[512];
    std::snprintf(path, sizeof(path), "%s/%s.ppm", CYBER_BACKEND_COMPARE_IMAGE_DIR, name);
    const Status<Error> written = cyber::platform::WriteFile(
        path, Span<const Byte>(reinterpret_cast<const Byte*>(file), cursor));
    if (!written)
    {
        std::fprintf(stderr, "backend compare: %s was not written: %s\n", path,
                     written.Error().message);
    }
}

void ExpectWithin(const char* name, const u8* reference, const u8* candidate) noexcept
{
    const Difference difference = Compare(reference, candidate);
    std::printf("%s: %u/%u pixels over tolerance, worst %u, mean %.3f\n", name,
                difference.pixelsOverTolerance, difference.pixels, difference.worstChannelDelta,
                difference.meanChannelDelta);
    if (!WithinTolerance(difference))
    {
        Report(name, difference);
    }
}

}  // namespace

CG_TEST(backend_compare_triangle)
{
    HeapAllocator heap;
    // One supersampled frame, reused by each renderer in turn: it is filled, then
    // resolved into the output image the comparison measures and writes.
    static u8 rendered[kRenderRowBytes * kRenderHeight];
    static u8 reference[kRowBytes * kHeight];
    static u8 candidate[kRowBytes * kHeight];
    const Color shading{1.0f, 0.5f, 0.25f, 1.0f};

    CG_REQUIRE(RenderSoftwareTriangle(heap, rendered, shading));
    ResolveBox(rendered, reference);
    CG_REQUIRE(RenderD3DTriangle(heap, rendered));
    ResolveBox(rendered, candidate);
    WriteImage("phase-14-triangle-software", reference);
    WriteImage("phase-14-triangle-d3d12", candidate);
    ExpectWithin("triangle", reference, candidate);
}

CG_TEST(backend_compare_textured_cube)
{
    HeapAllocator heap;
    static u8 rendered[kRenderRowBytes * kRenderHeight];
    static u8 reference[kRowBytes * kHeight];
    static u8 candidate[kRowBytes * kHeight];

    CG_REQUIRE(RenderSoftwareCube(heap, rendered));
    ResolveBox(rendered, reference);
    // The Direct3D 12 half reports the line of the step that stopped it, because
    // a boolean would say only that one of a hundred steps failed.
    const u32 stoppedAt = RenderD3DCube(heap, rendered);
    if (stoppedAt != 0)
    {
        char message[96];
        std::snprintf(message, sizeof(message), "d3d12 cube stopped at line %u", stoppedAt);
        CG_FAIL(message);
        return;
    }
    ResolveBox(rendered, candidate);
    WriteImage("phase-14-cube-software", reference);
    WriteImage("phase-14-cube-d3d12", candidate);
    ExpectWithin("textured cube", reference, candidate);
}
