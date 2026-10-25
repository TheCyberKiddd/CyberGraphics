#include <cyber/software/framebuffer.h>
#include <cyber/software/texture.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace cyber::software
{

using cyber::core::kInvalidIndex;

namespace
{

constexpr i32 kMaxTextureDimension = 16384;

constexpr f32 kUnorm8ToLinear = 1.0f / 255.0f;

[[nodiscard]] i32 ClampDimension(i32 value) noexcept
{
    return value < 1 ? 1 : value;
}

[[nodiscard]] u32 ComputeMipLevels(i32 width, i32 height, bool generateMips) noexcept
{
    if (!generateMips)
    {
        return 1;
    }
    u32 levels = 1;
    i32 w = width;
    i32 h = height;
    while ((w > 1 || h > 1) && levels < kMaxTextureMipLevels)
    {
        w = ClampDimension(w >> 1);
        h = ClampDimension(h >> 1);
        ++levels;
    }
    return levels;
}

[[nodiscard]] f32 WrapNormalized(f32 value, AddressMode mode) noexcept
{
    if (!std::isfinite(value))
    {
        return 0.0f;
    }
    switch (mode)
    {
    case AddressMode::Repeat:
    {
        const f32 folded = value - std::floor(value);
        return folded < 0.0f ? folded + 1.0f : folded;
    }
    case AddressMode::ClampToEdge:
        return std::clamp(value, 0.0f, 1.0f);
    case AddressMode::MirroredRepeat:
    {
        f32 folded = value * 0.5f;
        folded -= std::floor(folded);
        if (folded < 0.0f)
        {
            folded += 1.0f;
        }
        return folded <= 0.5f ? folded * 2.0f : 2.0f - folded * 2.0f;
    }
    }
    return 0.0f;
}

[[nodiscard]] i32 WrapTexel(i32 index, i32 size, AddressMode mode) noexcept
{
    if (size <= 1)
    {
        return 0;
    }
    switch (mode)
    {
    case AddressMode::Repeat:
    {
        const i32 wrapped = index % size;
        return wrapped < 0 ? wrapped + size : wrapped;
    }
    case AddressMode::ClampToEdge:
        return std::clamp(index, 0, size - 1);
    case AddressMode::MirroredRepeat:
    {
        const i32 period = size * 2;
        i32 mirrored = index % period;
        if (mirrored < 0)
        {
            mirrored += period;
        }
        return mirrored >= size ? period - 1 - mirrored : mirrored;
    }
    }
    return 0;
}

[[nodiscard]] f32 LerpChannel(f32 from, f32 to, f32 factor) noexcept
{
    return from + (to - from) * factor;
}

}  // namespace

Texture::~Texture()
{
    Release();
}

Texture::Texture(Texture&& other) noexcept
{
    MoveFrom(other);
}

Texture& Texture::operator=(Texture&& other) noexcept
{
    if (this != &other)
    {
        Release();
        MoveFrom(other);
    }
    return *this;
}

core::Result<Texture, core::Error> Texture::Create(const TextureDesc& desc,
                                                   memory::Allocator& allocator) noexcept
{
    if (!IsValidTextureFormat(desc.format))
    {
        return core::Result<Texture, core::Error>::Err(
            core::Error{core::ErrorCode::InvalidArgument, "unknown texture format"});
    }
    if (desc.width < 1 || desc.width > kMaxTextureDimension || desc.height < 1 ||
        desc.height > kMaxTextureDimension)
    {
        return core::Result<Texture, core::Error>::Err(
            core::Error{core::ErrorCode::InvalidArgument, "texture dimensions are out of range"});
    }

    const u32 levels = ComputeMipLevels(desc.width, desc.height, desc.generateMips);
    const usize pixelSize = TexturePixelSizeBytes(desc.format);

    Texture texture;
    texture.m_allocator = &allocator;
    texture.m_width = desc.width;
    texture.m_height = desc.height;
    texture.m_format = desc.format;
    texture.m_mipLevels = levels;

    usize totalBytes = 0;
    for (u32 level = 0; level < levels; ++level)
    {
        const i32 levelWidth = ClampDimension(desc.width >> level);
        const i32 levelHeight = ClampDimension(desc.height >> level);
        const usize texels = static_cast<usize>(levelWidth) * static_cast<usize>(levelHeight);
        if (texels > kInvalidIndex / pixelSize)
        {
            return core::Result<Texture, core::Error>::Err(
                core::Error{core::ErrorCode::Overflow, "texture mip chain is too large"});
        }
        const usize levelBytes = texels * pixelSize;
        if (totalBytes > kInvalidIndex - levelBytes)
        {
            return core::Result<Texture, core::Error>::Err(
                core::Error{core::ErrorCode::Overflow, "texture mip chain is too large"});
        }
        texture.m_levelOffsets[level] = totalBytes;
        totalBytes += levelBytes;
    }

    texture.m_data = static_cast<u8*>(allocator.Allocate(totalBytes, alignof(f32)));
    if (texture.m_data == nullptr)
    {
        return core::Result<Texture, core::Error>::Err(
            core::Error{core::ErrorCode::Internal, "failed to allocate texture storage"});
    }
    texture.m_sizeBytes = totalBytes;
    std::memset(texture.m_data, 0, totalBytes);
    return core::Result<Texture, core::Error>::Ok(std::move(texture));
}

bool Texture::IsValid() const noexcept
{
    return m_data != nullptr && m_mipLevels > 0;
}

i32 Texture::Width() const noexcept
{
    return m_width;
}

i32 Texture::Height() const noexcept
{
    return m_height;
}

TextureFormat Texture::Format() const noexcept
{
    return m_format;
}

u32 Texture::MipLevels() const noexcept
{
    return m_mipLevels;
}

i32 Texture::MipWidth(u32 level) const noexcept
{
    if (level >= m_mipLevels)
    {
        return 0;
    }
    return ClampDimension(m_width >> level);
}

i32 Texture::MipHeight(u32 level) const noexcept
{
    if (level >= m_mipLevels)
    {
        return 0;
    }
    return ClampDimension(m_height >> level);
}

usize Texture::DataSizeBytes() const noexcept
{
    return m_sizeBytes;
}

math::Color Texture::GetTexel(u32 level, i32 x, i32 y) const noexcept
{
    if (!IsValid() || level >= m_mipLevels)
    {
        return math::Color{0.0f, 0.0f, 0.0f, 0.0f};
    }
    const i32 width = MipWidth(level);
    const i32 height = MipHeight(level);
    if (x < 0 || y < 0 || x >= width || y >= height)
    {
        return math::Color{0.0f, 0.0f, 0.0f, 0.0f};
    }

    const usize pixelSize = TexturePixelSizeBytes(m_format);
    const u8* pixel =
        m_data + m_levelOffsets[level] +
        (static_cast<usize>(y) * static_cast<usize>(width) + static_cast<usize>(x)) * pixelSize;
    if (m_format == TextureFormat::RGBA8Unorm)
    {
        return math::Color{static_cast<f32>(pixel[0]) * kUnorm8ToLinear,
                           static_cast<f32>(pixel[1]) * kUnorm8ToLinear,
                           static_cast<f32>(pixel[2]) * kUnorm8ToLinear,
                           static_cast<f32>(pixel[3]) * kUnorm8ToLinear};
    }

    f32 channels[4] = {};
    std::memcpy(&channels[0], pixel + 0, sizeof(f32));
    std::memcpy(&channels[1], pixel + 4, sizeof(f32));
    std::memcpy(&channels[2], pixel + 8, sizeof(f32));
    std::memcpy(&channels[3], pixel + 12, sizeof(f32));
    return math::Color{channels[0], channels[1], channels[2], channels[3]};
}

void Texture::SetTexel(u32 level, i32 x, i32 y, const math::Color& color) noexcept
{
    if (!IsValid() || level >= m_mipLevels)
    {
        return;
    }
    const i32 width = MipWidth(level);
    const i32 height = MipHeight(level);
    if (x < 0 || y < 0 || x >= width || y >= height)
    {
        return;
    }

    const usize pixelSize = TexturePixelSizeBytes(m_format);
    u8* pixel =
        m_data + m_levelOffsets[level] +
        (static_cast<usize>(y) * static_cast<usize>(width) + static_cast<usize>(x)) * pixelSize;
    if (m_format == TextureFormat::RGBA8Unorm)
    {
        pixel[0] = QuantizeUnorm8(color.r);
        pixel[1] = QuantizeUnorm8(color.g);
        pixel[2] = QuantizeUnorm8(color.b);
        pixel[3] = QuantizeUnorm8(color.a);
        return;
    }

    const f32 channels[4] = {color.r, color.g, color.b, color.a};
    std::memcpy(pixel + 0, &channels[0], sizeof(f32));
    std::memcpy(pixel + 4, &channels[1], sizeof(f32));
    std::memcpy(pixel + 8, &channels[2], sizeof(f32));
    std::memcpy(pixel + 12, &channels[3], sizeof(f32));
}

bool Texture::Upload(memory::Span<const math::Color> colors) noexcept
{
    if (!IsValid())
    {
        return false;
    }
    const usize expected = static_cast<usize>(m_width) * static_cast<usize>(m_height);
    if (colors.Size() != expected)
    {
        return false;
    }
    for (i32 y = 0; y < m_height; ++y)
    {
        for (i32 x = 0; x < m_width; ++x)
        {
            SetTexel(0, x, y,
                     colors[static_cast<usize>(y) * static_cast<usize>(m_width) +
                            static_cast<usize>(x)]);
        }
    }
    if (m_mipLevels > 1)
    {
        GenerateMips();
    }
    return true;
}

void Texture::GenerateMips() noexcept
{
    if (!IsValid())
    {
        return;
    }
    for (u32 level = 1; level < m_mipLevels; ++level)
    {
        const i32 sourceWidth = MipWidth(level - 1);
        const i32 sourceHeight = MipHeight(level - 1);
        const i32 width = MipWidth(level);
        const i32 height = MipHeight(level);
        for (i32 y = 0; y < height; ++y)
        {
            for (i32 x = 0; x < width; ++x)
            {
                const i32 sourceX0 = std::min(x * 2, sourceWidth - 1);
                const i32 sourceY0 = std::min(y * 2, sourceHeight - 1);
                const i32 sourceX1 = std::min(sourceX0 + 1, sourceWidth - 1);
                const i32 sourceY1 = std::min(sourceY0 + 1, sourceHeight - 1);

                const math::Color c00 = GetTexel(level - 1, sourceX0, sourceY0);
                const math::Color c10 = GetTexel(level - 1, sourceX1, sourceY0);
                const math::Color c01 = GetTexel(level - 1, sourceX0, sourceY1);
                const math::Color c11 = GetTexel(level - 1, sourceX1, sourceY1);
                const math::Color average{(c00.r + c10.r + c01.r + c11.r) * 0.25f,
                                          (c00.g + c10.g + c01.g + c11.g) * 0.25f,
                                          (c00.b + c10.b + c01.b + c11.b) * 0.25f,
                                          (c00.a + c10.a + c01.a + c11.a) * 0.25f};
                SetTexel(level, x, y, average);
            }
        }
    }
}

void Texture::Release() noexcept
{
    if (m_data != nullptr && m_allocator != nullptr)
    {
        m_allocator->Deallocate(m_data);
    }
    m_data = nullptr;
    m_allocator = nullptr;
    m_sizeBytes = 0;
    m_width = 0;
    m_height = 0;
    m_mipLevels = 0;
    for (usize& offset : m_levelOffsets)
    {
        offset = 0;
    }
}

void Texture::MoveFrom(Texture& other) noexcept
{
    m_allocator = other.m_allocator;
    m_data = other.m_data;
    m_sizeBytes = other.m_sizeBytes;
    m_width = other.m_width;
    m_height = other.m_height;
    m_mipLevels = other.m_mipLevels;
    m_format = other.m_format;
    for (u32 level = 0; level < kMaxTextureMipLevels; ++level)
    {
        m_levelOffsets[level] = other.m_levelOffsets[level];
        other.m_levelOffsets[level] = 0;
    }

    other.m_allocator = nullptr;
    other.m_data = nullptr;
    other.m_sizeBytes = 0;
    other.m_width = 0;
    other.m_height = 0;
    other.m_mipLevels = 0;
}

namespace
{

[[nodiscard]] math::Color SampleOneLevel(const Texture& texture, f32 u, f32 v, u32 level,
                                         const SamplerDesc& sampler, FilterMode filter) noexcept
{
    const i32 width = texture.MipWidth(level);
    const i32 height = texture.MipHeight(level);
    const f32 wrappedU = WrapNormalized(u, sampler.addressU);
    const f32 wrappedV = WrapNormalized(v, sampler.addressV);

    if (filter == FilterMode::Nearest)
    {
        const i32 x = WrapTexel(static_cast<i32>(std::floor(wrappedU * static_cast<f32>(width))),
                                width, sampler.addressU);
        const i32 y = WrapTexel(static_cast<i32>(std::floor(wrappedV * static_cast<f32>(height))),
                                height, sampler.addressV);
        return texture.GetTexel(level, x, y);
    }

    const f32 x = wrappedU * static_cast<f32>(width) - 0.5f;
    const f32 y = wrappedV * static_cast<f32>(height) - 0.5f;
    const f32 flooredX = std::floor(x);
    const f32 flooredY = std::floor(y);
    const f32 fractionX = x - flooredX;
    const f32 fractionY = y - flooredY;
    const i32 x0 = static_cast<i32>(flooredX);
    const i32 y0 = static_cast<i32>(flooredY);
    const i32 x1 = WrapTexel(x0 + 1, width, sampler.addressU);
    const i32 y1 = WrapTexel(y0 + 1, height, sampler.addressV);
    const i32 wrappedX0 = WrapTexel(x0, width, sampler.addressU);
    const i32 wrappedY0 = WrapTexel(y0, height, sampler.addressV);

    const math::Color c00 = texture.GetTexel(level, wrappedX0, wrappedY0);
    const math::Color c10 = texture.GetTexel(level, x1, wrappedY0);
    const math::Color c01 = texture.GetTexel(level, wrappedX0, y1);
    const math::Color c11 = texture.GetTexel(level, x1, y1);

    const auto blend = [](f32 a, f32 b, f32 cx, f32 cy, f32 fx, f32 fy) noexcept
    {
        const f32 top = LerpChannel(a, cx, fx);
        const f32 bottom = LerpChannel(b, cy, fx);
        return LerpChannel(top, bottom, fy);
    };
    return math::Color{blend(c00.r, c01.r, c10.r, c11.r, fractionX, fractionY),
                       blend(c00.g, c01.g, c10.g, c11.g, fractionX, fractionY),
                       blend(c00.b, c01.b, c10.b, c11.b, fractionX, fractionY),
                       blend(c00.a, c01.a, c10.a, c11.a, fractionX, fractionY)};
}

}  // namespace

math::Color SampleTextureLevel(const Texture& texture, f32 u, f32 v, f32 level,
                               const SamplerDesc& sampler) noexcept
{
    if (!texture.IsValid())
    {
        return math::Color{0.0f, 0.0f, 0.0f, 0.0f};
    }

    const f32 lastLevel = static_cast<f32>(texture.MipLevels() - 1);
    const f32 clampedLevel = std::clamp(std::isfinite(level) ? level : 0.0f, 0.0f, lastLevel);

    if (sampler.mipFilter == FilterMode::Nearest)
    {
        const u32 floorLevel = static_cast<u32>(std::floor(clampedLevel));
        const FilterMode filter = floorLevel == 0 ? sampler.magFilter : sampler.minFilter;
        return SampleOneLevel(texture, u, v, floorLevel, sampler, filter);
    }

    const u32 level0 = static_cast<u32>(std::floor(clampedLevel));
    const u32 level1 = std::min(level0 + 1, texture.MipLevels() - 1);
    const f32 fraction = clampedLevel - static_cast<f32>(level0);
    const FilterMode filter0 = level0 == 0 ? sampler.magFilter : sampler.minFilter;
    const FilterMode filter1 = level1 == 0 ? sampler.magFilter : sampler.minFilter;
    const math::Color color0 = SampleOneLevel(texture, u, v, level0, sampler, filter0);
    if (fraction <= 0.0f || level1 == level0)
    {
        return color0;
    }
    const math::Color color1 = SampleOneLevel(texture, u, v, level1, sampler, filter1);
    return math::Color{
        LerpChannel(color0.r, color1.r, fraction), LerpChannel(color0.g, color1.g, fraction),
        LerpChannel(color0.b, color1.b, fraction), LerpChannel(color0.a, color1.a, fraction)};
}

math::Color SampleTexture(const Texture& texture, f32 u, f32 v, const SamplerDesc& sampler) noexcept
{
    return SampleTextureLevel(texture, u, v, 0.0f, sampler);
}

}  // namespace cyber::software
