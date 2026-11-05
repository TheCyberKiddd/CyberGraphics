#include "gpu_markers.h"

#if defined(CYBER_RHI_HAS_D3D12)

#include <cstring>
#include <d3d12.h>
#include <limits>
#include <utility>

namespace cyber::rhi
{

namespace
{

#if defined(CYBER_GPU_MARKERS)
[[nodiscard]] UINT MarkerByteCount(const char* label) noexcept
{
    if (label == nullptr)
    {
        return 0;
    }
    const std::size_t length = std::strlen(label);
    if (length >= (std::numeric_limits<UINT>::max)())
    {
        return 0;
    }
    return static_cast<UINT>(length + 1);
}
#endif

}  // namespace

GpuMarkerScope::GpuMarkerScope(ID3D12GraphicsCommandList* commandList, const char* label) noexcept
{
    Begin(commandList, label);
}

GpuMarkerScope::~GpuMarkerScope()
{
    End();
}

GpuMarkerScope::GpuMarkerScope(GpuMarkerScope&& other) noexcept : m_commandList(other.m_commandList)
{
    other.m_commandList = nullptr;
}

GpuMarkerScope& GpuMarkerScope::operator=(GpuMarkerScope&& other) noexcept
{
    if (this != &other)
    {
        End();
        m_commandList = other.m_commandList;
        other.m_commandList = nullptr;
    }
    return *this;
}

void GpuMarkerScope::Begin(ID3D12GraphicsCommandList* commandList, const char* label) noexcept
{
    End();
#if defined(CYBER_GPU_MARKERS)
    const UINT byteCount = MarkerByteCount(label);
    if (commandList != nullptr && byteCount != 0)
    {
        commandList->BeginEvent(1, label, byteCount);
        m_commandList = commandList;
    }
#else
    (void)commandList;
    (void)label;
#endif
}

void GpuMarkerScope::End() noexcept
{
#if defined(CYBER_GPU_MARKERS)
    if (m_commandList != nullptr)
    {
        m_commandList->EndEvent();
        m_commandList = nullptr;
    }
#else
    m_commandList = nullptr;
#endif
}

void GpuMarker::Set(ID3D12GraphicsCommandList* commandList, const char* label) noexcept
{
#if defined(CYBER_GPU_MARKERS)
    const UINT byteCount = MarkerByteCount(label);
    if (commandList != nullptr && byteCount != 0)
    {
        commandList->SetMarker(1, label, byteCount);
    }
#else
    (void)commandList;
    (void)label;
#endif
}

}  // namespace cyber::rhi

#endif
