#include <cyber/rhi/capabilities.h>
#include <cyber/rhi/d3d12.h>

#if defined(CYBER_RHI_HAS_D3D12)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <cyber/core/types.h>
#include <cyber/rhi/backend.h>

#include <dxgi1_6.h>

// The Windows SDK declares `interface` as a macro for its COM bindings, and
// `DeviceCreation::interface` is a field of this renderer's own interface table.
// The macro is dropped here, after the SDK headers have been parsed, so the RHI
// keeps its name and no later include in this file sees a rewrite.
#ifdef interface
#undef interface
#endif

#include <cstring>

namespace cyber::rhi
{
namespace
{

using cyber::core::u32;
using cyber::core::u64;
using cyber::core::u8;

// The D3D12 API version this backend targets, reported in decimal as encoded
// major * 100 + minor so an adapter's value compares and prints without a
// struct. Feature level is a per-device property and is reported by
// `Capabilities` once a device exists.
inline constexpr u32 kD3D12ApiVersion = 1200;

// Everything this increment needs from COM without a third-party wrapper: one
// owning pointer that releases on every exit path. `ComPtr` is deliberately not
// used because the rest of the renderer holds ownership explicitly, and this
// keeps the backend's ownership visible in the diff.
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
    // The address a COM creation call writes into. Reset first, so a failed
    // second call cannot leave the previous object owned twice.
    [[nodiscard]] void** PutVoid() noexcept
    {
        Reset();
        return reinterpret_cast<void**>(&m_pointer);
    }
    [[nodiscard]] T* operator->() const noexcept
    {
        return m_pointer;
    }
    [[nodiscard]] bool IsValid() const noexcept
    {
        return m_pointer != nullptr;
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
void CopyAdapterName(const wchar_t* wide, char (&out)[kAdapterNameBytes]) noexcept
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
// driver that enumerates thousands cannot make this loop unbounded.
inline constexpr u32 kMaxHardwareAdapters = 16;

// Writes one adapter description, returning a filled `AdapterInfo`.
//
// `driverVersion` stays 0 here on purpose: DXGI exposes a UMD version only
// through `CheckInterfaceSupport`, which reports it for D3D10/11 interfaces and
// not for D3D12. Reporting a D3D11 driver's version as the D3D12 driver's would
// be a lie in a bug report, so the field is filled by the device once Phase 14
// creates one.
AdapterInfo DescribeAdapter(IDXGIAdapter1& adapter) noexcept
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

// Appends `info` if the list has room, honouring the "return the total, write
// what fits" contract.
void Store(AdapterInfo value, AdapterInfo* out, u32 capacity, u32* written, u32* total) noexcept
{
    if (out != nullptr && *written < capacity)
    {
        out[*written] = value;
        ++*written;
    }
    ++*total;
}

u32 EnumerateD3D12Adapters(AdapterInfo* out, u32 capacity) noexcept
{
    ComOwner<IDXGIFactory4> factory;
    if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory4), factory.PutVoid())))
    {
        return 0;
    }

    u32 written = 0;
    u32 total = 0;

    for (u32 index = 0; index < kMaxHardwareAdapters; ++index)
    {
        IDXGIAdapter1* adapter = nullptr;
        if (factory->EnumAdapters1(index, &adapter) == DXGI_ERROR_NOT_FOUND)
        {
            break;
        }
        if (adapter == nullptr)
        {
            break;
        }

        DXGI_ADAPTER_DESC1 desc{};
        const bool isSoftware =
            SUCCEEDED(adapter->GetDesc1(&desc)) && (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0;
        // WARP is added last and exactly once, from EnumWarpAdapter, so it is
        // skipped here whether or not this driver lists it.
        if (!isSoftware)
        {
            Store(DescribeAdapter(*adapter), out, capacity, &written, &total);
        }
        adapter->Release();
    }

    // The software adapter last, so "first adapter" means the discrete GPU and
    // "last adapter" is the one CI can rely on when it has no hardware.
    ComOwner<IDXGIAdapter1> warp;
    if (SUCCEEDED(factory->EnumWarpAdapter(__uuidof(IDXGIAdapter1), warp.PutVoid())))
    {
        Store(DescribeAdapter(*warp.Get()), out, capacity, &written, &total);
    }

    // A machine without a driver still has WARP, so `total` is at least one on
    // every supported Windows version. Zero means DXGI itself failed, which the
    // caller sees as "no adapters" rather than as a crash.
    return total;
}

}  // namespace

// The table this backend advertises. A named function rather than a lambda so
// the description reads as a table and not as a statement.
[[nodiscard]] Backend MakeD3D12Backend() noexcept
{
    Backend value{};
    value.kind = BackendKind::D3D12;
    value.name = "d3d12";
    value.apiVersion = kD3D12ApiVersion;
    // Null until the device increment lands. `Device::Create` rejects an
    // incomplete table with `InvalidArgument` before any backend code runs, which
    // is what `rhi_d3d12_adapters` pins.
    value.interface = nullptr;
    value.createDevice = nullptr;
    value.destroyDevice = nullptr;
    // Adapters are the one thing that works without a device.
    value.enumerateAdapters = &EnumerateD3D12Adapters;
    return value;
}

const Backend* D3D12Backend() noexcept
{
    // Static and immutable after construction, so it is a constant rather than
    // global mutable state: the adapter list is gathered per call by
    // `EnumerateD3D12Adapters` and nothing is stored between calls.
    static const Backend backend = MakeD3D12Backend();
    return &backend;
}

}  // namespace cyber::rhi

#else

namespace cyber::rhi
{

const Backend* D3D12Backend() noexcept
{
    return nullptr;
}

}  // namespace cyber::rhi

#endif
