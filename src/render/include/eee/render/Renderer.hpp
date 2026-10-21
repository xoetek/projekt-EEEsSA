#pragma once

#include "eee/render/IRenderDevice.hpp"
#include "eee/scene/SceneSnapshot.hpp"

#include <array>
#include <vector>

namespace eee::render {

// Backend-agnostic renderer.
//
// Pass 1: instanced lit cubes with depth testing (the world).
// Pass 2: unlit screen-space quads with depth testing disabled (debug HUD).
//
// It owns geometry, both pipelines and three rotating instance buffers, and
// turns a SceneSnapshot into draw calls through IRenderDevice. No API-specific
// code lives here.
class Renderer {
public:
    static constexpr core::u32 kFramesInFlight = 3;

    explicit Renderer(IRenderDevice& device);
    ~Renderer();

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    bool initialize();
    void shutdown();

    void resize(core::i32 width, core::i32 height);

    void renderFrame(const scene::SceneSnapshot& snapshot);

    [[nodiscard]] core::u32 lastMeshCount() const { return lastMeshCount_; }
    [[nodiscard]] core::u32 lastOverlayCount() const { return lastOverlayCount_; }
    [[nodiscard]] core::u64 frameIndex() const { return frameIndex_; }

private:
    // mat4 + vec4: mirror of scene::MeshInstance so mesh data uploads directly.
    struct GpuInstance {
        core::Mat4 transform;
        core::Vec4 color;
    };

    bool ensureMeshCapacity(core::u32 capacity);
    bool ensureOverlayCapacity(core::u32 capacity);
    [[nodiscard]] core::u32 frameSlot() const {
        return static_cast<core::u32>(frameIndex_ % kFramesInFlight);
    }

    IRenderDevice& device_;

    BufferHandle meshVertexBuffer_ = kInvalidBuffer;
    BufferHandle meshIndexBuffer_ = kInvalidBuffer;
    std::array<BufferHandle, kFramesInFlight> meshInstanceBuffers_{};

    BufferHandle overlayVertexBuffer_ = kInvalidBuffer;
    BufferHandle overlayIndexBuffer_ = kInvalidBuffer;
    std::array<BufferHandle, kFramesInFlight> overlayInstanceBuffers_{};

    PipelineHandle meshPipeline_ = kInvalidPipeline;
    PipelineHandle overlayPipeline_ = kInvalidPipeline;

    std::vector<GpuInstance> overlayScratch_;

    core::u32 meshCapacity_ = 0;
    core::u32 overlayCapacity_ = 0;
    core::u32 lastMeshCount_ = 0;
    core::u32 lastOverlayCount_ = 0;
    core::u64 frameIndex_ = 0;
    core::i32 width_ = 1;
    core::i32 height_ = 1;
    bool initialized_ = false;
};

} // namespace eee::render
