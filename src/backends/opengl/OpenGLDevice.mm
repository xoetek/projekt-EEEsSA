#define GL_SILENCE_DEPRECATION 1

// NSOpenGLContext/NSView deprecations are intentional: macOS caps OpenGL at 4.1
// and this backend exists specifically to exercise that path.
#pragma clang diagnostic ignored "-Wdeprecated-declarations"

#import <Cocoa/Cocoa.h>
#import <OpenGL/gl3.h>

#include "eee/backends/OpenGLDevice.hpp"

#include "eee/core/Log.hpp"

#include <vector>

namespace eee::render {
namespace {

const char* kMeshVertexShader = R"GLSL(
#version 410 core
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in mat4 aModel;
layout(location = 6) in vec4 aColor;
uniform mat4 uViewProjection;
out vec3 vNormal;
out vec3 vWorldPos;
out vec4 vColor;
void main() {
    vec4 world = aModel * vec4(aPosition, 1.0);
    gl_Position = uViewProjection * world;
    vNormal = mat3(aModel) * aNormal;
    vWorldPos = world.xyz;
    vColor = aColor;
}
)GLSL";

const char* kMeshFragmentShader = R"GLSL(
#version 410 core
in vec3 vNormal;
in vec3 vWorldPos;
in vec4 vColor;
out vec4 fColor;
uniform vec3 uLightDirection;
uniform vec3 uCameraPosition;
uniform vec3 uFogColor;
uniform float uAmbient;
uniform float uFogStart;
uniform float uFogEnd;
float hash13(vec3 p) {
    p = fract(p * 0.1031);
    p += dot(p, p.yzx + 33.33);
    return fract((p.x + p.y) * p.z);
}
void main() {
    vec3 n = normalize(vNormal);
    vec3 lightDir = normalize(-uLightDirection);
    vec3 viewDir = normalize(uCameraPosition - vWorldPos);
    float ndl = max(dot(n, lightDir), 0.0);
    float spec = pow(max(dot(n, normalize(lightDir + viewDir)), 0.0), 48.0) * 0.28;
    vec3 ambientTint = mix(uFogColor, vec3(1.0), 0.45) * (0.72 + 0.28 * max(n.y, 0.0));
    float grain = hash13(floor(vWorldPos * 4.0));
    float stripe = 0.94 + 0.06 * sin(vWorldPos.y * 24.0 + vWorldPos.x * 0.7);
    vec3 baseColor = vColor.rgb * (0.86 + 0.24 * grain) * stripe;
    vec3 lit = baseColor * (uAmbient * ambientTint + (1.0 - uAmbient) * ndl) + vec3(spec);
    float dist = distance(uCameraPosition, vWorldPos);
    float fog = clamp((dist - uFogStart) / max(1.0, uFogEnd - uFogStart), 0.0, 1.0);
    fColor = vec4(mix(lit, uFogColor, fog), vColor.a);
}
)GLSL";

const char* kOverlayVertexShader = R"GLSL(
#version 410 core
layout(location = 0) in vec2 aPosition;
layout(location = 1) in mat4 aMvp;
layout(location = 5) in vec4 aColor;
out vec4 vColor;
void main() {
    gl_Position = aMvp * vec4(aPosition, 0.0, 1.0);
    vColor = aColor;
}
)GLSL";

const char* kOverlayFragmentShader = R"GLSL(
#version 410 core
in vec4 vColor;
out vec4 fColor;
void main() {
    fColor = vColor;
}
)GLSL";

