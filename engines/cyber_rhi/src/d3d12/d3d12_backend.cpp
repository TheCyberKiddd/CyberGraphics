#include <cyber/rhi/capabilities.h>
#include <cyber/rhi/d3d12.h>

#if defined(CYBER_RHI_HAS_D3D12)

#include "d3d12_com.h"
#include "d3d12_device.h"

#include <cyber/core/types.h>
#include <cyber/rhi/backend.h>

namespace cyber::rhi
{
namespace
{

using cyber::core::u32;

// Appends `value` if the list has room, honouring the "return the total, write
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
    detail::ComOwner<IDXGIFactory4> factory;
    if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory4), factory.PutVoid())))
    {
        return 0;
    }

    u32 written = 0;
    u32 total = 0;

    for (u32 index = 0; index < detail::kMaxHardwareAdapters; ++index)
    {
        IDXGIAdapter1* adapter = nullptr;
        if (factory->EnumAdapters1(index, &adapter) == DXGI_ERROR_NOT_FOUND || adapter == nullptr)
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
            Store(detail::DescribeAdapter(*adapter), out, capacity, &written, &total);
        }
        adapter->Release();
    }

    // The software adapter last, so "first adapter" means the discrete GPU and
    // "last adapter" is the one CI can rely on when it has no hardware.
    detail::ComOwner<IDXGIAdapter1> warp;
    if (SUCCEEDED(factory->EnumWarpAdapter(__uuidof(IDXGIAdapter1), warp.PutVoid())))
    {
        Store(detail::DescribeAdapter(*warp.Get()), out, capacity, &written, &total);
    }

    // A machine without a driver still has WARP, so `total` is at least one on
    // every supported Windows version. Zero means DXGI itself failed, which the
    // caller sees as "no adapters" rather than as a crash.
    return total;
}

}  // namespace

const Backend* D3D12Backend() noexcept
{
    // Static and immutable after construction, so it is a constant rather than
    // global mutable state: adapters are gathered per call by
    // `EnumerateD3D12Adapters` and nothing is cached between calls.
    static const Backend backend = []() noexcept
    {
        Backend value{};
        value.kind = BackendKind::D3D12;
        value.name = "d3d12";
        value.apiVersion = detail::kD3D12ApiVersion;
        value.interface = detail::D3D12Interface();
        value.enumerateAdapters = &EnumerateD3D12Adapters;
        value.createDevice = &detail::CreateD3D12Device;
        value.destroyDevice = &detail::DestroyD3D12Device;
        return value;
    }();
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
