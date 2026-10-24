#pragma once

#include <cyber/core/error.h>
#include <cyber/core/result.h>
#include <cyber/memory/allocator.h>
#include <cyber/memory/types.h>

namespace cyber::software
{

using cyber::core::f32;
using cyber::core::i32;

struct DepthBufferDesc
{
    i32 width = 0;
    i32 height = 0;
    // Bytes between the first sample of consecutive rows. 0 means tightly packed
    // (width * 4). A supplied stride must be at least that value and a multiple
    // of 4.
    core::usize strideBytes = 0;
};

// Move-only CPU depth target with allocator-owned storage.
//
// Depth is the reverse-Z value produced by the projection matrices in
// `cyber_math`: 1.0 at the near plane, 0.0 at the far plane. The buffer is
// cleared to 0 and the visibility test is greater-or-equal, so smaller depth
// values never occlude larger ones and a cleared buffer loses to every sample
// the clipper admits. NaN never passes the test.
//
// Create is the only operation that allocates. Every other operation is
// non-allocating. Coordinates are top-left origin and half-open: [0, width) and
// [0, height). Out-of-range reads return 0 and out-of-range writes are ignored.
class DepthBuffer
{
public:
    DepthBuffer() noexcept = default;
    ~DepthBuffer();

    DepthBuffer(const DepthBuffer&) = delete;
    DepthBuffer& operator=(const DepthBuffer&) = delete;

    DepthBuffer(DepthBuffer&& other) noexcept;
    DepthBuffer& operator=(DepthBuffer&& other) noexcept;

    // Width and height must be in [1, 16384], the stride must be a multiple of
    // 4 and large enough for one row, and the total byte size must not
    // overflow. Returns InvalidArgument otherwise, or Internal when allocation
    // fails.
    [[nodiscard]] static core::Result<DepthBuffer, core::Error>
    Create(const DepthBufferDesc& desc, memory::Allocator& allocator) noexcept;

    [[nodiscard]] bool IsValid() const noexcept;
    [[nodiscard]] i32 Width() const noexcept;
    [[nodiscard]] i32 Height() const noexcept;
    [[nodiscard]] core::usize StrideBytes() const noexcept;

    // Total owned bytes, including stride padding. Zero when invalid.
    [[nodiscard]] core::usize DataSizeBytes() const noexcept;

    // First sample of the image. Null when invalid. The pointer is non-owning
    // and stays valid until the buffer is destroyed, moved, or assigned.
    [[nodiscard]] f32* Data() noexcept;
    [[nodiscard]] const f32* Data() const noexcept;

    // First sample of row y. Returns nullptr when y is outside [0, height).
    [[nodiscard]] f32* Row(i32 y) noexcept;
    [[nodiscard]] const f32* Row(i32 y) const noexcept;

    // Writes depth to width samples of every row. Padding bytes are untouched.
    void Clear(f32 depth = 0.0f) noexcept;

    // Reverse-Z visibility test. When depth is greater than or equal to the
    // stored sample, writes depth and returns true; otherwise leaves the buffer
    // untouched and returns false. Out-of-range coordinates and NaN return false.
    bool TestAndWrite(i32 x, i32 y, f32 depth) noexcept;

    // Reads the stored depth. Out-of-range coordinates return 0.
    [[nodiscard]] f32 Get(i32 x, i32 y) const noexcept;

private:
    void Release() noexcept;
    void MoveFrom(DepthBuffer& other) noexcept;

    memory::Allocator* m_allocator = nullptr;
    f32* m_data = nullptr;
    core::usize m_sizeBytes = 0;
    core::usize m_strideBytes = 0;
    i32 m_width = 0;
    i32 m_height = 0;
};

}  // namespace cyber::software
