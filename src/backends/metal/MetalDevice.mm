#import <Cocoa/Cocoa.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

#include "eee/backends/MetalDevice.hpp"

#include "eee/core/Log.hpp"

#include <cstring>
#include <vector>

namespace eee::render {
namespace {

constexpr int kFramesInFlight = 3;

// NOTE: avoid MSL reserved identifiers here (`vertex`, `fragment`, `kernel`, …)
// as local variable names.
const char* kMeshShaderSource = R"MSL(
#include <metal_stdlib>
using namespace metal;

struct Vertex {
    packed_float3 position;
    packed_float3 normal;
};

struct Instance {
    float4x4 model;
    float4 color;
};

struct FrameConstants {
    float4x4 viewProjection;
    float4 lightDirection;
    float4 cameraPosition;
    float4 fogColor;
    float ambient;
    float fogStart;
    float fogEnd;
    float padding;
};

struct VertexOut {
    float4 position [[position]];
    float3 worldPosition;
    float3 normal;
    float4 color;
};

vertex VertexOut vs_main(uint vertexId [[vertex_id]],
                         const device Vertex* vertices [[buffer(0)]],
                         const device Instance* instances [[buffer(1)]],
                         constant FrameConstants& frame [[buffer(2)]],
                         uint instanceId [[instance_id]]) {
    Vertex v = vertices[vertexId];
    Instance inst = instances[instanceId];
    VertexOut result;
    result.position = frame.viewProjection * inst.model *
                      float4(v.position.x, v.position.y, v.position.z, 1.0);
    result.worldPosition = (inst.model * float4(v.position.x, v.position.y, v.position.z, 1.0)).xyz;
    result.normal =
        (inst.model * float4(v.normal.x, v.normal.y, v.normal.z, 0.0)).xyz;
    result.color = inst.color;
    return result;
}

float hash13(float3 p) {
    p = fract(p * 0.1031);
    p += dot(p, p.yzx + 33.33);
    return fract((p.x + p.y) * p.z);
}

fragment float4 fs_main(VertexOut in [[stage_in]],
                        constant FrameConstants& frame [[buffer(2)]]) {
    float3 n = normalize(in.normal);
    float3 lightDir = normalize(-frame.lightDirection.xyz);
    float3 viewDir = normalize(frame.cameraPosition.xyz - in.worldPosition);
    float ndl = max(dot(n, lightDir), 0.0);
    float3 halfDir = normalize(lightDir + viewDir);
    float spec = pow(max(dot(n, halfDir), 0.0), 48.0) * 0.28;
    float3 ambientTint = mix(frame.fogColor.rgb, float3(1.0), 0.45) * (0.72 + 0.28 * max(n.y, 0.0));
    float grain = hash13(floor(in.worldPosition * 4.0));
    float stripe = 0.94 + 0.06 * sin(in.worldPosition.y * 24.0 + in.worldPosition.x * 0.7);
    float3 baseColor = in.color.rgb * (0.86 + 0.24 * grain) * stripe;
    float3 lit = baseColor * (frame.ambient * ambientTint + (1.0 - frame.ambient) * ndl) + float3(spec);
    float dist = distance(frame.cameraPosition.xyz, in.worldPosition);
    float fog = saturate((dist - frame.fogStart) / max(1.0, frame.fogEnd - frame.fogStart));
    return float4(mix(lit, frame.fogColor.rgb, fog), in.color.a);
}
)MSL";

const char* kOverlayShaderSource = R"MSL(
#include <metal_stdlib>
using namespace metal;

struct Instance {
    float4x4 mvp;
    float4 color;
};

struct VertexOut {
    float4 position [[position]];
    float4 color;
};

vertex VertexOut overlay_vs(uint vertexId [[vertex_id]],
                            const device float2* positions [[buffer(0)]],
                            const device Instance* instances [[buffer(1)]],
                            uint instanceId [[instance_id]]) {
    VertexOut result;
    result.position = instances[instanceId].mvp * float4(positions[vertexId], 0.0, 1.0);
    result.color = instances[instanceId].color;
    return result;
}

fragment float4 overlay_fs(VertexOut in [[stage_in]]) {
    return in.color;
}
)MSL";

