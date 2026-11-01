#pragma once

// The RHI's own rule set, shared by every caller of the API (ADR 0018).
//
// The functions here are the checks the null backend has enforced since
// Phase 11, moved out of `null_backend.cpp` so a rule is written once and every
// caller enforces the same version of it. The null backend calls them while
// performing the work; `validating_device.cpp` calls them while forwarding to a
// real backend, so a rule cannot be enforced in one place and forgotten in the
// other.
//
// Every validator is a pure function of its arguments plus, where the rule
// needs it, the device's capabilities and a small existence probe (below). No
// validator allocates, none throws, and all of them report through `Error*`
// with Aloof pointers to static strings.

#include <cyber/core/error.h>
#include <cyber/core/result.h>
#include <cyber/core/types.h>
#include <cyber/memory/span.h>
#include <cyber/rhi/binding.h>
#include <cyber/rhi/buffer.h>
#include <cyber/rhi/capabilities.h>
#include <cyber/rhi/descriptors.h>
#include <cyber/rhi/format.h>
#include <cyber/rhi/handle.h>
#include <cyber/rhi/pipeline.h>
#include <cyber/rhi/resource_state.h>
#include <cyber/rhi/sampler.h>
#include <cyber/rhi/shader.h>
#include <cyber/rhi/submission.h>
#include <cyber/rhi/texture.h>

namespace cyber::rhi::validation
{

using cyber::core::Error;
using cyber::core::ErrorCode;
using cyber::core::Result;
using cyber::core::Status;
using cyber::core::u32;
using cyber::core::u64;
using cyber::memory::usize;

// Writes `code` and `message` into `out` and returns false. Writing over an
// earlier failure is a caller bug, not an error: no validator fuses messages.
[[nodiscard]] bool Fail(Error* out, ErrorCode code, const char* message) noexcept;

// Probes whether a handle names a live object of that kind. A wrapper and the
// null backend each provide this against their own live-object tables; a
// validator never touches a real device, so the same rule set serves both.
//
// Every entry is required: an all-null table is a caller bug, and a validator
// that needs a probe asserts on a null entry with CG_ASSERT.
struct Existence
{
    // Calls the probe implementation of each object kind. Owned by the caller.
    void* tables = nullptr;

    // True when the handle names a pipeline layout the tables believe is live.
    bool (*pipelineLayout)(void* tables, PipelineLayoutHandle handle) noexcept;
    // True when the handle names a live bind group layout.
    bool (*bindGroupLayout)(void* tables, BindGroupLayoutHandle handle) noexcept;
    // Stage of a live shader, or `ShaderStage::Count` for a stale handle.
    ShaderStage (*shaderStage)(void* tables, ShaderHandle handle) noexcept;
    // True when the handle names a live pipeline cache.
    bool (*cache)(void* tables, PipelineCacheHandle handle) noexcept;
};

// Usages that make a buffer bindable to a pipeline. Neither host-visible nor
// readback may combine with any of them.
inline constexpr BufferUsage kBufferPipelineUsages = BufferUsage::Vertex | BufferUsage::Index |
                                                     BufferUsage::Uniform | BufferUsage::Storage |
                                                     BufferUsage::Indirect;

// True when the buffer description is creatable on a device whose limits are
// `limits`. A description that names an impossible combination reports
// `ErrorCode::InvalidArgument`; one that names a value above the limit reports
// `ErrorCode::OutOfRange`.
[[nodiscard]] bool ValidateBufferDesc(const Limits& limits, const BufferDesc& desc,
                                      Error* out) noexcept;

// Shape rules shared by a texture and a view of it.
[[nodiscard]] bool ValidateTextureShape(const Limits& limits, const TextureDesc& desc,
                                        Error* out) noexcept;

// Whole texture description: shape, usage against format, multisample rules,
// and the feature the storage usage needs.
[[nodiscard]] bool ValidateTextureDesc(const Capabilities& capabilities, const TextureDesc& desc,
                                       Error* out) noexcept;

// True when a view of dimension `view` may expose a texture of dimension
// `base`.
[[nodiscard]] bool IsViewDimensionAllowed(TextureDimension base, TextureDimension view) noexcept;

// True when the view description is creatable over the texture `info` names.
// When `resolved` is non-null, writes the range the texture would actually
// resolve the description's range to.
[[nodiscard]] bool ValidateTextureView(const TextureInfo& info, const TextureViewDesc& desc,
                                       ResolvedRange* resolved, Error* out) noexcept;

// True when `value` is finite, non-negative, and at most `maximum`.
[[nodiscard]] bool IsFiniteNonNegative(float value, float maximum) noexcept;

// Whole sampler description: enumerators, anisotropy against the device limit
// and feature, LOD bounds, and bias range.
[[nodiscard]] bool ValidateSamplerDesc(const Capabilities& capabilities, const SamplerDesc& desc,
                                       Error* out) noexcept;

// Whole shader description: real stage and format, non-empty code, and an
// entry point for source formats.
[[nodiscard]] bool ValidateShaderDesc(const ShaderDesc& desc, Error* out) noexcept;

// Whole bind group layout description: non-empty, entry count and array count
// against the device limit, real binding types and visibility, and binding
// numbers that do not repeat. When `resourceCount` is non-null, writes the sum
// of the entry array counts, which is the resource count a group must supply.
[[nodiscard]] bool ValidateBindGroupLayout(const Limits& limits, const BindGroupLayoutDesc& desc,
                                           u32* resourceCount, Error* out) noexcept;

// Whole pipeline layout description: at least one used slot, every named group
// layout live, no repeated group layout across slots, and push-constant ranges
// that stay inside the device's push-constant limit and do not overlap for a
// stage they share.
[[nodiscard]] bool ValidatePipelineLayout(const Capabilities& capabilities, const Existence& live,
                                          const PipelineLayoutDesc& desc, Error* out) noexcept;

// Vertex input layout of a graphics pipeline: non-empty bindings and
// attributes, counts against the device limits, a real stride and input rate,
// formats the API can fetch as vertex input, and attributes that stay inside
// their binding's stride without repeating a location.
[[nodiscard]] bool ValidateVertexLayout(const Capabilities& capabilities,
                                        const VertexLayout& layout, Error* out) noexcept;

// One attachment's blend state: the write mask is a color mask, and when
// blending is enabled the format supports blending and the factors and
// operations are real enumerators.
[[nodiscard]] bool ValidateBlendState(Format format, const BlendState& blend, Error* out) noexcept;

// Whole graphics pipeline description: every state object before a backend
// touches a driver. Identifier validation is part of the contract; a rule a
// driver enforces too is checked here first so the message says what the RHI
// promised.
[[nodiscard]] bool ValidateGraphicsPipelineDesc(const Capabilities& capabilities,
                                                const Existence& live,
                                                const GraphicsPipelineDesc& desc,
                                                Error* out) noexcept;

// Whole compute pipeline description: the feature, a live layout and compute
// stage shader, and a live cache when one was named.
[[nodiscard]] bool ValidateComputePipelineDesc(const Capabilities& capabilities,
                                               const Existence& live,
                                               const ComputePipelineDesc& desc,
                                               Error* out) noexcept;

}  // namespace cyber::rhi::validation
