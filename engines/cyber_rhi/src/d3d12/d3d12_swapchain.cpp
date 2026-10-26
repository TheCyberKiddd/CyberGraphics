#include "d3d12_internal.h"

#if defined(CYBER_RHI_HAS_D3D12)

#include <cyber/rhi/swapchain.h>

namespace cyber::rhi::detail
{
namespace
{

using cyber::core::Error;
using cyber::core::ErrorCode;
using cyber::core::Result;
using cyber::core::Status;
using cyber::core::u32;
using cyber::core::u64;
using cyber::memory::kInvalidHandleIndex;

[[nodiscard]] Status<Error> StatusError(ErrorCode code, const char* message) noexcept
{
    return Status<Error>::Err(Error{code, message});
}

[[nodiscard]] u32 NextGeneration(u32 current) noexcept
{
    const u32 next = current + 1;
    return next == 0 ? 1 : next;
}

[[nodiscard]] D3D12_CPU_DESCRIPTOR_HANDLE CpuViewHandle(D3D12Device& device, u32 index) noexcept
{
    D3D12_CPU_DESCRIPTOR_HANDLE handle = device.viewHeap->GetCPUDescriptorHandleForHeapStart();
    handle.ptr += static_cast<SIZE_T>(index) * device.viewDescriptorSize;
    return handle;
}

// Presents pick an effect: `FLIP_DISCARD` for the modes that may drop a frame,
// `FLIP_SEQUENTIAL` for the one that must show every frame in order.
[[nodiscard]] DXGI_SWAP_EFFECT ToSwapEffect(PresentMode mode) noexcept
{
    return mode == PresentMode::Mailbox ? DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL
                                        : DXGI_SWAP_EFFECT_FLIP_DISCARD;
}

// Releases the entries a swapchain put in the device's texture and view tables.
// The image resources belong to the surface, so the texture entries are only
// released here, never through `destroyTexture`.
void ReleaseImages(D3D12Device& device, D3D12SwapChainSlot& slot) noexcept
{
    for (u32 index = 0; index < slot.imageCount && index < kMaxSwapChainImages; ++index)
    {
        if (slot.views[index].IsValid())
        {
            D3D12ViewSlot* view = Resolve(device, slot.views[index]);
            if (view != nullptr)
            {
                view->alive = false;
                view->generation = NextGeneration(view->generation);
            }
            slot.views[index] = TextureViewHandle{};
        }
        if (slot.textures[index].IsValid())
        {
            D3D12TextureSlot* texture = Resolve(device, slot.textures[index]);
            if (texture != nullptr)
            {
                if (texture->resource != nullptr)
                {
                    texture->resource->Release();
                    texture->resource = nullptr;
                }
                texture->alive = false;
                texture->generation = NextGeneration(texture->generation);
            }
            slot.textures[index] = TextureHandle{};
        }
    }
}

// Takes one image from the surface and publishes it as a texture and a view, so
// a caller can put the swapchain image into a render pass with no extra object.
[[nodiscard]] Status<Error> AdoptImage(D3D12Device& device, D3D12SwapChainSlot& slot,
                                       u32 imageIndex) noexcept
{
    if (imageIndex >= kMaxSwapChainImages)
    {
        return StatusError(ErrorCode::OutOfRange,
                           "d3d12 backend: swapchain image index is out of range");
    }
    ID3D12Resource* resource = nullptr;
    if (FAILED(slot.swapChain->GetBuffer(imageIndex, __uuidof(ID3D12Resource),
                                         reinterpret_cast<void**>(&resource))) ||
        resource == nullptr)
    {
        return StatusError(ErrorCode::Internal, "d3d12 backend: swapchain image is unavailable");
    }

    u32 textureIndex = kInvalidHandleIndex;
    if (device.freeTextureSlots.Size() > 0)
    {
        textureIndex = device.freeTextureSlots[device.freeTextureSlots.Size() - 1];
        (void)device.freeTextureSlots.TryResize(device.freeTextureSlots.Size() - 1);
    }
    else
    {
        if (!device.textures.TryPushBack(D3D12TextureSlot{}))
        {
            resource->Release();
            return StatusError(ErrorCode::Internal, "d3d12 backend: texture table growth failed");
        }
        textureIndex = static_cast<u32>(device.textures.Size() - 1);
    }

    D3D12TextureSlot& texture = device.textures[textureIndex];
    texture.generation = NextGeneration(texture.generation);
    texture.alive = true;
    texture.swapChainOwned = true;
    texture.resource = resource;
    texture.desc = TextureDesc{};
    texture.desc.format = slot.format;
    texture.desc.dimension = TextureDimension::Dim2D;
    texture.desc.usage = TextureUsage::RenderTarget;
    texture.desc.width = slot.width;
    texture.desc.height = slot.height;
    texture.desc.depth = 1;
    texture.desc.arrayLayers = 1;
    texture.desc.mipLevels = 1;
    texture.desc.sampleCount = 1;
    for (u32 subresource = 0; subresource < kMaxTrackedSubresources; ++subresource)
    {
        texture.states[subresource] = ResourceState::Present;
        texture.native[subresource] = D3D12_RESOURCE_STATE_PRESENT;
    }

    u32 viewIndex = kInvalidHandleIndex;
    if (device.freeViewSlots.Size() > 0)
    {
        viewIndex = device.freeViewSlots[device.freeViewSlots.Size() - 1];
        (void)device.freeViewSlots.TryResize(device.freeViewSlots.Size() - 1);
    }
    else
    {
        if (!device.views.TryPushBack(D3D12ViewSlot{}))
        {
            texture.resource->Release();
            texture.resource = nullptr;
            texture.alive = false;
            return StatusError(ErrorCode::Internal, "d3d12 backend: view table growth failed");
        }
        viewIndex = static_cast<u32>(device.views.Size() - 1);
    }

    const u32 descriptorIndex = device.viewDescriptorsUsed;
    if (descriptorIndex >= D3D12_MAX_SHADER_VISIBLE_DESCRIPTOR_HEAP_SIZE_TIER_1)
    {
        return StatusError(ErrorCode::OutOfRange,
                           "d3d12 backend: shader-visible descriptor heap is full");
    }
    D3D12_RENDER_TARGET_VIEW_DESC viewDesc{};
    viewDesc.Format = ToDxgiFormat(slot.format);
    viewDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
    device.device->CreateRenderTargetView(resource, &viewDesc,
                                          CpuViewHandle(device, descriptorIndex));
    device.viewDescriptorsUsed = descriptorIndex + 1;

    D3D12ViewSlot& view = device.views[viewIndex];
    view.generation = NextGeneration(view.generation);
    view.alive = true;
    view.texture = TextureHandle{};
    view.texture.index = textureIndex;
    view.texture.generation = texture.generation;
    view.desc = TextureViewDesc{};
    view.desc.texture = view.texture;
    view.desc.format = slot.format;
    view.desc.dimension = TextureDimension::Dim2D;
    view.descriptorIndex = descriptorIndex;

    TextureHandle textureHandle = view.texture;
    TextureViewHandle viewHandle;
    viewHandle.index = viewIndex;
    viewHandle.generation = view.generation;
    slot.textures[imageIndex] = textureHandle;
    slot.views[imageIndex] = viewHandle;
    return Status<Error>::Ok();
}

// Creates the surface and adopts every image in it.
[[nodiscard]] Status<Error> CreateImages(D3D12Device& device, D3D12SwapChainSlot& slot) noexcept
{
    if (slot.swapChain == nullptr)
    {
        return StatusError(ErrorCode::InvalidArgument, "d3d12 backend: swapchain is missing");
    }
    for (u32 index = 0; index < slot.imageCount; ++index)
    {
        const Status<Error> adopted = AdoptImage(device, slot, index);
        if (!adopted)
        {
            ReleaseImages(device, slot);
            return adopted;
        }
    }
    return Status<Error>::Ok();
}

}  // namespace

D3D12SwapChainSlot* Resolve(D3D12Device& device, SwapChainHandle handle) noexcept
{
    if (!handle.IsValid() || handle.index >= device.swapChains.Size())
    {
        return nullptr;
    }
    D3D12SwapChainSlot& slot = device.swapChains[handle.index];
    return slot.alive && slot.generation == handle.generation ? &slot : nullptr;
}

namespace
{

Status<Error> AcquireNextImage(void* internal, SwapChainFrame* out) noexcept
{
    if (internal == nullptr || out == nullptr)
    {
        return StatusError(ErrorCode::InvalidArgument,
                           "d3d12 backend: acquiring an image needs a swapchain and an output");
    }
    auto& slot = *static_cast<D3D12SwapChainSlot*>(internal);
    auto& device = *static_cast<D3D12Device*>(slot.device);
    const std::lock_guard<std::mutex> lock(device.mutex);
    if (slot.swapChain == nullptr)
    {
        return StatusError(ErrorCode::InvalidArgument, "d3d12 backend: swapchain is released");
    }
    if (slot.acquired)
    {
        return StatusError(ErrorCode::AlreadyExists, "d3d12 backend: an image is already acquired");
    }
    const u32 imageIndex = slot.swapChain->GetCurrentBackBufferIndex();
    if (imageIndex >= slot.imageCount)
    {
        return StatusError(ErrorCode::Internal,
                           "d3d12 backend: surface reported an image outside the ring");
    }
    SwapChainFrame frame{};
    frame.valid = true;
    frame.imageIndex = imageIndex;
    frame.texture = slot.textures[imageIndex];
    frame.view = slot.views[imageIndex];
    slot.acquired = true;
    slot.currentImage = imageIndex;
    *out = frame;
    return Status<Error>::Ok();
}

Status<Error> Present(void* internal) noexcept
{
    if (internal == nullptr)
    {
        return StatusError(ErrorCode::InvalidArgument, "d3d12 backend: swapchain is null");
    }
    auto& slot = *static_cast<D3D12SwapChainSlot*>(internal);
    auto& device = *static_cast<D3D12Device*>(slot.device);
    const std::lock_guard<std::mutex> lock(device.mutex);
    if (slot.swapChain == nullptr)
    {
        return StatusError(ErrorCode::InvalidArgument, "d3d12 backend: swapchain is released");
    }
    if (!slot.acquired)
    {
        return StatusError(ErrorCode::InvalidArgument,
                           "d3d12 backend: nothing is acquired to present");
    }

    // Fifo waits for the display; the other two hand the frame over as soon as
    // the queue is done with it.
    const UINT syncInterval = slot.presentMode == PresentMode::Fifo ? 1U : 0U;
    const UINT flags = slot.allowsTearing && slot.presentMode == PresentMode::Immediate
                           ? DXGI_PRESENT_ALLOW_TEARING
                           : 0U;
    const HRESULT presented = slot.swapChain->Present(syncInterval, flags);
    if (presented == DXGI_ERROR_WAS_STILL_DRAWING)
    {
        // The frame was not queued; the caller retries once the GPU catches up.
        return StatusError(ErrorCode::NotFound,
                           "d3d12 backend: the surface is still drawing the last frame");
    }
    if (FAILED(presented))
    {
        return StatusError(ErrorCode::NotFound, "d3d12 backend: present failed");
    }

    D3D12TextureSlot* texture = Resolve(device, slot.textures[slot.currentImage]);
    if (texture != nullptr)
    {
        texture->states[0] = ResourceState::Present;
        texture->native[0] = D3D12_RESOURCE_STATE_PRESENT;
    }
    slot.acquired = false;
    ++slot.presents;
    return Status<Error>::Ok();
}

Status<Error> Resize(void* internal, u32 width, u32 height) noexcept
{
    if (internal == nullptr)
    {
        return StatusError(ErrorCode::InvalidArgument, "d3d12 backend: swapchain is null");
    }
    if (width == 0 || height == 0)
    {
        return StatusError(ErrorCode::OutOfRange,
                           "d3d12 backend: swapchain extent must be non-zero");
    }
    auto& slot = *static_cast<D3D12SwapChainSlot*>(internal);
    auto& device = *static_cast<D3D12Device*>(slot.device);
    const std::lock_guard<std::mutex> lock(device.mutex);
    if (slot.swapChain == nullptr)
    {
        return StatusError(ErrorCode::InvalidArgument, "d3d12 backend: swapchain is released");
    }

    ReleaseImages(device, slot);
    const UINT flags = slot.allowsTearing ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0U;
    if (FAILED(slot.swapChain->ResizeBuffers(slot.imageCount, width, height,
                                             ToDxgiFormat(slot.format), flags)))
    {
        return StatusError(ErrorCode::Internal, "d3d12 backend: swapchain resize failed");
    }
    slot.width = width;
    slot.height = height;
    slot.acquired = false;
    slot.currentImage = 0;
    return CreateImages(device, slot);
}

SwapChainInfo Info(const void* internal) noexcept
{
    if (internal == nullptr)
    {
        return SwapChainInfo{};
    }
    const auto& slot = *static_cast<const D3D12SwapChainSlot*>(internal);
    if (!slot.alive)
    {
        return SwapChainInfo{};
    }
    SwapChainInfo info{};
    info.valid = true;
    info.format = slot.format;
    info.width = slot.width;
    info.height = slot.height;
    info.imageCount = slot.imageCount;
    info.currentImage = slot.currentImage;
    info.presentMode = slot.presentMode;
    info.acquired = slot.acquired;
    return info;
}

[[nodiscard]] const SwapChainInterface* BuildSwapChainInterface() noexcept
{
    static const SwapChainInterface interface = []() noexcept
    {
        SwapChainInterface value{};
        value.acquireNextImage = &AcquireNextImage;
        value.present = &Present;
        value.resize = &Resize;
        value.info = &Info;
        return value;
    }();
    return &interface;
}

}  // namespace

Status<Error> D3D12CreateSwapChain(void* internal, const SwapChainDesc& desc, SwapChainHandle* out,
                                   SwapChainCreation* creation) noexcept
{
    if (internal == nullptr || out == nullptr || creation == nullptr)
    {
        return StatusError(ErrorCode::InvalidArgument,
                           "d3d12 backend: swapchain creation needs a device, an output, and a "
                           "table");
    }
    const Status<Error> validated = ValidateSwapChainDesc(desc);
    if (!validated)
    {
        return validated;
    }
    if (desc.imageCount < kMinSwapChainImages || desc.imageCount > kMaxSwapChainImages)
    {
        return StatusError(ErrorCode::OutOfRange,
                           "d3d12 backend: swapchain image count is outside 2..4");
    }

    auto& device = *static_cast<D3D12Device*>(internal);
    const std::lock_guard<std::mutex> lock(device.mutex);
    if (device.queues[static_cast<u32>(QueueKind::Graphics)].queue == nullptr)
    {
        return StatusError(ErrorCode::InvalidArgument,
                           "d3d12 backend: presenting needs the graphics queue");
    }

    ComOwner<IDXGIFactory2> factory;
    if (FAILED(CreateDXGIFactory2(0, __uuidof(IDXGIFactory2), factory.PutVoid())) ||
        !factory.IsValid())
    {
        return StatusError(ErrorCode::Internal, "d3d12 backend: DXGI factory creation failed");
    }

    const HWND window = static_cast<HWND>(const_cast<void*>(desc.surface.nativeWindow));
    const DXGI_FORMAT format = ToDxgiFormat(desc.format);
    // Tearing is a factory-wide capability and lives on the fifth revision of
    // the factory interface, so it is asked for separately.
    BOOL tearing = FALSE;
    ComOwner<IDXGIFactory5> factory5;
    if (SUCCEEDED(factory->QueryInterface(__uuidof(IDXGIFactory5), factory5.PutVoid())) &&
        factory5.IsValid())
    {
        if (FAILED(factory5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &tearing,
                                                 sizeof(tearing))))
        {
            tearing = FALSE;
        }
    }
    const bool allowsTearing = tearing == TRUE;
    if (desc.presentMode == PresentMode::Immediate && !allowsTearing)
    {
        return StatusError(ErrorCode::Unsupported,
                           "d3d12 backend: this adapter cannot present without tearing");
    }

