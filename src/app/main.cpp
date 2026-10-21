#include "eee/core/Input.hpp"
#include "eee/core/Log.hpp"
#include "eee/logic/RpgGame.hpp"
#include "eee/platform/Window.hpp"
#include "eee/render/DebugText.hpp"
#include "eee/render/DeviceRegistry.hpp"
#include "eee/render/IRenderDevice.hpp"
#include "eee/render/Renderer.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>

namespace {

namespace core = eee::core;
namespace scene = eee::scene;
namespace render = eee::render;
namespace logic = eee::logic;

constexpr core::Vec4 kWhite{0.95f, 0.96f, 1.0f, 1.0f};
constexpr core::Vec4 kGold{1.0f, 0.83f, 0.35f, 1.0f};
constexpr core::Vec4 kDim{0.62f, 0.66f, 0.75f, 1.0f};
constexpr core::Vec4 kPanel{0.04f, 0.05f, 0.09f, 0.94f};
constexpr core::Vec4 kBorder{0.55f, 0.48f, 0.28f, 1.0f};
constexpr core::Vec4 kHealth{0.82f, 0.22f, 0.24f, 1.0f};
constexpr core::Vec4 kExperience{0.25f, 0.55f, 0.95f, 1.0f};

struct Options {
    std::string backend;
    core::i32 width = 1280;
    core::i32 height = 720;
    core::i64 frames = 0; // 0 = run until the window closes
    bool headless = false;
    bool listBackends = false;
    bool showFps = false;
    bool vsync = true;
    bool showHelp = false;
};

void printUsage() {
    std::printf(
        "EEEsSA — low-poly third-person RPG\n"
        "\n"
        "Usage: eee_demo [options]\n"
        "  --backend=<name>   Graphics backend to use (default: first available)\n"
        "  --width=<px>       Window width (default: 1280)\n"
        "  --height=<px>      Window height (default: 720)\n"
        "  --frames=<n>       Render n frames then exit (default: until closed)\n"
        "  --headless         Do not create a window (only useful with null)\n"
        "  --fps              Show the FPS overlay at startup\n"
        "  --vsync            Enable vsync (default)\n"
        "  --no-vsync         Disable vsync\n"
        "  --list-backends    Print compiled-in backends and exit\n"
        "  --help             Show this message\n"
        "\n"
        "Controls: WASD move, Shift sprint, mouse right-drag or arrows orbit,\n"
        "          scroll zoom, Space/left-click attack, E talk, ESC menu.\n"
        "          F1 or ` toggles the FPS overlay, V toggles vsync.\n");
}

std::string defaultBackend() {
    const auto names = render::DeviceRegistry::instance().names();
    for (const std::string_view preferred : {"metal", "opengl", "vulkan", "null"}) {
        for (const std::string& name : names) {
            if (name == preferred) {
                return name;
            }
        }
    }
    return "null";
}

bool parseOptions(int argc, char** argv, Options& options) {
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        auto valueOf = [&](std::string_view prefix) -> std::string {
            return std::string(arg.substr(prefix.size()));
        };

        if (arg == "--help" || arg == "-h") {
            options.showHelp = true;
        } else if (arg == "--list-backends") {
            options.listBackends = true;
        } else if (arg == "--headless") {
            options.headless = true;
        } else if (arg == "--fps") {
            options.showFps = true;
        } else if (arg == "--vsync") {
            options.vsync = true;
        } else if (arg == "--no-vsync") {
            options.vsync = false;
        } else if (arg.starts_with("--backend=")) {
            options.backend = valueOf("--backend=");
        } else if (arg.starts_with("--width=")) {
            options.width = std::atoi(valueOf("--width=").c_str());
        } else if (arg.starts_with("--height=")) {
            options.height = std::atoi(valueOf("--height=").c_str());
        } else if (arg.starts_with("--frames=")) {
            options.frames = std::atoll(valueOf("--frames=").c_str());
        } else {
            core::logf(core::LogLevel::Error, "unknown argument: %s", std::string(arg).c_str());
            return false;
        }
    }
    if (options.width <= 0 || options.height <= 0) {
        core::logError("width and height must be positive");
        return false;
    }
    return true;
}

