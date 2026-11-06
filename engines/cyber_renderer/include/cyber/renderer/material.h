#pragma once

#include <cyber/core/error.h>
#include <cyber/core/result.h>
#include <cyber/core/types.h>
#include <cyber/jobs/job_system.h>
#include <cyber/memory/allocator.h>
#include <cyber/memory/span.h>
#include <cyber/memory/string_view.h>
#include <cyber/renderer/hot_pipeline.h>
#include <cyber/rhi/command_list.h>
#include <cyber/rhi/device.h>
#include <cyber/rhi/handle.h>
#include <cyber/shaders/reflection.h>

#include <array>

namespace cyber::renderer
{

using cyber::core::u32;
using cyber::core::u64;
using cyber::core::u8;

inline constexpr u32 kMaxMaterialParameters = 64;
inline constexpr u32 kMaxMaterialTextureSlots = 16;
inline constexpr u32 kMaxMaterialNameBytes = 63;
inline constexpr u32 kMaxMaterialUriBytes = 512;
inline constexpr u32 kMaxMaterialFileBytes = 64 * 1024;
inline constexpr u32 kMaxMaterialFileRecords = 256;
inline constexpr u32 kMaxMaterialFileRecordBytes = 1024;

enum class MaterialModel : u8
{
    MetallicRoughness = 0,
};

enum class MaterialQueue : u8
{
    Opaque = 0,
    Masked,
    Transparent,
    Count,
};

enum class MaterialCullMode : u8
{
    None = 0,
    Back,
    Front,
};

enum class MaterialBlendMode : u8
{
    Off = 0,
    Alpha,
};

enum class MaterialColorSpace : u8
{
    Linear = 0,
    Srgb,
};

struct MaterialRenderState
{
    MaterialCullMode cull = MaterialCullMode::Back;
    MaterialBlendMode blend = MaterialBlendMode::Off;
    bool depthTest = true;
    bool depthWrite = true;
};

struct MaterialValue
{
    shaders::ValueType type = shaders::ValueType::Float;
    std::array<float, 4> components{};
};

struct MaterialParameterDesc
{
    memory::StringView name{};
    MaterialValue defaultValue{};
    MaterialValue minimum{};
    MaterialValue maximum{};
};

struct MaterialTextureSlotDesc
{
    memory::StringView name{};
    memory::StringView defaultUri{};
    MaterialColorSpace colorSpace = MaterialColorSpace::Linear;
};

struct MaterialDefinitionDesc
{
    memory::StringView name{};
    MaterialModel model = MaterialModel::MetallicRoughness;
    MaterialQueue queue = MaterialQueue::Opaque;
    /// State metadata; the supplied pipeline must have been built with it.
    MaterialRenderState renderState{};
    const HotGraphicsPipeline* pipeline = nullptr;
    rhi::SamplerHandle defaultSampler{};
    memory::Span<const shaders::Reflection* const> stages{};
    memory::Span<const MaterialParameterDesc> parameters{};
    memory::Span<const MaterialTextureSlotDesc> textureSlots{};
};

struct MaterialTextureView
{
    memory::StringView slot{};
    rhi::TextureViewHandle view{};
};

struct MaterialParameterSchema
{
    std::array<char, kMaxMaterialNameBytes + 1> name{};
    shaders::ValueType type = shaders::ValueType::Float;
    MaterialValue defaultValue{};
    MaterialValue minimum{};
    MaterialValue maximum{};
    u32 offsetBytes = 0;
};

struct MaterialTextureSchema
{
    std::array<char, kMaxMaterialNameBytes + 1> name{};
    std::array<char, kMaxMaterialUriBytes + 1> defaultUri{};
    u32 defaultUriBytes = 0;
    MaterialColorSpace colorSpace = MaterialColorSpace::Linear;
};

/// Validated immutable material metadata associated with one hot pipeline.
class MaterialDefinition
{
public:
    /// Validate authored schemas against shader reflection and copy the
    /// metadata into a self-contained value.
    ///
    /// @param desc Borrowed material description and reflections.
    /// @return The definition, or InvalidArgument/Unsupported when schemas,
    ///         reflection, pipeline state, or bounds do not match.
    [[nodiscard]] static core::Result<MaterialDefinition, core::Error>
    Create(const MaterialDefinitionDesc& desc) noexcept;

