#pragma once

#include <cyber/core/error.h>
#include <cyber/core/result.h>
#include <cyber/core/types.h>
#include <cyber/jobs/job_system.h>
#include <cyber/memory/allocator.h>
#include <cyber/renderer/reflected_layout.h>
#include <cyber/rhi/device.h>
#include <cyber/rhi/handle.h>
#include <cyber/rhi/pipeline.h>
#include <cyber/shaders/compiler.h>

namespace cyber::renderer
{

using cyber::core::u32;
using cyber::core::u64;

/// Creation parameters for a HotGraphicsPipeline.
struct HotGraphicsPipelineDesc
{
    /// Device that owns every created object. Borrowed; must outlive the pipeline.
    rhi::Device* device = nullptr;
    /// Job system that builds pipelines. Borrowed. Null builds synchronously
    /// inside Rebuild (tools and tests only).
    jobs::JobSystem* jobs = nullptr;
    /// Render state. `layout`, `vertexShader`, `pixelShader`, and
    /// `vertexLayout` are ignored and filled from the compiled shaders; the
    /// `colorAttachments` span is copied (at most 8 attachments).
    rhi::GraphicsPipelineDesc state{};
    /// Pipeline Current() returns until the first build succeeds. Borrowed and
    /// never destroyed by this object; may be invalid.
    rhi::PipelineHandle fallback{};
    /// Frames the GPU may lag behind the CPU. A replaced object is destroyed
    /// once BeginFrame has been called with a frame index at least this many
    /// frames after the swap. Must be at least 1.
    u32 framesInFlight = 2;
    /// Debug label; borrowed static string.
    const char* label = "hot graphics pipeline";
};

/// A graphics pipeline that can be rebuilt from new shaders while it is used.
///
/// Rebuild copies the shaders and starts CPU-only reflection and vertex-layout
/// preparation on the job system. BeginFrame creates the RHI shader, layout,
/// and pipeline objects and is the only place the pipeline changes, so every
/// command list recorded between two BeginFrame calls sees one version.
///
/// Bind-group layouts are reused when a rebuild's reflected layout equals the
/// current one, so the caller's bind groups stay valid; LayoutGeneration
/// changes only when they do not, and the caller must then create new groups
/// against BindGroupLayout(slot).
///
/// Threading: every member function is called from the owning (render)
/// thread. Job workers prepare reflection and vertex-layout data only; shader,
/// layout, and pipeline RHI objects are created and destroyed by the owner.
class HotGraphicsPipeline
{
public:
    /// Construct an empty pipeline wrapper.
    HotGraphicsPipeline() noexcept = default;
    /// Wait for builds and release the owned pipeline objects.
    ~HotGraphicsPipeline();

    /// Hot graphics pipelines are move-only.
    HotGraphicsPipeline(const HotGraphicsPipeline&) = delete;
    HotGraphicsPipeline& operator=(const HotGraphicsPipeline&) = delete;
    HotGraphicsPipeline(HotGraphicsPipeline&& other) noexcept;
    HotGraphicsPipeline& operator=(HotGraphicsPipeline&& other) noexcept;

    /// Create an empty hot pipeline. Nothing is built until Rebuild.
    ///
    /// @param desc Configuration; copied.
    /// @param allocator Backs all state and copies; must outlive the object.
    /// @return The object, or ErrorCode::InvalidArgument for a null device, zero
    ///         framesInFlight, or more than 8 color attachments.
    [[nodiscard]] static core::Result<HotGraphicsPipeline, core::Error>
    Create(const HotGraphicsPipelineDesc& desc, memory::Allocator& allocator) noexcept;

    /// Request a build from a vertex and a pixel shader. Both are copied. If a
    /// build is already running, this request replaces any queued one and
    /// starts when the running build is published.
    ///
    /// @return Ok, ErrorCode::InvalidArgument when the stages are wrong, or
    ///         ErrorCode::Internal when the copy cannot be allocated.
    [[nodiscard]] core::Status<core::Error> Rebuild(const shaders::CompiledShader& vertex,
                                                    const shaders::CompiledShader& pixel) noexcept;

    /// Frame boundary. Publishes a finished build (or records its error and
    /// keeps the current pipeline), starts a queued build, and destroys
    /// replaced objects older than framesInFlight frames.
    ///
    /// @param frameIndex Monotonically increasing frame number.
    void BeginFrame(u64 frameIndex) noexcept;

    /// The pipeline to draw with this frame: the newest published build, or
    /// the fallback before the first success.
    [[nodiscard]] rhi::PipelineHandle Current() const noexcept;

    /// True once a build has been published.
    [[nodiscard]] bool HasBuilt() const noexcept;

    /// Bind-group layout of `slot` for the current build; invalid before the
    /// first build or for an unused slot.
    [[nodiscard]] rhi::BindGroupLayoutHandle
    BindGroupLayout(rhi::BindGroupSlot slot) const noexcept;

    /// The reflected layout of the current build.
    [[nodiscard]] const ReflectedLayout& Layout() const noexcept;

    /// Number of successful swaps.
    [[nodiscard]] u32 Generation() const noexcept;

    /// Number of times the bind-group layouts changed; starts at 0 and becomes
    /// 1 with the first build.
    [[nodiscard]] u32 LayoutGeneration() const noexcept;

    /// True while a build is running or queued.
    [[nodiscard]] bool IsBuilding() const noexcept;

    /// Error of the most recent failed build, or null after a success.
    [[nodiscard]] const core::Error* LastError() const noexcept;

    /// Objects waiting for the frames-in-flight window to pass.
    [[nodiscard]] u32 RetiredCount() const noexcept;

    /// Wait for the running build without publishing it. For shutdown and
    /// tests only.
    void WaitForBuild() noexcept;

private:
    struct Impl;
    explicit HotGraphicsPipeline(Impl* impl) noexcept;
    Impl* m_impl = nullptr;
};

}  // namespace cyber::renderer
