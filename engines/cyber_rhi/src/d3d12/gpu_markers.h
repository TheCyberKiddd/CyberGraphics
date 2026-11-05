#pragma once

#if defined(CYBER_RHI_HAS_D3D12)

struct ID3D12GraphicsCommandList;

namespace cyber::rhi
{

inline constexpr unsigned int kMaxGpuMarkerDepth = 64;

class GpuMarkerScope
{
public:
    GpuMarkerScope() noexcept = default;
    GpuMarkerScope(ID3D12GraphicsCommandList* commandList, const char* label) noexcept;
    ~GpuMarkerScope();

    GpuMarkerScope(const GpuMarkerScope&) = delete;
    GpuMarkerScope& operator=(const GpuMarkerScope&) = delete;
    GpuMarkerScope(GpuMarkerScope&& other) noexcept;
    GpuMarkerScope& operator=(GpuMarkerScope&& other) noexcept;

    void Begin(ID3D12GraphicsCommandList* commandList, const char* label) noexcept;
    void End() noexcept;
    static void Set(ID3D12GraphicsCommandList* commandList, const char* label) noexcept;

private:
    ID3D12GraphicsCommandList* m_commandList = nullptr;
};

class GpuMarker
{
public:
    static void Set(ID3D12GraphicsCommandList* commandList, const char* label) noexcept;
};

}  // namespace cyber::rhi

#endif