    /// Return the immutable material name.
    /// @return A view valid while this definition lives.
    [[nodiscard]] memory::StringView Name() const noexcept;
    /// Return the material model.
    /// @return The model declared at creation.
    [[nodiscard]] MaterialModel Model() const noexcept;
    /// Return the draw queue.
    /// @return Opaque, masked, or transparent.
    [[nodiscard]] MaterialQueue Queue() const noexcept;
    /// Return the render-state metadata.
    /// @return The state the associated pipeline is required to implement.
    [[nodiscard]] MaterialRenderState RenderState() const noexcept;
    /// Return the borrowed pipeline.
    /// @return The pipeline, or null for an empty definition.
    [[nodiscard]] const HotGraphicsPipeline* Pipeline() const noexcept;
    /// Return the pipeline layout generation captured at creation.
    /// @return Generation used to invalidate instances after incompatible reload.
    [[nodiscard]] u32 PipelineLayoutGeneration() const noexcept;
    /// Return the number of reflected parameter schemas.
    /// @return Parameter count.
    [[nodiscard]] u32 ParameterCount() const noexcept;
    /// Find a parameter schema by index.
    /// @param index Schema index below ParameterCount().
    /// @return Pointer to the schema, or null when out of range.
    [[nodiscard]] const MaterialParameterSchema* Parameter(u32 index) const noexcept;
    /// Return the number of declared texture slots.
    /// @return Texture slot count.
    [[nodiscard]] u32 TextureSlotCount() const noexcept;
    /// Find a texture slot schema by index.
    /// @param index Schema index below TextureSlotCount().
    /// @return Pointer to the schema, or null when out of range.
    [[nodiscard]] const MaterialTextureSchema* TextureSlot(u32 index) const noexcept;
    /// Return the reflected parameter block size before device alignment.
    /// @return Number of bytes in the shader's b1, space2 block.
    [[nodiscard]] u32 ParameterBlockSizeBytes() const noexcept;
    /// Return the binding position for the reflected parameter block.
    /// @return Binding index in the PerMaterial group.
    [[nodiscard]] u32 ParameterBlockBinding() const noexcept;

private:
    std::array<char, kMaxMaterialNameBytes + 1> m_name{};
    MaterialModel m_model = MaterialModel::MetallicRoughness;
    MaterialQueue m_queue = MaterialQueue::Opaque;
    MaterialRenderState m_renderState{};
    const HotGraphicsPipeline* m_pipeline = nullptr;
    rhi::SamplerHandle m_defaultSampler{};
    u32 m_layoutGeneration = 0;
    u32 m_parameterBlockSizeBytes = 0;
    u32 m_parameterBlockBinding = kNoBinding;
    u32 m_parameterCount = 0;
    u32 m_textureSlotCount = 0;
    std::array<MaterialParameterSchema, kMaxMaterialParameters> m_parameters{};
    std::array<MaterialTextureSchema, kMaxMaterialTextureSlots> m_textureSlots{};

    friend class MaterialInstance;
};

/// One material instance with its own reflected parameter block and upload
/// resources. The definition, device, and allocator are borrowed and must
/// outlive the instance. Destroy it only after submissions using its resources
/// have completed.
class MaterialInstance
{
public:
    /// Construct an empty instance.
    MaterialInstance() noexcept = default;
    /// Destroy owned GPU resources and CPU bytes. Caller guarantees no in-flight use.
    ~MaterialInstance();
    /// Material instances are move-only.
    MaterialInstance(const MaterialInstance&) = delete;
    MaterialInstance& operator=(const MaterialInstance&) = delete;
    /// Transfer all resource ownership from another instance.
    MaterialInstance(MaterialInstance&& other) noexcept;
    /// Destroy the current instance and take ownership from another.
    MaterialInstance& operator=(MaterialInstance&& other) noexcept;

    /// Create buffers and a per-material bind group for the definition.
    ///
    /// @param definition Validated definition; borrowed for the instance life.
    /// @param device RHI device that owns the GPU resources.
    /// @param allocator Allocator for the CPU parameter block; borrowed.
    /// @param textures Resolved caller-owned views, one for every declared slot.
    /// @return The instance, or the first validation/device/allocation error.
    [[nodiscard]] static core::Result<MaterialInstance, core::Error>
    Create(const MaterialDefinition& definition, rhi::Device& device, memory::Allocator& allocator,
           memory::Span<const MaterialTextureView> textures) noexcept;

    /// Set one named parameter after validating its reflected type and range.
    ///
    /// @param name Parameter name.
    /// @param value New finite value.
    /// @return Ok, or InvalidArgument for an unknown name, mismatched type, or
    ///         a component outside its declared range.
    [[nodiscard]] core::Status<core::Error> SetParameter(memory::StringView name,
                                                         const MaterialValue& value) noexcept;

    /// Replace a texture slot and rebuild the bind group.
    ///
    /// The caller must ensure the old bind group is no longer in flight before
    /// changing resources. The texture view remains caller-owned.
    ///
    /// @param slot Declared texture slot.
    /// @param view Live texture view owned by the caller.
    /// @return Ok, or InvalidArgument/Device error; on failure the old group
    ///         and view remain unchanged.
    [[nodiscard]] core::Status<core::Error> SetTexture(memory::StringView slot,
                                                       rhi::TextureViewHandle view) noexcept;

    /// Record staging upload and transitions on a graphics-capable command list.
    /// The list must be submitted before graphics work reads this instance, and
    /// the caller must keep the block out of use by in-flight work while dirty.
    ///
    /// @param commandList Recording command list on the ordered submission path.
    /// @return Ok when no upload is needed or recording succeeds; an RHI error
    ///         otherwise. Discard the command list after a partial failure.
    [[nodiscard]] core::Status<core::Error> RecordUpload(rhi::CommandList& commandList) noexcept;

