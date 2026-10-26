#pragma once

#include <cyber/core/types.h>
#include <cyber/memory/handle.h>

namespace cyber::rhi
{

using cyber::core::u32;
using cyber::core::u64;

// Every RHI object is referred to by a generation-counted handle. The rule is
// the one ADR 0006 set for `SlotMap`: a slot index identifies storage and a
// generation identifies one lifetime of that slot. Destroying an object bumps
// its generation, so a handle the caller still holds stops resolving instead of
// aliasing a newer object in the same slot.
//
// The tag type prevents one kind of handle becoming another. `BufferHandle` and
// `TextureHandle` are the same two integers at run time and different types at
// compile time, which is what stops a buffer being bound as a texture.

struct QueueTag
{
};
struct CommandListTag
{
};
struct BufferTag
{
};
struct TextureTag
{
};
struct TextureViewTag
{
};
struct SamplerTag
{
};
struct ShaderTag
{
};
struct PipelineTag
{
};
struct BindGroupLayoutTag
{
};
struct BindGroupTag
{
};
struct PipelineLayoutTag
{
};
struct PipelineCacheTag
{
};
struct FenceTag
{
};
struct SwapChainTag
{
};

// A queue class owned by the device. Queues are never destroyed individually.
using QueueHandle = memory::Handle<QueueTag>;
// A recorded command list. Recording is single-threaded (Phase 13).
using CommandListHandle = memory::Handle<CommandListTag>;
// A linear resource (Phase 11).
using BufferHandle = memory::Handle<BufferTag>;
// An image resource: 1D, 2D, 3D, cube, or an array (Phase 11).
using TextureHandle = memory::Handle<TextureTag>;
// A typed window onto a texture. Views are what shaders bind (Phase 11).
using TextureViewHandle = memory::Handle<TextureViewTag>;
// A filter and address state object (Phase 11).
using SamplerHandle = memory::Handle<SamplerTag>;
// Compiled or interpreted shader code for one stage (Phase 12).
using ShaderHandle = memory::Handle<ShaderTag>;
// A complete graphics or compute pipeline state (Phase 12).
using PipelineHandle = memory::Handle<PipelineTag>;
// The layout of a binding group: slot types, counts, and visible stages
// (Phase 12).
using BindGroupLayoutHandle = memory::Handle<BindGroupLayoutTag>;
// A group of resources bound together at one slot, matching a layout
// (Phase 12).
using BindGroupHandle = memory::Handle<BindGroupTag>;
// The binding layout of a pipeline: a group per slot and push-constant ranges
// (Phase 12).
using PipelineLayoutHandle = memory::Handle<PipelineLayoutTag>;
// Compiled pipeline state saved between runs, keyed by a description hash
// (Phase 12).
using PipelineCacheHandle = memory::Handle<PipelineCacheTag>;
// A monotonic 64-bit timeline the CPU and GPU can wait on.
using FenceHandle = memory::Handle<FenceTag>;
// A ring of presentable images owned by the device and backed by a surface the
// caller owns (Phase 13).
using SwapChainHandle = memory::Handle<SwapChainTag>;

// Packs a handle into one 64-bit value with the index in the high half and the
// generation in the low half. The form is for logging, hashing, and the future C
// ABI; round-tripping it is lossless for every valid handle and for the invalid
// one. Sort order is slot order.
template <typename Tag>
[[nodiscard]] constexpr u64 PackHandle(memory::Handle<Tag> handle) noexcept
{
    return (static_cast<u64>(handle.index) << 32) | static_cast<u64>(handle.generation);
}

// Inverse of `PackHandle`.
template <typename Tag>
[[nodiscard]] constexpr memory::Handle<Tag> UnpackHandle(u64 packed) noexcept
{
    memory::Handle<Tag> handle;
    handle.index = static_cast<u32>(packed >> 32);
    handle.generation = static_cast<u32>(packed & 0xFFFFFFFFULL);
    return handle;
}

}  // namespace cyber::rhi
