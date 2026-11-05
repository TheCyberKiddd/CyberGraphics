#include "../src/d3d12/d3d12_device.h"
#include "../src/d3d12/d3d12_internal.h"

#include <cyber/core/log.h>
#include <cyber/core/types.h>
#include <cyber/memory/heap_allocator.h>
#include <cyber/rhi/command_list.h>
#include <cyber/rhi/d3d12.h>
#include <cyber/test/test.h>

#include <cstdio>
#include <cstring>

using cyber::core::Logger;
using cyber::core::LogRecord;
using cyber::core::LogSink;
using cyber::core::u32;
using cyber::memory::HeapAllocator;
using cyber::rhi::BufferDesc;
using cyber::rhi::BufferHandle;
using cyber::rhi::BufferUsage;
using cyber::rhi::D3D12Backend;
using cyber::rhi::DeviceDesc;
using cyber::rhi::FenceDesc;
using cyber::rhi::detail::CreateD3D12Device;
using cyber::rhi::detail::D3D12Device;
using cyber::rhi::detail::DestroyD3D12Device;

namespace
{

class DeviceLostLogCapture final : public LogSink
{
public:
    explicit DeviceLostLogCapture(Logger& logger) noexcept : m_logger(&logger)
    {
        m_attached = m_logger->AddSink(this);
    }

    ~DeviceLostLogCapture() override
    {
        if (m_attached)
        {
            (void)m_logger->RemoveSink(this);
        }
    }

    [[nodiscard]] bool IsAttached() const noexcept
    {
        return m_attached;
    }

    void Write(const LogRecord& record) noexcept override
    {
        const cyber::core::usize remaining = sizeof(m_messages) - m_length;
        if (remaining <= 1)
        {
            return;
        }
        const int written = std::snprintf(m_messages + m_length, remaining, "%s\n", record.message);
        if (written > 0)
        {
            const cyber::core::usize amount = static_cast<cyber::core::usize>(written);
            m_length += amount < remaining ? amount : remaining - 1;
        }
    }

    [[nodiscard]] bool Contains(const char* text) const noexcept
    {
        return std::strstr(m_messages, text) != nullptr;
    }

private:
    Logger* m_logger = nullptr;
    char m_messages[8192]{};
    cyber::core::usize m_length = 0;
    bool m_attached = false;
};

}  // namespace

CG_TEST(rhi_device_lost_is_sticky_and_logs_dred)
{
    HeapAllocator heap;
    const cyber::rhi::Backend* backend = D3D12Backend();
    CG_REQUIRE(backend != nullptr);

    cyber::rhi::AdapterInfo adapters[32]{};
    const u32 adapterCount = backend->enumerateAdapters(
        adapters, static_cast<u32>(sizeof(adapters) / sizeof(adapters[0])));
    CG_REQUIRE(adapterCount > 0);

    DeviceDesc desc{};
    desc.backend = backend;
    desc.adapterIndex = adapterCount - 1;
    auto created = CreateD3D12Device(desc, heap);
    CG_REQUIRE(static_cast<bool>(created));
    const cyber::rhi::DeviceCreation creation = created.Value();
    auto& device = *static_cast<D3D12Device*>(creation.internal);

    BufferDesc bufferDesc{};
    bufferDesc.usage = BufferUsage::Vertex;
    bufferDesc.sizeBytes = 256;
    bufferDesc.label = "device-loss cleanup buffer";
    BufferHandle buffer{};
    const auto createdBuffer =
        creation.interface->createBuffer(creation.internal, bufferDesc, &buffer);
    CG_EXPECT_TRUE(createdBuffer.IsOk());
    if (!createdBuffer)
    {
        DestroyD3D12Device(creation.internal);
        return;
    }

    ID3D12Device5* device5 = nullptr;
    const HRESULT queried =
        device.device->QueryInterface(__uuidof(ID3D12Device5), reinterpret_cast<void**>(&device5));
    if (FAILED(queried) || device5 == nullptr)
    {
        DestroyD3D12Device(creation.internal);
        CG_SKIP("the installed D3D12 runtime does not expose ID3D12Device5");
    }

    DeviceLostLogCapture capture(cyber::core::DefaultLogger());
    CG_EXPECT_TRUE(capture.IsAttached());
    device5->RemoveDevice();
    device5->Release();

    const auto removed = creation.interface->waitIdle(creation.internal);
    CG_EXPECT_FALSE(removed);
    if (!removed)
    {
        CG_EXPECT_EQ(removed.Error().code, cyber::core::ErrorCode::DeviceLost);
        CG_EXPECT_TRUE(std::strstr(removed.Error().message, "device lost") != nullptr);
    }

    cyber::rhi::CommandListHandle rejectedList{};
    cyber::rhi::CommandListCreation listCreation{};
    const cyber::rhi::CommandListDesc listDesc{};
    const auto createList = creation.interface->createCommandList(creation.internal, listDesc,
                                                                  &rejectedList, &listCreation);
    CG_EXPECT_FALSE(createList);
    if (!createList)
    {
        CG_EXPECT_EQ(createList.Error().code, cyber::core::ErrorCode::DeviceLost);
    }

    cyber::rhi::FenceHandle fence{};
    const auto laterCreate =
        creation.interface->createFence(creation.internal, FenceDesc{}, &fence);
    CG_EXPECT_FALSE(laterCreate);
    if (!laterCreate)
    {
        CG_EXPECT_EQ(laterCreate.Error().code, cyber::core::ErrorCode::DeviceLost);
    }
    BufferHandle rejectedBuffer{};
    const auto laterBufferCreate =
        creation.interface->createBuffer(creation.internal, bufferDesc, &rejectedBuffer);
    CG_EXPECT_FALSE(laterBufferCreate);
    if (!laterBufferCreate)
    {
        CG_EXPECT_EQ(laterBufferCreate.Error().code, cyber::core::ErrorCode::DeviceLost);
    }
    const auto laterWait = creation.interface->waitIdle(creation.internal);
    CG_EXPECT_FALSE(laterWait);
    if (!laterWait)
    {
        CG_EXPECT_EQ(laterWait.Error().code, cyber::core::ErrorCode::DeviceLost);
    }

    if (capture.IsAttached())
    {
        CG_EXPECT_TRUE(capture.Contains("d3d12 device removed"));
        CG_EXPECT_TRUE(capture.Contains("last_fence"));
        CG_EXPECT_TRUE(capture.Contains("DRED"));
    }

    CG_EXPECT_TRUE(creation.interface->destroyBuffer(creation.internal, buffer).IsOk());
    DestroyD3D12Device(creation.internal);
    CG_EXPECT_EQ(heap.Stats().used, cyber::core::u64{0});
}
