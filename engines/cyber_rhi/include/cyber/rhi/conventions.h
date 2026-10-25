#pragma once

#include <cyber/rhi/capabilities.h>

namespace cyber::rhi
{

// Conventions at the RHI boundary. These are the engine's conventions, not any
// API's: the same values the software renderer uses, so a scene renders the same
// way on both paths and a projection matrix can be shared.
//
//   Clip-space Y        +Y up
//   NDC depth           [0, 1], reverse-Z: near plane = 1, far plane = 0
//   Texture origin      top-left (0, 0), V grows downward
//   View                right-handed, +Y up world
//   Vectors and matrices column vectors, column-major storage
//   Units               meters, radians, linear RGB
//
// A backend is responsible for converting its API to these at the edge. The
// three predicates below are the single source of truth for which conversions a
// backend needs, so no backend hard-codes its own answer and no two of them
// disagree about, say, whether depth is -1..1.
//
// See ADR 0002 for the math conventions and ADR 0013 for the boundary decision.

// Clip-space Y points up in the RHI. Always true; a named constant so a backend
// reads a value instead of an assumption.
inline constexpr bool kClipSpaceYUp = true;

// Depth maps the near plane to 1 and the far plane to 0.
inline constexpr bool kReverseZ = true;

// The NDC depth range, as its inclusive endpoints.
inline constexpr float kNdcDepthMin = 0.0f;
inline constexpr float kNdcDepthMax = 1.0f;

// Texture coordinate (0, 0) is the top-left texel and V grows downward.
inline constexpr bool kTextureOriginTopLeft = true;

// The view transform is right-handed.
inline constexpr bool kRightHandedView = true;

// A backend whose clip space has +Y down must have its projection negated in Y,
// or its viewport height negated, before it draws RHI-convention vertices.
[[nodiscard]] constexpr bool BackendNeedsClipSpaceYFlip(BackendKind kind) noexcept
{
    switch (kind)
    {
    case BackendKind::Null:
        return false;
    case BackendKind::D3D12:
        return true;
    case BackendKind::Vulkan:
        return true;
    case BackendKind::Metal:
        return false;
    case BackendKind::WebGpu:
        return false;
    case BackendKind::Count:
        break;
    }
    return false;
}

// True when the backend's depth range already matches the RHI's 0..1 reverse-Z
// mapping. A false answer means the backend remaps depth in its projection
// upload or its viewport depth range; Metal's native range is -1..1.
[[nodiscard]] constexpr bool BackendUsesZeroToOneDepth(BackendKind kind) noexcept
{
    switch (kind)
    {
    case BackendKind::Null:
    case BackendKind::D3D12:
    case BackendKind::Vulkan:
    case BackendKind::WebGpu:
        return true;
    case BackendKind::Metal:
        return false;
    case BackendKind::Count:
        break;
    }
    // An unknown kind answers the RHI's own convention, which needs no remap.
    return true;
}

// True when the backend's texture origin already matches the RHI's top-left
// origin. A false answer means the backend flips V inside its samplers.
[[nodiscard]] constexpr bool BackendUsesTopLeftTextureOrigin(BackendKind kind) noexcept
{
    switch (kind)
    {
    case BackendKind::Null:
    case BackendKind::D3D12:
    case BackendKind::Vulkan:
    case BackendKind::WebGpu:
        return true;
    case BackendKind::Metal:
        return false;
    case BackendKind::Count:
        break;
    }
    // An unknown kind answers the RHI's own convention, which needs no remap.
    return true;
}

}  // namespace cyber::rhi
