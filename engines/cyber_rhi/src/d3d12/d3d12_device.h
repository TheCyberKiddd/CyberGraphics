#pragma once

#if defined(CYBER_RHI_HAS_D3D12)

#include <cyber/core/error.h>
#include <cyber/core/result.h>
#include <cyber/memory/allocator.h>
#include <cyber/rhi/backend.h>

namespace cyber::rhi::detail
{

// Per-device table of the Direct3D 12 backend. Static lifetime. Every entry is
// non-null, because `Device::Create` rejects an incomplete table before any
// backend code runs.
[[nodiscard]] const DeviceInterface* D3D12Interface() noexcept;

// Creates a device on the adapter at `desc.adapterIndex`, using the same order
// `D3D12Backend().enumerateAdapters` reports: hardware adapters in DXGI's
// preference order, then WARP last.
//
// `ErrorCode::OutOfRange` for an adapter index that does not exist,
// `ErrorCode::Unsupported` when the adapter cannot create a D3D12 device, and
// `ErrorCode::Internal` when the device or one of its queues cannot be created.
[[nodiscard]] Result<DeviceCreation, Error>
CreateD3D12Device(const DeviceDesc& desc, memory::Allocator& allocator) noexcept;

// Releases everything `CreateD3D12Device` produced. Safe on null.
void DestroyD3D12Device(void* internal) noexcept;

}  // namespace cyber::rhi::detail

#endif