struct PipelineRecord {
    void* state = nullptr;
    bool depthTest = true;
    PipelineKind kind = PipelineKind::Mesh;
    PipelineDesc desc;
};

class MetalDevice final : public IRenderDevice {
public:
    [[nodiscard]] const char* name() const override { return "metal"; }

    ~MetalDevice() override { shutdown(); }

    bool initialize(const DeviceInitDesc& desc) override {
        view_ = (__bridge NSView*)desc.window.view;
        if (view_ == nil) {
            core::logError("metal: the Metal backend requires a native window");
            return false;
        }

        device_ = MTLCreateSystemDefaultDevice();
        if (device_ == nil) {
            core::logError("metal: no Metal device available on this machine");
            return false;
        }

        queue_ = [device_ newCommandQueue];
        if (queue_ == nil) {
            core::logError("metal: failed to create a command queue");
            return false;
        }

        semaphore_ = dispatch_semaphore_create(kFramesInFlight);
        inFlightGroup_ = dispatch_group_create();

        maxSamples_ = 1;
        for (core::u8 samples : {8, 4, 2}) {
            if ([device_ supportsTextureSampleCount:samples]) {
                maxSamples_ = samples;
                break;
            }
        }
        samples_ = 1;

        layer_ = [CAMetalLayer layer];
        layer_.device = device_;
        layer_.pixelFormat = MTLPixelFormatBGRA8Unorm;
        layer_.framebufferOnly = YES;
        layer_.opaque = YES;
        layer_.displaySyncEnabled = YES;
        view_.wantsLayer = YES;
        view_.layer = layer_;

        createDepthStencilStates();

        const ShaderSources meshSources = meshShaderSources();
        PipelineDesc meshDesc;
        meshDesc.kind = PipelineKind::Mesh;
        meshDesc.depthTest = true;
        meshDesc.alphaBlend = false;
        meshDesc.shaders = meshSources;
        if (createPipeline(meshDesc) == kInvalidPipeline) {
            return false;
        }

        const ShaderSources overlaySources = overlayShaderSources();
        PipelineDesc overlayDesc;
        overlayDesc.kind = PipelineKind::Overlay;
        overlayDesc.depthTest = false;
        overlayDesc.alphaBlend = true;
        overlayDesc.shaders = overlaySources;
        if (createPipeline(overlayDesc) == kInvalidPipeline) {
            return false;
        }

        resize(desc.width, desc.height);
        core::logf(core::LogLevel::Info, "metal: using %s, %d frames in flight",
                   device_.name.UTF8String, kFramesInFlight);
        return true;
    }

    void shutdown() override {
        if (encoder_ != nil) {
            [encoder_ endEncoding];
            encoder_ = nil;
        }
        if (commandBuffer_ != nil) {
            [commandBuffer_ commit];
            [commandBuffer_ waitUntilCompleted];
            commandBuffer_ = nil;
        }
        if (inFlightGroup_ != nil) {
            dispatch_group_wait(inFlightGroup_, DISPATCH_TIME_FOREVER);
        }
        drawable_ = nil;
        currentVertex_ = nil;
        currentInstance_ = nil;
        currentIndex_ = nil;
        currentPipeline_ = nil;

        for (void*& entry : buffers_) {
            if (entry != nullptr) {
                id<MTLBuffer> buffer = (__bridge_transfer id<MTLBuffer>)entry;
                (void)buffer;
                entry = nullptr;
            }
        }
        buffers_.clear();

        for (PipelineRecord& record : pipelines_) {
            if (record.state != nullptr) {
                id<MTLRenderPipelineState> state =
                    (__bridge_transfer id<MTLRenderPipelineState>)record.state;
                (void)state;
                record.state = nullptr;
            }
        }
        pipelines_.clear();

        depthTexture_ = nil;
        msaaColor_ = nil;
        msaaDepth_ = nil;
        depthTestState_ = nil;
        noDepthTestState_ = nil;

        if (view_ != nil && layer_ != nil) {
            view_.layer = nil;
            view_.wantsLayer = NO;
        }
        layer_ = nil;
        queue_ = nil;
        device_ = nil;
        view_ = nil;

        semaphore_ = nil;
        inFlightGroup_ = nil;

        frameActive_ = false;
        ready_ = false;
    }

