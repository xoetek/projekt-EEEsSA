#pragma once

#include "eee/render/IRenderDevice.hpp"

#include <cstring>
#include <string>
#include <vector>

namespace eee::test {

// Records every call the shared renderer makes so tests can assert on the
// command stream without a GPU, window or platform.
class MockDevice final : public eee::render::IRenderDevice {
public:
    using BufferHandle = eee::render::BufferHandle;
    using PipelineHandle = eee::render::PipelineHandle;

    struct BufferRecord {
        eee::render::BufferUsage usage = eee::render::BufferUsage::Vertex;
        std::vector<eee::core::u8> bytes;
        bool alive = true;
        eee::core::u32 updates = 0;
    };

    struct PipelineRecord {
        eee::render::PipelineKind kind = eee::render::PipelineKind::Mesh;
        bool depthTest = true;
        bool alphaBlend = true;
        bool alive = true;
        std::string vertexSource;
    };

    struct DrawRecord {
        eee::core::u32 indexCount = 0;
        eee::core::u32 instanceCount = 0;
        PipelineHandle pipeline = 0;
        BufferHandle vertexBuffer = 0;
        BufferHandle instanceBuffer = 0;
        BufferHandle indexBuffer = 0;
    };

    [[nodiscard]] const char* name() const override { return "mock"; }

    bool initialize(const eee::render::DeviceInitDesc& desc) override {
        initDesc = desc;
        ++initializeCalls;
        return initializeSucceeds;
    }

    void shutdown() override { ++shutdownCalls; }

    void resize(eee::core::i32 width, eee::core::i32 height) override {
        width_ = width;
        height_ = height;
        ++resizeCalls;
    }

    void setVSync(bool enabled) override {
        vsync = enabled;
        ++vsyncCalls;
    }

    void setMultisampleSamples(eee::core::u8 samples) override {
        samples = samples <= 1 ? 1 : samples;
        ++multisampleCalls;
    }
    [[nodiscard]] eee::core::u8 multisampleSamples() const override { return samples; }
    [[nodiscard]] eee::core::u8 maxMultisampleSamples() const override { return 8; }

    void beginFrame(const eee::render::FrameDesc& desc) override {
        clearColor = desc.clearColor;
        ++beginFrames;
    }

    void endFrame() override { ++endFrames; }

    void waitIdle() override { ++waitIdleCalls; }

    BufferHandle createBuffer(eee::render::BufferUsage usage, const void* data,
                              eee::core::usize size) override {
        BufferRecord record;
        record.usage = usage;
        record.bytes.assign(size, 0);
        if (data != nullptr && size > 0) {
            std::memcpy(record.bytes.data(), data, size);
        }
        buffers.push_back(std::move(record));
        ++createBufferCalls;
        return static_cast<BufferHandle>(buffers.size());
    }

    void updateBuffer(BufferHandle handle, const void* data, eee::core::usize size,
                      eee::core::usize offset) override {
        if (!validBuffer(handle) || data == nullptr) {
            return;
        }
        BufferRecord& record = buffers[handle - 1];
        if (offset + size > record.bytes.size()) {
            record.bytes.resize(offset + size, 0);
        }
        std::memcpy(record.bytes.data() + offset, data, size);
        ++record.updates;
        ++updateBufferCalls;
        lastUpdatedBuffer = handle;
    }

    void destroyBuffer(BufferHandle handle) override {
        if (validBuffer(handle)) {
            buffers[handle - 1].alive = false;
            ++destroyBufferCalls;
        }
    }

    PipelineHandle createPipeline(const eee::render::PipelineDesc& desc) override {
        PipelineRecord record;
        record.kind = desc.kind;
        record.depthTest = desc.depthTest;
        record.alphaBlend = desc.alphaBlend;
        record.vertexSource = desc.shaders.vertex;
        pipelines.push_back(std::move(record));
        ++createPipelineCalls;
        return static_cast<PipelineHandle>(pipelines.size());
    }

    void destroyPipeline(PipelineHandle handle) override {
        if (validPipeline(handle)) {
            pipelines[handle - 1].alive = false;
            ++destroyPipelineCalls;
        }
    }