    DXGI_SWAP_CHAIN_DESC1 surface{};
    surface.Width = desc.surface.width;
    surface.Height = desc.surface.height;
    surface.Format = format;
    surface.Stereo = FALSE;
    surface.SampleDesc.Count = 1;
    surface.SampleDesc.Quality = 0;
    surface.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    surface.BufferCount = desc.imageCount;
    surface.Scaling = DXGI_SCALING_STRETCH;
    surface.SwapEffect = ToSwapEffect(desc.presentMode);
    // A flip-model swapchain cannot use a window's alpha, so the surface is
    // opaque and the window compositor is not asked to blend it.
    surface.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
    surface.Flags = allowsTearing ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0U;

    ComOwner<IDXGISwapChain1> created;
    IDXGISwapChain1* createdPointer = nullptr;
    const HRESULT result =
        factory->CreateSwapChainForHwnd(device.queues[static_cast<u32>(QueueKind::Graphics)].queue,
                                        window, &surface, nullptr, nullptr, &createdPointer);
    created.Attach(createdPointer);
    if (FAILED(result) || !created.IsValid())
    {
        return StatusError(ErrorCode::Internal, "d3d12 backend: swapchain creation failed");
    }
    // Alt+Enter belongs to the application's window policy, not to the renderer.
    (void)factory->MakeWindowAssociation(window, DXGI_MWA_NO_ALT_ENTER);

