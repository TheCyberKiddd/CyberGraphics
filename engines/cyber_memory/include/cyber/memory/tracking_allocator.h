#pragma once

#include <cyber/memory/allocator.h>

namespace cyber::memory
{

// One outstanding allocation reported by TrackingAllocator. Address is the
// payload pointer returned to the caller; tag points at caller-owned text.
struct LeakInfo
{
    const void* address = nullptr;
    usize size = 0;
    const char* tag = nullptr;
};

// Receives one leak record. The callback must not allocate through the tracked
// allocator or retain the record after returning.
using LeakCallback = void (*)(void* context, const LeakInfo& leak) noexcept;

// Allocator wrapper that records every outstanding allocation in an intrusive
// list. The 64-byte header is allocated from the upstream allocator together
// with the payload, so reporting never allocates. Payload alignment is limited
// to 64 bytes.
class TrackingAllocator final : public Allocator
{
public:
    explicit TrackingAllocator(Allocator& upstream, const char* defaultTag = "untracked") noexcept;
    ~TrackingAllocator() override;

    TrackingAllocator(const TrackingAllocator&) = delete;
    TrackingAllocator& operator=(const TrackingAllocator&) = delete;

    [[nodiscard]] void* Allocate(usize size, usize alignment = kDefaultAlignment) noexcept override;

    // Same as Allocate but records tag instead of the default tag. The tag
    // pointer must remain valid while the allocation is live.
    [[nodiscard]] void* AllocateTagged(usize size, usize alignment, const char* tag) noexcept;

    void Deallocate(void* pointer) noexcept override;
    void Reset() noexcept override;
    [[nodiscard]] bool Owns(const void* pointer) const noexcept override;
    [[nodiscard]] AllocatorStats Stats() const noexcept override;

    [[nodiscard]] usize LeakCount() const noexcept;
    [[nodiscard]] usize ReportLeaks(LeakCallback callback, void* context) const noexcept;

    [[nodiscard]] Allocator& Upstream() noexcept;
    [[nodiscard]] const Allocator& Upstream() const noexcept;

    void SetDefaultTag(const char* tag) noexcept;
    [[nodiscard]] const char* DefaultTag() const noexcept;

private:
    struct Header
    {
        Header* prev = nullptr;
        Header* next = nullptr;
        usize size = 0;
        const char* tag = nullptr;
        u32 magic = 0;
        u32 reserved = 0;
        u64 reserved2 = 0;
        u64 padding = 0;
        u64 padding2 = 0;
    };

    static_assert(sizeof(Header) == 64);

    static constexpr u32 kMagic = 0x43474D54U;  // "CGMT"

    void Unlink(Header* header) noexcept;

    Allocator* m_upstream = nullptr;
    Header* m_head = nullptr;
    usize m_used = 0;
    usize m_peak = 0;
    usize m_allocations = 0;
    const char* m_defaultTag = "untracked";
};

}  // namespace cyber::memory