    void resize(core::i32 width, core::i32 height) override {
        width_ = width > 0 ? width : 1;
        height_ = height > 0 ? height : 1;
        if (layer_ != nil) {
            layer_.drawableSize =
                CGSizeMake(static_cast<CGFloat>(width_), static_cast<CGFloat>(height_));
        }
        createDepthTexture();
    }

    void setVSync(bool enabled) override {
        vsync_ = enabled;
        if (layer_ != nil) {
            layer_.displaySyncEnabled = enabled ? YES : NO;
        }
        core::logf(core::LogLevel::Info, "metal: vsync %s", enabled ? "on" : "off");
    }

    void setMultisampleSamples(core::u8 samples) override {
        const core::u8 clamped = samples <= 1 ? 1 : std::min(samples, maxSamples_);
        if (clamped == samples_) {
            return;
        }
        waitIdle();
        samples_ = clamped;

        for (PipelineRecord& record : pipelines_) {
            if (record.state == nullptr) {
                continue;
            }
            id<MTLRenderPipelineState> fresh = buildPipelineState(record.desc);
            if (fresh != nil) {
                if (record.state != nullptr) {
                    id<MTLRenderPipelineState> old =
                        (__bridge_transfer id<MTLRenderPipelineState>)record.state;
                    (void)old;
                }
                record.state = (__bridge_retained void*)fresh;
            }
        }
        createDepthTexture();
        core::logf(core::LogLevel::Info, "metal: MSAA %s",
                   samples_ <= 1 ? "off" : std::to_string(samples_).c_str());
    }

    [[nodiscard]] core::u8 multisampleSamples() const override { return samples_; }
    [[nodiscard]] core::u8 maxMultisampleSamples() const override { return maxSamples_; }

    void beginFrame(const FrameDesc& desc) override {
        const bool hasDepth =
            depthTexture_ != nil || (msaaColor_ != nil && msaaDepth_ != nil);
        if (layer_ == nil || queue_ == nil || !hasDepth || semaphore_ == nil) {
            return;
        }

        // Blocks until fewer than kFramesInFlight command buffers are pending.
        dispatch_semaphore_wait(semaphore_, DISPATCH_TIME_FOREVER);

        drawable_ = [layer_ nextDrawable];
        if (drawable_ == nil) {
            dispatch_semaphore_signal(semaphore_);
            return;
        }

        MTLRenderPassDescriptor* pass = [MTLRenderPassDescriptor renderPassDescriptor];
        if (samples_ > 1 && msaaColor_ != nil) {
            pass.colorAttachments[0].texture = msaaColor_;
            pass.colorAttachments[0].resolveTexture = drawable_.texture;
            pass.colorAttachments[0].storeAction = MTLStoreActionMultisampleResolve;
        } else {
            pass.colorAttachments[0].texture = drawable_.texture;
            pass.colorAttachments[0].storeAction = MTLStoreActionStore;
        }
        pass.colorAttachments[0].loadAction = MTLLoadActionClear;
        pass.colorAttachments[0].clearColor = MTLClearColor{
            static_cast<double>(desc.clearColor.x), static_cast<double>(desc.clearColor.y),
            static_cast<double>(desc.clearColor.z), static_cast<double>(desc.clearColor.w)};

        pass.depthAttachment.texture = samples_ > 1 ? msaaDepth_ : depthTexture_;
        pass.depthAttachment.loadAction = MTLLoadActionClear;
        pass.depthAttachment.storeAction = MTLStoreActionDontCare;
        pass.depthAttachment.clearDepth = 1.0;

        commandBuffer_ = [queue_ commandBuffer];
        if (commandBuffer_ == nil) {
            dispatch_semaphore_signal(semaphore_);
            drawable_ = nil;
            return;
        }

        encoder_ = [commandBuffer_ renderCommandEncoderWithDescriptor:pass];
        if (encoder_ == nil) {
            dispatch_semaphore_signal(semaphore_);
            commandBuffer_ = nil;
            drawable_ = nil;
            return;
        }

        frameActive_ = true;
        ready_ = false;
        currentKind_ = PipelineKind::Mesh;
    }