    ComOwner<IDXGISwapChain3> swapChain;
    if (FAILED(created->QueryInterface(__uuidof(IDXGISwapChain3), swapChain.PutVoid())) ||
        !swapChain.IsValid())
    {
        return StatusError(ErrorCode::Unsupported,
                           "d3d12 backend: swapchain does not implement IDXGISwapChain3");
    }

    u32 index = kInvalidHandleIndex;
    if (device.freeSwapChainSlots.Size() > 0)
    {
        index = device.freeSwapChainSlots[device.freeSwapChainSlots.Size() - 1];
        (void)device.freeSwapChainSlots.TryResize(device.freeSwapChainSlots.Size() - 1);
    }
    else
    {
        if (!device.swapChains.TryPushBack(D3D12SwapChainSlot{}))
        {
            return StatusError(ErrorCode::Internal, "d3d12 backend: swapchain table growth failed");
        }
        index = static_cast<u32>(device.swapChains.Size() - 1);
    }

    D3D12SwapChainSlot& slot = device.swapChains[index];
    slot.generation = NextGeneration(slot.generation);
    slot.alive = true;
    slot.device = &device;
    slot.swapChain = swapChain.Get();
    (void)slot.swapChain->AddRef();
    slot.format = desc.format;
    slot.width = desc.surface.width;
    slot.height = desc.surface.height;
    slot.imageCount = desc.imageCount;
    slot.presentMode = desc.presentMode;
    slot.allowsTearing = allowsTearing;
    slot.acquired = false;
    slot.currentImage = 0;
    slot.presents = 0;
    for (u32 image = 0; image < kMaxSwapChainImages; ++image)
    {
        slot.textures[image] = TextureHandle{};
        slot.views[image] = TextureViewHandle{};
    }

