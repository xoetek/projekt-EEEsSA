#include "TestHarness.hpp"

#include "MockDevice.hpp"

#include "eee/render/DebugText.hpp"
#include "eee/render/Renderer.hpp"
#include "eee/scene/SceneSnapshot.hpp"

#include <cstring>

namespace {

using namespace eee;

scene::SceneSnapshot makeSnapshot(eee::core::u32 meshCount, eee::core::u32 overlayCount) {
    scene::SceneSnapshot snapshot;
    snapshot.camera.position = {0.0f, 10.0f, 20.0f};
    snapshot.camera.target = {0.0f, 0.0f, 0.0f};
    snapshot.camera.up = {0.0f, 1.0f, 0.0f};
    snapshot.camera.fovYRadians = 1.0f;
    snapshot.camera.nearZ = 0.5f;
    snapshot.camera.farZ = 200.0f;
    snapshot.clearColor = {0.1f, 0.2f, 0.3f, 1.0f};
    snapshot.lightDirection = {-0.5f, -1.0f, -0.25f};
    snapshot.ambient = 0.2f;

    for (eee::core::u32 i = 0; i < meshCount; ++i) {
        scene::MeshInstance instance;
        instance.transform = core::Mat4::trs3(
            {static_cast<float>(i) * 2.0f, 0.0f, 0.0f}, {0.0f, 0.5f, 0.0f}, {1.0f, 1.0f, 1.0f});
        instance.color = core::Vec4::rgb(0.25f + 0.01f * static_cast<float>(i), 0.5f, 0.75f);
        snapshot.meshes.push_back(instance);
    }

    for (eee::core::u32 i = 0; i < overlayCount; ++i) {
        scene::OverlaySprite sprite;
        sprite.transform = core::Mat4::trs({10.0f + static_cast<float>(i) * 20.0f, 20.0f}, 0.0f,
                                           {8.0f, 8.0f});
        sprite.color = core::Vec4::rgb(0.9f, 0.9f, 0.4f);
        snapshot.overlay.push_back(sprite);
    }

    return snapshot;
}

bool matricesEqual(const core::Mat4& a, const core::Mat4& b) {
    for (int i = 0; i < 16; ++i) {
        if (a.m[i] != b.m[i]) {
            return false;
        }
    }
    return true;
}

bool matricesNearlyEqual(const core::Mat4& a, const core::Mat4& b, float epsilon = 1e-4f) {
    for (int i = 0; i < 16; ++i) {
        if (!eee::test::nearlyEqual(a.m[i], b.m[i], epsilon)) {
            return false;
        }
    }
    return true;
}

void testDebugText() {
    scene::SceneSnapshot snapshot;
    const core::Vec4 color = core::Vec4::rgb(1.0f, 1.0f, 1.0f);

    eee::render::appendDebugText(snapshot, "A", {0.0f, 0.0f}, 1.0f, color);
    CHECK(snapshot.overlay.size() == 18); // 'A' has 18 lit pixels
    CHECK(snapshot.meshes.empty());

    eee::render::appendDebugText(snapshot, "B", {10.0f, 0.0f}, 1.0f, color);
    CHECK(snapshot.overlay.size() == 18 + 20);

    scene::SceneSnapshot unknown;
    eee::render::appendDebugText(unknown, "\x01", {0.0f, 0.0f}, 1.0f, color);
    CHECK(unknown.overlay.empty());

    scene::SceneSnapshot whitespace;
    eee::render::appendDebugText(whitespace, " \n ", {0.0f, 0.0f}, 1.0f, color);
    CHECK(whitespace.overlay.empty());

    scene::SceneSnapshot lower;
    eee::render::appendDebugText(lower, "a", {0.0f, 0.0f}, 1.0f, color);
    CHECK(lower.overlay.size() == 18);

    scene::SceneSnapshot placement;
    eee::render::appendDebugText(placement, "A\nA", {0.0f, 0.0f}, 1.0f, color);
    CHECK(placement.overlay.size() == 36);
    float minY = 0.0f;
    float maxX = 0.0f;
    for (const scene::OverlaySprite& sprite : placement.overlay) {
        minY = std::fmin(minY, sprite.transform.m[13]);
        maxX = std::fmax(maxX, sprite.transform.m[12]);
    }
    CHECK(eee::test::nearlyEqual(minY, -15.5f));
    CHECK(eee::test::nearlyEqual(maxX, 4.5f));
}

void testOverlayHelpers() {
    using eee::render::appendOverlayRect;
    using eee::render::appendDebugTextCentered;
    using eee::render::debugTextWidth;

    CHECK(eee::test::nearlyEqual(debugTextWidth("A", 1.0f), 5.0f));
    CHECK(eee::test::nearlyEqual(debugTextWidth("AB", 1.0f), 11.0f));
    CHECK(eee::test::nearlyEqual(debugTextWidth("A\nABC", 1.0f), 17.0f));
    CHECK(eee::test::nearlyEqual(debugTextWidth("", 3.0f), 0.0f));

    scene::SceneSnapshot snapshot;
    appendOverlayRect(snapshot, {50.0f, 60.0f}, {20.0f, 10.0f},
                      core::Vec4::rgb(0.2f, 0.4f, 0.6f));
    CHECK(snapshot.overlay.size() == 1);
    CHECK(eee::test::nearlyEqual(snapshot.overlay[0].transform.m[12], 50.0f));
    CHECK(eee::test::nearlyEqual(snapshot.overlay[0].transform.m[13], 60.0f));
    CHECK(eee::test::nearlyEqual(snapshot.overlay[0].transform.m[0], 20.0f));
    CHECK(eee::test::nearlyEqual(snapshot.overlay[0].transform.m[5], 10.0f));

    scene::SceneSnapshot centered;
    appendDebugTextCentered(centered, "A", 100.0f, 10.0f, 1.0f,
                            core::Vec4::rgb(1.0f, 1.0f, 1.0f));
    CHECK(centered.overlay.size() == 18); // 'A' lit pixels
    float minX = 1e9f;
    float maxX = -1e9f;
    for (const scene::OverlaySprite& sprite : centered.overlay) {
        minX = std::fmin(minX, sprite.transform.m[12]);
        maxX = std::fmax(maxX, sprite.transform.m[12]);
    }
    CHECK(eee::test::nearlyEqual(minX, 98.0f));
    CHECK(eee::test::nearlyEqual(maxX, 102.0f));
}

void testRendererInitialization() {
    eee::test::MockDevice mock;
    eee::render::Renderer renderer(mock);
    CHECK(renderer.initialize());
    CHECK(renderer.initialize()); // idempotent

    CHECK(mock.createBufferCalls == 10); // 4 geometry + 3 mesh + 3 overlay buffers
    CHECK(mock.createPipelineCalls == 2);
    CHECK(mock.aliveBufferCount() == 10);
    CHECK(mock.alivePipelineCount() == 2);

    CHECK(mock.pipeline(1).kind == eee::render::PipelineKind::Mesh);
    CHECK(mock.pipeline(1).depthTest);
    CHECK(!mock.pipeline(1).alphaBlend);
    CHECK(mock.pipeline(1).vertexSource == "mock-mesh-vs");

    CHECK(mock.pipeline(2).kind == eee::render::PipelineKind::Overlay);
    CHECK(!mock.pipeline(2).depthTest);
    CHECK(mock.pipeline(2).alphaBlend);
    CHECK(mock.pipeline(2).vertexSource == "mock-overlay-vs");

    // Instance buffers are triple-buffered.
    CHECK(mock.buffer(5).bytes.size() == 1024 * 80);
    CHECK(mock.buffer(8).bytes.size() == 512 * 80);
}

void testMeshPass() {
    eee::test::MockDevice mock;
    eee::render::Renderer renderer(mock);
    CHECK(renderer.initialize());

    const scene::SceneSnapshot snapshot = makeSnapshot(3, 0);
    renderer.renderFrame(snapshot);

    CHECK(mock.beginFrames == 1);
    CHECK(mock.endFrames == 1);
    CHECK(mock.draws.size() == 1);

    const auto& draw = mock.draws[0];
    CHECK(draw.indexCount == 36);
    CHECK(draw.instanceCount == 3);
    CHECK(draw.pipeline == 1);
    CHECK(mock.pipeline(draw.pipeline).kind == eee::render::PipelineKind::Mesh);
    CHECK(draw.vertexBuffer == 1);
    CHECK(draw.indexBuffer == 2);
    CHECK(mock.vertexStride == 24);
    CHECK(mock.instanceStride == 80);

    CHECK(mock.constantCalls == 1);
    CHECK(eee::test::nearlyEqual(mock.constants.ambient, 0.2f));
    CHECK(eee::test::nearlyEqual(mock.constants.lightDirection.x, -0.5f));
    CHECK(eee::test::nearlyEqual(mock.constants.lightDirection.z, -0.25f));
    CHECK(!matricesEqual(mock.constants.viewProjection, core::Mat4::identity()));

    const auto& bytes = mock.buffer(draw.instanceBuffer).bytes;
    CHECK(bytes.size() >= 3 * sizeof(scene::MeshInstance));
    scene::MeshInstance uploaded{};
    std::memcpy(&uploaded, bytes.data(), sizeof(uploaded));
    CHECK(matricesEqual(uploaded.transform, snapshot.meshes[0].transform));
    CHECK(eee::test::nearlyEqual(uploaded.color.x, snapshot.meshes[0].color.x));

    CHECK(renderer.lastMeshCount() == 3);
    CHECK(renderer.lastOverlayCount() == 0);
    CHECK(renderer.frameIndex() == 1);
}

void testOverlayPass() {
    eee::test::MockDevice mock;
    eee::render::Renderer renderer(mock);
    CHECK(renderer.initialize());
    renderer.resize(800, 600);
    CHECK(mock.resizeCalls == 1);
    CHECK(mock.width_ == 800);
    CHECK(mock.height_ == 600);

    const scene::SceneSnapshot snapshot = makeSnapshot(1, 2);
    renderer.renderFrame(snapshot);

    CHECK(mock.draws.size() == 2);
    CHECK(mock.draws[0].indexCount == 36);
    CHECK(mock.draws[0].instanceCount == 1);
    CHECK(mock.draws[1].indexCount == 6);
    CHECK(mock.draws[1].instanceCount == 2);
    CHECK(mock.draws[1].pipeline == 2);
    CHECK(!mock.pipeline(mock.draws[1].pipeline).depthTest);
    CHECK(mock.vertexStride == 8);

    const auto& bytes = mock.buffer(mock.draws[1].instanceBuffer).bytes;
    CHECK(bytes.size() >= 2 * 80);
    struct GpuInstance {
        core::Mat4 transform;
        core::Vec4 color;
    };
    GpuInstance uploaded{};
    std::memcpy(&uploaded, bytes.data(), sizeof(uploaded));

    const core::Mat4 ortho = core::Mat4::orthographic(0.0f, 800.0f, 0.0f, 600.0f, -1.0f, 1.0f);
    const core::Mat4 expected = ortho * snapshot.overlay[0].transform;
    CHECK(matricesNearlyEqual(uploaded.transform, expected));
    CHECK(eee::test::nearlyEqual(uploaded.color.x, snapshot.overlay[0].color.x));

    CHECK(renderer.lastMeshCount() == 1);
    CHECK(renderer.lastOverlayCount() == 2);
}

void testFrameSlotRotation() {
    eee::test::MockDevice mock;
    eee::render::Renderer renderer(mock);
    CHECK(renderer.initialize());

    const scene::SceneSnapshot snapshot = makeSnapshot(2, 0);
    eee::render::BufferHandle handles[4] = {};
    for (int frame = 0; frame < 4; ++frame) {
        renderer.renderFrame(snapshot);
        CHECK(mock.draws.size() == static_cast<eee::core::usize>(frame + 1));
        handles[frame] = mock.draws.back().instanceBuffer;
    }

    CHECK(handles[0] != handles[1]);
    CHECK(handles[1] != handles[2]);
    CHECK(handles[2] != handles[0]);
    CHECK(handles[3] == handles[0]); // three frames in flight, then reuse
}

void testQualityCulling() {
    eee::test::MockDevice mock;
    eee::render::Renderer renderer(mock);
    CHECK(renderer.initialize());

    scene::SceneSnapshot snapshot = makeSnapshot(3, 0);
    scene::MeshInstance far;
    far.transform = core::Mat4::translation({200.0f, 0.0f, 0.0f});
    far.color = core::Vec4::rgb(1.0f, 1.0f, 1.0f);
    snapshot.meshes.push_back(far);

    renderer.setQuality(eee::render::QualityLevel::Low);
    renderer.renderFrame(snapshot);
    CHECK(mock.draws.size() == 1);
    CHECK(mock.draws[0].instanceCount == 3); // far cube culled
    CHECK(eee::test::nearlyEqual(mock.constants.fogStart, 14.0f));
    CHECK(eee::test::nearlyEqual(mock.constants.fogEnd, 46.0f));

    renderer.setQuality(eee::render::QualityLevel::Ultra);
    renderer.renderFrame(snapshot);
    CHECK(mock.draws.size() == 2);
    CHECK(mock.draws[1].instanceCount == 4); // no distance culling
}

void testEmptySnapshot() {
    eee::test::MockDevice mock;
    eee::render::Renderer renderer(mock);
    CHECK(renderer.initialize());

    const scene::SceneSnapshot snapshot = makeSnapshot(0, 0);
    renderer.renderFrame(snapshot);

    CHECK(mock.beginFrames == 1);
    CHECK(mock.endFrames == 1);
    CHECK(mock.draws.empty());
    CHECK(mock.constantCalls == 0);
    CHECK(renderer.lastMeshCount() == 0);
    CHECK(renderer.lastOverlayCount() == 0);
}

void testCapacityGrowth() {
    eee::test::MockDevice mock;
    eee::render::Renderer renderer(mock);
    CHECK(renderer.initialize());

    const scene::SceneSnapshot snapshot = makeSnapshot(1500, 0);
    renderer.renderFrame(snapshot);

    CHECK(mock.draws.size() == 1);
    CHECK(mock.draws[0].instanceCount == 1500);
    CHECK(mock.buffer(mock.draws[0].instanceBuffer).bytes.size() >= 1500 * 80);
    CHECK(mock.destroyBufferCalls >= 3); // old instance triple buffer destroyed
}

void testUninitializedRenderer() {
    eee::test::MockDevice mock;
    eee::render::Renderer renderer(mock);
    renderer.renderFrame(makeSnapshot(5, 5));
    CHECK(mock.draws.empty());
    CHECK(mock.beginFrames == 0);
}

void testShutdown() {
    eee::test::MockDevice mock;
    eee::render::Renderer renderer(mock);
    CHECK(renderer.initialize());
    renderer.renderFrame(makeSnapshot(4, 2));
    renderer.shutdown();

    CHECK(mock.waitIdleCalls >= 1);
    CHECK(mock.aliveBufferCount() == 0);
    CHECK(mock.alivePipelineCount() == 0);
    CHECK(mock.destroyBufferCalls == 10);
    CHECK(mock.destroyPipelineCalls == 2);
}

} // namespace

int main() {
    testDebugText();
    testOverlayHelpers();
    testRendererInitialization();
    testMeshPass();
    testOverlayPass();
    testFrameSlotRotation();
    testQualityCulling();
    testEmptySnapshot();
    testCapacityGrowth();
    testUninitializedRenderer();
    testShutdown();
    return eee::test::report("render");
}