    void endFrame() override {
        if (!frameActive_ || encoder_ == nil || commandBuffer_ == nil) {
            return;
        }

        [encoder_ endEncoding];
        encoder_ = nil;

        dispatch_group_enter(inFlightGroup_);
        dispatch_semaphore_t semaphore = semaphore_;
        dispatch_group_t group = inFlightGroup_;
        [commandBuffer_ addCompletedHandler:^(id<MTLCommandBuffer> buffer) {
            (void)buffer;
            dispatch_semaphore_signal(semaphore);
            dispatch_group_leave(group);
        }];

        [commandBuffer_ presentDrawable:drawable_];
        [commandBuffer_ commit];

        commandBuffer_ = nil;
        drawable_ = nil;
        frameActive_ = false;
        ready_ = false;
        ++frameIndex_;
    }

    void waitIdle() override {
        if (inFlightGroup_ != nil) {
            dispatch_group_wait(inFlightGroup_, DISPATCH_TIME_FOREVER);
        }
    }

    BufferHandle createBuffer(BufferUsage usage, const void* data, core::usize size) override {
        if (device_ == nil) {
            return kInvalidBuffer;
        }
        const core::usize length = size == 0 ? 256 : size;
        id<MTLBuffer> buffer =
            [device_ newBufferWithLength:length options:MTLResourceStorageModeShared];
        if (buffer == nil) {
            return kInvalidBuffer;
        }
        if (data != nullptr) {
            std::memcpy(buffer.contents, data, size);
        }

        buffers_.push_back((__bridge_retained void*)buffer);
        (void)usage;
        return static_cast<BufferHandle>(buffers_.size());
    }

    void updateBuffer(BufferHandle handle, const void* data, core::usize size,
                      core::usize offset) override {
        id<MTLBuffer> buffer = lookupBuffer(handle);
        if (buffer == nil || data == nullptr) {
            return;
        }
        if (offset + size > buffer.length) {
            core::logError("metal: updateBuffer out of bounds");
            return;
        }
        std::memcpy(static_cast<uint8_t*>(buffer.contents) + offset, data, size);
    }

    void destroyBuffer(BufferHandle handle) override {
        if (handle == 0 || handle > buffers_.size()) {
            return;
        }
        if (buffers_[handle - 1] != nullptr) {
            id<MTLBuffer> buffer = (__bridge_transfer id<MTLBuffer>)buffers_[handle - 1];
            (void)buffer;
            buffers_[handle - 1] = nullptr;
        }
    }

    PipelineHandle createPipeline(const PipelineDesc& desc) override {
        if (device_ == nil) {
            return kInvalidPipeline;
        }
        id<MTLRenderPipelineState> state = buildPipelineState(desc);
        if (state == nil) {
            return kInvalidPipeline;
        }
        PipelineRecord record;
        record.state = (__bridge_retained void*)state;
        record.depthTest = desc.depthTest;
        record.kind = desc.kind;
        record.desc = desc;
        pipelines_.push_back(std::move(record));
        return static_cast<PipelineHandle>(pipelines_.size());
    }

    void destroyPipeline(PipelineHandle handle) override {
        if (handle == 0 || handle > pipelines_.size()) {
            return;
        }
        PipelineRecord& record = pipelines_[handle - 1];
        if (record.state != nullptr) {
            id<MTLRenderPipelineState> state =
                (__bridge_transfer id<MTLRenderPipelineState>)record.state;
            (void)state;
            record.state = nullptr;
        }
    }

    void bindPipeline(PipelineHandle handle) override {
        if (handle == 0 || handle > pipelines_.size()) {
            return;
        }
        PipelineRecord& record = pipelines_[handle - 1];
        if (record.state == nullptr) {
            return;
        }
        currentPipeline_ = (__bridge id<MTLRenderPipelineState>)record.state;
        currentKind_ = record.kind;
        if (encoder_ != nil) {
            [encoder_ setRenderPipelineState:currentPipeline_];
            [encoder_ setDepthStencilState:record.depthTest ? depthTestState_ : noDepthTestState_];
            ready_ = true;
        }
    }

