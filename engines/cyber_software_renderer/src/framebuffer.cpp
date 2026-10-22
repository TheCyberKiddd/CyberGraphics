#include <cyber/software/framebuffer.h>

#include <cstring>
#include <new>
#include <utility>

namespace cyber::software
{

using cyber::core::i64;
using cyber::core::kInvalidIndex;

namespace
{

constexpr i32 kMaxDimension = 16384;
constexpr usize kStorageAlignment = 16;
constexpr usize kMaxSizeBytes = static_cast<usize>(1) << 34;  // 16 GiB guard

bool CheckedMultiply(usize lhs, usize rhs, usize& result) noexcept
{
    if (lhs != 0 && rhs > kInvalidIndex / lhs)
    {
        return false;
    }
    result = lhs * rhs;
    return true;
}

f32 ReadFloat(const u8* pointer) noexcept
{
    f32 value = 0.0f;
    std::memcpy(&value, pointer, sizeof(value));
    return value;
}

void WriteFloat(u8* pointer, f32 value) noexcept
{
    std::memcpy(pointer, &value, sizeof(value));
}

void StorePixel(u8* pointer, PixelFormat format, const math::Color& color) noexcept
{
    if (format == PixelFormat::RGBA8Unorm)
    {
        pointer[0] = QuantizeUnorm8(color.r);
        pointer[1] = QuantizeUnorm8(color.g);
        pointer[2] = QuantizeUnorm8(color.b);
        pointer[3] = QuantizeUnorm8(color.a);
        return;
    }

    WriteFloat(pointer + 0, color.r);
    WriteFloat(pointer + 4, color.g);
    WriteFloat(pointer + 8, color.b);
    WriteFloat(pointer + 12, color.a);
}

math::Color LoadPixel(const u8* pointer, PixelFormat format) noexcept
{
    if (format == PixelFormat::RGBA8Unorm)
    {
        constexpr f32 kScale = 1.0f / 255.0f;
        return math::Color{
            static_cast<f32>(pointer[0]) * kScale, static_cast<f32>(pointer[1]) * kScale,
            static_cast<f32>(pointer[2]) * kScale, static_cast<f32>(pointer[3]) * kScale};
    }

    return math::Color{ReadFloat(pointer + 0), ReadFloat(pointer + 4), ReadFloat(pointer + 8),
                       ReadFloat(pointer + 12)};
}

i32 ClampToInt(i64 value, i32 low, i32 high) noexcept
{
    if (value < low)
    {
        return low;
    }
    if (value > high)
    {
        return high;
    }
    return static_cast<i32>(value);
}

}  // namespace

u8 QuantizeUnorm8(f32 value) noexcept
{
    if (!(value > 0.0f))
    {
        return 0;
    }
    if (value >= 1.0f)
    {
        return 255;
    }
    return static_cast<u8>(value * 255.0f + 0.5f);
}

u8 EncodeSrgb8(f32 value) noexcept
{
    if (!(value > 0.0f))
    {
        return 0;
    }
    if (value >= 1.0f)
    {
        return 255;
    }
    return QuantizeUnorm8(math::LinearToSrgb(value));
}

Framebuffer::~Framebuffer()
{
    Release();
}

Framebuffer::Framebuffer(Framebuffer&& other) noexcept
{
    MoveFrom(other);
}

Framebuffer& Framebuffer::operator=(Framebuffer&& other) noexcept
{
    if (this != &other)
    {
        Release();
        MoveFrom(other);
    }
    return *this;
}

core::Result<Framebuffer, core::Error> Framebuffer::Create(const FramebufferDesc& desc,
                                                           memory::Allocator& allocator) noexcept
{
    if (!IsValidPixelFormat(desc.format))
    {
        return core::Result<Framebuffer, core::Error>::Err(
            core::Error{core::ErrorCode::InvalidArgument, "invalid pixel format"});
    }
    if (desc.width <= 0 || desc.height <= 0 || desc.width > kMaxDimension ||
        desc.height > kMaxDimension)
    {
        return core::Result<Framebuffer, core::Error>::Err(core::Error{
            core::ErrorCode::InvalidArgument, "framebuffer dimensions must be in [1, 16384]"});
    }

    const usize width = static_cast<usize>(desc.width);
    const usize height = static_cast<usize>(desc.height);
    const usize pixelBytes = PixelSizeBytes(desc.format);
    usize tightStride = 0;
    if (!CheckedMultiply(width, pixelBytes, tightStride))
    {
        return core::Result<Framebuffer, core::Error>::Err(
            core::Error{core::ErrorCode::Overflow, "framebuffer row size overflows"});
    }

    const usize strideBytes = desc.strideBytes == 0 ? tightStride : desc.strideBytes;
    if (strideBytes < tightStride)
    {
        return core::Result<Framebuffer, core::Error>::Err(core::Error{
            core::ErrorCode::InvalidArgument, "framebuffer stride is smaller than a row"});
    }
    if (desc.format == PixelFormat::RGBA32F && (strideBytes % alignof(f32)) != 0)
    {
        return core::Result<Framebuffer, core::Error>::Err(
            core::Error{core::ErrorCode::InvalidArgument,
                        "RGBA32F framebuffer stride must be a multiple of 4"});
    }

    usize sizeBytes = 0;
    if (!CheckedMultiply(strideBytes, height, sizeBytes) || sizeBytes > kMaxSizeBytes)
    {
        return core::Result<Framebuffer, core::Error>::Err(
            core::Error{core::ErrorCode::Overflow, "framebuffer size overflows"});
    }

    void* storage = allocator.Allocate(sizeBytes, kStorageAlignment);
    if (storage == nullptr)
    {
        return core::Result<Framebuffer, core::Error>::Err(
            core::Error{core::ErrorCode::Internal, "failed to allocate framebuffer storage"});
    }

    Framebuffer framebuffer;
    framebuffer.m_allocator = &allocator;
    framebuffer.m_data = static_cast<u8*>(storage);
    framebuffer.m_sizeBytes = sizeBytes;
    framebuffer.m_strideBytes = strideBytes;
    framebuffer.m_width = desc.width;
    framebuffer.m_height = desc.height;
    framebuffer.m_format = desc.format;
    std::memset(framebuffer.m_data, 0, sizeBytes);
    return core::Result<Framebuffer, core::Error>::Ok(std::move(framebuffer));
}

bool Framebuffer::IsValid() const noexcept
{
    return m_data != nullptr && m_width > 0 && m_height > 0 && m_strideBytes > 0 &&
           IsValidPixelFormat(m_format);
}

i32 Framebuffer::Width() const noexcept
{
    return m_width;
}

i32 Framebuffer::Height() const noexcept
{
    return m_height;
}

PixelFormat Framebuffer::Format() const noexcept
{
    return m_format;
}

usize Framebuffer::StrideBytes() const noexcept
{
    return m_strideBytes;
}

usize Framebuffer::DataSizeBytes() const noexcept
{
    return m_sizeBytes;
}

u8* Framebuffer::Data() noexcept
{
    return m_data;
}

const u8* Framebuffer::Data() const noexcept
{
    return m_data;
}

u8* Framebuffer::Row(i32 y) noexcept
{
    if (m_data == nullptr || y < 0 || y >= m_height)
    {
        return nullptr;
    }
    return m_data + static_cast<usize>(y) * m_strideBytes;
}

const u8* Framebuffer::Row(i32 y) const noexcept
{
    if (m_data == nullptr || y < 0 || y >= m_height)
    {
        return nullptr;
    }
    return m_data + static_cast<usize>(y) * m_strideBytes;
}

bool Framebuffer::Contains(i32 x, i32 y) const noexcept
{
    return x >= 0 && y >= 0 && x < m_width && y < m_height && m_data != nullptr;
}

void Framebuffer::Clear(const math::Color& color) noexcept
{
    ClearRect(0, 0, m_width, m_height, color);
}

void Framebuffer::ClearRect(i32 x, i32 y, i32 width, i32 height, const math::Color& color) noexcept
{
    if (!IsValid() || width <= 0 || height <= 0)
    {
        return;
    }

    const i32 x0 = ClampToInt(x, 0, m_width);
    const i32 y0 = ClampToInt(y, 0, m_height);
    const i32 x1 = ClampToInt(static_cast<i64>(x) + width, 0, m_width);
    const i32 y1 = ClampToInt(static_cast<i64>(y) + height, 0, m_height);
    if (x0 >= x1 || y0 >= y1)
    {
        return;
    }

    const usize pixelBytes = PixelSizeBytes(m_format);
    for (i32 row = y0; row < y1; ++row)
    {
        u8* pixels = Row(row) + static_cast<usize>(x0) * pixelBytes;
        for (i32 column = x0; column < x1; ++column)
        {
            StorePixel(pixels, m_format, color);
            pixels += pixelBytes;
        }
    }
}

void Framebuffer::SetPixel(i32 x, i32 y, const math::Color& color) noexcept
{
    if (!Contains(x, y))
    {
        return;
    }
    const usize pixelBytes = PixelSizeBytes(m_format);
    StorePixel(Row(y) + static_cast<usize>(x) * pixelBytes, m_format, color);
}

math::Color Framebuffer::GetPixel(i32 x, i32 y) const noexcept
{
    if (!Contains(x, y))
    {
        return math::Color{0.0f, 0.0f, 0.0f, 0.0f};
    }
    const usize pixelBytes = PixelSizeBytes(m_format);
    return LoadPixel(Row(y) + static_cast<usize>(x) * pixelBytes, m_format);
}

void Framebuffer::Blit(const Framebuffer& source, i32 destX, i32 destY) noexcept
{
    if (!IsValid() || !source.IsValid())
    {
        return;
    }

    const i64 sourceX0 = destX < 0 ? -static_cast<i64>(destX) : 0;
    const i64 sourceY0 = destY < 0 ? -static_cast<i64>(destY) : 0;
    const i64 sourceX1 = source.Width();
    const i64 sourceY1 = source.Height();
    if (sourceX0 >= sourceX1 || sourceY0 >= sourceY1)
    {
        return;
    }

    const i64 destX0 = static_cast<i64>(destX) + sourceX0;
    const i64 destY0 = static_cast<i64>(destY) + sourceY0;
    i64 copyWidth = sourceX1 - sourceX0;
    i64 copyHeight = sourceY1 - sourceY0;
    if (destX0 + copyWidth > m_width)
    {
        copyWidth = static_cast<i64>(m_width) - destX0;
    }
    if (destY0 + copyHeight > m_height)
    {
        copyHeight = static_cast<i64>(m_height) - destY0;
    }
    if (copyWidth <= 0 || copyHeight <= 0)
    {
        return;
    }

    const usize pixelBytes = PixelSizeBytes(m_format);
    if (m_format == source.m_format)
    {
        const usize rowBytes = static_cast<usize>(copyWidth) * pixelBytes;
        for (i64 row = 0; row < copyHeight; ++row)
        {
            u8* destination =
                Row(static_cast<i32>(destY0 + row)) + static_cast<usize>(destX0) * pixelBytes;
            const u8* sourceRow = source.Row(static_cast<i32>(sourceY0 + row)) +
                                  static_cast<usize>(sourceX0) * pixelBytes;
            std::memcpy(destination, sourceRow, rowBytes);
        }
        return;
    }

    for (i64 row = 0; row < copyHeight; ++row)
    {
        for (i64 column = 0; column < copyWidth; ++column)
        {
            const math::Color color = source.GetPixel(static_cast<i32>(sourceX0 + column),
                                                      static_cast<i32>(sourceY0 + row));
            SetPixel(static_cast<i32>(destX0 + column), static_cast<i32>(destY0 + row), color);
        }
    }
}

core::Status<core::Error> Framebuffer::Convert(const Framebuffer& source,
                                               Framebuffer& destination) noexcept
{
    if (!source.IsValid() || !destination.IsValid())
    {
        return core::Status<core::Error>::Err(
            core::Error{core::ErrorCode::InvalidArgument, "framebuffer is invalid"});
    }
    if (source.Width() != destination.Width() || source.Height() != destination.Height())
    {
        return core::Status<core::Error>::Err(
            core::Error{core::ErrorCode::InvalidArgument, "framebuffer dimensions do not match"});
    }
    if (&source == &destination)
    {
        return core::Status<core::Error>::Ok();
    }

    destination.Blit(source, 0, 0);
    return core::Status<core::Error>::Ok();
}

void Framebuffer::Release() noexcept
{
    if (m_allocator != nullptr && m_data != nullptr)
    {
        m_allocator->Deallocate(m_data);
    }
    m_allocator = nullptr;
    m_data = nullptr;
    m_sizeBytes = 0;
    m_strideBytes = 0;
    m_width = 0;
    m_height = 0;
    m_format = PixelFormat::RGBA8Unorm;
}

void Framebuffer::MoveFrom(Framebuffer& other) noexcept
{
    m_allocator = other.m_allocator;
    m_data = other.m_data;
    m_sizeBytes = other.m_sizeBytes;
    m_strideBytes = other.m_strideBytes;
    m_width = other.m_width;
    m_height = other.m_height;
    m_format = other.m_format;

    other.m_allocator = nullptr;
    other.m_data = nullptr;
    other.m_sizeBytes = 0;
    other.m_strideBytes = 0;
    other.m_width = 0;
    other.m_height = 0;
    other.m_format = PixelFormat::RGBA8Unorm;
}

}  // namespace cyber::software