void appendBorder(scene::SceneSnapshot& snapshot, core::Vec2 center, core::Vec2 size,
                  core::f32 thickness, core::Vec4 color) {
    const core::f32 halfW = size.x * 0.5f;
    const core::f32 halfH = size.y * 0.5f;
    render::appendOverlayRect(snapshot,
                              {center.x, center.y + halfH - thickness * 0.5f},
                              {size.x, thickness}, color);
    render::appendOverlayRect(snapshot,
                              {center.x, center.y - halfH + thickness * 0.5f},
                              {size.x, thickness}, color);
    render::appendOverlayRect(snapshot,
                              {center.x - halfW + thickness * 0.5f, center.y},
                              {thickness, size.y}, color);
    render::appendOverlayRect(snapshot,
                              {center.x + halfW - thickness * 0.5f, center.y},
                              {thickness, size.y}, color);
}

void appendDialogue(scene::SceneSnapshot& snapshot, const logic::RpgGame& game,
                    core::f32 width, core::f32 height, core::f32 ui, core::f32 margin) {
    if (!game.dialogueActive()) {
        return;
    }

    const core::f32 boxWidth = std::min(width - margin * 2.0f, ui * 380.0f);
    const core::f32 boxHeight = ui * 64.0f;
    const core::Vec2 center{width * 0.5f, margin + boxHeight * 0.5f + ui * 34.0f};

    render::appendOverlayRect(snapshot, center, {boxWidth, boxHeight}, {0.03f, 0.04f, 0.07f, 0.92f});
    appendBorder(snapshot, center, {boxWidth, boxHeight}, ui, kBorder);
    render::appendDebugTextCentered(snapshot, game.dialogueName(), center.x,
                                    center.y + boxHeight * 0.5f - ui * 15.0f, ui, kGold);
    render::appendDebugTextCentered(snapshot, game.dialogueText(), center.x,
                                    center.y + boxHeight * 0.5f - ui * 34.0f, ui * 0.9f,
                                    kWhite);
}

void appendHud(scene::SceneSnapshot& snapshot, const logic::RpgGame& game, core::f32 width,
               core::f32 height) {
    const core::f32 ui = std::max(2.0f, height / 300.0f);
    const core::f32 margin = ui * 10.0f;
    const logic::PlayerState& player = game.player();
    char line[160];

    core::f32 y = height - margin;
    std::snprintf(line, sizeof(line), "LV %d   GOLD %d   KILLS %d", player.level, player.gold,
                  player.kills);
    render::appendDebugText(snapshot, line, {margin, y}, ui, kWhite);

    y -= ui * 20.0f;
    std::snprintf(line, sizeof(line), "HP %d/%d", static_cast<int>(std::ceil(player.health)),
                  static_cast<int>(player.maxHealth));
    render::appendDebugText(snapshot, line, {margin, y}, ui, kWhite);

    const core::f32 barWidth = ui * 170.0f;
    const core::f32 barHeight = ui * 10.0f;
    const core::f32 barY = y - ui * 16.0f;
    render::appendOverlayRect(snapshot, {margin + barWidth * 0.5f, barY},
                              {barWidth, barHeight}, {0.08f, 0.08f, 0.10f, 0.9f});
    const core::f32 healthFraction =
        core::clamp(player.health / std::max(1.0f, player.maxHealth), 0.0f, 1.0f);
    render::appendOverlayRect(snapshot,
                              {margin + barWidth * healthFraction * 0.5f, barY},
                              {barWidth * healthFraction, barHeight}, kHealth);

    y = barY - ui * 20.0f;
    std::snprintf(line, sizeof(line), "XP %d/%d", player.xp, player.xpToNext);
    render::appendDebugText(snapshot, line, {margin, y}, ui, kWhite);

    const core::f32 xpWidth = ui * 120.0f;
    const core::f32 xpHeight = ui * 6.0f;
    const core::f32 xpY = y - ui * 13.0f;
    render::appendOverlayRect(snapshot, {margin + xpWidth * 0.5f, xpY}, {xpWidth, xpHeight},
                              {0.08f, 0.08f, 0.10f, 0.9f});
    const core::f32 xpFraction =
        core::clamp(static_cast<core::f32>(player.xp) /
                        static_cast<core::f32>(std::max(1, player.xpToNext)),
                    0.0f, 1.0f);
    render::appendOverlayRect(snapshot, {margin + xpWidth * xpFraction * 0.5f, xpY},
                              {xpWidth * xpFraction, xpHeight}, kExperience);

    std::snprintf(line, sizeof(line), "SLIMES %u", game.aliveEnemies());
    render::appendDebugText(snapshot, line, {margin, xpY - ui * 20.0f}, ui, kDim);

    render::appendDebugText(snapshot,
                            "WASD MOVE   SPACE ATTACK   E TALK   ESC MENU",
                            {margin, margin + ui * 7.0f}, ui * 0.85f, kDim);

    appendDialogue(snapshot, game, width, height, ui, margin);
}