    void setFrameConstants(const FrameConstants& constants) override {
        if (encoder_ == nil || currentKind_ != PipelineKind::Mesh) {
            return;
        }
        // Layout matches the MSL FrameConstants exactly (see static_asserts).
        [encoder_ setVertexBytes:&constants length:sizeof(FrameConstants) atIndex:2];
        [encoder_ setFragmentBytes:&constants length:sizeof(FrameConstants) atIndex:2];
    }

    void bindVertexBuffer(BufferHandle handle, core::u32 /*stride*/) override {
        currentVertex_ = lookupBuffer(handle);
        if (encoder_ != nil && currentVertex_ != nil) {
            [encoder_ setVertexBuffer:currentVertex_ offset:0 atIndex:0];
        }
    }

    void bindInstanceBuffer(BufferHandle handle, core::u32 /*stride*/) override {
        currentInstance_ = lookupBuffer(handle);
        if (encoder_ != nil && currentInstance_ != nil) {
            [encoder_ setVertexBuffer:currentInstance_ offset:0 atIndex:1];
        }
    }

    void bindIndexBuffer(BufferHandle handle) override { currentIndex_ = lookupBuffer(handle); }

    void drawIndexed(core::u32 indexCount, core::u32 instanceCount) override {
        if (encoder_ == nil || currentIndex_ == nil || currentPipeline_ == nil || !ready_) {
            return;
        }
        [encoder_ drawIndexedPrimitives:MTLPrimitiveTypeTriangle
                             indexCount:indexCount
                              indexType:MTLIndexTypeUInt16
                            indexBuffer:currentIndex_
                      indexBufferOffset:0
                          instanceCount:instanceCount];
        ++drawCalls_;
    }

    [[nodiscard]] ShaderSources meshShaderSources() const override {
        return {kMeshShaderSource, kMeshShaderSource};
    }

    [[nodiscard]] ShaderSources overlayShaderSources() const override {
        return {kOverlayShaderSource, kOverlayShaderSource};
    }

private:
    id<MTLRenderPipelineState> buildPipelineState(const PipelineDesc& desc) {
        if (device_ == nil) {
            return nil;
        }
        @autoreleasepool {
            NSError* error = nil;
            id<MTLLibrary> library =
                [device_ newLibraryWithSource:[NSString stringWithUTF8String:desc.shaders.vertex.c_str()]
                                      options:nil
                                        error:&error];
            if (library == nil) {
                core::logf(core::LogLevel::Error, "metal: shader compilation failed: %s",
                           error != nil ? error.localizedDescription.UTF8String : "unknown");
                return nil;
            }

            const bool mesh = desc.kind == PipelineKind::Mesh;
            MTLRenderPipelineDescriptor* pipelineDesc =
                [[MTLRenderPipelineDescriptor alloc] init];
            pipelineDesc.label = mesh ? @"mesh" : @"overlay";
            pipelineDesc.vertexFunction =
                [library newFunctionWithName:mesh ? @"vs_main" : @"overlay_vs"];
            pipelineDesc.fragmentFunction =
                [library newFunctionWithName:mesh ? @"fs_main" : @"overlay_fs"];
            pipelineDesc.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
            pipelineDesc.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;
            pipelineDesc.rasterSampleCount = samples_;

            if (desc.alphaBlend) {
                MTLRenderPipelineColorAttachmentDescriptor* attachment =
                    pipelineDesc.colorAttachments[0];
                attachment.blendingEnabled = YES;
                attachment.sourceRGBBlendFactor = MTLBlendFactorSourceAlpha;
                attachment.destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
                attachment.sourceAlphaBlendFactor = MTLBlendFactorSourceAlpha;
                attachment.destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
            }

            id<MTLRenderPipelineState> state =
                [device_ newRenderPipelineStateWithDescriptor:pipelineDesc error:&error];
            if (state == nil) {
                core::logf(core::LogLevel::Error, "metal: pipeline creation failed: %s",
                           error != nil ? error.localizedDescription.UTF8String : "unknown");
            }
            return state;
        }
    }

    void createDepthStencilStates() {
        MTLDepthStencilDescriptor* descriptor = [[MTLDepthStencilDescriptor alloc] init];
        descriptor.depthCompareFunction = MTLCompareFunctionLess;
        descriptor.depthWriteEnabled = YES;
        depthTestState_ = [device_ newDepthStencilStateWithDescriptor:descriptor];

        descriptor.depthCompareFunction = MTLCompareFunctionAlways;
        descriptor.depthWriteEnabled = NO;
        noDepthTestState_ = [device_ newDepthStencilStateWithDescriptor:descriptor];
    }

