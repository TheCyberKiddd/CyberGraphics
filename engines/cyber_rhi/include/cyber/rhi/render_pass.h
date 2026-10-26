#pragma once

#include <cyber/core/error.h>
#include <cyber/core/result.h>
#include <cyber/core/types.h>
#include <cyber/memory/span.h>
#include <cyber/rhi/format.h>
#include <cyber/rhi/handle.h>
#include <cyber/rhi/pipeline.h>

namespace cyber::rhi
{

using cyber::core::Error;
using cyber::core::Status;
using cyber::core::u32;
using cyber::core::u8;

// A render pass is the window in which pixels are written. It names its
// attachments, says what to do with their old contents, and says what to do
// with the new ones. Everything a graphics pipeline writes happens inside one.
//
// The pass resolves its own extent and sample count from the attachments it is
// given, so a caller cannot state an extent that disagrees with the textures.
// Every pipeline bound inside the pass must match that extent and sample count,
// which is checked when the pipeline is set.

// What happens to an attachment's previous contents when the pass begins.
enum class LoadOp : u8
{
    // Keep the previous contents. The attachment must already hold something
    // defined: the backend cannot invent contents it never wrote.
    Load = 0,
    // Overwrite every texel with the pass's clear value.
    Clear,
    // Leave the previous contents undefined. Cheapest, and correct whenever the
    // pass writes every texel it later reads. A `DontCare` load combined with a
    // `StoreOp::DontCare` store means the attachment is never touched at all.
    DontCare,
    // Count of enumerators, for table tests. Not an operation.
    Count
};

// Number of real load operations, not counting `Count`.
inline constexpr u32 kLoadOpCount = static_cast<u32>(LoadOp::Count);

// Human-readable load operation, or "unknown". The string has static lifetime.
[[nodiscard]] constexpr const char* LoadOpName(LoadOp op) noexcept
{
    switch (op)
    {
    case LoadOp::Load:
        return "load";
    case LoadOp::Clear:
        return "clear";
    case LoadOp::DontCare:
        return "dont_care";
    case LoadOp::Count:
        break;
    }
    return "unknown";
}

// What happens to an attachment's contents when the pass ends.
enum class StoreOp : u8
{
    // Keep the result. Required for an attachment a later pass or a present
    // reads.
    Store = 0,
    // Discard the result. The contents become undefined, which is what a
    // depth buffer written once and never read wants.
    DontCare,
    // Count of enumerators, for table tests. Not an operation.
    Count
};

// Number of real store operations, not counting `Count`.
inline constexpr u32 kStoreOpCount = static_cast<u32>(StoreOp::Count);

// Human-readable store operation, or "unknown". The string has static lifetime.
[[nodiscard]] constexpr const char* StoreOpName(StoreOp op) noexcept
{
    switch (op)
    {
    case StoreOp::Store:
        return "store";
    case StoreOp::DontCare:
        return "dont_care";
    case StoreOp::Count:
        break;
    }
    return "unknown";
}

// Largest finite single-precision float, used to reject infinities without
// pulling in a floating-point classification library.
inline constexpr float kMaxFiniteFloat = 3.402823466e+38F;

// True when `value` is neither NaN nor an infinity. A clear value that is NaN
// passes every API's validation and produces a frame nobody can debug, so the
// RHI rejects it at the descriptor.
[[nodiscard]] constexpr bool IsFiniteFloat(float value) noexcept
{
    return value == value && value >= -kMaxFiniteFloat && value <= kMaxFiniteFloat;
}

// Value a color attachment is cleared to when the pass begins.
//
// The value is in linear RGB, as every color the renderer handles is (see
// `docs/math-conventions.md`): an 8-bit sRGB attachment receives the value
// already encoded by the backend, and a float attachment receives it unchanged.
// A format that cannot represent a component clamps or quantises it.
struct ClearColorValue
{
    float red = 0.0f;
    float green = 0.0f;
    float blue = 0.0f;
    float alpha = 1.0f;

    // A clear value from four components. No validation: `IsFinite` reports it.
    [[nodiscard]] static constexpr ClearColorValue Of(float r, float g, float b,
                                                      float a = 1.0f) noexcept
    {
        return ClearColorValue{r, g, b, a};
    }

    // True when every component is finite.
    [[nodiscard]] constexpr bool IsFinite() const noexcept
    {
        return IsFiniteFloat(red) && IsFiniteFloat(green) && IsFiniteFloat(blue) &&
               IsFiniteFloat(alpha);
    }

    friend constexpr bool operator==(ClearColorValue lhs, ClearColorValue rhs) noexcept
    {
        return lhs.red == rhs.red && lhs.green == rhs.green && lhs.blue == rhs.blue &&
               lhs.alpha == rhs.alpha;
    }

    friend constexpr bool operator!=(ClearColorValue lhs, ClearColorValue rhs) noexcept
    {
        return !(lhs == rhs);
    }
};

// Value a depth or depth-stencil attachment is cleared to when the pass begins.
//
// `depth` defaults to 1 because the project renders with reverse-Z in a 0..1
// range: the far plane is 1 and the near plane is 0, so clearing to 1 is
// clearing to "infinitely far away", which is the value a comparison test must
// lose against.
struct ClearDepthStencilValue
{
    float depth = 1.0f;
    u8 stencil = 0;

