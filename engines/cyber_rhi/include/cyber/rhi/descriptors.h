#pragma once

#include <cyber/core/types.h>

namespace cyber::rhi
{

using cyber::core::u32;
using cyber::core::u64;
using cyber::core::u8;

struct Backend;

// Work a queue accepts. A device exposes at most one queue of each class in
// Phase 10; multi-queue submission and cross-queue waits arrive with command
// lists in Phase 13.
enum class QueueKind : u8
{
    Graphics = 0,
    Compute,
    Copy,
    // Count of enumerators, for table tests. Not a queue class.
    Count
};

// Enumerators `QueueKind` has, `Count` included.
inline constexpr u32 kQueueKindCount = static_cast<u32>(QueueKind::Count);

// Human-readable queue name, or "unknown". The string has static lifetime.
[[nodiscard]] constexpr const char* QueueKindName(QueueKind kind) noexcept
{
    switch (kind)
    {
    case QueueKind::Graphics:
        return "graphics";
    case QueueKind::Compute:
        return "compute";
    case QueueKind::Copy:
        return "copy";
    case QueueKind::Count:
        break;
    }
    return "unknown";
}

// How much checking a backend performs. The null backend validates every call at
// every level, because validation is its purpose; a real backend maps this onto
// its own debug layer and ignores it in shipping builds.
enum class ValidationLevel : u8
{
    // No backend-side checks. Argument errors are found by the driver or not at
    // all.
    Off = 0,
    // Handle validity, descriptor ranges, and capability limits.
    Standard,
    // Standard plus state tracking, leak reports, and per-call logging.
    Exhaustive
};

// A value that means "wait until the fence reaches the value, with no timeout".
inline constexpr u64 kInfiniteWaitNanoseconds = 0xFFFFFFFFFFFFFFFFULL;

// Description of a device to create. A null or invalid backend is
// `ErrorCode::InvalidArgument`.
struct DeviceDesc
{
    // Backend to create the device on. Non-owning; the backend outlives the
    // device.
    const Backend* backend = nullptr;
    // Index into the backend's adapter list, as returned by
    // `EnumerateAdapters`. Out of range is `ErrorCode::OutOfRange`.
    u32 adapterIndex = 0;
    // Requested validation. Defaults to `Standard` so a debug build is checked
    // without the caller opting in.
    ValidationLevel validation = ValidationLevel::Standard;
    // Optional name for logs and debuggers. Borrowed, must outlive the device or
    // be null.
    const char* label = nullptr;
};

// Description of a fence. A fence is created at a value and only moves forward.
struct FenceDesc
{
    // Value the timeline starts at. Signals below it are ignored as already
    // satisfied.
    u64 initialValue = 0;
    // Optional name for logs and debuggers. Borrowed.
    const char* label = nullptr;
};

// A surface the caller owns and the RHI borrows: a window, a view, or a display.
//
// The RHI never creates or destroys one; `cyber_platform` does. The native handle
// is opaque here so no backend header leaks into a caller. Phase 13 consumes this
// to create a swapchain.
struct SurfaceDesc
{
    // Non-owning native handle: `HWND` on Windows, `NSView*` on Apple platforms,
    // a `wl_surface*`/`xcb_window_t` on Linux. Null is invalid.
    const void* nativeWindow = nullptr;
    // Surface size in pixels, not points. Zero is invalid.
    u32 width = 0;
    u32 height = 0;
};

}  // namespace cyber::rhi
