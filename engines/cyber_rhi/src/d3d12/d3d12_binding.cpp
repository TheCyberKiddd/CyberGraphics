#include "d3d12_internal.h"

#if defined(CYBER_RHI_HAS_D3D12)

#include <cyber/rhi/binding.h>
#include <cyber/rhi/shader.h>

// The HLSL compiler that ships with the Windows SDK is included through
// `d3d12_com.h`: it is an OS component, not a third-party dependency, and
// Phase 16 replaces it with the project's own shader pipeline.
#include <cstring>

namespace cyber::rhi::detail
{
namespace
{

using cyber::core::Error;
using cyber::core::ErrorCode;
using cyber::core::Status;
using cyber::core::u32;
using cyber::core::u64;
using cyber::core::usize;
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

[[nodiscard]] D3D12_CPU_DESCRIPTOR_HANDLE CpuHandleAt(D3D12Device& device, u32 index) noexcept
{
    D3D12_CPU_DESCRIPTOR_HANDLE handle = device.viewHeap->GetCPUDescriptorHandleForHeapStart();
    handle.ptr += static_cast<SIZE_T>(index) * device.viewDescriptorSize;
    return handle;
}

[[nodiscard]] D3D12_CPU_DESCRIPTOR_HANDLE SamplerCpuAt(D3D12Device& device, u32 index) noexcept
{
    D3D12_CPU_DESCRIPTOR_HANDLE handle = device.samplerHeap->GetCPUDescriptorHandleForHeapStart();
    handle.ptr += static_cast<SIZE_T>(index) * device.samplerDescriptorSize;
    return handle;
}

// Compile target for a stage. Every stage the RHI names has one; the shader model
// is 5.1 because that is what feature level 11_0 guarantees.
[[nodiscard]] const char* TargetFor(ShaderStage stage) noexcept
{
    switch (stage)
    {
    case ShaderStage::Vertex:
        return "vs_5_1";
    case ShaderStage::Pixel:
        return "ps_5_1";
    case ShaderStage::Compute:
        return "cs_5_1";
    case ShaderStage::Count:
        break;
    }
    return nullptr;
}

// D3D12's descriptor tables hold one range each in this backend: every binding of
// a group lives in one CBV/SRV/UAV table that the group's block spans.
[[nodiscard]] u32 LayoutDescriptorCount(const D3D12LayoutSlot& layout) noexcept
{
    return layout.descriptorCount;
}

}  // namespace

D3D12ShaderSlot* Resolve(D3D12Device& device, ShaderHandle handle) noexcept
{
    if (!handle.IsValid() || handle.index >= device.shaders.Size())
    {
        return nullptr;
    }
    D3D12ShaderSlot& slot = device.shaders[handle.index];
    return slot.alive && slot.generation == handle.generation ? &slot : nullptr;
}

D3D12LayoutSlot* Resolve(D3D12Device& device, BindGroupLayoutHandle handle) noexcept
{
    if (!handle.IsValid() || handle.index >= device.layouts.Size())
    {
        return nullptr;
    }
    D3D12LayoutSlot& slot = device.layouts[handle.index];
    return slot.alive && slot.generation == handle.generation ? &slot : nullptr;
}

D3D12GroupSlot* Resolve(D3D12Device& device, BindGroupHandle handle) noexcept
{
    if (!handle.IsValid() || handle.index >= device.groups.Size())
    {
        return nullptr;
    }
    D3D12GroupSlot& slot = device.groups[handle.index];
    return slot.alive && slot.generation == handle.generation ? &slot : nullptr;
}

D3D12PipelineLayoutSlot* Resolve(D3D12Device& device, PipelineLayoutHandle handle) noexcept
{
    if (!handle.IsValid() || handle.index >= device.pipelineLayouts.Size())
    {
        return nullptr;
    }
    D3D12PipelineLayoutSlot& slot = device.pipelineLayouts[handle.index];
    return slot.alive && slot.generation == handle.generation ? &slot : nullptr;
}

// ---------------------------------------------------------------------------
// Shaders
// ---------------------------------------------------------------------------

Status<Error> D3D12CreateShader(void* internal, const ShaderDesc& desc, ShaderHandle* out) noexcept
{
    if (internal == nullptr || out == nullptr)
    {
        return StatusError(ErrorCode::InvalidArgument,
                           "d3d12 backend: shader creation needs a device and an output handle");
    }
    if (desc.code.Data() == nullptr || desc.code.Size() == 0)
    {
        return StatusError(ErrorCode::InvalidArgument, "d3d12 backend: shader code is empty");
    }
    if (desc.stage >= ShaderStage::Count)
    {
        return StatusError(ErrorCode::InvalidArgument, "d3d12 backend: shader stage is invalid");
    }

    auto& device = *static_cast<D3D12Device*>(internal);

    const void* source = desc.code.Data();
    u64 sourceSize = desc.code.Size();
    ID3DBlob* compiled = nullptr;

    if (desc.format == ShaderBytecodeFormat::HlslSource)
    {
        const char* target = TargetFor(desc.stage);
        if (target == nullptr)
        {
            return StatusError(ErrorCode::Unsupported,
                               "d3d12 backend: stage has no HLSL shader model target");
        }
        if (desc.entryPoint == nullptr)
        {
            return StatusError(ErrorCode::InvalidArgument,
                               "d3d12 backend: HLSL source needs an entry point");
        }

        // The source is not null-terminated, so it is copied into the device
        // allocator first: D3DCompile reads to a terminator.
        auto* text = static_cast<char*>(device.allocator->Allocate(desc.code.Size() + 1, 1));
        if (text == nullptr)
        {
            return StatusError(ErrorCode::Internal,
                               "d3d12 backend: shader source allocation failed");
        }
        std::memcpy(text, desc.code.Data(), desc.code.Size());
        text[desc.code.Size()] = '\0';

        UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#if defined(CG_DEBUG) && CG_DEBUG
        flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
        flags |= D3DCOMPILE_OPTIMIZATION_LEVEL3;
#endif

        ID3DBlob* errors = nullptr;
        const HRESULT result = D3DCompile(text, desc.code.Size(), desc.label, nullptr, nullptr,
                                          desc.entryPoint, target, flags, 0, &compiled, &errors);
        device.allocator->Deallocate(text);
        if (errors != nullptr)
        {
            errors->Release();
        }
        if (FAILED(result) || compiled == nullptr)
        {
            // A shader that does not compile is the caller's mistake, and the
            // message says which entry point and target failed.
            return StatusError(ErrorCode::InvalidArgument,
                               "d3d12 backend: HLSL compilation failed");
        }
        source = compiled->GetBufferPointer();
        sourceSize = compiled->GetBufferSize();
    }
    else if (desc.format != ShaderBytecodeFormat::Dxil)
    {
        return StatusError(ErrorCode::Unsupported,
                           "d3d12 backend: this shader format is not consumed by Direct3D 12");
    }

    void* code = device.allocator->Allocate(static_cast<cyber::core::usize>(sourceSize), 1);
    if (code == nullptr)
    {
        if (compiled != nullptr)
        {
            compiled->Release();
        }
        return StatusError(ErrorCode::Internal, "d3d12 backend: shader bytecode allocation failed");
    }
    std::memcpy(code, source, static_cast<cyber::core::usize>(sourceSize));
    if (compiled != nullptr)
    {
        compiled->Release();
    }

    const std::lock_guard<std::mutex> lock(device.mutex);
    u32 index = kInvalidHandleIndex;
    if (device.freeShaderSlots.Size() > 0)
    {
        index = device.freeShaderSlots[device.freeShaderSlots.Size() - 1];
        (void)device.freeShaderSlots.TryResize(device.freeShaderSlots.Size() - 1);
    }
    else
    {
        if (!device.shaders.TryPushBack(D3D12ShaderSlot{}))
        {
            device.allocator->Deallocate(code);
            return StatusError(ErrorCode::Internal, "d3d12 backend: shader table growth failed");
        }
        index = static_cast<u32>(device.shaders.Size() - 1);
    }

    D3D12ShaderSlot& slot = device.shaders[index];
    slot.generation = NextGeneration(slot.generation);
    slot.alive = true;
    slot.code = code;
    slot.codeSize = sourceSize;
    slot.stage = desc.stage;
    slot.format = desc.format;

    ShaderHandle handle;
    handle.index = index;
    handle.generation = slot.generation;
    *out = handle;
    return Status<Error>::Ok();
}

Status<Error> D3D12DestroyShader(void* internal, ShaderHandle handle) noexcept
{
    if (internal == nullptr)
    {
        return StatusError(ErrorCode::InvalidArgument, "d3d12 backend: device is null");
    }
    auto& device = *static_cast<D3D12Device*>(internal);
    const std::lock_guard<std::mutex> lock(device.mutex);
    D3D12ShaderSlot* slot = Resolve(device, handle);
    if (slot == nullptr)
    {
        return StatusError(ErrorCode::InvalidArgument,
                           "d3d12 backend: shader handle is stale or invalid");
    }
    device.allocator->Deallocate(slot->code);
    slot->code = nullptr;
    slot->codeSize = 0;
    slot->alive = false;
    slot->generation = NextGeneration(slot->generation);
    if (!device.freeShaderSlots.TryPushBack(handle.index))
    {
        return StatusError(ErrorCode::Internal, "d3d12 backend: shader free list growth failed");
    }
    return Status<Error>::Ok();
}

ShaderInfo D3D12ShaderInfo(const void* internal, ShaderHandle handle) noexcept
{
    if (internal == nullptr)
    {
        return ShaderInfo{};
    }
    auto& device = *static_cast<D3D12Device*>(const_cast<void*>(internal));
    const std::lock_guard<std::mutex> lock(device.mutex);
    const D3D12ShaderSlot* slot = Resolve(device, handle);
    if (slot == nullptr)
    {
        return ShaderInfo{};
    }
    ShaderInfo info{};
    info.valid = true;
    info.stage = slot->stage;
    info.format = slot->format;
    info.codeSize = slot->codeSize;
    return info;
}

// ---------------------------------------------------------------------------
// Bind group layouts
// ---------------------------------------------------------------------------

Status<Error> D3D12CreateBindGroupLayout(void* internal, const BindGroupLayoutDesc& desc,
                                         BindGroupLayoutHandle* out) noexcept
{
    if (internal == nullptr || out == nullptr)
    {
        return StatusError(ErrorCode::InvalidArgument,
                           "d3d12 backend: layout creation needs a device and an output handle");
    }
    if (desc.entries.Data() == nullptr || desc.entries.Size() == 0)
    {
        return StatusError(ErrorCode::InvalidArgument, "d3d12 backend: layout has no entries");
    }
    if (desc.entries.Size() > kMaxLayoutEntries)
    {
        return StatusError(ErrorCode::OutOfRange,
                           "d3d12 backend: layout has more entries than a table can hold");
    }

    auto& device = *static_cast<D3D12Device*>(internal);
    const std::lock_guard<std::mutex> lock(device.mutex);

    u32 descriptors = 0;
    for (usize index = 0; index < desc.entries.Size(); ++index)
    {
        const BindGroupLayoutEntry& entry = desc.entries[index];
        if (entry.type >= BindingType::Count)
        {
            return StatusError(ErrorCode::InvalidArgument,
                               "d3d12 backend: layout entry type is invalid");
        }
        if (entry.arrayCount == 0)
        {
            return StatusError(ErrorCode::InvalidArgument,
                               "d3d12 backend: layout entry array count is zero");
        }
        descriptors += entry.arrayCount;
    }

    u32 index = kInvalidHandleIndex;
    if (device.freeLayoutSlots.Size() > 0)
    {
        index = device.freeLayoutSlots[device.freeLayoutSlots.Size() - 1];
        (void)device.freeLayoutSlots.TryResize(device.freeLayoutSlots.Size() - 1);
    }
    else
    {
        if (!device.layouts.TryPushBack(D3D12LayoutSlot{}))
        {
            return StatusError(ErrorCode::Internal, "d3d12 backend: layout table growth failed");
        }
        index = static_cast<u32>(device.layouts.Size() - 1);
    }

    D3D12LayoutSlot& slot = device.layouts[index];
    slot.generation = NextGeneration(slot.generation);
    slot.alive = true;
    slot.entryCount = static_cast<u32>(desc.entries.Size());
    slot.descriptorCount = descriptors;
    for (usize entry = 0; entry < desc.entries.Size(); ++entry)
    {
        slot.entries[entry] = desc.entries[entry];
    }

    BindGroupLayoutHandle handle;
    handle.index = index;
    handle.generation = slot.generation;
    *out = handle;
    return Status<Error>::Ok();
}

Status<Error> D3D12DestroyBindGroupLayout(void* internal, BindGroupLayoutHandle handle) noexcept
{
    if (internal == nullptr)
    {
        return StatusError(ErrorCode::InvalidArgument, "d3d12 backend: device is null");
    }
    auto& device = *static_cast<D3D12Device*>(internal);
    const std::lock_guard<std::mutex> lock(device.mutex);
    D3D12LayoutSlot* slot = Resolve(device, handle);
    if (slot == nullptr)
    {
        return StatusError(ErrorCode::InvalidArgument,
                           "d3d12 backend: layout handle is stale or invalid");
    }
    slot->alive = false;
    slot->entryCount = 0;
    slot->descriptorCount = 0;
    slot->generation = NextGeneration(slot->generation);
    if (!device.freeLayoutSlots.TryPushBack(handle.index))
    {
        return StatusError(ErrorCode::Internal, "d3d12 backend: layout free list growth failed");
    }
    return Status<Error>::Ok();
}

// ---------------------------------------------------------------------------
// Bind groups
// ---------------------------------------------------------------------------

Status<Error> D3D12CreateBindGroup(void* internal, const BindGroupDesc& desc,
                                   BindGroupHandle* out) noexcept
{
    if (internal == nullptr || out == nullptr)
    {
        return StatusError(ErrorCode::InvalidArgument,
                           "d3d12 backend: group creation needs a device and an output handle");
    }
    auto& device = *static_cast<D3D12Device*>(internal);
    const std::lock_guard<std::mutex> lock(device.mutex);

    const D3D12LayoutSlot* layout = Resolve(device, desc.layout);
    if (layout == nullptr)
    {
        return StatusError(ErrorCode::InvalidArgument, "d3d12 backend: group names a stale layout");
    }
    if (desc.resources.Size() != layout->descriptorCount)
    {
        return StatusError(ErrorCode::InvalidArgument,
                           "d3d12 backend: group resource count does not match its layout");
    }

    // One contiguous block from the shader-visible heap: a root descriptor table
    // is a range, so every binding of a group must sit together.
    const u32 capacity = D3D12_MAX_SHADER_VISIBLE_DESCRIPTOR_HEAP_SIZE_TIER_1;
    const u32 first = device.viewDescriptorsUsed;
    if (first + layout->descriptorCount > capacity)
    {
        return StatusError(ErrorCode::OutOfRange,
                           "d3d12 backend: shader-visible descriptor heap is full");
    }

    u32 resourceIndex = 0;
    for (u32 entryIndex = 0; entryIndex < layout->entryCount; ++entryIndex)
    {
        const BindGroupLayoutEntry& entry = layout->entries[entryIndex];
        for (u32 element = 0; element < entry.arrayCount; ++element)
        {
            const BindingResource& resource = desc.resources[resourceIndex];
            const D3D12_CPU_DESCRIPTOR_HANDLE destination =
                CpuHandleAt(device, first + resourceIndex);
            ++resourceIndex;

            switch (entry.type)
            {
            case BindingType::UniformBuffer:
            {
                const D3D12BufferSlot* buffer = Resolve(device, resource.buffer);
                if (buffer == nullptr)
                {
                    return StatusError(ErrorCode::InvalidArgument,
                                       "d3d12 backend: group names a stale buffer");
                }
                const u64 size =
                    resource.size == 0 ? buffer->desc.sizeBytes - resource.offset : resource.size;
                D3D12_CONSTANT_BUFFER_VIEW_DESC view{};
                view.BufferLocation = buffer->resource->GetGPUVirtualAddress() + resource.offset;
                // A constant buffer view is expressed in 256-byte units, and the
                // size the API accepts is capped at 64 KiB.
                const u64 aligned = (size + 255ULL) & ~255ULL;
                view.SizeInBytes = static_cast<UINT>(aligned > 65536ULL ? 65536ULL : aligned);
                device.device->CreateConstantBufferView(&view, destination);
                break;
            }
            case BindingType::StorageBuffer:
            {
                const D3D12BufferSlot* buffer = Resolve(device, resource.buffer);
                if (buffer == nullptr)
                {
                    return StatusError(ErrorCode::InvalidArgument,
                                       "d3d12 backend: group names a stale buffer");
                }
                const u64 size =
                    resource.size == 0 ? buffer->desc.sizeBytes - resource.offset : resource.size;
                const u32 stride = buffer->desc.elementStride;
                D3D12_SHADER_RESOURCE_VIEW_DESC view{};
                view.Format = DXGI_FORMAT_UNKNOWN;
                view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                view.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
                if (stride == 0)
                {
                    view.Buffer.FirstElement = resource.offset / 4;
                    view.Buffer.NumElements = static_cast<UINT>(size / 4);
                }
                else
                {
                    view.Buffer.StructureByteStride = stride;
                    view.Buffer.FirstElement = resource.offset / stride;
                    view.Buffer.NumElements = static_cast<UINT>(size / stride);
                }
                device.device->CreateShaderResourceView(buffer->resource, &view, destination);
                break;
            }
            case BindingType::SampledTexture:
            {
                const D3D12ViewSlot* view = Resolve(device, resource.textureView);
                if (view == nullptr)
                {
                    return StatusError(ErrorCode::InvalidArgument,
                                       "d3d12 backend: group names a stale texture view");
                }
                device.device->CopyDescriptorsSimple(1, destination,
                                                     CpuHandleAt(device, view->descriptorIndex),
                                                     D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
                break;
            }
            case BindingType::StorageTexture:
            {
                const D3D12ViewSlot* view = Resolve(device, resource.textureView);
                if (view == nullptr)
                {
                    return StatusError(ErrorCode::InvalidArgument,
                                       "d3d12 backend: group names a stale texture view");
                }
                const D3D12TextureSlot* texture = Resolve(device, view->texture);
                if (texture == nullptr)
                {
                    return StatusError(ErrorCode::InvalidArgument,
                                       "d3d12 backend: group names a stale texture");
                }
                D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};
                uav.Format = ToDxgiFormat(view->desc.format);
                uav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
                uav.Texture2D.MipSlice = view->desc.range.baseMipLevel;
                device.device->CreateUnorderedAccessView(texture->resource, nullptr, &uav,
                                                         destination);
                break;
            }
            case BindingType::Sampler:
            {
                const D3D12SamplerSlot* sampler = Resolve(device, resource.sampler);
                if (sampler == nullptr)
                {
                    return StatusError(ErrorCode::InvalidArgument,
                                       "d3d12 backend: group names a stale sampler");
                }
                device.device->CopyDescriptorsSimple(1, destination,
                                                     SamplerCpuAt(device, sampler->descriptorIndex),
                                                     D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
                break;
            }
            case BindingType::Count:
                return StatusError(ErrorCode::InvalidArgument,
                                   "d3d12 backend: group resource type is invalid");
            }
        }
    }
    device.viewDescriptorsUsed = first + layout->descriptorCount;

    u32 index = kInvalidHandleIndex;
    if (device.freeGroupSlots.Size() > 0)
    {
        index = device.freeGroupSlots[device.freeGroupSlots.Size() - 1];
        (void)device.freeGroupSlots.TryResize(device.freeGroupSlots.Size() - 1);
    }
    else
    {
        if (!device.groups.TryPushBack(D3D12GroupSlot{}))
        {
            return StatusError(ErrorCode::Internal, "d3d12 backend: group table growth failed");
        }
        index = static_cast<u32>(device.groups.Size() - 1);
    }

    D3D12GroupSlot& slot = device.groups[index];
    slot.generation = NextGeneration(slot.generation);
    slot.alive = true;
    slot.layout = desc.layout;
    slot.descriptorIndex = first;
    slot.descriptorCount = LayoutDescriptorCount(*layout);

    BindGroupHandle handle;
    handle.index = index;
    handle.generation = slot.generation;
    *out = handle;
    return Status<Error>::Ok();
}

Status<Error> D3D12DestroyBindGroup(void* internal, BindGroupHandle handle) noexcept
{
    if (internal == nullptr)
    {
        return StatusError(ErrorCode::InvalidArgument, "d3d12 backend: device is null");
    }
    auto& device = *static_cast<D3D12Device*>(internal);
    const std::lock_guard<std::mutex> lock(device.mutex);
    D3D12GroupSlot* slot = Resolve(device, handle);
    if (slot == nullptr)
    {
        return StatusError(ErrorCode::InvalidArgument,
                           "d3d12 backend: group handle is stale or invalid");
    }
    slot->alive = false;
    slot->layout = BindGroupLayoutHandle{};
    slot->generation = NextGeneration(slot->generation);
    if (!device.freeGroupSlots.TryPushBack(handle.index))
    {
        return StatusError(ErrorCode::Internal, "d3d12 backend: group free list growth failed");
    }
    return Status<Error>::Ok();
}

BindGroupInfo D3D12BindGroupInfo(const void* internal, BindGroupHandle handle) noexcept
{
    if (internal == nullptr)
    {
        return BindGroupInfo{};
    }
    auto& device = *static_cast<D3D12Device*>(const_cast<void*>(internal));
    const std::lock_guard<std::mutex> lock(device.mutex);
    const D3D12GroupSlot* slot = Resolve(device, handle);
    if (slot == nullptr)
    {
        return BindGroupInfo{};
    }
    BindGroupInfo info{};
    info.valid = true;
    info.layout = slot->layout;
    info.resourceCount = slot->descriptorCount;
    return info;
}

// ---------------------------------------------------------------------------
// Pipeline layouts
// ---------------------------------------------------------------------------

Status<Error> D3D12CreatePipelineLayout(void* internal, const PipelineLayoutDesc& desc,
                                        PipelineLayoutHandle* out) noexcept
{
    if (internal == nullptr || out == nullptr)
    {
        return StatusError(ErrorCode::InvalidArgument,
                           "d3d12 backend: pipeline layout creation needs a device and an output");
    }

    auto& device = *static_cast<D3D12Device*>(internal);
    const std::lock_guard<std::mutex> lock(device.mutex);

    // A used slot contributes one descriptor table; the RHI's four slots become
    // register spaces 0..3, so the same shader works for any layout.
    D3D12_ROOT_PARAMETER parameters[kBindGroupSlotCount]{};
    D3D12_DESCRIPTOR_RANGE ranges[kBindGroupSlotCount]{};
    u32 parameterCount = 0;
    u32 descriptorCounts[kBindGroupSlotCount]{};

    for (u32 slotIndex = 0; slotIndex < kBindGroupSlotCount; ++slotIndex)
    {
        if (!desc.groups[slotIndex].IsValid())
        {
            continue;
        }
        const D3D12LayoutSlot* layout = Resolve(device, desc.groups[slotIndex]);
        if (layout == nullptr)
        {
            return StatusError(ErrorCode::InvalidArgument,
                               "d3d12 backend: pipeline layout names a stale group layout");
        }
        descriptorCounts[slotIndex] = layout->descriptorCount;
        ranges[slotIndex].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        ranges[slotIndex].NumDescriptors = layout->descriptorCount;
        ranges[slotIndex].BaseShaderRegister = 0;
        ranges[slotIndex].RegisterSpace = slotIndex;
        ranges[slotIndex].OffsetInDescriptorsFromTableStart = 0;

        parameters[parameterCount].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        parameters[parameterCount].DescriptorTable.NumDescriptorRanges = 1;
        parameters[parameterCount].DescriptorTable.pDescriptorRanges = &ranges[slotIndex];
        parameters[parameterCount].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        ++parameterCount;
    }

    u32 pushConstantBytes = 0;
    for (usize index = 0; index < desc.pushConstants.Size(); ++index)
    {
        const PushConstantRange& range = desc.pushConstants[index];
        const u32 end = range.offsetBytes + range.sizeBytes;
        if (end > pushConstantBytes)
        {
            pushConstantBytes = end;
        }
    }
    if (pushConstantBytes > 0)
    {
        parameters[parameterCount].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        parameters[parameterCount].Constants.ShaderRegister = 0;
        parameters[parameterCount].Constants.RegisterSpace = 0;
        parameters[parameterCount].Constants.Num32BitValues = (pushConstantBytes + 3) / 4;
        parameters[parameterCount].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        ++parameterCount;
    }

    D3D12_ROOT_SIGNATURE_DESC signature{};
    signature.NumParameters = parameterCount;
    signature.pParameters = parameterCount > 0 ? parameters : nullptr;
    signature.NumStaticSamplers = 0;
    signature.pStaticSamplers = nullptr;
    signature.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ID3DBlob* blob = nullptr;
    ID3DBlob* errors = nullptr;
    const HRESULT serialized =
        D3D12SerializeRootSignature(&signature, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &errors);
    if (errors != nullptr)
    {
        errors->Release();
    }
    if (FAILED(serialized) || blob == nullptr)
    {
        return StatusError(ErrorCode::Internal,
                           "d3d12 backend: root signature serialization failed");
    }

    ID3D12RootSignature* rootSignature = nullptr;
    const HRESULT created = device.device->CreateRootSignature(
        0, blob->GetBufferPointer(), blob->GetBufferSize(), __uuidof(ID3D12RootSignature),
        reinterpret_cast<void**>(&rootSignature));
    blob->Release();
    if (FAILED(created) || rootSignature == nullptr)
    {
        return StatusError(ErrorCode::Internal, "d3d12 backend: root signature creation failed");
    }
    NameObject(rootSignature, desc.label);

    u32 index = kInvalidHandleIndex;
    if (device.freePipelineLayoutSlots.Size() > 0)
    {
        index = device.freePipelineLayoutSlots[device.freePipelineLayoutSlots.Size() - 1];
        (void)device.freePipelineLayoutSlots.TryResize(device.freePipelineLayoutSlots.Size() - 1);
    }
    else
    {
        if (!device.pipelineLayouts.TryPushBack(D3D12PipelineLayoutSlot{}))
        {
            rootSignature->Release();
            return StatusError(ErrorCode::Internal,
                               "d3d12 backend: pipeline layout table growth failed");
        }
        index = static_cast<u32>(device.pipelineLayouts.Size() - 1);
    }

    D3D12PipelineLayoutSlot& slot = device.pipelineLayouts[index];
    slot.generation = NextGeneration(slot.generation);
    slot.alive = true;
    slot.rootSignature = rootSignature;
    slot.pushConstantBytes = pushConstantBytes;
    for (u32 groupIndex = 0; groupIndex < kBindGroupSlotCount; ++groupIndex)
    {
        slot.descriptorCounts[groupIndex] = descriptorCounts[groupIndex];
    }

    PipelineLayoutHandle handle;
    handle.index = index;
    handle.generation = slot.generation;
    *out = handle;
    return Status<Error>::Ok();
}

Status<Error> D3D12DestroyPipelineLayout(void* internal, PipelineLayoutHandle handle) noexcept
{
    if (internal == nullptr)
    {
        return StatusError(ErrorCode::InvalidArgument, "d3d12 backend: device is null");
    }
    auto& device = *static_cast<D3D12Device*>(internal);
    const std::lock_guard<std::mutex> lock(device.mutex);
    D3D12PipelineLayoutSlot* slot = Resolve(device, handle);
    if (slot == nullptr)
    {
        return StatusError(ErrorCode::InvalidArgument,
                           "d3d12 backend: pipeline layout handle is stale or invalid");
    }
    if (slot->rootSignature != nullptr)
    {
        slot->rootSignature->Release();
        slot->rootSignature = nullptr;
    }
    slot->alive = false;
    slot->generation = NextGeneration(slot->generation);
    if (!device.freePipelineLayoutSlots.TryPushBack(handle.index))
    {
        return StatusError(ErrorCode::Internal,
                           "d3d12 backend: pipeline layout free list growth failed");
    }
    return Status<Error>::Ok();
}

}  // namespace cyber::rhi::detail

#endif