GLuint compileShader(GLenum type, const char* source, const char* label) {
    const GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);

    GLint ok = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (ok != GL_TRUE) {
        char log[2048];
        GLsizei length = 0;
        glGetShaderInfoLog(shader, sizeof(log), &length, log);
        core::logf(core::LogLevel::Error, "opengl: %s shader compilation failed: %.*s", label,
                   static_cast<int>(length), log);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

struct PipelineRecord {
    GLuint program = 0;
    PipelineKind kind = PipelineKind::Mesh;
    bool depthTest = true;
};

class OpenGLDevice final : public IRenderDevice {
public:
    [[nodiscard]] const char* name() const override { return "opengl"; }

    ~OpenGLDevice() override { shutdown(); }

    bool initialize(const DeviceInitDesc& desc) override {
        if (desc.window.view == nullptr) {
            core::logError("opengl: the OpenGL backend requires a native window");
            return false;
        }

        view_ = (__bridge NSView*)desc.window.view;
        [view_ setWantsBestResolutionOpenGLSurface:YES];

        NSOpenGLPixelFormatAttribute attributes[] = {
            NSOpenGLPFAOpenGLProfile, NSOpenGLProfileVersion4_1Core,
            NSOpenGLPFAAccelerated,
            NSOpenGLPFADoubleBuffer,
            NSOpenGLPFAColorSize, 24,
            NSOpenGLPFAAlphaSize, 8,
            NSOpenGLPFADepthSize, 24,
            0,
        };

        NSOpenGLPixelFormat* pixelFormat =
            [[NSOpenGLPixelFormat alloc] initWithAttributes:attributes];
        if (pixelFormat == nil) {
            core::logError("opengl: failed to create a 4.1 core pixel format");
            return false;
        }

        context_ = [[NSOpenGLContext alloc] initWithFormat:pixelFormat shareContext:nil];
        if (context_ == nil) {
            core::logError("opengl: failed to create an NSOpenGLContext");
            return false;
        }

        [context_ setView:view_];
        [context_ makeCurrentContext];
        setVSync(true);

        GLint maxSamples = 1;
        glGetIntegerv(GL_MAX_SAMPLES, &maxSamples);
        maxSamples_ = static_cast<core::u8>(core::clamp(static_cast<core::i32>(maxSamples), 1, 8));

        const GLubyte* version = glGetString(GL_VERSION);
        const GLubyte* renderer = glGetString(GL_RENDERER);
        core::logf(core::LogLevel::Info, "opengl: %s (%s)",
                   version != nullptr ? reinterpret_cast<const char*>(version) : "unknown",
                   renderer != nullptr ? reinterpret_cast<const char*>(renderer) : "unknown");

        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);
        glDepthMask(GL_TRUE);
        glDisable(GL_CULL_FACE);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        glGenVertexArrays(1, &vao_);
        glBindVertexArray(vao_);

        resize(desc.width, desc.height);
        return true;
    }

    void shutdown() override {
        if (context_ == nil) {
            return;
        }
        [context_ makeCurrentContext];

        for (GLuint& buffer : buffers_) {
            if (buffer != 0) {
                glDeleteBuffers(1, &buffer);
                buffer = 0;
            }
        }
        buffers_.clear();

        for (PipelineRecord& pipeline : pipelines_) {
            if (pipeline.program != 0) {
                glDeleteProgram(pipeline.program);
                pipeline.program = 0;
            }
        }
        pipelines_.clear();

        if (vao_ != 0) {
            glDeleteVertexArrays(1, &vao_);
            vao_ = 0;
        }
        destroyTargets();

        currentProgram_ = 0;
        [NSOpenGLContext clearCurrentContext];
        [context_ clearDrawable];
        context_ = nil;
        view_ = nil;
    }

    void resize(core::i32 width, core::i32 height) override {
        width_ = width > 0 ? width : 1;
        height_ = height > 0 ? height : 1;
        if (context_ != nil) {
            [context_ makeCurrentContext];
            [context_ update];
            glViewport(0, 0, width_, height_);
            if (samples_ > 1) {
                createTargets();
            }
        }
    }

    void setMultisampleSamples(core::u8 samples) override {
        const core::u8 clamped =
            samples <= 1 ? 1 : std::min(samples, maxMultisampleSamples());
        if (clamped == samples_) {
            return;
        }
        samples_ = clamped;
        if (context_ != nil) {
            [context_ makeCurrentContext];
            createTargets();
        }
        core::logf(core::LogLevel::Info, "opengl: MSAA %s",
                   samples_ <= 1 ? "off" : std::to_string(samples_).c_str());
    }

    [[nodiscard]] core::u8 multisampleSamples() const override { return samples_; }
    [[nodiscard]] core::u8 maxMultisampleSamples() const override { return maxSamples_; }

    void setVSync(bool enabled) override {
        vsync_ = enabled;
        if (context_ != nil) {
            [context_ makeCurrentContext];
            GLint swapInterval = enabled ? 1 : 0;
            [context_ setValues:&swapInterval forParameter:NSOpenGLContextParameterSwapInterval];
        }
        core::logf(core::LogLevel::Info, "opengl: vsync %s", enabled ? "on" : "off");
    }

    void beginFrame(const FrameDesc& desc) override {
        if (context_ == nil) {
            return;
        }
        [context_ makeCurrentContext];
        glBindFramebuffer(GL_FRAMEBUFFER, samples_ > 1 ? fbo_ : 0);
        glViewport(0, 0, width_, height_);
        glClearColor(desc.clearColor.x, desc.clearColor.y, desc.clearColor.z, desc.clearColor.w);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    }

    void endFrame() override {
        if (context_ != nil) {
            if (samples_ > 1 && fbo_ != 0) {
                glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo_);
                glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
                glBlitFramebuffer(0, 0, width_, height_, 0, 0, width_, height_,
                                  GL_COLOR_BUFFER_BIT, GL_NEAREST);
                glBindFramebuffer(GL_FRAMEBUFFER, 0);
            }
            [context_ flushBuffer];
        }
    }

    void waitIdle() override {
        if (context_ != nil) {
            [context_ makeCurrentContext];
            glFinish();
        }
    }

    BufferHandle createBuffer(BufferUsage usage, const void* data, core::usize size) override {
        GLuint buffer = 0;
        glGenBuffers(1, &buffer);
        if (buffer == 0) {
            return kInvalidBuffer;
        }

        const GLenum target = usage == BufferUsage::Index ? GL_ELEMENT_ARRAY_BUFFER : GL_ARRAY_BUFFER;
        const GLenum usageHint = usage == BufferUsage::Instance ? GL_DYNAMIC_DRAW : GL_STATIC_DRAW;

        glBindBuffer(target, buffer);
        glBufferData(target, static_cast<GLsizeiptr>(size), data, usageHint);
        glBindBuffer(target, 0);

        return storeBuffer(buffer);
    }

    void updateBuffer(BufferHandle handle, const void* data, core::usize size,
                      core::usize offset) override {
        const GLuint buffer = lookupBuffer(handle);
        if (buffer == 0 || data == nullptr) {
            return;
        }
        glBindBuffer(GL_ARRAY_BUFFER, buffer);
        glBufferSubData(GL_ARRAY_BUFFER, static_cast<GLintptr>(offset),
                        static_cast<GLsizeiptr>(size), data);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
    }

    void destroyBuffer(BufferHandle handle) override {
        if (handle == 0 || handle > buffers_.size()) {
            return;
        }
        GLuint& buffer = buffers_[handle - 1];
        if (buffer != 0) {
            glDeleteBuffers(1, &buffer);
            buffer = 0;
        }
    }

    PipelineHandle createPipeline(const PipelineDesc& desc) override {
        const GLuint vertex = compileShader(GL_VERTEX_SHADER, desc.shaders.vertex.c_str(), "vertex");
        const GLuint fragment =
            compileShader(GL_FRAGMENT_SHADER, desc.shaders.fragment.c_str(), "fragment");
        if (vertex == 0 || fragment == 0) {
            if (vertex != 0) {
                glDeleteShader(vertex);
            }
            if (fragment != 0) {
                glDeleteShader(fragment);
            }
            return kInvalidPipeline;
        }

        const GLuint program = glCreateProgram();
        glAttachShader(program, vertex);
        glAttachShader(program, fragment);
        glLinkProgram(program);

        glDeleteShader(vertex);
        glDeleteShader(fragment);

        GLint ok = GL_FALSE;
        glGetProgramiv(program, GL_LINK_STATUS, &ok);
        if (ok != GL_TRUE) {
            char log[2048];
            GLsizei length = 0;
            glGetProgramInfoLog(program, sizeof(log), &length, log);
            core::logf(core::LogLevel::Error, "opengl: program link failed: %.*s",
                       static_cast<int>(length), log);
            glDeleteProgram(program);
            return kInvalidPipeline;
        }

        return storePipeline(PipelineRecord{program, desc.kind, desc.depthTest});
    }

    void destroyPipeline(PipelineHandle handle) override {
        if (handle == 0 || handle > pipelines_.size()) {
            return;
        }
        PipelineRecord& pipeline = pipelines_[handle - 1];
        if (pipeline.program != 0) {
            glDeleteProgram(pipeline.program);
            pipeline.program = 0;
        }
    }

    void bindPipeline(PipelineHandle pipeline) override {
        if (pipeline == 0 || pipeline > pipelines_.size()) {
            return;
        }
        const PipelineRecord& record = pipelines_[pipeline - 1];
        glUseProgram(record.program);
        if (record.depthTest) {
            glEnable(GL_DEPTH_TEST);
        } else {
            glDisable(GL_DEPTH_TEST);
        }
        currentProgram_ = record.program;
        currentKind_ = record.kind;
    }

    void setFrameConstants(const FrameConstants& constants) override {
        if (currentProgram_ == 0) {
            return;
        }
        const GLint viewProjection =
            glGetUniformLocation(currentProgram_, "uViewProjection");
        if (viewProjection >= 0) {
            glUniformMatrix4fv(viewProjection, 1, GL_FALSE, constants.viewProjection.m);
        }
        const GLint lightDirection = glGetUniformLocation(currentProgram_, "uLightDirection");
        if (lightDirection >= 0) {
            glUniform3f(lightDirection, constants.lightDirection.x, constants.lightDirection.y,
                        constants.lightDirection.z);
        }
        const GLint ambient = glGetUniformLocation(currentProgram_, "uAmbient");
        if (ambient >= 0) {
            glUniform1f(ambient, constants.ambient);
        }
        const GLint cameraPosition = glGetUniformLocation(currentProgram_, "uCameraPosition");
        if (cameraPosition >= 0) {
            glUniform3f(cameraPosition, constants.cameraPosition.x, constants.cameraPosition.y,
                        constants.cameraPosition.z);
        }
        const GLint fogColor = glGetUniformLocation(currentProgram_, "uFogColor");
        if (fogColor >= 0) {
            glUniform3f(fogColor, constants.fogColor.x, constants.fogColor.y, constants.fogColor.z);
        }
        const GLint fogStart = glGetUniformLocation(currentProgram_, "uFogStart");
        if (fogStart >= 0) {
            glUniform1f(fogStart, constants.fogStart);
        }
        const GLint fogEnd = glGetUniformLocation(currentProgram_, "uFogEnd");
        if (fogEnd >= 0) {
            glUniform1f(fogEnd, constants.fogEnd);
        }
    }

    void bindVertexBuffer(BufferHandle handle, core::u32 stride) override {
        const GLuint buffer = lookupBuffer(handle);
        glBindVertexArray(vao_);
        glBindBuffer(GL_ARRAY_BUFFER, buffer);
        glEnableVertexAttribArray(0);

        if (currentKind_ == PipelineKind::Mesh) {
            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, static_cast<GLsizei>(stride), nullptr);
            glEnableVertexAttribArray(1);
            glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, static_cast<GLsizei>(stride),
                                  reinterpret_cast<const void*>(static_cast<uintptr_t>(12)));
            glVertexAttribDivisor(1, 0);
        } else {
            glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, static_cast<GLsizei>(stride), nullptr);
            glDisableVertexAttribArray(1);
        }
        glVertexAttribDivisor(0, 0);
    }

    void bindInstanceBuffer(BufferHandle handle, core::u32 stride) override {
        const GLuint buffer = lookupBuffer(handle);
        glBindVertexArray(vao_);
        glBindBuffer(GL_ARRAY_BUFFER, buffer);

        // Mesh: mat4 at 2..5, color at 6. Overlay: mat4 at 1..4, color at 5.
        const GLuint base = currentKind_ == PipelineKind::Mesh ? 2u : 1u;
        for (GLuint column = 0; column < 4; ++column) {
            const GLuint location = base + column;
            glEnableVertexAttribArray(location);
            glVertexAttribPointer(location, 4, GL_FLOAT, GL_FALSE, static_cast<GLsizei>(stride),
                                  reinterpret_cast<const void*>(static_cast<uintptr_t>(column) * 16));
            glVertexAttribDivisor(location, 1);
        }

        const GLuint colorLocation = base + 4;
        glEnableVertexAttribArray(colorLocation);
        glVertexAttribPointer(colorLocation, 4, GL_FLOAT, GL_FALSE, static_cast<GLsizei>(stride),
                              reinterpret_cast<const void*>(static_cast<uintptr_t>(64)));
        glVertexAttribDivisor(colorLocation, 1);

        // The overlay shader does not read mesh color location 6; leaving it
        // enabled would fetch past the end of the overlay instance buffer.
        if (currentKind_ != PipelineKind::Mesh) {
            glDisableVertexAttribArray(6);
        }
    }

    void bindIndexBuffer(BufferHandle handle) override {
        const GLuint buffer = lookupBuffer(handle);
        glBindVertexArray(vao_);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, buffer);
    }

    void drawIndexed(core::u32 indexCount, core::u32 instanceCount) override {
        if (currentProgram_ == 0) {
            return;
        }
        glDrawElementsInstanced(GL_TRIANGLES, static_cast<GLsizei>(indexCount), GL_UNSIGNED_SHORT,
                                nullptr, static_cast<GLsizei>(instanceCount));
        ++drawCalls_;
    }

    [[nodiscard]] ShaderSources meshShaderSources() const override {
        return {kMeshVertexShader, kMeshFragmentShader};
    }

    [[nodiscard]] ShaderSources overlayShaderSources() const override {
        return {kOverlayVertexShader, kOverlayFragmentShader};
    }

