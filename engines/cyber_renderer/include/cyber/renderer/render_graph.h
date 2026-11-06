#pragma once

#include <cyber/core/error.h>
#include <cyber/core/result.h>
#include <cyber/core/types.h>
#include <cyber/memory/allocator.h>
#include <cyber/memory/dynamic_array.h>
#include <cyber/memory/span.h>
#include <cyber/rhi/command_list.h>
#include <cyber/rhi/device.h>

namespace cyber::renderer
{

using cyber::core::Error;
using cyber::core::Result;
using cyber::core::Status;
using cyber::core::u32;
using cyber::core::u8;
using cyber::core::usize;

class RenderGraph;
class RenderPassBuilder;
class RenderPassContext;

/// Logical resource handle owned by one render graph.
struct GraphResourceHandle
{
    u32 index = 0xFFFFFFFFU;
    u32 generation = 0;
    const RenderGraph* owner = nullptr;

    /// True when this handle has a graph owner and a non-sentinel index.
    [[nodiscard]] constexpr bool IsValid() const noexcept
    {
        return owner != nullptr && index != 0xFFFFFFFFU && generation != 0;
    }
};

/// Callback used to declare a pass's resource accesses before graph compilation.
using RenderPassSetup = Status<Error> (*)(RenderPassBuilder& builder, void* userData) noexcept;

/// Callback used to record a pass's commands after its barriers are inserted.
using RenderPassExecute = Status<Error> (*)(RenderPassContext& context, void* userData) noexcept;

/// Pass callback and liveness description.
struct RenderPassDesc
{
    /// Non-empty pass label copied into graph-owned storage.
    const char* name = nullptr;
    /// Declares all resource reads and writes. Invoked once by AddPass.
    RenderPassSetup setup = nullptr;
    /// Records commands during Execute. Required for every pass.
    RenderPassExecute execute = nullptr;
    /// Caller-owned data passed to setup and execute callbacks.
    void* userData = nullptr;
    /// Keeps the pass alive even when it does not contribute to an exported resource.
    bool sideEffect = false;
};

/// Resolves only resources declared by the currently executing pass.
class RenderPassContext
{
public:
    /// Returns the active caller-owned command list for this execute callback.
    /// @return Borrowed recording list; it remains owned by the caller.
    [[nodiscard]] cyber::rhi::CommandList& Commands() const noexcept;

    /// Retrieves a live buffer declared by this pass, or an invalid handle.
    /// @param resource Graph resource declared by the pass.
    /// @return Borrowed RHI handle; the graph retains ownership of transient buffers.
    [[nodiscard]] cyber::rhi::BufferHandle GetBuffer(GraphResourceHandle resource) const noexcept;

    /// Retrieves a live texture declared by this pass, or an invalid handle.
    /// @param resource Graph resource declared by the pass.
    /// @return Borrowed RHI handle; the graph retains ownership of transient textures.
    [[nodiscard]] cyber::rhi::TextureHandle GetTexture(GraphResourceHandle resource) const noexcept;

private:
    friend class RenderGraph;
    RenderPassContext(const RenderGraph& graph, u32 passIndex,
                      cyber::rhi::CommandList& commandList) noexcept;

    const RenderGraph* m_graph = nullptr;
    u32 m_passIndex = 0;
    cyber::rhi::CommandList* m_commandList = nullptr;
};

/// Setup-stage API for declaring pass resource access.
class RenderPassBuilder
{
public:
    /// Declares a read of one graph resource.
    /// @param resource Imported or previously written transient resource.
    /// @param state Read-only RHI state required while this pass executes.
    /// @return Success, or InvalidArgument for an undeclared, incompatible, or duplicate use.
    [[nodiscard]] Status<Error> Read(GraphResourceHandle resource,
                                     cyber::rhi::ResourceState state) noexcept;

    /// Declares a write of one graph resource.
    /// @param resource Graph resource written by this pass.
    /// @param state Single write RHI state required while this pass executes.
    /// @return Success, or InvalidArgument for an undeclared, incompatible, or duplicate use.
    [[nodiscard]] Status<Error> Write(GraphResourceHandle resource,
                                      cyber::rhi::ResourceState state) noexcept;

private:
    friend class RenderGraph;
    RenderPassBuilder(RenderGraph& graph, u32 passIndex) noexcept;

    RenderGraph* m_graph = nullptr;
    u32 m_passIndex = 0;
};

/// Explicit, deterministic render graph with inferred hazards and barriers.
class RenderGraph
{
public:
    /// Creates an empty graph borrowing a valid device and allocator.
    /// @param device RHI device used for imported-resource validation and transient creation.
    /// @param allocator Allocator for graph metadata; must outlive the graph.
    explicit RenderGraph(cyber::rhi::Device& device, cyber::memory::Allocator& allocator) noexcept;
    ~RenderGraph();

