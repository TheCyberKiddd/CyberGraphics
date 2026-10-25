#pragma once

#include <cyber/core/error.h>
#include <cyber/core/result.h>
#include <cyber/core/types.h>
#include <cyber/memory/allocator.h>
#include <cyber/rhi/backend.h>
#include <cyber/rhi/capabilities.h>
#include <cyber/rhi/descriptors.h>
#include <cyber/rhi/handle.h>

namespace cyber::rhi
{

using cyber::core::Error;
using cyber::core::Result;
using cyber::core::Status;
using cyber::core::u64;

// The GPU device: the owner of every RHI object and the only way to create or
// destroy one. A device is a value that owns a backend's per-device state, not a
// handle, because it owns the table that resolves handles.
//
// Lifetime rules (ADR 0013):
//
// - The device owns every object created from it. Destroying the device waits
//   for submitted work and then releases what remains; there is no per-object
//   reference count.
// - Destroying an object invalidates its handle. A stale handle is reported as
//   `ErrorCode::InvalidArgument`, never resolved into a newer object.
// - Handles must not outlive their device. Using one after the device is gone is
//   a caller bug the type system cannot catch; in debug builds the handle fails
//   to resolve rather than corrupting memory, because nothing is left to
//   resolve it.
// - A moved-from device is empty: it has no capabilities, returns invalid queue
//   handles, and rejects every call.
//
// Threading rules: `Capabilities`, `Queue`, fence creation, `FenceValue`,
// `SignalFence`, and `WaitFence` are safe to call from any thread. The device
// does its own locking; a backend whose native API needs external
// synchronization implements it internally.
class Device
{
public:
    Device() noexcept = default;
    ~Device();

    Device(const Device&) = delete;
    Device& operator=(const Device&) = delete;

    Device(Device&& other) noexcept;
    Device& operator=(Device&& other) noexcept;

    // Creates a device on `desc.backend` with `allocator` backing any
    // bookkeeping the backend keeps. Returns:
    // - `ErrorCode::InvalidArgument` when the backend is null, its tables are
    //   incomplete, or `desc.adapterIndex` names an adapter the backend cannot
    //   enumerate.
    // - `ErrorCode::Unsupported` when the backend exists but cannot create a
    //   device on this adapter.
    // - `ErrorCode::Internal` when allocation or backend setup fails.
    // The returned device owns backend state; nothing is shared with another
    // device.
    [[nodiscard]] static Result<Device, Error> Create(const DeviceDesc& desc,
                                                      memory::Allocator& allocator) noexcept;

    // True when this device owns backend state.
    [[nodiscard]] bool IsValid() const noexcept;

    // Name of the backend that created the device, or "none" for an empty one.
    [[nodiscard]] const char* BackendName() const noexcept;

    // Guarantees of this device. A value type; safe to call from any thread and
    // safe to keep. An empty device reports zeroed limits and no features.
    [[nodiscard]] Capabilities Capabilities() const noexcept;

    // Handle of the queue for `kind`, or an invalid handle when this device has
    // no such queue. Queue handles live as long as the device.
    [[nodiscard]] QueueHandle Queue(QueueKind kind) const noexcept;

    // Creates a fence whose timeline starts at `desc.initialValue`.
    // `ErrorCode::InvalidArgument` for an empty device; `ErrorCode::Internal`
    // when the backend cannot allocate the object.
    [[nodiscard]] Result<FenceHandle, Error> CreateFence(const FenceDesc& desc) noexcept;

    // Destroys a fence and invalidates its handle. A stale or invalid handle is
    // `ErrorCode::InvalidArgument`.
    [[nodiscard]] Status<Error> DestroyFence(FenceHandle fence) noexcept;

    // Advances `fence` to `value`. A value at or below the current one is
    // accepted and ignored, because the timeline never moves backwards; a stale
    // handle is `ErrorCode::InvalidArgument`.
    [[nodiscard]] Status<Error> SignalFence(FenceHandle fence, u64 value) noexcept;

    // Waits until `fence` reaches at least `value`. `timeoutNanoseconds` is
    // `0` to poll and `kInfiniteWaitNanoseconds` to block. Returns
    // `ErrorCode::NotFound` when the timeout expires first, which a caller must
    // treat separately from the `ErrorCode::InvalidArgument` a stale handle
    // produces.
    [[nodiscard]] Status<Error>
    WaitFence(FenceHandle fence, u64 value,
              u64 timeoutNanoseconds = kInfiniteWaitNanoseconds) noexcept;

    // Current value of `fence`, or 0 for a stale handle.
    [[nodiscard]] u64 FenceValue(FenceHandle fence) const noexcept;

    // Blocks until the device has finished all submitted work. Phase 10 submits
    // nothing, so this is a drain point rather than a synchronization tool; from
    // Phase 13 it also retires objects awaiting GPU completion. Returns
    // `ErrorCode::InvalidArgument` for an empty device.
    [[nodiscard]] Status<Error> WaitIdle() noexcept;

private:
    void Release() noexcept;
    void MoveFrom(Device& other) noexcept;

    const Backend* m_backend = nullptr;
    const DeviceInterface* m_interface = nullptr;
    void* m_internal = nullptr;
};

}  // namespace cyber::rhi