    const Status<Error> images = CreateImages(device, slot);
    if (!images)
    {
        slot.swapChain->Release();
        slot.swapChain = nullptr;
        slot.alive = false;
        return images;
    }

    SwapChainHandle handle;
    handle.index = index;
    handle.generation = slot.generation;
    *out = handle;
    creation->interface = BuildSwapChainInterface();
    creation->internal = &slot;
    return Status<Error>::Ok();
}

Status<Error> D3D12DestroySwapChain(void* internal, SwapChainHandle handle) noexcept
{
    if (internal == nullptr)
    {
        return StatusError(ErrorCode::InvalidArgument, "d3d12 backend: device is null");
    }
    auto& device = *static_cast<D3D12Device*>(internal);
    const std::lock_guard<std::mutex> lock(device.mutex);
    D3D12SwapChainSlot* slot = Resolve(device, handle);
    if (slot == nullptr)
    {
        return StatusError(ErrorCode::InvalidArgument,
                           "d3d12 backend: swapchain handle is stale or invalid");
    }
    ReleaseImages(device, *slot);
    if (slot->swapChain != nullptr)
    {
        slot->swapChain->Release();
        slot->swapChain = nullptr;
    }
    slot->alive = false;
    slot->acquired = false;
    slot->generation = NextGeneration(slot->generation);
    if (!device.freeSwapChainSlots.TryPushBack(handle.index))
    {
        return StatusError(ErrorCode::Internal, "d3d12 backend: swapchain free list growth failed");
    }
    return Status<Error>::Ok();
}

void ReleaseSwapChains(D3D12Device& device) noexcept
{
    for (u32 index = 0; index < device.swapChains.Size(); ++index)
    {
        D3D12SwapChainSlot& slot = device.swapChains[index];
        ReleaseImages(device, slot);
        if (slot.swapChain != nullptr)
        {
            slot.swapChain->Release();
            slot.swapChain = nullptr;
        }
        slot.alive = false;
    }
}

}  // namespace cyber::rhi::detail

#endif