    void bindPipeline(PipelineHandle handle) override {
        currentPipeline = handle;
        ++bindPipelineCalls;
    }

    void setFrameConstants(const eee::render::FrameConstants& value) override {
        constants = value;
        ++constantCalls;
    }

    void bindVertexBuffer(BufferHandle handle, eee::core::u32 stride) override {
        currentVertex = handle;
        vertexStride = stride;
    }

    void bindInstanceBuffer(BufferHandle handle, eee::core::u32 stride) override {
        currentInstance = handle;
        instanceStride = stride;
    }

    void bindIndexBuffer(BufferHandle handle) override { currentIndex = handle; }

    void drawIndexed(eee::core::u32 indexCount, eee::core::u32 instanceCount) override {
        DrawRecord record;
        record.indexCount = indexCount;
        record.instanceCount = instanceCount;
        record.pipeline = currentPipeline;
        record.vertexBuffer = currentVertex;
        record.instanceBuffer = currentInstance;
        record.indexBuffer = currentIndex;
        draws.push_back(record);
        ++drawCalls;
    }

    [[nodiscard]] eee::render::ShaderSources meshShaderSources() const override {
        return {"mock-mesh-vs", "mock-mesh-fs"};
    }

    [[nodiscard]] eee::render::ShaderSources overlayShaderSources() const override {
        return {"mock-overlay-vs", "mock-overlay-fs"};
    }

    [[nodiscard]] bool validBuffer(BufferHandle handle) const {
        return handle >= 1 && handle <= buffers.size() && buffers[handle - 1].alive;
    }

    [[nodiscard]] bool validPipeline(PipelineHandle handle) const {
        return handle >= 1 && handle <= pipelines.size() && pipelines[handle - 1].alive;
    }

    [[nodiscard]] const BufferRecord& buffer(BufferHandle handle) const {
        return buffers[handle - 1];
    }

    [[nodiscard]] const PipelineRecord& pipeline(PipelineHandle handle) const {
        return pipelines[handle - 1];
    }

    [[nodiscard]] eee::core::usize aliveBufferCount() const {
        eee::core::usize count = 0;
        for (const BufferRecord& record : buffers) {
            count += record.alive ? 1 : 0;
        }
        return count;
    }

    [[nodiscard]] eee::core::usize alivePipelineCount() const {
        eee::core::usize count = 0;
        for (const PipelineRecord& record : pipelines) {
            count += record.alive ? 1 : 0;
        }
        return count;
    }

    // Recorded state.
    eee::render::DeviceInitDesc initDesc{};
    bool initializeSucceeds = true;
    bool vsync = true;
    eee::core::u8 samples = 1;
    eee::core::Vec4 clearColor{};
    eee::render::FrameConstants constants{};
    eee::core::i32 width_ = 0;
    eee::core::i32 height_ = 0;
    eee::core::u32 vertexStride = 0;
    eee::core::u32 instanceStride = 0;
    BufferHandle currentVertex = 0;
    BufferHandle currentInstance = 0;
    BufferHandle currentIndex = 0;
    PipelineHandle currentPipeline = 0;
    BufferHandle lastUpdatedBuffer = 0;

    eee::core::u32 initializeCalls = 0;
    eee::core::u32 shutdownCalls = 0;
    eee::core::u32 resizeCalls = 0;
    eee::core::u32 vsyncCalls = 0;
    eee::core::u32 multisampleCalls = 0;
    eee::core::u32 beginFrames = 0;
    eee::core::u32 endFrames = 0;
    eee::core::u32 waitIdleCalls = 0;
    eee::core::u32 createBufferCalls = 0;
    eee::core::u32 updateBufferCalls = 0;
    eee::core::u32 destroyBufferCalls = 0;
    eee::core::u32 createPipelineCalls = 0;
    eee::core::u32 destroyPipelineCalls = 0;
    eee::core::u32 bindPipelineCalls = 0;
    eee::core::u32 constantCalls = 0;
    eee::core::u32 drawCalls = 0;

    std::vector<BufferRecord> buffers;
    std::vector<PipelineRecord> pipelines;
    std::vector<DrawRecord> draws;
};

} // namespace eee::test