void appendMenu(scene::SceneSnapshot& snapshot, const logic::RpgGame& game, core::f32 width,
                core::f32 height) {
    if (!game.menuActive()) {
        return;
    }

    const core::f32 ui = std::max(2.0f, height / 300.0f);
    const core::Vec2 center{width * 0.5f, height * 0.5f};
    const core::f32 panelWidth = std::min(width - ui * 40.0f, ui * 360.0f);
    const core::f32 panelHeight = std::min(height - ui * 40.0f, ui * 260.0f);

    render::appendOverlayRect(snapshot, center, {width, height}, {0.0f, 0.0f, 0.0f, 0.55f});
    render::appendOverlayRect(snapshot, center, {panelWidth, panelHeight}, kPanel);
    appendBorder(snapshot, center, {panelWidth, panelHeight}, ui * 1.2f, kBorder);

    const core::f32 top = center.y + panelHeight * 0.5f - ui * 22.0f;
    render::appendDebugTextCentered(snapshot, game.dead() ? "YOU DIED" : "PAUSED", center.x, top,
                                    ui * 2.0f, game.dead() ? core::Vec4{0.92f, 0.25f, 0.25f, 1.0f}
                                                          : kGold);

    const logic::PlayerState& player = game.player();
    char line[160];
    core::f32 y = top - ui * 34.0f;
    std::snprintf(line, sizeof(line), "LEVEL %d    HP %d/%d", player.level,
                  static_cast<int>(std::ceil(player.health)),
                  static_cast<int>(player.maxHealth));
    render::appendDebugTextCentered(snapshot, line, center.x, y, ui, kWhite);
    y -= ui * 16.0f;

    std::snprintf(line, sizeof(line), "XP %d/%d    GOLD %d", player.xp, player.xpToNext,
                  player.gold);
    render::appendDebugTextCentered(snapshot, line, center.x, y, ui, kWhite);
    y -= ui * 16.0f;

    std::snprintf(line, sizeof(line), "KILLS %d    SLIMES %u", player.kills,
                  game.aliveEnemies());
    render::appendDebugTextCentered(snapshot, line, center.x, y, ui, kWhite);
    y -= ui * 30.0f;

    for (core::usize i = 0; i < game.menuOptionCount(); ++i) {
        const bool selected = static_cast<core::i32>(i) == game.menuSelection();
        std::string text = selected ? "> " : "  ";
        text += std::string(game.menuOption(i));
        if (selected) {
            text += " <";
        }
        render::appendDebugTextCentered(snapshot, text, center.x, y, ui,
                                        selected ? kGold : kDim);
        y -= ui * 20.0f;
    }

    const char* help = game.dead() ? "W/S SELECT   ENTER CONFIRM"
                                   : "W/S SELECT   ENTER CONFIRM   ESC RESUME";
    render::appendDebugTextCentered(snapshot, help, center.x,
                                    center.y - panelHeight * 0.5f + ui * 10.0f, ui * 0.8f,
                                    kDim);
}

} // namespace

