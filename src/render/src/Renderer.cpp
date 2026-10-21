#include "eee/render/Renderer.hpp"

#include "eee/core/Log.hpp"

#include <algorithm>
#include <array>
#include <cstddef>

namespace eee::render {
namespace {

// Unit cube, 24 vertices (4 per face so every face has a flat normal).
// Layout: vec3 position, vec3 normal.
constexpr std::array<core::f32, 144> kCubeVertices = {
    // +X
    0.5f, -0.5f, -0.5f, 1.0f, 0.0f, 0.0f,
    0.5f, -0.5f, 0.5f, 1.0f, 0.0f, 0.0f,
    0.5f, 0.5f, 0.5f, 1.0f, 0.0f, 0.0f,
    0.5f, 0.5f, -0.5f, 1.0f, 0.0f, 0.0f,
    // -X
    -0.5f, -0.5f, 0.5f, -1.0f, 0.0f, 0.0f,
    -0.5f, -0.5f, -0.5f, -1.0f, 0.0f, 0.0f,
    -0.5f, 0.5f, -0.5f, -1.0f, 0.0f, 0.0f,
    -0.5f, 0.5f, 0.5f, -1.0f, 0.0f, 0.0f,
    // +Y
    -0.5f, 0.5f, -0.5f, 0.0f, 1.0f, 0.0f,
    0.5f, 0.5f, -0.5f, 0.0f, 1.0f, 0.0f,
    0.5f, 0.5f, 0.5f, 0.0f, 1.0f, 0.0f,
    -0.5f, 0.5f, 0.5f, 0.0f, 1.0f, 0.0f,
    // -Y
    -0.5f, -0.5f, 0.5f, 0.0f, -1.0f, 0.0f,
    0.5f, -0.5f, 0.5f, 0.0f, -1.0f, 0.0f,
    0.5f, -0.5f, -0.5f, 0.0f, -1.0f, 0.0f,
    -0.5f, -0.5f, -0.5f, 0.0f, -1.0f, 0.0f,
    // +Z
    -0.5f, -0.5f, 0.5f, 0.0f, 0.0f, 1.0f,
    -0.5f, 0.5f, 0.5f, 0.0f, 0.0f, 1.0f,
    0.5f, 0.5f, 0.5f, 0.0f, 0.0f, 1.0f,
    0.5f, -0.5f, 0.5f, 0.0f, 0.0f, 1.0f,
    // -Z
    0.5f, -0.5f, -0.5f, 0.0f, 0.0f, -1.0f,
    0.5f, 0.5f, -0.5f, 0.0f, 0.0f, -1.0f,
    -0.5f, 0.5f, -0.5f, 0.0f, 0.0f, -1.0f,
    -0.5f, -0.5f, -0.5f, 0.0f, 0.0f, -1.0f,
};

constexpr std::array<core::u16, 36> kCubeIndices = {
    0,  1,  2,  0,  2,  3,   // +X
    4,  5,  6,  4,  6,  7,   // -X
    8,  9,  10, 8,  10, 11,  // +Y
    12, 13, 14, 12, 14, 15,  // -Y
    16, 17, 18, 16, 18, 19,  // +Z
    20, 21, 22, 20, 22, 23,  // -Z
};

// Unit quad used by the debug overlay (vec2, stride 8).
constexpr std::array<core::f32, 8> kQuadVertices = {
    -0.5f, -0.5f, 0.5f, -0.5f, -0.5f, 0.5f, 0.5f, 0.5f,
};
constexpr std::array<core::u16, 6> kQuadIndices = {0, 1, 2, 2, 1, 3};

constexpr core::u32 kMeshVertexStride = 6 * sizeof(core::f32);
constexpr core::u32 kMeshIndexCount = static_cast<core::u32>(kCubeIndices.size());
constexpr core::u32 kOverlayVertexStride = 2 * sizeof(core::f32);
constexpr core::u32 kOverlayIndexCount = static_cast<core::u32>(kQuadIndices.size());
constexpr core::u32 kDefaultMeshCapacity = 1024;
constexpr core::u32 kDefaultOverlayCapacity = 512;

static_assert(sizeof(scene::MeshInstance) == sizeof(core::Mat4) + sizeof(core::Vec4),
              "MeshInstance must be uploadable as-is");
static_assert(offsetof(scene::MeshInstance, color) == sizeof(core::Mat4),
              "MeshInstance color must follow the transform");

} // namespace

Renderer::Renderer(IRenderDevice& device) : device_(device) {}

Renderer::~Renderer() { shutdown(); }

bool Renderer::initialize() {
    if (initialized_) {
        return true;
    }

    meshVertexBuffer_ = device_.createBuffer(BufferUsage::Vertex, kCubeVertices.data(),
                                             kCubeVertices.size() * sizeof(core::f32));
    meshIndexBuffer_ = device_.createBuffer(BufferUsage::Index, kCubeIndices.data(),
                                            kCubeIndices.size() * sizeof(core::u16));
    overlayVertexBuffer_ = device_.createBuffer(BufferUsage::Vertex, kQuadVertices.data(),
                                                kQuadVertices.size() * sizeof(core::f32));
    overlayIndexBuffer_ = device_.createBuffer(BufferUsage::Index, kQuadIndices.data(),
                                               kQuadIndices.size() * sizeof(core::u16));

    PipelineDesc meshDesc;
    meshDesc.kind = PipelineKind::Mesh;
    meshDesc.depthTest = true;
    meshDesc.alphaBlend = false;
    meshDesc.shaders = device_.meshShaderSources();
    meshPipeline_ = device_.createPipeline(meshDesc);

    PipelineDesc overlayDesc;
    overlayDesc.kind = PipelineKind::Overlay;
    overlayDesc.depthTest = false;
    overlayDesc.alphaBlend = true;
    overlayDesc.shaders = device_.overlayShaderSources();
    overlayPipeline_ = device_.createPipeline(overlayDesc);

    if (meshVertexBuffer_ == kInvalidBuffer || meshIndexBuffer_ == kInvalidBuffer ||
        overlayVertexBuffer_ == kInvalidBuffer || overlayIndexBuffer_ == kInvalidBuffer ||
        meshPipeline_ == kInvalidPipeline || overlayPipeline_ == kInvalidPipeline) {
        core::logError("renderer: failed to create core GPU resources");
        return false;
    }

    if (!ensureMeshCapacity(kDefaultMeshCapacity) ||
        !ensureOverlayCapacity(kDefaultOverlayCapacity)) {
        return false;
    }

    initialized_ = true;
    core::logf(core::LogLevel::Info,
               "renderer initialized on backend '%s' (3 frames in flight)", device_.name());
    return true;
}

void Renderer::shutdown() {
    if (!initialized_) {
        return;
    }

    // Make sure no GPU work still references the buffers we are about to free.
    device_.waitIdle();

    for (BufferHandle& buffer : meshInstanceBuffers_) {
        if (buffer != kInvalidBuffer) {
            device_.destroyBuffer(buffer);
            buffer = kInvalidBuffer;
        }
    }
    for (BufferHandle& buffer : overlayInstanceBuffers_) {
        if (buffer != kInvalidBuffer) {
            device_.destroyBuffer(buffer);
            buffer = kInvalidBuffer;
        }
    }

    if (meshVertexBuffer_ != kInvalidBuffer) {
        device_.destroyBuffer(meshVertexBuffer_);
    }
    if (meshIndexBuffer_ != kInvalidBuffer) {
        device_.destroyBuffer(meshIndexBuffer_);
    }
    if (overlayVertexBuffer_ != kInvalidBuffer) {
        device_.destroyBuffer(overlayVertexBuffer_);
    }
    if (overlayIndexBuffer_ != kInvalidBuffer) {
        device_.destroyBuffer(overlayIndexBuffer_);
    }
    if (meshPipeline_ != kInvalidPipeline) {
        device_.destroyPipeline(meshPipeline_);
    }
    if (overlayPipeline_ != kInvalidPipeline) {
        device_.destroyPipeline(overlayPipeline_);
    }

    meshVertexBuffer_ = meshIndexBuffer_ = kInvalidBuffer;
    overlayVertexBuffer_ = overlayIndexBuffer_ = kInvalidBuffer;
    meshPipeline_ = overlayPipeline_ = kInvalidPipeline;
    meshCapacity_ = 0;
    overlayCapacity_ = 0;
    overlayScratch_.clear();
    lastMeshCount_ = 0;
    lastOverlayCount_ = 0;
    initialized_ = false;
}

void Renderer::resize(core::i32 width, core::i32 height) {
    width_ = width > 0 ? width : 1;
    height_ = height > 0 ? height : 1;
    device_.resize(width_, height_);
}

bool Renderer::ensureMeshCapacity(core::u32 capacity) {
    if (capacity <= meshCapacity_ && meshInstanceBuffers_[0] != kInvalidBuffer) {
        return true;
    }

    // Growing means freeing buffers that frames in flight may still reference.
    device_.waitIdle();

    for (BufferHandle& buffer : meshInstanceBuffers_) {
        if (buffer != kInvalidBuffer) {
            device_.destroyBuffer(buffer);
            buffer = kInvalidBuffer;
        }
    }

    meshCapacity_ = std::max(capacity, kDefaultMeshCapacity);
    const core::usize bytes = static_cast<core::usize>(meshCapacity_) * sizeof(GpuInstance);
    for (BufferHandle& buffer : meshInstanceBuffers_) {
        buffer = device_.createBuffer(BufferUsage::Instance, nullptr, bytes);
        if (buffer == kInvalidBuffer) {
            core::logError("renderer: failed to allocate mesh instance buffers");
            return false;
        }
    }
    return true;
}

bool Renderer::ensureOverlayCapacity(core::u32 capacity) {
    if (capacity <= overlayCapacity_ && overlayInstanceBuffers_[0] != kInvalidBuffer) {
        return true;
    }

    device_.waitIdle();

    for (BufferHandle& buffer : overlayInstanceBuffers_) {
        if (buffer != kInvalidBuffer) {
            device_.destroyBuffer(buffer);
            buffer = kInvalidBuffer;
        }
    }

    overlayCapacity_ = std::max(capacity, kDefaultOverlayCapacity);
    const core::usize bytes = static_cast<core::usize>(overlayCapacity_) * sizeof(GpuInstance);
    for (BufferHandle& buffer : overlayInstanceBuffers_) {
        buffer = device_.createBuffer(BufferUsage::Instance, nullptr, bytes);
        if (buffer == kInvalidBuffer) {
            core::logError("renderer: failed to allocate overlay instance buffers");
            return false;
        }
    }
    return true;
}

void Renderer::renderFrame(const scene::SceneSnapshot& snapshot) {
    if (!initialized_) {
        return;
    }

    const core::u32 slot = frameSlot();
    const auto meshCount = static_cast<core::u32>(snapshot.meshes.size());
    const auto overlayCount = static_cast<core::u32>(snapshot.overlay.size());

    if (!ensureMeshCapacity(std::max(meshCount, 1u)) ||
        !ensureOverlayCapacity(std::max(overlayCount, 1u))) {
        return;
    }

    FrameDesc frame;
    frame.clearColor = snapshot.clearColor;
    device_.beginFrame(frame);

    if (meshCount > 0) {
        const core::f32 aspect =
            static_cast<core::f32>(width_) / static_cast<core::f32>(height_ > 0 ? height_ : 1);
        const scene::Camera& camera = snapshot.camera;
        const core::Mat4 view =
            core::Mat4::lookAt(camera.position, camera.target, camera.up);
        const core::Mat4 projection = core::Mat4::perspective(
            camera.fovYRadians, aspect, camera.nearZ, camera.farZ);

        FrameConstants constants;
        constants.viewProjection = projection * view;
        constants.lightDirection = {snapshot.lightDirection.x, snapshot.lightDirection.y,
                                    snapshot.lightDirection.z, 0.0f};
        constants.ambient = snapshot.ambient;

        device_.bindPipeline(meshPipeline_);
        device_.setFrameConstants(constants);
        device_.updateBuffer(meshInstanceBuffers_[slot], snapshot.meshes.data(),
                             static_cast<core::usize>(meshCount) * sizeof(scene::MeshInstance));
        device_.bindVertexBuffer(meshVertexBuffer_, kMeshVertexStride);
        device_.bindInstanceBuffer(meshInstanceBuffers_[slot], sizeof(GpuInstance));
        device_.bindIndexBuffer(meshIndexBuffer_);
        device_.drawIndexed(kMeshIndexCount, meshCount);
    }

    if (overlayCount > 0) {
        const core::Mat4 ortho = core::Mat4::orthographic(
            0.0f, static_cast<core::f32>(width_), 0.0f, static_cast<core::f32>(height_), -1.0f,
            1.0f);

        overlayScratch_.resize(overlayCount);
        for (core::u32 i = 0; i < overlayCount; ++i) {
            overlayScratch_[i].transform = ortho * snapshot.overlay[i].transform;
            overlayScratch_[i].color = snapshot.overlay[i].color;
        }

        device_.bindPipeline(overlayPipeline_);
        device_.updateBuffer(overlayInstanceBuffers_[slot], overlayScratch_.data(),
                             static_cast<core::usize>(overlayCount) * sizeof(GpuInstance));
        device_.bindVertexBuffer(overlayVertexBuffer_, kOverlayVertexStride);
        device_.bindInstanceBuffer(overlayInstanceBuffers_[slot], sizeof(GpuInstance));
        device_.bindIndexBuffer(overlayIndexBuffer_);
        device_.drawIndexed(kOverlayIndexCount, overlayCount);
    }

    device_.endFrame();

    lastMeshCount_ = meshCount;
    lastOverlayCount_ = overlayCount;
    ++frameIndex_;
}

} // namespace eee::render
