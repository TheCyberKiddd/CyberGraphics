#pragma once

#if defined(CYBER_RHI_HAS_D3D12)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <cyber/core/types.h>
#include <cyber/rhi/capabilities.h>

// Every SDK header this backend needs is parsed here, before the `interface`
// macro is dropped below: `d3d12.h`, `dxgi1_6.h`, and `d3dcompiler.h` all declare
// COM types with that keyword, so a translation unit that included one of them
// afterwards would see a broken header.
#include <d3d12.h>
#include <d3dcompiler.h>
#include <dxgi1_6.h>

// The Windows SDK declares `interface` as a macro for its COM bindings, and
// `DeviceCreation::interface` is a field of this renderer's own interface table.
// The macro is dropped here, after the SDK headers have been parsed, so every
// backend source keeps the RHI's name.
#ifdef interface
#undef interface
#endif

#include <cstring>

namespace cyber::rhi::detail
{

using cyber::core::u32;
using cyber::core::u64;

// The D3D12 API version this backend targets, reported in decimal as encoded
// major * 100 + minor so an adapter's value compares and prints without a
// struct. Feature level is a per-device property and arrives with
// `Capabilities`.
inline constexpr u32 kD3D12ApiVersion = 1200;

// Everything this backend needs from COM without a third-party wrapper: one
// owning pointer that releases on every exit path. `ComPtr` is deliberately not
// used because the rest of the renderer holds ownership explicitly, and this
// keeps it visible in the diff.
template <typename T>
class ComOwner
{
public:
    ComOwner() noexcept = default;
    ComOwner(const ComOwner&) = delete;
    ComOwner& operator=(const ComOwner&) = delete;

    ~ComOwner() noexcept
    {
        Reset();
    }

    [[nodiscard]] T* Get() const noexcept
    {
        return m_pointer;
    }
    [[nodiscard]] T* operator->() const noexcept
    {
        return m_pointer;
    }
    [[nodiscard]] bool IsValid() const noexcept
    {
        return m_pointer != nullptr;
    }

    // The address a COM creation call writes into. Reset first, so a failed
    // second call cannot leave the previous object owned twice.
    [[nodiscard]] void** PutVoid() noexcept
    {
        Reset();
        return reinterpret_cast<void**>(&m_pointer);
    }

    // Takes ownership of a reference the caller already holds, releasing whatever
    // was owned before. Used where an enumeration call hands back a raw pointer.
    void Attach(T* pointer) noexcept
    {
        Reset();
        m_pointer = pointer;
    }

    void Reset() noexcept
    {
        if (m_pointer != nullptr)
        {
            m_pointer->Release();
            m_pointer = nullptr;
        }
    }

private:
    T* m_pointer = nullptr;
};

// Copies a wide adapter description into the fixed narrow buffer the RHI
// reports. DXGI names are ASCII in practice, so a byte-for-byte narrowing is
// correct and avoids a code-page conversion the process locale would influence.
// The result is always null-terminated, and an empty name becomes "unknown" so a
// caller never logs a blank adapter.
inline void CopyAdapterName(const wchar_t* wide, char (&out)[kAdapterNameBytes]) noexcept
{
    u32 written = 0;
    if (wide != nullptr)
    {
        while (wide[written] != L'\0' && written + 1 < kAdapterNameBytes)
        {
            const wchar_t character = wide[written];
            const bool printable = character >= 0x20 && character <= 0x7E;
            out[written] = printable ? static_cast<char>(character) : '?';
            ++written;
        }
    }
    out[written] = '\0';
    if (written == 0)
    {
        const char fallback[] = "unknown";
        std::memcpy(out, fallback, sizeof(fallback));
    }
}

// Number of hardware adapters DXGI reports, capped so a machine with a broken
// driver that enumerates thousands cannot make a walk unbounded. Enumeration and
// device creation share the cap because they share the order.
inline constexpr u32 kMaxHardwareAdapters = 16;

// Microsoft's PCI vendor id. Every WARP adapter reports it and no hardware
// vendor does.
inline constexpr u32 kMicrosoftVendorId = 0x1414;

// Writes one adapter description, filled from a live DXGI adapter. `driverVersion`
// stays 0: DXGI exposes a UMD version only for D3D10/11 interfaces, and
// reporting that as the D3D12 driver's would be a lie in a bug report.
inline AdapterInfo DescribeAdapter(IDXGIAdapter1& adapter) noexcept
{
    AdapterInfo info{};
    info.backend = BackendKind::D3D12;
    info.apiVersion = kD3D12ApiVersion;

    DXGI_ADAPTER_DESC1 desc{};
    if (SUCCEEDED(adapter.GetDesc1(&desc)))
    {
        CopyAdapterName(desc.Description, info.name);
        info.vendorId = desc.VendorId;
        info.deviceId = desc.DeviceId;
        info.dedicatedVideoMemoryBytes = static_cast<u64>(desc.DedicatedVideoMemory);
        info.sharedSystemMemoryBytes = static_cast<u64>(desc.SharedSystemMemory);
    }
    return info;
}

}  // namespace cyber::rhi::detail

#endif