int main(int argc, char** argv) {
    Options options;
    if (!parseOptions(argc, argv, options)) {
        printUsage();
        return 2;
    }
    if (options.showHelp) {
        printUsage();
        return 0;
    }

    render::registerBuiltinDevices();
    auto& registry = render::DeviceRegistry::instance();

    if (options.listBackends) {
        std::printf("Available backends:\n");
        for (const std::string& name : registry.names()) {
            std::printf("  - %s\n", name.c_str());
        }
        return 0;
    }

    if (options.backend.empty()) {
        options.backend = defaultBackend();
    }
    if (!registry.contains(options.backend)) {
        core::logf(core::LogLevel::Error, "unknown backend '%s'", options.backend.c_str());
        return 2;
    }

    // The null backend does not present anything, so a window is pointless.
    const bool headless = options.headless || options.backend == "null";

    eee::platform::WindowDesc windowDesc;
    windowDesc.title = "EEEsSA — low-poly RPG";
    windowDesc.width = options.width;
    windowDesc.height = options.height;
    windowDesc.headless = headless;

    auto window = eee::platform::createWindow(windowDesc);
    if (!window) {
        core::logError("failed to create a window");
        return 1;
    }

    core::i32 framebufferWidth = options.width;
    core::i32 framebufferHeight = options.height;
    window->getFramebufferSize(framebufferWidth, framebufferHeight);

    auto device = registry.create(options.backend);
    if (!device) {
        core::logf(core::LogLevel::Error, "failed to instantiate backend '%s'",
                   options.backend.c_str());
        return 1;
    }

    render::DeviceInitDesc initDesc;
    initDesc.window = window->native();
    initDesc.width = framebufferWidth;
    initDesc.height = framebufferHeight;

    if (!device->initialize(initDesc)) {
        core::logf(core::LogLevel::Error, "backend '%s' failed to initialize",
                   options.backend.c_str());
        return 1;
    }

    bool vsyncEnabled = options.vsync;
    device->setVSync(vsyncEnabled);

    render::Renderer renderer(*device);
    if (!renderer.initialize()) {
        core::logError("renderer initialization failed");
        device->shutdown();
        return 1;
    }
    renderer.resize(framebufferWidth, framebufferHeight);

    logic::RpgGame game;

    eee::core::InputState input;
    using Clock = std::chrono::steady_clock;
    auto previous = Clock::now();
    const auto start = previous;

    core::i64 renderedFrames = 0;
    bool fpsVisible = options.showFps;
    core::f32 smoothedFrameTime = 0.0f;

    while (!window->shouldClose()) {
        const auto now = Clock::now();
        const float dt = std::chrono::duration<float>(now - previous).count();
        previous = now;

        if (!window->pollEvents(input)) {
            break;
        }

        if (input.wasPressed(core::Key::F1) || input.wasPressed(core::Key::Backquote)) {
            fpsVisible = !fpsVisible;
        }
        if (input.wasPressed('V')) {
            vsyncEnabled = !vsyncEnabled;
            device->setVSync(vsyncEnabled);
        }

        core::i32 width = framebufferWidth;
        core::i32 height = framebufferHeight;
        window->getFramebufferSize(width, height);
        if (width != framebufferWidth || height != framebufferHeight) {
            framebufferWidth = width;
            framebufferHeight = height;
            renderer.resize(framebufferWidth, framebufferHeight);
        }

        game.update(dt, input);
        if (game.quitRequested()) {
            break;
        }

        scene::SceneSnapshot snapshot = game.buildSnapshot();

        if (dt > 0.0f) {
            smoothedFrameTime = smoothedFrameTime <= 0.0f
                                    ? dt
                                    : smoothedFrameTime + (dt - smoothedFrameTime) * 0.05f;
        }

        if (fpsVisible) {
            const core::f32 pixelSize =
                std::max(2.0f, static_cast<core::f32>(framebufferHeight) / 300.0f);
            const core::f32 margin = pixelSize * 6.0f;
            const core::f32 frameMs = smoothedFrameTime * 1000.0f;
            const core::f32 fps = smoothedFrameTime > 0.0f ? 1.0f / smoothedFrameTime : 0.0f;

            char text[128];
            std::snprintf(text, sizeof(text), "FPS %d  %.1f MS\nVSYNC %s\nMESH %u",
                          static_cast<int>(std::lround(fps)), static_cast<double>(frameMs),
                          vsyncEnabled ? "ON" : "OFF",
                          static_cast<unsigned>(snapshot.meshes.size()));

            const core::f32 widthPixels = render::debugTextWidth(text, pixelSize);
            render::appendDebugText(snapshot, text,
                                    {static_cast<core::f32>(framebufferWidth) - margin - widthPixels,
                                     static_cast<core::f32>(framebufferHeight) - margin},
                                    pixelSize, core::Vec4::rgb(0.98f, 0.95f, 0.55f));
        }

        appendHud(snapshot, game, static_cast<core::f32>(framebufferWidth),
                  static_cast<core::f32>(framebufferHeight));
        appendMenu(snapshot, game, static_cast<core::f32>(framebufferWidth),
                   static_cast<core::f32>(framebufferHeight));

        renderer.renderFrame(snapshot);

        input.endFrame();
        ++renderedFrames;

        if (options.frames > 0 && renderedFrames >= options.frames) {
            break;
        }
    }

    const float seconds = std::chrono::duration<float>(Clock::now() - start).count();
    core::logf(core::LogLevel::Info,
               "rendered %lld frames in %.2fs (%.1f fps) using '%s', %u meshes + %u overlay quads",
               static_cast<long long>(renderedFrames), seconds,
               seconds > 0.0f ? static_cast<double>(renderedFrames) / seconds : 0.0,
               device->name(), renderer.lastMeshCount(), renderer.lastOverlayCount());

    renderer.shutdown();
    device->shutdown();
    return 0;
}
