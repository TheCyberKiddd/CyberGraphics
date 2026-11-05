#include <cyber/core/log.h>
#include <cyber/core/types.h>
#include <cyber/memory/heap_allocator.h>
#include <cyber/rhi/backend.h>
#include <cyber/rhi/device.h>
#include <cyber/rhi/texture.h>
#include <cyber/test/test.h>

#include <cstdio>
#include <cstring>
#include <utility>

using namespace cyber::rhi;
using cyber::core::Logger;
using cyber::core::LogRecord;
using cyber::core::LogSink;
using cyber::core::u32;
using cyber::core::usize;
using cyber::memory::HeapAllocator;

namespace
{

class LeakLogCapture final : public LogSink
{
public:
    explicit LeakLogCapture(Logger& logger) noexcept : m_logger(&logger)
    {
        m_attached = m_logger->AddSink(this);
    }

    ~LeakLogCapture() override
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

    [[nodiscard]] bool Contains(const char* text) const noexcept
    {
        return std::strstr(m_messages, text) != nullptr;
    }

    void Write(const LogRecord& record) noexcept override
    {
        const usize remaining = sizeof(m_messages) - m_length;
        if (remaining <= 1)
        {
            return;
        }
        const int written = std::snprintf(m_messages + m_length, remaining, "%s\n", record.message);
        if (written > 0)
        {
            const usize amount = static_cast<usize>(written);
            m_length += amount < remaining ? amount : remaining - 1;
        }
    }

private:
    Logger* m_logger = nullptr;
    char m_messages[2048]{};
    usize m_length = 0;
    bool m_attached = false;
};

}  // namespace

CG_TEST(rhi_leak_report_names_live_resources)
{
    HeapAllocator heap;
    DeviceDesc deviceDesc{};
    deviceDesc.backend = NullBackend();
    auto createdDevice = Device::Create(deviceDesc, heap);
    CG_REQUIRE(static_cast<bool>(createdDevice));
    Device device = std::move(createdDevice.Value());

    LeakLogCapture capture(cyber::core::DefaultLogger());
    CG_REQUIRE(capture.IsAttached());

    BufferDesc bufferDesc{};
    bufferDesc.usage = BufferUsage::Vertex;
    bufferDesc.sizeBytes = 128;
    bufferDesc.label = "intentional buffer leak";
    auto buffer = device.CreateBuffer(bufferDesc);
    CG_REQUIRE(static_cast<bool>(buffer));

    TextureDesc textureDesc{};
    textureDesc.dimension = TextureDimension::Dim2D;
    textureDesc.format = Format::RGBA8Unorm;
    textureDesc.usage = TextureUsage::RenderTarget;
    textureDesc.width = 4;
    textureDesc.height = 4;
    textureDesc.label = "intentional texture leak";
    auto texture = device.CreateTexture(textureDesc);
    CG_REQUIRE(static_cast<bool>(texture));

    CG_EXPECT_EQ(device.ReportLeaks(), u32{2});
    CG_EXPECT_TRUE(capture.Contains("intentional buffer leak"));
    CG_EXPECT_TRUE(capture.Contains("intentional texture leak"));

    CG_EXPECT_TRUE(device.DestroyBuffer(buffer.Value()).IsOk());
    CG_EXPECT_TRUE(device.DestroyTexture(texture.Value()).IsOk());
    CG_EXPECT_EQ(device.ReportLeaks(), u32{0});
}