    /// Return the current per-material bind group.
    /// @return A live handle, or an invalid handle for an empty instance.
    [[nodiscard]] rhi::BindGroupHandle BindGroup() const noexcept;
    /// Return the monotonically increasing content revision.
    /// @return Revision number.
    [[nodiscard]] u64 Revision() const noexcept;
    /// Check whether an upload is pending.
    /// @return True when parameter bytes have not been successfully recorded.
    [[nodiscard]] bool IsDirty() const noexcept;
    /// Return the reflected, unaligned parameter block size.
    /// @return Byte size.
    [[nodiscard]] u32 ParameterBlockSizeBytes() const noexcept;
    /// Read one current parameter value.
    ///
    /// @param name Parameter name.
    /// @return The value, or NotFound for an undeclared name and
    ///         InvalidArgument for an empty or stale instance.
    [[nodiscard]] core::Result<MaterialValue, core::Error>
    GetParameter(memory::StringView name) const noexcept;

private:
    void Reset() noexcept;
    [[nodiscard]] core::Status<core::Error> RebuildBindGroup() noexcept;

    const MaterialDefinition* m_definition = nullptr;
    rhi::Device* m_device = nullptr;
    memory::Allocator* m_allocator = nullptr;
    memory::Byte* m_parameterBytes = nullptr;
    u32 m_parameterBlockSizeBytes = 0;
    u32 m_allocationSizeBytes = 0;
    u64 m_revision = 0;
    bool m_dirty = false;
    bool m_hasRecordedUpload = false;
    std::array<MaterialValue, kMaxMaterialParameters> m_values{};
    rhi::BufferHandle m_uniformBuffer{};
    rhi::BufferHandle m_stagingBuffer{};
    rhi::BindGroupHandle m_bindGroup{};
    std::array<rhi::TextureViewHandle, kMaxMaterialTextureSlots> m_textureViews{};
};

struct MaterialDrawSortKey
{
    MaterialQueue queue = MaterialQueue::Opaque;
    /// Stable caller-defined pipeline identity.
    u64 pipelineIdentity = 0;
    /// Stable caller-defined material identity.
    u64 materialIdentity = 0;
    /// Positive distance along the camera's forward axis, `-viewPosition.z`.
    float viewDepth = 0.0F;
    /// Stable sequence assigned when submitting the draw.
    u64 submissionSequence = 0;
};

/// Sort draw keys in place without allocating. Opaque/masked keys group by
/// pipeline and material, then sort near-to-far; transparent keys sort far-to-
/// near before deterministic identity and submission-sequence ties.
///
/// @param draws Draw keys to sort.
/// @return Ok, or InvalidArgument when a queue or finite non-negative depth is invalid.
[[nodiscard]] core::Status<core::Error>
SortMaterialDraws(memory::Span<MaterialDrawSortKey> draws) noexcept;

struct MaterialFileParameter
{
    std::array<char, kMaxMaterialNameBytes + 1> name{};
    MaterialValue defaultValue{};
    MaterialValue minimum{};
    MaterialValue maximum{};
};

struct MaterialFileTexture
{
    std::array<char, kMaxMaterialNameBytes + 1> name{};
    std::array<char, kMaxMaterialUriBytes + 1> defaultUri{};
    u32 defaultUriBytes = 0;
    bool hasDefaultUri = false;
    MaterialColorSpace colorSpace = MaterialColorSpace::Linear;
};

/// Parsed `.cybermat` v1 definition; owns all text in fixed-size arrays.
struct MaterialFile
{
    std::array<char, kMaxMaterialNameBytes + 1> name{};
    std::array<char, kMaxMaterialUriBytes + 1> vertexShaderUri{};
    std::array<char, kMaxMaterialUriBytes + 1> pixelShaderUri{};
    MaterialModel model = MaterialModel::MetallicRoughness;
    MaterialQueue queue = MaterialQueue::Opaque;
    MaterialRenderState renderState{};
    u32 parameterCount = 0;
    u32 textureSlotCount = 0;
    std::array<MaterialFileParameter, kMaxMaterialParameters> parameters{};
    std::array<MaterialFileTexture, kMaxMaterialTextureSlots> textureSlots{};
};

struct MaterialParseError
{
    core::Error error{};
    u32 line = 0;
    u32 column = 0;
};

/// Parse a bounded `.cybermat` v1 document without filesystem or GPU access.
///
/// @param text UTF-8 bytes without a BOM.
/// @return The complete file, or an error with one-based source line/column.
[[nodiscard]] core::Result<MaterialFile, MaterialParseError>
ParseMaterialFile(memory::StringView text) noexcept;

/// Serialize a valid material file using canonical record order and LF endings.
///
/// @param file File value to serialize.
/// @param output Caller-owned destination bytes.
/// @return Bytes written, or OutOfRange if the destination is too small and
///         InvalidArgument if the file value violates the format.
[[nodiscard]] core::Result<u32, core::Error>
WriteMaterialFile(const MaterialFile& file, memory::Span<memory::Byte> output) noexcept;

}  // namespace cyber::renderer