    [[nodiscard]] static constexpr ClearDepthStencilValue Of(float d, u8 s = 0) noexcept
    {
        return ClearDepthStencilValue{d, s};
    }

    // True when the depth is finite and inside the 0..1 range.
    [[nodiscard]] constexpr bool IsValid() const noexcept
    {
        return IsFiniteFloat(depth) && depth >= 0.0f && depth <= 1.0f;
    }

    friend constexpr bool operator==(ClearDepthStencilValue lhs,
                                     ClearDepthStencilValue rhs) noexcept
    {
        return lhs.depth == rhs.depth && lhs.stencil == rhs.stencil;
    }

    friend constexpr bool operator!=(ClearDepthStencilValue lhs,
                                     ClearDepthStencilValue rhs) noexcept
    {
        return !(lhs == rhs);
    }
};

// One color attachment of a pass.
struct ColorAttachment
{
    // View the pass writes. Must be a live view whose texture has
    // `TextureUsage::RenderTarget`, and must not also appear as another
    // attachment of the same pass.
    TextureViewHandle view;
    // Optional view that receives the resolved image when the attachment is
    // multisampled. An invalid handle means no resolve. The resolve target must
    // have one sample and the same format and extent as `view`.
    TextureViewHandle resolveView;
    // What to do with the previous contents.
    LoadOp loadOp = LoadOp::Clear;
    // What to do with the result.
    StoreOp storeOp = StoreOp::Store;
    // Value written when `loadOp` is `Clear`. Ignored otherwise.
    ClearColorValue clearValue{};
};

// The depth or depth-stencil attachment of a pass.
struct DepthStencilAttachment
{
    // View the pass reads for depth testing and may write. Must be a live view
    // whose texture has `TextureUsage::DepthStencil`.
    TextureViewHandle view;
    // What to do with the previous depth contents.
    LoadOp depthLoadOp = LoadOp::Clear;
    // What to do with the resulting depth contents.
    StoreOp depthStoreOp = StoreOp::Store;
    // What to do with the previous stencil contents. Must be `DontCare` for a
    // format with no stencil channel.
    LoadOp stencilLoadOp = LoadOp::DontCare;
    // What to do with the resulting stencil contents.
    StoreOp stencilStoreOp = StoreOp::DontCare;
    // True when the pass only tests against depth and never writes it. A
    // read-only attachment may not be cleared, because a clear is a write.
    bool readOnly = false;
    // Values written when the matching load operation is `Clear`.
    ClearDepthStencilValue clearValue{};
};

// Description of a render pass. A zeroed descriptor is invalid: it has no color
// attachment and no depth attachment.
struct RenderPassDesc
{
    // Color attachments in slot order, from one to `kMaxColorAttachments`.
    memory::Span<const ColorAttachment> colorAttachments{};
    // Depth attachment, used only when `hasDepthStencil` is true.
    DepthStencilAttachment depthStencil{};
    // True when the pass has a depth or depth-stencil attachment.
    bool hasDepthStencil = false;
    // Optional name for logs, debuggers, and GPU markers. Borrowed.
    const char* label = nullptr;
};

// What a pass reports about itself once it has begun. `valid` is false when no
// pass is open.
struct RenderPassInfo
{
    bool valid = false;
    u32 colorAttachmentCount = 0;
    // Extent every pipeline in the pass must match, in texels.
    u32 width = 0;
    u32 height = 0;
    // Sample count every attachment and pipeline must share.
    u32 sampleCount = 1;
    bool hasDepthStencil = false;
    // Format of the depth attachment, or `Format::Count` when there is none.
    Format depthStencilFormat = Format::Count;
    // Format of each color attachment in slot order. Entries past
    // `colorAttachmentCount` are `Format::Count`.
    Format colorFormats[kMaxColorAttachments] = {};
};

// Checks the parts of a render pass description that need no access to the
// attachments' textures: attachment presence, duplicates, load and store
// combinations, read-only depth rules, and clear values. A backend adds the
// shape, format, and state checks it can only make after resolving a view.
//
// Returns `ErrorCode::InvalidArgument` for a description no backend can record,
// with a message naming the rule that failed. Nothing is allocated.
[[nodiscard]] Status<Error> ValidateRenderPassDesc(const RenderPassDesc& desc) noexcept;

// Largest sample count any attachment and pipeline in one pass may declare.
inline constexpr u32 kMaxRenderPassSampleCount = 8;

// True when `op` is a real store operation.
[[nodiscard]] constexpr bool IsValidStoreOp(StoreOp op) noexcept
{
    return static_cast<u32>(op) < kStoreOpCount;
}

// True when `op` is a real load operation.
[[nodiscard]] constexpr bool IsValidLoadOp(LoadOp op) noexcept
{
    return static_cast<u32>(op) < kLoadOpCount;
}

}  // namespace cyber::rhi