private:
    void destroyTargets() {
        if (depthRbo_ != 0) {
            glDeleteRenderbuffers(1, &depthRbo_);
            depthRbo_ = 0;
        }
        if (colorRbo_ != 0) {
            glDeleteRenderbuffers(1, &colorRbo_);
            colorRbo_ = 0;
        }
        if (fbo_ != 0) {
            glDeleteFramebuffers(1, &fbo_);
            fbo_ = 0;
        }
    }

    void createTargets() {
        destroyTargets();
        if (samples_ <= 1) {
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            return;
        }

        glGenFramebuffers(1, &fbo_);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo_);

        glGenRenderbuffers(1, &colorRbo_);
        glBindRenderbuffer(GL_RENDERBUFFER, colorRbo_);
        glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples_, GL_RGBA8, width_, height_);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, colorRbo_);

        glGenRenderbuffers(1, &depthRbo_);
        glBindRenderbuffer(GL_RENDERBUFFER, depthRbo_);
        glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples_, GL_DEPTH_COMPONENT24, width_,
                                         height_);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depthRbo_);

        const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        if (status != GL_FRAMEBUFFER_COMPLETE) {
            core::logError("opengl: multisample framebuffer incomplete, falling back to 0x MSAA");
            samples_ = 1;
            destroyTargets();
        }
    }

    BufferHandle storeBuffer(GLuint buffer) {
        for (core::usize i = 0; i < buffers_.size(); ++i) {
            if (buffers_[i] == 0) {
                buffers_[i] = buffer;
                return static_cast<BufferHandle>(i + 1);
            }
        }
        buffers_.push_back(buffer);
        return static_cast<BufferHandle>(buffers_.size());
    }

    [[nodiscard]] GLuint lookupBuffer(BufferHandle handle) const {
        if (handle == 0 || handle > buffers_.size()) {
            return 0;
        }
        return buffers_[handle - 1];
    }

    PipelineHandle storePipeline(const PipelineRecord& record) {
        for (core::usize i = 0; i < pipelines_.size(); ++i) {
            if (pipelines_[i].program == 0) {
                pipelines_[i] = record;
                return static_cast<PipelineHandle>(i + 1);
            }
        }
        pipelines_.push_back(record);
        return static_cast<PipelineHandle>(pipelines_.size());
    }

    NSOpenGLContext* __strong context_ = nil;
    NSView* __strong view_ = nil;
    GLuint vao_ = 0;
    GLuint currentProgram_ = 0;
    PipelineKind currentKind_ = PipelineKind::Mesh;
    core::i32 width_ = 1;
    core::i32 height_ = 1;
    GLuint fbo_ = 0;
    GLuint colorRbo_ = 0;
    GLuint depthRbo_ = 0;
    core::u8 samples_ = 1;
    core::u8 maxSamples_ = 1;
    bool vsync_ = true;
    core::u64 drawCalls_ = 0;
    std::vector<GLuint> buffers_;
    std::vector<PipelineRecord> pipelines_;
};

} // namespace

std::unique_ptr<IRenderDevice> createOpenGLDevice() {
    return std::make_unique<OpenGLDevice>();
}

} // namespace eee::render
