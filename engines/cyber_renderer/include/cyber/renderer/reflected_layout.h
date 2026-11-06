#pragma once

#include <cyber/core/error.h>
#include <cyber/core/result.h>
#include <cyber/core/types.h>
#include <cyber/memory/span.h>
#include <cyber/rhi/binding.h>
#include <cyber/rhi/device.h>
#include <cyber/rhi/handle.h>
#include <cyber/rhi/pipeline.h>
#include <cyber/rhi/shader.h>
#include <cyber/shaders/reflection.h>

#include <array>

namespace cyber::renderer
{

using cyber::core::u32;

/// Most bindings one bind-group slot may hold in a reflected layout.
inline constexpr u32 kMaxReflectedBindingsPerSlot = 16;
/// Largest push-constant block a reflected layout accepts: the minimum every
/// Vulkan implementation guarantees, and well inside D3D12's root signature.
inline constexpr u32 kMaxReflectedPushConstantBytes = 128;
/// Largest reflected material constant buffer supported by this phase.
inline constexpr u32 kMaxReflectedMaterialConstantBytes = 4096;
/// Most vertex attributes a reflected vertex layout holds.
inline constexpr u32 kMaxReflectedVertexAttributes = shaders::kMaxStageVariables;
/// Returned by ReflectedLayout::FindBinding for a name the slot does not hold.
inline constexpr u32 kNoBinding = 0xFFFFFFFFU;

/// Map a shader-language stage to its RHI equivalent.
///
/// @param stage Shader-language stage to convert.
/// @return The matching RHI stage, or ShaderStage::Count for an invalid value.
[[nodiscard]] rhi::ShaderStage ToRhiStage(shaders::Stage stage) noexcept;

/// One binding of a reflected bind-group slot, with the name the shader uses.
struct ReflectedBinding
{
    rhi::BindGroupLayoutEntry entry{};
    shaders::ReflectedName name{};
    /// Register index in the shader (t, s, or u register).
    u32 registerIndex = 0;
};

/// Bind-group and push-constant layout derived from the reflection of every
/// stage of one program. Trivially copyable and comparable.
///
/// Binding convention (ADR 0017, ADR 0020): register space S is bind-group
/// slot S. Inside a slot, uniform buffers come first, then sampled textures,
/// then samplers, each in register order. The binding number is the position
/// in that order, which is also the order the D3D12 backend writes descriptors
/// in. The constant buffer at b0, space0 is the push-constant block; b1, space2
/// is the per-material parameter block.
struct ReflectedLayout
{
    std::array<std::array<ReflectedBinding, kMaxReflectedBindingsPerSlot>, rhi::kBindGroupSlotCount>
        slots{};
    std::array<u32, rhi::kBindGroupSlotCount> bindingCounts{};
    /// Binding index of the b1, space2 material constant buffer, or kNoBinding.
    u32 materialConstantsBinding = kNoBinding;
    /// Byte size of the reflected b1, space2 block, or zero when absent.
    u32 materialConstantsSizeBytes = 0;
    /// Reflected members in the material block; layout equality includes these
    /// so hot reload invalidates materials when member offsets or types change.
    u32 materialConstantsMemberCount = 0;
    std::array<shaders::ConstantMember, shaders::kMaxConstantMembers> materialConstantsMembers{};
    /// Push-constant range; `sizeBytes` is zero when the program has none.
    rhi::PushConstantRange pushConstants{};

    /// Check whether a bind-group slot has reflected bindings.
    ///
    /// @param slot Slot to check.
    /// @return True when the slot is in range and contains one or more bindings.
    [[nodiscard]] bool UsesSlot(rhi::BindGroupSlot slot) const noexcept
    {
        const u32 index = static_cast<u32>(slot);
        return index < rhi::kBindGroupSlotCount && bindingCounts[index] != 0;
    }

    /// Find a resource's binding number in a bind-group slot.
    ///
    /// @param slot Slot to search.
    /// @param name Shader resource name to find.
    /// @return Binding number, or kNoBinding when absent or the slot is invalid.
    [[nodiscard]] u32 FindBinding(rhi::BindGroupSlot slot, memory::StringView name) const noexcept;
};

/// Compare two reflected layouts field by field, including names and visibility.
///
/// @param lhs First layout.
/// @param rhs Second layout.
/// @return True when all reflected fields match.
[[nodiscard]] bool operator==(const ReflectedLayout& lhs, const ReflectedLayout& rhs) noexcept;

/// Merge the reflection of every stage of a program into one layout.
///
/// @param stages Borrowed reflections, one per stage; null entries are skipped.
/// @return The layout, or:
///         - ErrorCode::InvalidArgument when two stages are the same stage, or
///           a register is used by two different resource kinds;
///         - ErrorCode::Unsupported for a constant buffer other than
///           unsupported constant buffer, a push block over
///           kMaxReflectedPushConstantBytes, a material block over
///           kMaxReflectedMaterialConstantBytes, a register space beyond the
///           four bind-group slots, a storage texture (the D3D12 backend has no
///           UAV descriptor ranges yet), more than kMaxReflectedBindingsPerSlot
///           bindings, or registers that are not contiguous from 0 within a
///           resource class and slot.
[[nodiscard]] core::Result<ReflectedLayout, core::Error>
BuildReflectedLayout(memory::Span<const shaders::Reflection* const> stages) noexcept;

/// Interleaved single-buffer vertex layout derived from vertex-stage inputs.
struct ReflectedVertexLayout
{
    rhi::VertexBinding binding{};
    std::array<rhi::VertexAttribute, kMaxReflectedVertexAttributes> attributes{};
    u32 attributeCount = 0;

    /// Return the layout view expected by an RHI pipeline description.
    ///
    /// @return Spans borrowing this ReflectedVertexLayout object.
    [[nodiscard]] rhi::VertexLayout View() const noexcept;
};

/// Build a tightly packed, interleaved vertex layout from a vertex stage: the
/// inputs in ascending location order, each at the next 4-byte-aligned offset.
///
/// @param vertex Reflection of a vertex stage.
/// @return The layout, or ErrorCode::InvalidArgument for a non-vertex stage
///         or an input type with no vertex format.
[[nodiscard]] core::Result<ReflectedVertexLayout, core::Error>
BuildVertexLayout(const shaders::Reflection& vertex) noexcept;

/// RHI objects that realize a ReflectedLayout. Handles of unused slots are
/// invalid.
struct ReflectedLayoutObjects
{
    std::array<rhi::BindGroupLayoutHandle, rhi::kBindGroupSlotCount> groups{};
    rhi::PipelineLayoutHandle pipelineLayout{};
};

/// Create the bind-group layouts and the pipeline layout for `layout`.
///
/// @param device Device to create on; borrowed.
/// @param layout Layout to realize.
/// @param label Optional debug label; borrowed for the call.
/// @return The objects, or the device's error. Nothing is leaked on failure.
[[nodiscard]] core::Result<ReflectedLayoutObjects, core::Error>
CreateReflectedLayoutObjects(rhi::Device& device, const ReflectedLayout& layout,
                             const char* label) noexcept;

/// Destroy objects created by CreateReflectedLayoutObjects and reset the
/// handles. Invalid handles are skipped.
///
/// @param device Device that owns the objects.
/// @param objects Handles to destroy; reset after the call.
void DestroyReflectedLayoutObjects(rhi::Device& device, ReflectedLayoutObjects& objects) noexcept;

}  // namespace cyber::renderer
