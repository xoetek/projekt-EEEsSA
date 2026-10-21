#include "eee/backends/NullDevice.hpp"

#include "eee/core/Log.hpp"

#include <vector>

namespace eee::render {
namespace {

class NullDevice final : public IRenderDevice {
public:
    [[nodiscard]] const char* name() const override { return "null"; }

    bool initialize(const DeviceInitDesc& desc) override {
        width_ = desc.width;
        height_ = desc.height;
        core::logf(core::LogLevel::Info, "null backend initialized (%dx%d, headless)", width_,
                   height_);
        return true;
    }

    void shutdown() override {
        buffers_.clear();
        pipelines_.clear();
    }

    void resize(core::i32 width, core::i32 height) override {
        width_ = width;
        height_ = height;
    }

    void setVSync(bool enabled) override {
        vsync_ = enabled;
        core::logf(core::LogLevel::Info, "null: vsync %s", enabled ? "on" : "off");
    }

    void setMultisampleSamples(core::u8 samples) override {
        samples_ = samples <= 1 ? 1 : samples;
    }
    [[nodiscard]] core::u8 multisampleSamples() const override { return samples_; }
    [[nodiscard]] core::u8 maxMultisampleSamples() const override { return 8; }

    void beginFrame(const FrameDesc& desc) override {
        lastClearColor_ = desc.clearColor;
        inFrame_ = true;
    }

    void endFrame() override {
        inFrame_ = false;
        ++frameIndex_;
    }

    void waitIdle() override {}

    BufferHandle createBuffer(BufferUsage usage, const void* data, core::usize size) override {
        BufferRecord record;
        record.usage = usage;
        record.size = size;
        record.hasData = data != nullptr;
        buffers_.push_back(record);
        return static_cast<BufferHandle>(buffers_.size());
    }

    void updateBuffer(BufferHandle buffer, const void* data, core::usize size,
                      core::usize offset) override {
        if (!validBuffer(buffer)) {
            return;
        }
        (void)data;
        (void)size;
        (void)offset;
        ++buffers_[buffer - 1].updateCount;
    }

    void destroyBuffer(BufferHandle buffer) override {
        if (validBuffer(buffer)) {
            buffers_[buffer - 1].alive = false;
        }
    }

    PipelineHandle createPipeline(const PipelineDesc& desc) override {
        PipelineRecord record;
        record.kind = desc.kind;
        record.depthTest = desc.depthTest;
        record.alphaBlend = desc.alphaBlend;
        pipelines_.push_back(record);
        return static_cast<PipelineHandle>(pipelines_.size());
    }

    void destroyPipeline(PipelineHandle pipeline) override {
        if (validPipeline(pipeline)) {
            pipelines_[pipeline - 1].alive = false;
        }
    }

    void bindPipeline(PipelineHandle pipeline) override {
        currentPipeline_ = pipeline;
        if (validPipeline(pipeline)) {
            currentKind_ = pipelines_[pipeline - 1].kind;
            depthEnabled_ = pipelines_[pipeline - 1].depthTest;
        }
    }

    void setFrameConstants(const FrameConstants& constants) override {
        lastConstants_ = constants;
        ++constantUpdates_;
    }

    void bindVertexBuffer(BufferHandle buffer, core::u32 /*stride*/) override {
        currentVertex_ = buffer;
    }

    void bindInstanceBuffer(BufferHandle buffer, core::u32 /*stride*/) override {
        currentInstance_ = buffer;
    }

    void bindIndexBuffer(BufferHandle buffer) override { currentIndex_ = buffer; }

    void drawIndexed(core::u32 indexCount, core::u32 instanceCount) override {
        (void)indexCount;
        if (currentPipeline_ == kInvalidPipeline) {
            return;
        }
        ++drawCalls_;
        drawnInstances_ += instanceCount;
    }

    [[nodiscard]] ShaderSources meshShaderSources() const override {
        return {"// null: mesh vertex shader", "// null: mesh fragment shader"};
    }

    [[nodiscard]] ShaderSources overlayShaderSources() const override {
        return {"// null: overlay vertex shader", "// null: overlay fragment shader"};
    }

private:
    struct BufferRecord {
        BufferUsage usage = BufferUsage::Vertex;
        core::usize size = 0;
        bool hasData = false;
        core::u32 updateCount = 0;
        bool alive = true;
    };

    struct PipelineRecord {
        PipelineKind kind = PipelineKind::Mesh;
        bool depthTest = true;
        bool alphaBlend = true;
        bool alive = true;
    };

    [[nodiscard]] bool validBuffer(BufferHandle handle) const {
        return handle >= 1 && handle <= buffers_.size() && buffers_[handle - 1].alive;
    }

    [[nodiscard]] bool validPipeline(PipelineHandle handle) const {
        return handle >= 1 && handle <= pipelines_.size() && pipelines_[handle - 1].alive;
    }

    core::i32 width_ = 0;
    core::i32 height_ = 0;
    bool vsync_ = true;
    core::u8 samples_ = 1;
    bool inFrame_ = false;
    bool depthEnabled_ = true;
    core::u64 frameIndex_ = 0;
    core::u64 drawCalls_ = 0;
    core::u64 drawnInstances_ = 0;
    core::u64 constantUpdates_ = 0;
    core::Vec4 lastClearColor_{};
    FrameConstants lastConstants_{};
    PipelineKind currentKind_ = PipelineKind::Mesh;

    std::vector<BufferRecord> buffers_;
    std::vector<PipelineRecord> pipelines_;
    BufferHandle currentVertex_ = kInvalidBuffer;
    BufferHandle currentInstance_ = kInvalidBuffer;
    BufferHandle currentIndex_ = kInvalidBuffer;
    PipelineHandle currentPipeline_ = kInvalidPipeline;
};

} // namespace

std::unique_ptr<IRenderDevice> createNullDevice() { return std::make_unique<NullDevice>(); }

} // namespace eee::render