    void createDepthTexture() {
        depthTexture_ = nil;
        msaaColor_ = nil;
        msaaDepth_ = nil;
        if (device_ == nil) {
            return;
        }

        if (samples_ <= 1) {
            MTLTextureDescriptor* descriptor = [MTLTextureDescriptor
                texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float
                                             width:static_cast<NSUInteger>(width_)
                                            height:static_cast<NSUInteger>(height_)
                                         mipmapped:NO];
            descriptor.usage = MTLTextureUsageRenderTarget;
            descriptor.storageMode = MTLStorageModePrivate;
            depthTexture_ = [device_ newTextureWithDescriptor:descriptor];
            return;
        }

        MTLTextureDescriptor* color = [MTLTextureDescriptor
            texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                         width:static_cast<NSUInteger>(width_)
                                        height:static_cast<NSUInteger>(height_)
                                     mipmapped:NO];
        color.textureType = MTLTextureType2DMultisample;
        color.sampleCount = samples_;
        color.usage = MTLTextureUsageRenderTarget;
        color.storageMode = MTLStorageModePrivate;
        msaaColor_ = [device_ newTextureWithDescriptor:color];

        MTLTextureDescriptor* depth = [MTLTextureDescriptor
            texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float
                                         width:static_cast<NSUInteger>(width_)
                                        height:static_cast<NSUInteger>(height_)
                                     mipmapped:NO];
        depth.textureType = MTLTextureType2DMultisample;
        depth.sampleCount = samples_;
        depth.usage = MTLTextureUsageRenderTarget;
        depth.storageMode = MTLStorageModePrivate;
        msaaDepth_ = [device_ newTextureWithDescriptor:depth];
    }

    [[nodiscard]] id<MTLBuffer> lookupBuffer(BufferHandle handle) const {
        if (handle == 0 || handle > buffers_.size() || buffers_[handle - 1] == nullptr) {
            return nil;
        }
        return (__bridge id<MTLBuffer>)buffers_[handle - 1];
    }

    id<MTLDevice> __strong device_ = nil;
    id<MTLCommandQueue> __strong queue_ = nil;
    CAMetalLayer* __strong layer_ = nil;
    NSView* __strong view_ = nil;

    id<CAMetalDrawable> __strong drawable_ = nil;
    id<MTLCommandBuffer> __strong commandBuffer_ = nil;
    id<MTLRenderCommandEncoder> __strong encoder_ = nil;
    id<MTLBuffer> __strong currentVertex_ = nil;
    id<MTLBuffer> __strong currentInstance_ = nil;
    id<MTLBuffer> __strong currentIndex_ = nil;
    id<MTLRenderPipelineState> __strong currentPipeline_ = nil;

    id<MTLTexture> __strong depthTexture_ = nil;
    id<MTLTexture> __strong msaaColor_ = nil;
    id<MTLTexture> __strong msaaDepth_ = nil;
    id<MTLDepthStencilState> __strong depthTestState_ = nil;
    id<MTLDepthStencilState> __strong noDepthTestState_ = nil;

    dispatch_semaphore_t __strong semaphore_ = nil;
    dispatch_group_t __strong inFlightGroup_ = nil;

    std::vector<void*> buffers_;
    std::vector<PipelineRecord> pipelines_;

    core::i32 width_ = 1;
    core::i32 height_ = 1;
    core::u8 samples_ = 1;
    core::u8 maxSamples_ = 1;
    bool vsync_ = true;
    core::u64 frameIndex_ = 0;
    core::u64 drawCalls_ = 0;
    PipelineKind currentKind_ = PipelineKind::Mesh;
    bool frameActive_ = false;
    bool ready_ = false;
};

static_assert(sizeof(FrameConstants) == 128, "FrameConstants must match the MSL layout");

} // namespace

std::unique_ptr<IRenderDevice> createMetalDevice() {
    return std::make_unique<MetalDevice>();
}

} // namespace eee::render