    RenderGraph(const RenderGraph&) = delete;
    RenderGraph& operator=(const RenderGraph&) = delete;
    RenderGraph(RenderGraph&&) = delete;
    RenderGraph& operator=(RenderGraph&&) = delete;

    /// Imports a live buffer without taking ownership.
    /// @param name Non-empty label of at most 63 bytes, copied by the graph.
    /// @param buffer Live buffer owned by the same device.
    /// @return A graph handle, or InvalidArgument for an invalid or duplicate import.
    [[nodiscard]] Result<GraphResourceHandle, Error>
    ImportBuffer(const char* name, cyber::rhi::BufferHandle buffer) noexcept;

    /// Imports a live texture whose subresources share one tracked state.
    /// @param name Non-empty label of at most 63 bytes, copied by the graph.
    /// @param texture Live texture owned by the same device.
    /// @return A graph handle, or InvalidArgument for an invalid, duplicate, or mixed-state import.
    [[nodiscard]] Result<GraphResourceHandle, Error>
    ImportTexture(const char* name, cyber::rhi::TextureHandle texture) noexcept;

    /// Creates a graph-owned transient buffer.
    /// @param name Non-empty label of at most 63 bytes, copied by the graph.
    /// @param desc Valid RHI buffer description; lifetime is forced to Transient.
    /// @return A graph handle, or InvalidArgument for a malformed description.
    [[nodiscard]] Result<GraphResourceHandle, Error>
    CreateTransientBuffer(const char* name, const cyber::rhi::BufferDesc& desc) noexcept;

    /// Creates a graph-owned transient texture.
    /// @param name Non-empty label of at most 63 bytes, copied by the graph.
    /// @param desc Valid RHI texture description; lifetime is forced to Transient.
    /// @return A graph handle, or InvalidArgument for a malformed description.
    [[nodiscard]] Result<GraphResourceHandle, Error>
    CreateTransientTexture(const char* name, const cyber::rhi::TextureDesc& desc) noexcept;

    /// Adds a pass and invokes its setup callback immediately.
    /// @param desc Name, setup/execute callbacks, user data, and side-effect root.
    /// @return Success, or InvalidArgument for malformed callbacks/name or the setup error.
    [[nodiscard]] Status<Error> AddPass(const RenderPassDesc& desc) noexcept;

    /// Marks a resource as an observable graph output.
    /// @param resource Resource whose last writer roots pass liveness.
    /// @param finalState State to transition to after the final live pass, or Undefined for none.
    /// @return Success, or InvalidArgument for an invalid handle or incompatible final state.
    [[nodiscard]] Status<Error>
    Export(GraphResourceHandle resource,
           cyber::rhi::ResourceState finalState = cyber::rhi::ResourceState::Undefined) noexcept;

    /// Computes dependencies, pass liveness, and transient alias slots.
    /// @return Success, or InvalidArgument for invalid declarations and Internal on allocation
    /// failure.
    [[nodiscard]] Status<Error> Compile() noexcept;

    /// Records barriers and executes live passes on a caller-owned command list.
    /// @param commandList Recording list for the graph's RHI device.
    /// @return Success, or the first allocation, barrier, callback, or list-state error.
    /// A successful execution must be acknowledged after command-list submission before reuse.
    [[nodiscard]] Status<Error> Execute(cyber::rhi::CommandList& commandList) noexcept;

    /// Acknowledges a successful submission of the command list recorded by the latest execution.
    /// Call only after submission succeeds; the graph may then record another frame.
    /// @return Success, or InvalidArgument when there is no successful execution awaiting
    /// submission.
    [[nodiscard]] Status<Error> AcknowledgeSubmission() noexcept;

    /// Destroys graph-owned transient resources and leaves imported resources untouched.
    /// @return Success, or the first RHI destruction error; failed handles remain owned.
    /// The caller must first discard any unsubmitted recording and ensure submitted GPU work
    /// no longer references graph-owned resources.
    [[nodiscard]] Status<Error> Destroy() noexcept;

    /// Number of passes declared in the graph.
    [[nodiscard]] u32 PassCount() const noexcept;

    /// Number of declared passes removed by compilation.
    [[nodiscard]] u32 CulledPassCount() const noexcept;

    /// Number of physical transient resources allocated after compilation.
    [[nodiscard]] u32 TransientAllocationCount() const noexcept;

    /// Number of transient logical resources reusing an existing physical slot.
    [[nodiscard]] u32 AliasedResourceCount() const noexcept;

