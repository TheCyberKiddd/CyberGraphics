#include <cyber/software/depth_buffer.h>

#include <algorithm>
#include <cstring>
#include <new>
#include <utility>

namespace cyber::software
{

using cyber::core::kInvalidIndex;
using cyber::core::usize;

namespace
{

constexpr i32 kMaxDimension = 16384;
constexpr usize kStorageAlignment = 16;
constexpr usize kSampleBytes = sizeof(f32);
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

}  // namespace

DepthBuffer::~DepthBuffer()
{
    Release();
}

DepthBuffer::DepthBuffer(DepthBuffer&& other) noexcept
{
    MoveFrom(other);
}

DepthBuffer& DepthBuffer::operator=(DepthBuffer&& other) noexcept
{
    if (this != &other)
    {
        Release();
        MoveFrom(other);
    }
    return *this;
}

core::Result<DepthBuffer, core::Error> DepthBuffer::Create(const DepthBufferDesc& desc,
                                                           memory::Allocator& allocator) noexcept
{
    if (desc.width <= 0 || desc.height <= 0 || desc.width > kMaxDimension ||
        desc.height > kMaxDimension)
    {
        return core::Result<DepthBuffer, core::Error>::Err(core::Error{
            core::ErrorCode::InvalidArgument, "depth dimensions must be in [1, 16384]"});
    }

    const usize width = static_cast<usize>(desc.width);
    const usize height = static_cast<usize>(desc.height);
    usize tightStride = 0;
    if (!CheckedMultiply(width, kSampleBytes, tightStride))
    {
        return core::Result<DepthBuffer, core::Error>::Err(
            core::Error{core::ErrorCode::Overflow, "depth row size overflows"});
    }

    const usize strideBytes = desc.strideBytes == 0 ? tightStride : desc.strideBytes;
    if (strideBytes < tightStride || (strideBytes % kSampleBytes) != 0)
    {
        return core::Result<DepthBuffer, core::Error>::Err(
            core::Error{core::ErrorCode::InvalidArgument,
                        "depth stride must be at least one row and a multiple of 4"});
    }

    usize sizeBytes = 0;
    if (!CheckedMultiply(strideBytes, height, sizeBytes) || sizeBytes > kMaxSizeBytes)
    {
        return core::Result<DepthBuffer, core::Error>::Err(
            core::Error{core::ErrorCode::Overflow, "depth buffer size overflows"});
    }

    void* storage = allocator.Allocate(sizeBytes, kStorageAlignment);
    if (storage == nullptr)
    {
        return core::Result<DepthBuffer, core::Error>::Err(
            core::Error{core::ErrorCode::Internal, "failed to allocate depth storage"});
    }

    DepthBuffer buffer;
    buffer.m_allocator = &allocator;
    buffer.m_data = static_cast<f32*>(storage);
    buffer.m_sizeBytes = sizeBytes;
    buffer.m_strideBytes = strideBytes;
    buffer.m_width = desc.width;
    buffer.m_height = desc.height;
    std::memset(buffer.m_data, 0, sizeBytes);
    return core::Result<DepthBuffer, core::Error>::Ok(std::move(buffer));
}

bool DepthBuffer::IsValid() const noexcept
{
    return m_data != nullptr && m_width > 0 && m_height > 0 && m_strideBytes > 0;
}

i32 DepthBuffer::Width() const noexcept
{
    return m_width;
}

i32 DepthBuffer::Height() const noexcept
{
    return m_height;
}

usize DepthBuffer::StrideBytes() const noexcept
{
    return m_strideBytes;
}

usize DepthBuffer::DataSizeBytes() const noexcept
{
    return m_sizeBytes;
}

f32* DepthBuffer::Data() noexcept
{
    return m_data;
}

const f32* DepthBuffer::Data() const noexcept
{
    return m_data;
}

f32* DepthBuffer::Row(i32 y) noexcept
{
    if (m_data == nullptr || y < 0 || y >= m_height)
    {
        return nullptr;
    }
    return reinterpret_cast<f32*>(reinterpret_cast<core::u8*>(m_data) +
                                  static_cast<usize>(y) * m_strideBytes);
}

const f32* DepthBuffer::Row(i32 y) const noexcept
{
    if (m_data == nullptr || y < 0 || y >= m_height)
    {
        return nullptr;
    }
    return reinterpret_cast<const f32*>(reinterpret_cast<const core::u8*>(m_data) +
                                        static_cast<usize>(y) * m_strideBytes);
}

void DepthBuffer::Clear(f32 depth) noexcept
{
    if (!IsValid())
    {
        return;
    }
    const usize rowBytes = static_cast<usize>(m_width) * kSampleBytes;
    if (depth == 0.0f)
    {
        // The reverse-Z far value is all-zero bytes, so a clear is a memset.
        for (i32 y = 0; y < m_height; ++y)
        {
            std::memset(Row(y), 0, rowBytes);
        }
        return;
    }
    for (i32 y = 0; y < m_height; ++y)
    {
        std::fill_n(Row(y), static_cast<usize>(m_width), depth);
    }
}

bool DepthBuffer::TestAndWrite(i32 x, i32 y, f32 depth) noexcept
{
    if (m_data == nullptr || x < 0 || y < 0 || x >= m_width || y >= m_height)
    {
        return false;
    }
    f32& stored = Row(y)[x];
    if (!(depth >= stored))
    {
        return false;
    }
    stored = depth;
    return true;
}

f32 DepthBuffer::Get(i32 x, i32 y) const noexcept
{
    if (m_data == nullptr || x < 0 || y < 0 || x >= m_width || y >= m_height)
    {
        return 0.0f;
    }
    return Row(y)[x];
}

void DepthBuffer::Release() noexcept
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
}

void DepthBuffer::MoveFrom(DepthBuffer& other) noexcept
{
    m_allocator = other.m_allocator;
    m_data = other.m_data;
    m_sizeBytes = other.m_sizeBytes;
    m_strideBytes = other.m_strideBytes;
    m_width = other.m_width;
    m_height = other.m_height;

    other.m_allocator = nullptr;
    other.m_data = nullptr;
    other.m_sizeBytes = 0;
    other.m_strideBytes = 0;
    other.m_width = 0;
    other.m_height = 0;
}

}  // namespace cyber::software
