#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <cstdio>
#include <cstdlib>
#include <d3d12.h>
#include <dxgi1_6.h>

namespace
{

void PrintResult(const char* operation, HRESULT result, ID3D12Device* device) noexcept
{
    const HRESULT removedReason = device != nullptr ? device->GetDeviceRemovedReason() : S_OK;
    std::printf("%s: HRESULT=0x%08lX removedReason=0x%08lX\n", operation,
                static_cast<unsigned long>(result), static_cast<unsigned long>(removedReason));
}

}  // namespace

int main()
{
    IDXGIFactory1* factory = nullptr;
    HRESULT result =
        CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&factory));
    PrintResult("CreateDXGIFactory1", result, nullptr);
    if (FAILED(result) || factory == nullptr)
    {
        return 1;
    }

    IDXGIAdapter1* adapter = nullptr;
    for (UINT index = 0;; ++index)
    {
        result = factory->EnumAdapters1(index, &adapter);
        if (result == DXGI_ERROR_NOT_FOUND)
        {
            std::printf("No hardware adapter is available for the probe.\n");
            factory->Release();
            return 1;
        }
        if (FAILED(result) || adapter == nullptr)
        {
            PrintResult("EnumAdapters1", result, nullptr);
            factory->Release();
            return 1;
        }

        DXGI_ADAPTER_DESC1 desc{};
        result = adapter->GetDesc1(&desc);
        if (FAILED(result))
        {
            PrintResult("IDXGIAdapter1::GetDesc1", result, nullptr);
            adapter->Release();
            factory->Release();
            return 1;
        }
        if ((desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) == 0)
        {
            std::printf("Adapter: %ls (vendor 0x%04X, device 0x%04X)\n", desc.Description,
                        desc.VendorId, desc.DeviceId);
            break;
        }
        adapter->Release();
        adapter = nullptr;
    }
    factory->Release();

    ID3D12Device* device = nullptr;
    result = D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device),
                               reinterpret_cast<void**>(&device));
    PrintResult("D3D12CreateDevice", result, device);
    if (FAILED(result) || device == nullptr)
    {
        adapter->Release();
        return 1;
    }

    D3D12_COMMAND_QUEUE_DESC queueDesc{};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    ID3D12CommandQueue* queue = nullptr;
    result = device->CreateCommandQueue(&queueDesc, __uuidof(ID3D12CommandQueue),
                                        reinterpret_cast<void**>(&queue));
    PrintResult("CreateCommandQueue(DIRECT)", result, device);
    if (FAILED(result) || queue == nullptr)
    {
        device->Release();
        adapter->Release();
        return 1;
    }

    ID3D12CommandAllocator* allocator = nullptr;
    result = device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                            __uuidof(ID3D12CommandAllocator),
                                            reinterpret_cast<void**>(&allocator));
    PrintResult("CreateCommandAllocator(DIRECT)", result, device);
    if (FAILED(result) || allocator == nullptr)
    {
        queue->Release();
        device->Release();
        adapter->Release();
        return 1;
    }

    ID3D12GraphicsCommandList* list = nullptr;
    result = device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator, nullptr,
                                       __uuidof(ID3D12GraphicsCommandList),
                                       reinterpret_cast<void**>(&list));
    PrintResult("CreateCommandList(DIRECT)", result, device);
    if (FAILED(result) || list == nullptr)
    {
        allocator->Release();
        queue->Release();
        device->Release();
        adapter->Release();
        return 1;
    }

    result = list->Close();
    PrintResult("ID3D12GraphicsCommandList::Close", result, device);

    list->Release();
    allocator->Release();
    queue->Release();
    device->Release();
    adapter->Release();
    return FAILED(result) ? 1 : 0;
}