    /// Writes a deterministic human-readable graph dump.
    /// @param output Caller-owned storage; requires room for the full text and a trailing NUL.
    /// @return Number of bytes written, or OutOfRange without writing when capacity is
    /// insufficient.
    [[nodiscard]] Result<usize, Error>
    WriteTextDump(cyber::memory::Span<char> output) const noexcept;

    /// Writes a deterministic Graphviz DOT description of live dependencies and resources.
    /// @param output Caller-owned storage; requires room for the full text and a trailing NUL.
    /// @return Number of bytes written, or OutOfRange without writing when capacity is
    /// insufficient.
    [[nodiscard]] Result<usize, Error>
    WriteGraphvizDot(cyber::memory::Span<char> output) const noexcept;

private:
    friend class RenderPassBuilder;
    friend class RenderPassContext;

    enum class ResourceKind : u8
    {
        Buffer,
        Texture
    };

    struct ResourceRecord
    {
        ResourceKind kind = ResourceKind::Buffer;
        char name[64]{};
        bool imported = false;
        bool exported = false;
        bool hasLiveUse = false;
        cyber::rhi::ResourceState initialState = cyber::rhi::ResourceState::Undefined;
        cyber::rhi::ResourceState currentState = cyber::rhi::ResourceState::Undefined;
        cyber::rhi::ResourceState finalState = cyber::rhi::ResourceState::Undefined;
        cyber::rhi::BufferDesc bufferDesc{};
        cyber::rhi::TextureDesc textureDesc{};
        cyber::rhi::BufferHandle importedBuffer{};
        cyber::rhi::TextureHandle importedTexture{};
        u32 firstPass = 0xFFFFFFFFU;
        u32 lastPass = 0;
        u32 physicalSlot = 0xFFFFFFFFU;
    };

    struct PassRecord
    {
        char name[64]{};
        u32 useStart = 0;
        u32 useCount = 0;
        bool sideEffect = false;
        bool live = false;
        RenderPassExecute execute = nullptr;
        void* userData = nullptr;
    };

    struct UseRecord
    {
        u32 resourceIndex = 0;
        cyber::rhi::ResourceState state = cyber::rhi::ResourceState::Undefined;
        bool write = false;
    };

    struct Dependency
    {
        u32 producer = 0;
        u32 consumer = 0;
    };

    struct PhysicalSlot
    {
        ResourceKind kind = ResourceKind::Buffer;
        u32 firstResource = 0;
        u32 lastPass = 0;
        cyber::rhi::ResourceState state = cyber::rhi::ResourceState::Undefined;
        cyber::rhi::BufferHandle buffer{};
        cyber::rhi::TextureHandle texture{};
    };

    [[nodiscard]] Status<Error> AddUse(u32 passIndex, GraphResourceHandle resource,
                                       cyber::rhi::ResourceState state, bool write) noexcept;
    [[nodiscard]] bool IsValidResource(GraphResourceHandle resource) const noexcept;
    [[nodiscard]] bool IsDeclaredByPass(u32 passIndex, u32 resourceIndex) const noexcept;
    [[nodiscard]] cyber::rhi::BufferHandle ResolveBuffer(u32 resourceIndex) const noexcept;
    [[nodiscard]] cyber::rhi::TextureHandle ResolveTexture(u32 resourceIndex) const noexcept;
    [[nodiscard]] bool Compatible(const ResourceRecord& lhs,
                                  const ResourceRecord& rhs) const noexcept;
    [[nodiscard]] Status<Error> DestroyTransientSlots() noexcept;
    [[nodiscard]] Status<Error> EnsureTransientResources() noexcept;
    [[nodiscard]] Status<Error> Transition(u32 resourceIndex, cyber::rhi::ResourceState target,
                                           cyber::rhi::CommandList& commandList) noexcept;

    cyber::rhi::Device* m_device = nullptr;
    cyber::memory::Allocator* m_allocator = nullptr;
    cyber::memory::DynamicArray<ResourceRecord> m_resources;
    cyber::memory::DynamicArray<PassRecord> m_passes;
    cyber::memory::DynamicArray<UseRecord> m_uses;
    cyber::memory::DynamicArray<Dependency> m_dependencies;
    cyber::memory::DynamicArray<PhysicalSlot> m_slots;
    u32 m_generation = 1;
    u32 m_setupPass = 0xFFFFFFFFU;
    bool m_settingUp = false;
    bool m_compiled = false;
    bool m_executionPending = false;
    bool m_poisoned = false;
    bool m_destroyed = false;
};

}  // namespace cyber::renderer
