#pragma once

#include <cyber/core/error.h>
#include <cyber/core/result.h>
#include <cyber/core/types.h>
#include <cyber/memory/allocator.h>
#include <cyber/memory/span.h>
#include <cyber/rhi/capabilities.h>
#include <cyber/rhi/descriptors.h>
#include <cyber/rhi/handle.h>

namespace cyber::rhi
{

using cyber::core::Error;
using cyber::core::Result;
using cyber::core::Status;

// A backend is a table of function pointers, not a class hierarchy. The reasons
// are in ADR 0013: the runtime has RTTI disabled, the Phase 25 plugin ABI is a
// C table, and a missing method is visible in a diff when the table is written
// out by hand.
//
// A backend never allocates without an explicit allocator, never throws, and
// never keeps global mutable state. Everything a backend owns hangs off the
// `internal` pointer of a device, so two devices on the same backend share
// nothing.

struct DeviceInterface;

// What a backend hands back when a device is created. Both members are required.
struct DeviceCreation
{
    // Per-device function table, with static lifetime.
    const DeviceInterface* interface = nullptr;
    // Backend-private device state, owned by the device and released by
    // `Backend::destroyDevice`.
    void* internal = nullptr;
};

// Per-device function table. Every method is required: a null entry is a
// backend bug that the device creation path rejects.
struct DeviceInterface
{
    // Capabilities of this device. Must be safe to call from any thread and must
    // not allocate. `internal` is never null.
    Capabilities (*capabilities)(const void* internal) noexcept;
    // Handle of the queue for a class, or an invalid handle when the backend has
    // none. A kind outside the enum is out of range and yields an invalid handle.
    QueueHandle (*queue)(const void* internal, QueueKind kind) noexcept;
    // Creates a fence. `out` must be non-null.
    Status<Error> (*createFence)(void* internal, const FenceDesc& desc, FenceHandle* out) noexcept;
    // Destroys a fence. A stale handle is `ErrorCode::InvalidArgument`.
    Status<Error> (*destroyFence)(void* internal, FenceHandle fence) noexcept;
    // Advances a fence's timeline. Values below the current value are ignored;
    // the timeline never goes backwards.
    Status<Error> (*signalFence)(void* internal, FenceHandle fence, u64 value) noexcept;
    // Waits until the fence reaches `value`. Returns `ErrorCode::NotFound` on
    // timeout and `ErrorCode::InvalidArgument` for a stale handle.
    Status<Error> (*waitFence)(void* internal, FenceHandle fence, u64 value,
                               u64 timeoutNanoseconds) noexcept;
    // Current value of a fence, or 0 for a stale handle.
    u64 (*fenceValue)(const void* internal, FenceHandle fence) noexcept;
    // Blocks until the device has finished all submitted work. Phase 10 has no
    // submission, so every backend completes immediately.
    Status<Error> (*waitIdle)(void* internal) noexcept;
};

// A graphics backend. Obtained from the backend's own entry point (for example
// `NullBackend()`), never constructed by a caller.
struct Backend
{
    // Which API this backend drives.
    BackendKind kind = BackendKind::Null;
    // Human-readable name. Static lifetime.
    const char* name = "unknown";
    // API version the backend targets, or 0 when the concept does not apply.
    u32 apiVersion = 0;
    // Per-device table this backend implements. Static lifetime.
    const DeviceInterface* interface = nullptr;
    // Writes up to `out.Size()` adapters and returns the number written. The
    // return value may exceed the span for a backend with more adapters than fit,
    // so a caller can size its buffer and call again. Must not allocate.
    u32 (*enumerateAdapters)(AdapterInfo* out, u32 capacity) noexcept;
    // Creates a device on adapter `desc.adapterIndex`, using `allocator` for
    // backend-owned bookkeeping. Returns `ErrorCode::Internal` when a required
    // table entry is missing or the device cannot be created, and
    // `ErrorCode::OutOfRange` for an adapter index that does not exist.
    Result<DeviceCreation, Error> (*createDevice)(const DeviceDesc& desc,
                                                  memory::Allocator& allocator) noexcept;
    // Releases everything `createDevice` produced, after calling `waitIdle`.
    // Never null for a valid backend.
    void (*destroyDevice)(void* internal) noexcept;
};

// Lists the adapters a backend exposes into `out`, returning the number written.
// A null backend or a null enumerator returns 0. This is a thin convenience over
// `Backend::enumerateAdapters` that cannot overflow the span.
[[nodiscard]] inline u32 EnumerateAdapters(const Backend& backend,
                                           memory::Span<AdapterInfo> out) noexcept
{
    if (backend.enumerateAdapters == nullptr)
    {
        return 0;
    }
    // An empty span is a valid query: it asks for the count without writing a
    // single adapter.
    return backend.enumerateAdapters(out.Data(), static_cast<u32>(out.Size()));
}

// The validation backend: it drives no hardware, allocates nothing on a GPU, and
// checks every call it accepts. Use it for tests, for a headless run, and for
// comparing an image the GPU produced with one the software renderer produced.
//
// The returned pointer has static lifetime and is never null.
[[nodiscard]] const Backend* NullBackend() noexcept;

}  // namespace cyber::rhi
