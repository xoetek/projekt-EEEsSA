#pragma once

#include "eee/core/Math.hpp"
#include "eee/core/Types.hpp"
#include "eee/platform/Window.hpp"

#include <string>

namespace eee::render {

using BufferHandle = core::u32;
using PipelineHandle = core::u32;

inline constexpr BufferHandle kInvalidBuffer = 0;
inline constexpr PipelineHandle kInvalidPipeline = 0;

enum class BufferUsage { Vertex, Index, Instance };

// Distinguishes the two pipelines the shared renderer builds. Backends use it
// to configure the right vertex layout and depth state.
enum class PipelineKind { Mesh, Overlay };

struct DeviceInitDesc {
    platform::NativeWindow window{};
    core::i32 width = 0;
    core::i32 height = 0;
};

struct FrameDesc {
    core::Vec4 clearColor{0.0f, 0.0f, 0.0f, 1.0f};
};

struct FrameConstants {
    core::Mat4 viewProjection = core::Mat4::identity();
    // xyz = direction the light travels; w is padding for MSL alignment.
    core::Vec4 lightDirection{-0.45f, -1.0f, -0.35f, 0.0f};
    core::f32 ambient = 0.16f;
    core::f32 padding[3]{};
};

// Shaders are authored in the backend's native language (GLSL for OpenGL,
// MSL for Metal, SPIR-V/GLSL for Vulkan). The shared renderer never inspects
// them; it asks the device for both standard programs.
struct ShaderSources {
    std::string vertex;
    std::string fragment;
};

struct PipelineDesc {
    ShaderSources shaders{};
    PipelineKind kind = PipelineKind::Mesh;
    bool depthTest = true;
    bool alphaBlend = true;
};

// ---------------------------------------------------------------------------
// The low-level graphics contract implemented by every backend.
//
// Mesh pass:
//   * vertex buffer   : 24 x (vec3 position + vec3 normal), stride 24
//   * instance buffer : mat4 model (64) + vec4 color (16), stride 80
//   * index buffer    : 36 x u16
// Overlay pass:
//   * vertex buffer   : 4 x vec2, stride 8
//   * instance buffer : mat4 mvp (64) + vec4 color (16), stride 80
//   * index buffer    : 6 x u16
// The renderer rotates three instance buffers so up to three frames can be in
// flight (required by the asynchronous Metal backend).
// ---------------------------------------------------------------------------
class IRenderDevice {
public:
    virtual ~IRenderDevice() = default;

    [[nodiscard]] virtual const char* name() const = 0;

    virtual bool initialize(const DeviceInitDesc& desc) = 0;
    virtual void shutdown() = 0;
    virtual void resize(core::i32 width, core::i32 height) = 0;

    // Toggle presentation sync with the display refresh. Backends apply this
    // at the next frame; APIs that need swapchain recreation do it internally.
    virtual void setVSync(bool enabled) = 0;

    virtual void beginFrame(const FrameDesc& desc) = 0;
    virtual void endFrame() = 0;
    virtual void waitIdle() = 0;

    virtual BufferHandle createBuffer(BufferUsage usage, const void* data, core::usize size) = 0;
    virtual void updateBuffer(BufferHandle buffer, const void* data, core::usize size,
                              core::usize offset = 0) = 0;
    virtual void destroyBuffer(BufferHandle buffer) = 0;

    virtual PipelineHandle createPipeline(const PipelineDesc& desc) = 0;
    virtual void destroyPipeline(PipelineHandle pipeline) = 0;

    virtual void bindPipeline(PipelineHandle pipeline) = 0;
    virtual void setFrameConstants(const FrameConstants& constants) = 0;
    virtual void bindVertexBuffer(BufferHandle buffer, core::u32 stride) = 0;
    virtual void bindInstanceBuffer(BufferHandle buffer, core::u32 stride) = 0;
    virtual void bindIndexBuffer(BufferHandle buffer) = 0;
    virtual void drawIndexed(core::u32 indexCount, core::u32 instanceCount) = 0;

    [[nodiscard]] virtual ShaderSources meshShaderSources() const = 0;
    [[nodiscard]] virtual ShaderSources overlayShaderSources() const = 0;
};

} // namespace eee::render
