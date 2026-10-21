#include "eee/core/Input.hpp"
#include "eee/core/Log.hpp"
#include "eee/logic/Classes.hpp"
#include "eee/logic/Items.hpp"
#include "eee/logic/RpgGame.hpp"
#include "eee/platform/Audio.hpp"
#include "eee/platform/Window.hpp"
#include "eee/render/DebugText.hpp"
#include "eee/render/DeviceRegistry.hpp"
#include "eee/render/IRenderDevice.hpp"
#include "eee/net/Protocol.hpp"
#include "eee/net/TcpTransport.hpp"
#include "eee/render/Renderer.hpp"
#include "eee/session/AuthoritativeClient.hpp"
#include "eee/session/LockstepSession.hpp"
#include "eee/session/SessionDriver.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>

namespace {

namespace core = eee::core;
namespace scene = eee::scene;
namespace render = eee::render;
namespace logic = eee::logic;
namespace platform = eee::platform;
namespace session = eee::session;
namespace net = eee::net;

constexpr core::Vec4 kWhite{0.95f, 0.96f, 1.0f, 1.0f};
constexpr core::Vec4 kGold{1.0f, 0.83f, 0.35f, 1.0f};
constexpr core::Vec4 kDim{0.62f, 0.66f, 0.75f, 1.0f};
constexpr core::Vec4 kPanel{0.04f, 0.05f, 0.09f, 0.95f};
constexpr core::Vec4 kBorder{0.55f, 0.48f, 0.28f, 1.0f};
constexpr core::Vec4 kHealth{0.82f, 0.22f, 0.24f, 1.0f};
constexpr core::Vec4 kMana{0.20f, 0.45f, 0.95f, 1.0f};
constexpr core::Vec4 kXp{0.85f, 0.70f, 0.25f, 1.0f};
constexpr core::Vec4 kEnemyHealth{0.90f, 0.20f, 0.20f, 1.0f};

struct GraphicsSettings {
    core::i32 aaIndex = 2;      // 0,2,4,8
    core::i32 qualityIndex = 2; // low, medium, high, ultra
    core::i32 fpsIndex = 6;     // 30..240, off
    bool vsync = true;
    bool invertY = false;
};

constexpr core::i32 kAaValues[] = {0, 2, 4, 8};
constexpr core::i32 kFpsValues[] = {30, 60, 90, 120, 144, 240, 0};
constexpr const char* kAaNames[] = {"OFF", "2X", "4X", "8X"};
constexpr const char* kQualityNames[] = {"LOW", "MEDIUM", "HIGH", "ULTRA"};
constexpr const char* kFpsNames[] = {"30", "60", "90", "120", "144", "240", "OFF"};
constexpr const char* kSettingsLabels[] = {"ANTI-ALIASING", "QUALITY", "FPS CAP",
                                          "VSYNC",         "INVERT Y", "BACK"};

void applyGraphics(const GraphicsSettings& settings, render::IRenderDevice& device,
                   render::Renderer& renderer) {
    device.setMultisampleSamples(static_cast<core::u8>(kAaValues[settings.aaIndex]));
    renderer.setQuality(static_cast<render::QualityLevel>(settings.qualityIndex));
    device.setVSync(settings.vsync);
}

struct Options {
    std::string backend;
    core::i32 width = 1280;
    core::i32 height = 720;
    core::i64 frames = 0;
    bool headless = false;
    bool listBackends = false;
    bool showFps = false;
    bool vsync = true;
    std::string startClass;
    bool host = false;
    bool join = false;
    std::string hostPort = "27015";
    std::string joinAddress;
    core::i32 aa = 4;
    core::i32 quality = 2;
    core::i32 fpsCap = 0;
    bool showHelp = false;
};

void printUsage() {
    std::printf(
        "EEEsSA — low-poly third-person ARPG\n"
        "\n"
        "Usage: eee_demo [options]\n"
        "  --backend=<name>   Graphics backend (default: first available)\n"
        "  --width=<px>       Window width (default: 1280)\n"
        "  --height=<px>      Window height (default: 720)\n"
        "  --frames=<n>       Render n frames then exit\n"
        "  --headless         Do not create a window (null backend)\n"
        "  --fps              Show the FPS overlay at startup\n"
        "  --vsync/--no-vsync Vsync on/off\n"
        "  --class=<name>     Start immediately as warrior/archer/mage\n"
        "  --aa=<off|2|4|8>   MSAA level (default 4)\n"
        "  --quality=<level>  low/medium/high/ultra (default high)\n"
        "  --fps-cap=<n>      Frame cap (0 = off)\n"
        "  --host[=port]      Host a 2-player LAN co-op session (default 27015)\n"
        "  --join=host[:port] Join a co-op session\n"
        "  --list-backends    Print available backends\n"
        "  --help             Show this message\n"
        "\n"
        "Controls: WASD move, Shift sprint, mouse orbits camera, wheel zooms.\n"
        "          LMB attack, RMB interact, Q/E skills, Space jump, I inventory,\n"
        "          ESC menu, F1/` FPS, F2 or G graphics, V vsync.\n");
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
        } else if (arg == "--host") {
            options.host = true;
        } else if (arg.starts_with("--host=")) {
            options.host = true;
            options.hostPort = valueOf("--host=");
        } else if (arg.starts_with("--join=")) {
            options.join = true;
            options.joinAddress = valueOf("--join=");
        } else if (arg.starts_with("--class=")) {
            options.startClass = valueOf("--class=");
        } else if (arg.starts_with("--aa=")) {
            const std::string value = valueOf("--aa=");
            if (value == "off" || value == "0") {
                options.aa = 0;
            } else if (value == "2" || value == "4" || value == "8") {
                options.aa = std::atoi(value.c_str());
            } else {
                core::logf(core::LogLevel::Error, "unknown AA mode '%s'", value.c_str());
                return false;
            }
        } else if (arg.starts_with("--quality=")) {
            const std::string value = valueOf("--quality=");
            if (value == "low") {
                options.quality = 0;
            } else if (value == "medium") {
                options.quality = 1;
            } else if (value == "high") {
                options.quality = 2;
            } else if (value == "ultra") {
                options.quality = 3;
            } else {
                core::logf(core::LogLevel::Error, "unknown quality '%s'", value.c_str());
                return false;
            }
        } else if (arg.starts_with("--fps-cap=")) {
            options.fpsCap = std::atoi(valueOf("--fps-cap=").c_str());
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
    if (options.host && options.join) {
        core::logError("--host and --join are mutually exclusive");
        return false;
    }
    return true;
}

core::Vec4 rarityColor(logic::Rarity rarity) {
    switch (rarity) {
    case logic::Rarity::Normal: return {0.82f, 0.82f, 0.82f, 1.0f};
    case logic::Rarity::Magic: return {0.35f, 0.58f, 1.00f, 1.0f};
    case logic::Rarity::Rare: return {1.00f, 0.82f, 0.25f, 1.0f};
    case logic::Rarity::Unique: return {1.00f, 0.45f, 0.15f, 1.0f};
    }
    return kWhite;
}

bool parseClassId(std::string_view value, logic::ClassId& out) {
    if (value.empty() || value == "warrior") {
        out = logic::ClassId::Warrior;
    } else if (value == "archer") {
        out = logic::ClassId::Archer;
    } else if (value == "mage") {
        out = logic::ClassId::Mage;
    } else {
        return false;
    }
    return true;
}

bool sendMessage(net::TcpTransport& transport, net::ConnectionId connection,
                 const net::Message& message) {
    return transport.send(connection, net::encode(message));
}

std::optional<net::Message> waitForMessage(net::TcpTransport& transport,
                                           net::ConnectionId connection, int timeoutMs) {
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (std::chrono::steady_clock::now() < deadline) {
        auto bytes = transport.receive(connection);
        if (bytes.has_value()) {
            net::Message message;
            if (net::decode(*bytes, message)) {
                return message;
            }
            return std::nullopt;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return std::nullopt;
}

std::optional<net::ConnectionId> acceptWithTimeout(net::TcpTransport& transport,
                                                   int timeoutMs) {
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (std::chrono::steady_clock::now() < deadline) {
        auto connection = transport.accept();
        if (connection.has_value()) {
            return connection;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return std::nullopt;
}

void appendBorder(scene::SceneSnapshot& snapshot, core::Vec2 center, core::Vec2 size,
                  core::f32 thickness, core::Vec4 color) {
    const core::f32 halfW = size.x * 0.5f;
    const core::f32 halfH = size.y * 0.5f;
    render::appendOverlayRect(snapshot, {center.x, center.y + halfH - thickness * 0.5f},
                              {size.x, thickness}, color);
    render::appendOverlayRect(snapshot, {center.x, center.y - halfH + thickness * 0.5f},
                              {size.x, thickness}, color);
    render::appendOverlayRect(snapshot, {center.x - halfW + thickness * 0.5f, center.y},
                              {thickness, size.y}, color);
    render::appendOverlayRect(snapshot, {center.x + halfW - thickness * 0.5f, center.y},
                              {thickness, size.y}, color);
}

void appendBar(scene::SceneSnapshot& snapshot, core::f32 x, core::f32 y, core::f32 width,
               core::f32 height, core::f32 fraction, core::Vec4 fill) {
    fraction = core::clamp(fraction, 0.0f, 1.0f);
    render::appendOverlayRect(snapshot, {x + width * 0.5f, y}, {width, height},
                              {0.08f, 0.08f, 0.10f, 0.92f});
    if (fraction > 0.0f) {
        render::appendOverlayRect(snapshot, {x + width * fraction * 0.5f, y},
                                  {width * fraction, height}, fill);
    }
    render::appendOverlayRect(snapshot, {x + width * 0.5f, y}, {width, height * 0.18f},
                              {1.0f, 1.0f, 1.0f, 0.10f});
}

void appendHud(scene::SceneSnapshot& snapshot, const logic::RpgGame& game, core::f32 width,
               core::f32 height) {
    const core::f32 ui = std::max(2.0f, height / 300.0f);
    const core::f32 margin = ui * 10.0f;
    const logic::PlayerState& player = game.player(game.localViewerId());
    char line[192];

    core::f32 y = height - margin;
    std::snprintf(line, sizeof(line), "%s   LV %d   %s",
                  logic::classDef(player.classId).name, player.loadout.level, game.zone().name);
    render::appendDebugText(snapshot, line, {margin, y}, ui, kWhite);

    y -= ui * 20.0f;
    std::snprintf(line, sizeof(line), "HP %d/%d", static_cast<int>(std::ceil(player.health)),
                  static_cast<int>(player.maxHealth));
    render::appendDebugText(snapshot, line, {margin, y}, ui, kWhite);
    appendBar(snapshot, margin, y - ui * 15.0f, ui * 190.0f, ui * 10.0f,
              player.health / std::max(1.0f, player.maxHealth), kHealth);

    y -= ui * 32.0f;
    std::snprintf(line, sizeof(line), "MP %d/%d", static_cast<int>(std::ceil(player.mana)),
                  static_cast<int>(player.maxMana));
    render::appendDebugText(snapshot, line, {margin, y}, ui, kWhite);
    appendBar(snapshot, margin, y - ui * 15.0f, ui * 190.0f, ui * 8.0f,
              player.mana / std::max(1.0f, player.maxMana), kMana);

    y -= ui * 28.0f;
    std::snprintf(line, sizeof(line), "XP %d/%d", player.loadout.xp, player.loadout.xpToNext);
    render::appendDebugText(snapshot, line, {margin, y}, ui, kWhite);
    appendBar(snapshot, margin, y - ui * 12.0f, ui * 190.0f, ui * 6.0f,
              static_cast<core::f32>(player.loadout.xp) /
                  static_cast<core::f32>(std::max(1, player.loadout.xpToNext)),
              kXp);

    y -= ui * 24.0f;
    std::snprintf(line, sizeof(line), "GOLD %d   KILLS %d", player.loadout.gold,
                  player.loadout.kills);
    render::appendDebugText(snapshot, line, {margin, y}, ui, kDim);

    if (game.activePlayers() > 1) {
        const core::PlayerId otherId = game.localViewerId() == core::PlayerId::P0
                                            ? core::PlayerId::P1
                                            : core::PlayerId::P0;
        const logic::PlayerState& ally = game.player(otherId);
        y -= ui * 18.0f;
        std::snprintf(line, sizeof(line), "ALLY %s  HP %d/%d",
                      logic::classDef(ally.classId).name,
                      static_cast<int>(std::ceil(ally.health)),
                      static_cast<int>(ally.maxHealth));
        render::appendDebugText(snapshot, line, {margin, y}, ui * 0.8f,
                                ally.playerDead ? core::Vec4{0.9f, 0.3f, 0.3f, 1.0f} : kDim);
    }

    // Skill bar.
    const logic::ClassDef& cls = logic::classDef(player.classId);
    const core::f32 boxW = ui * 96.0f;
    const core::f32 boxH = ui * 40.0f;
    const core::f32 gap = ui * 8.0f;
    const core::f32 totalW = boxW * 2.0f + gap;
    const core::f32 baseX = width * 0.5f - totalW * 0.5f;
    const core::f32 baseY = margin + ui * 12.0f;

    auto drawSkill = [&](core::f32 x, const char* key, logic::SkillId skill, core::f32 cooldown,
                         bool locked) {
        render::appendOverlayRect(snapshot, {x + boxW * 0.5f, baseY + boxH * 0.5f},
                                  {boxW, boxH}, {0.05f, 0.06f, 0.10f, 0.88f});
        appendBorder(snapshot, {x + boxW * 0.5f, baseY + boxH * 0.5f}, {boxW, boxH}, ui,
                     locked ? kDim : kBorder);
        if (locked) {
            std::snprintf(line, sizeof(line), "%s  LOCKED", key);
            render::appendDebugTextCentered(snapshot, line, x + boxW * 0.5f,
                                            baseY + boxH * 0.55f, ui, kDim);
            return;
        }
        const logic::SkillDef& def = logic::skillDef(skill);
        std::snprintf(line, sizeof(line), "%s  %s", key, def.name);
        render::appendDebugTextCentered(snapshot, line, x + boxW * 0.5f,
                                        baseY + boxH * 0.62f, ui * 0.82f, kGold);
        if (cooldown > 0.0f) {
            std::snprintf(line, sizeof(line), "%.1fS", static_cast<double>(cooldown));
            render::appendDebugTextCentered(snapshot, line, x + boxW * 0.5f,
                                            baseY + boxH * 0.20f, ui * 0.8f, kDim);
        } else {
            std::snprintf(line, sizeof(line), "%d MANA", static_cast<int>(def.manaCost));
            render::appendDebugTextCentered(snapshot, line, x + boxW * 0.5f,
                                            baseY + boxH * 0.20f, ui * 0.8f, kDim);
        }
    };

    drawSkill(baseX, "Q", cls.primary, player.qCooldown, false);
    drawSkill(baseX + boxW + gap, "E", cls.secondary, player.eCooldown,
              !game.secondaryUnlocked());

    render::appendDebugText(snapshot,
                            "LMB ATTACK   RMB INTERACT   SPACE JUMP   I BAG   ESC MENU",
                            {margin, margin + ui * 24.0f}, ui * 0.75f, kDim);

    if (!game.interactionHint().empty()) {
        render::appendDebugTextCentered(snapshot, game.interactionHint(), width * 0.5f,
                                        baseY + boxH + ui * 25.0f, ui, kGold);
    }
    if (!game.message().empty() && game.messageTimer() > 0.0f) {
        render::appendDebugTextCentered(snapshot, game.message(), width * 0.5f,
                                        baseY + boxH + ui * 45.0f, ui * 0.9f, kWhite);
    }

    if (const logic::EnemyState* boss = game.boss()) {
        if (boss->alive) {
            const core::f32 barWidth = width * 0.38f;
            const core::f32 barX = width * 0.5f - barWidth * 0.5f;
            const core::f32 barY = height - margin - ui * 44.0f;
            render::appendDebugTextCentered(snapshot, boss->name, width * 0.5f,
                                            height - margin - ui * 8.0f, ui * 1.1f, kGold);
            appendBar(snapshot, barX, barY, barWidth, ui * 12.0f,
                      boss->health / std::max(1.0f, boss->maxHealth), kEnemyHealth);
        }
    }

    if (game.bannerTimer() > 0.0f) {
        const core::f32 boxW = std::min(width - margin * 2.0f, ui * 420.0f);
        const core::f32 boxH = ui * 70.0f;
        const core::Vec2 center{width * 0.5f, height * 0.70f};
        render::appendOverlayRect(snapshot, center, {boxW, boxH}, {0.02f, 0.03f, 0.06f, 0.88f});
        appendBorder(snapshot, center, {boxW, boxH}, ui, kBorder);
        render::appendDebugTextCentered(snapshot, game.bannerTitle(), center.x,
                                        center.y + boxH * 0.5f - ui * 18.0f, ui * 1.2f, kGold);
        render::appendDebugTextCentered(snapshot, game.bannerText(), center.x,
                                        center.y + boxH * 0.5f - ui * 42.0f, ui * 0.8f, kWhite);
    }

    if (game.dialogueActive()) {
        const core::f32 boxW = std::min(width - margin * 2.0f, ui * 440.0f);
        const core::f32 boxH = ui * 62.0f;
        const core::Vec2 center{width * 0.5f, height * 0.34f};
        render::appendOverlayRect(snapshot, center, {boxW, boxH}, {0.03f, 0.04f, 0.07f, 0.94f});
        appendBorder(snapshot, center, {boxW, boxH}, ui, kBorder);
        render::appendDebugTextCentered(snapshot, game.dialogueName(), center.x,
                                        center.y + boxH * 0.5f - ui * 16.0f, ui, kGold);
        render::appendDebugTextCentered(snapshot, game.dialogueText(), center.x,
                                        center.y + boxH * 0.5f - ui * 38.0f, ui * 0.85f,
                                        kWhite);
    }
}

void appendInventory(scene::SceneSnapshot& snapshot, const logic::RpgGame& game, core::f32 width,
                     core::f32 height) {
    const core::f32 ui = std::max(2.0f, height / 300.0f);
    const core::f32 margin = ui * 10.0f;
    const core::Vec2 center{width * 0.5f, height * 0.5f};
    const core::f32 panelW = std::min(width - margin * 2.0f, ui * 540.0f);
    const core::f32 panelH = std::min(height - margin * 2.0f, ui * 300.0f);

    render::appendOverlayRect(snapshot, center, {width, height}, {0.0f, 0.0f, 0.0f, 0.60f});
    render::appendOverlayRect(snapshot, center, {panelW, panelH}, kPanel);
    appendBorder(snapshot, center, {panelW, panelH}, ui * 1.2f, kBorder);

    const core::f32 top = center.y + panelH * 0.5f;
    render::appendDebugTextCentered(snapshot, "INVENTORY", center.x, top - ui * 22.0f, ui * 1.4f,
                                    kGold);
    char line[192];
    std::snprintf(line, sizeof(line), "GOLD %d", game.player().loadout.gold);
    render::appendDebugTextCentered(snapshot, line, center.x, top - ui * 44.0f, ui, kDim);

    const core::f32 cell = ui * 28.0f;
    const core::f32 gap = ui * 4.0f;
    const core::f32 gridX = center.x - panelW * 0.5f + ui * 16.0f;
    const core::f32 gridTop = top - ui * 70.0f;
    const core::i32 columns = 8;

    for (core::usize i = 0; i < game.inventoryCapacity(); ++i) {
        const core::i32 col = static_cast<core::i32>(i) % columns;
        const core::i32 row = static_cast<core::i32>(i) / columns;
        const core::Vec2 cellCenter{gridX + cell * 0.5f + static_cast<core::f32>(col) * (cell + gap),
                                    gridTop - cell * 0.5f -
                                        static_cast<core::f32>(row) * (cell + gap)};
        render::appendOverlayRect(snapshot, cellCenter, {cell, cell},
                                  {0.07f, 0.08f, 0.12f, 0.95f});
        appendBorder(snapshot, cellCenter, {cell, cell}, ui,
                     static_cast<core::i32>(i) == game.inventorySelection() ? kGold : kDim);

        if (i < game.inventory().size()) {
            const logic::Item& item = game.inventory()[i];
            const core::Vec4 color = rarityColor(item.rarity);
            render::appendOverlayRect(snapshot, cellCenter, {cell * 0.55f, cell * 0.55f}, color);
            if (static_cast<core::i32>(i) == game.inventorySelection()) {
                appendBorder(snapshot, cellCenter, {cell * 0.62f, cell * 0.62f}, ui, kWhite);
            }
        }
    }

    const core::f32 detailsX = gridX + columns * (cell + gap) + ui * 18.0f;
    const core::f32 detailsTop = gridTop - ui * 4.0f;
    const logic::Item* selected =
        game.inventory().empty()
            ? nullptr
            : &game.inventory()[static_cast<core::usize>(
                  game.inventorySelection() % static_cast<core::i32>(game.inventory().size()))];

    if (selected == nullptr) {
        render::appendDebugText(snapshot, "NO ITEMS. SLAY ENEMIES.", {detailsX, detailsTop}, ui,
                                kDim);
    } else {
        const core::Vec4 color = rarityColor(selected->rarity);
        render::appendDebugText(snapshot, selected->name, {detailsX, detailsTop}, ui, color);
        core::f32 y = detailsTop - ui * 18.0f;
        std::snprintf(line, sizeof(line), "%s   %s", logic::rarityName(selected->rarity),
                      logic::slotName(selected->slot));
        render::appendDebugText(snapshot, line, {detailsX, y}, ui * 0.8f, kDim);
        y -= ui * 15.0f;
        std::snprintf(line, sizeof(line), "ITEM LEVEL %d", selected->itemLevel);
        render::appendDebugText(snapshot, line, {detailsX, y}, ui * 0.8f,
                                selected->itemLevel > game.player().loadout.level ? kEnemyHealth : kDim);
        y -= ui * 15.0f;
        if (selected->baseDamage > 0) {
            std::snprintf(line, sizeof(line), "DAMAGE %d", selected->baseDamage);
            render::appendDebugText(snapshot, line, {detailsX, y}, ui * 0.8f, kWhite);
            y -= ui * 15.0f;
        }
        if (selected->baseArmour > 0) {
            std::snprintf(line, sizeof(line), "ARMOUR %d", selected->baseArmour);
            render::appendDebugText(snapshot, line, {detailsX, y}, ui * 0.8f, kWhite);
            y -= ui * 15.0f;
        }
        if (selected->slot == logic::ItemSlot::Flask) {
            render::appendDebugText(snapshot, selected->manaFlask ? "RESTORES MANA" : "RESTORES HEALTH",
                                    {detailsX, y}, ui * 0.8f, kWhite);
            y -= ui * 15.0f;
        }
        if (selected->slot == logic::ItemSlot::SkillGem) {
            render::appendDebugText(snapshot,
                                    selected->gemClass == game.player().classId
                                        ? "USE TO UNLOCK E SKILL"
                                        : "WRONG CLASS - SELL",
                                    {detailsX, y}, ui * 0.8f, kGold);
            y -= ui * 15.0f;
        }
        for (const logic::Modifier& mod : selected->mods) {
            render::appendDebugText(snapshot, mod.label, {detailsX, y}, ui * 0.75f, kWhite);
            y -= ui * 13.0f;
        }

        if (selected->slot != logic::ItemSlot::SkillGem &&
            selected->slot != logic::ItemSlot::Flask) {
            const logic::ItemComparison comparison = game.compareItem(*selected);
            auto delta = [&](const char* label, core::f32 value, bool percent = false) {
                if (std::fabs(value) < 0.01f) {
                    return;
                }
                char text[64];
                if (percent) {
                    std::snprintf(text, sizeof(text), "%s %+d%%", label,
                                  static_cast<int>(std::lround(value)));
                } else {
                    std::snprintf(text, sizeof(text), "%s %+d", label,
                                  static_cast<int>(std::lround(value)));
                }
                render::appendDebugText(snapshot, text, {detailsX, y}, ui * 0.75f,
                                        value > 0.0f ? core::Vec4{0.35f, 0.95f, 0.45f, 1.0f}
                                                     : core::Vec4{0.95f, 0.35f, 0.35f, 1.0f});
                y -= ui * 12.0f;
            };
            render::appendDebugText(snapshot, "COMPARED TO EQUIPPED", {detailsX, y}, ui * 0.65f,
                                    kDim);
            y -= ui * 12.0f;
            delta("DMG", comparison.damage);
            delta("ARMOUR", comparison.armour);
            delta("LIFE", comparison.life);
            delta("MANA", comparison.mana);
            delta("CRIT", comparison.critChance, true);
            delta("MOVE", comparison.moveSpeed, true);
            if (!comparison.equippable) {
                render::appendDebugText(snapshot, "REQUIRES A HIGHER LEVEL", {detailsX, y},
                                        ui * 0.7f, kEnemyHealth);
                y -= ui * 12.0f;
            }
        }

        y -= ui * 3.0f;
        render::appendDebugText(snapshot, "ENTER EQUIP / USE   X DROP", {detailsX, y}, ui * 0.7f,
                                kGold);
    }

    const logic::PlayerState& player = game.player(game.localViewerId());
    std::snprintf(line, sizeof(line), "DMG %d   ARMOUR %d   CRIT %d%%   MOVE %d%%",
                  static_cast<int>(std::lround(player.damage)),
                  static_cast<int>(std::lround(player.armour)),
                  static_cast<int>(std::lround(player.critChance)),
                  static_cast<int>(std::lround(
                      (player.moveSpeed / logic::classDef(player.classId).moveSpeed - 1.0f) * 100.0f)));
    render::appendDebugText(snapshot, line, {gridX, center.y - panelH * 0.5f + ui * 24.0f},
                            ui * 0.68f, kWhite);

    // Equipped gear.
    core::f32 equipY = gridTop - static_cast<core::f32>(columns / 8 * 3) * (cell + gap) - ui * 30.0f;
    render::appendDebugText(snapshot, "EQUIPPED", {gridX, equipY}, ui * 0.85f, kGold);
    equipY -= ui * 13.0f;
    const logic::ItemSlot slots[] = {logic::ItemSlot::Weapon, logic::ItemSlot::Armour,
                                     logic::ItemSlot::Helmet, logic::ItemSlot::Boots,
                                     logic::ItemSlot::Ring, logic::ItemSlot::Amulet};
    for (logic::ItemSlot slot : slots) {
        const logic::Item& item = game.equipped(slot);
        std::snprintf(line, sizeof(line), "%s: %s", logic::slotName(slot),
                      item.empty() ? "NONE" : item.name.c_str());
        render::appendDebugText(snapshot, line, {gridX, equipY}, ui * 0.62f,
                                item.empty() ? kDim : rarityColor(item.rarity));
        equipY -= ui * 11.0f;
    }

    render::appendDebugTextCentered(
        snapshot, "ARROWS/WASD SELECT   ENTER EQUIP/USE   I / ESC CLOSE", center.x,
        center.y - panelH * 0.5f + ui * 8.0f, ui * 0.8f, kDim);
}

void appendMenu(scene::SceneSnapshot& snapshot, const logic::RpgGame& game, core::f32 width,
                core::f32 height) {
    const core::f32 ui = std::max(2.0f, height / 300.0f);
    const core::Vec2 center{width * 0.5f, height * 0.5f};
    const core::f32 panelW = std::min(width - ui * 40.0f,
                                      game.classSelect() ? ui * 500.0f : ui * 380.0f);
    const core::f32 panelH = std::min(height - ui * 40.0f,
                                      game.classSelect() ? ui * 360.0f : ui * 270.0f);

    render::appendOverlayRect(snapshot, center, {width, height}, {0.0f, 0.0f, 0.0f, 0.62f});
    render::appendOverlayRect(snapshot, center, {panelW, panelH}, kPanel);
    appendBorder(snapshot, center, {panelW, panelH}, ui * 1.2f, kBorder);

    const core::f32 top = center.y + panelH * 0.5f - ui * 22.0f;
    const char* title = game.classSelect() ? "CHOOSE YOUR CLASS"
                        : game.dead()      ? "YOU DIED"
                        : game.victory()   ? "THE OATH IS KEPT"
                                           : "PAUSED";
    render::appendDebugTextCentered(snapshot, title, center.x, top, ui * 1.8f,
                                    game.dead() ? kEnemyHealth : kGold);

    char line[192];
    if (game.classSelect()) {
        core::f32 y = top - ui * 40.0f;
        for (core::usize i = 0; i < game.menuOptionCount(); ++i) {
            const bool selected = static_cast<core::i32>(i) == game.menuSelection();
            std::string text = selected ? "> " : "  ";
            text += std::string(game.menuOption(i));
            if (selected) {
                text += " <";
            }
            render::appendDebugTextCentered(snapshot, text, center.x, y, ui * 1.1f,
                                            selected ? kGold : kDim);
            y -= ui * 24.0f;
        }

        const logic::ClassDef& cls =
            logic::classDef(static_cast<logic::ClassId>(game.menuSelection()));
        y -= ui * 8.0f;
        std::snprintf(line, sizeof(line), "HP %d   MANA %d   DAMAGE %d   %s",
                      static_cast<int>(cls.health), static_cast<int>(cls.mana),
                      static_cast<int>(cls.damage), cls.ranged ? "RANGED" : "MELEE");
        render::appendDebugTextCentered(snapshot, line, center.x, y, ui * 0.85f, kDim);
        y -= ui * 22.0f;
        render::appendDebugTextCentered(snapshot, cls.description, center.x, y, ui * 0.8f,
                                        kWhite);
        render::appendDebugTextCentered(snapshot, "W/S SELECT   ENTER BEGIN", center.x,
                                        center.y - panelH * 0.5f + ui * 10.0f, ui * 0.8f,
                                        kDim);
        return;
    }

    const logic::PlayerState& player = game.player(game.localViewerId());
    core::f32 y = top - ui * 32.0f;
    std::snprintf(line, sizeof(line), "LEVEL %d   %s", player.loadout.level,
                  logic::classDef(player.classId).name);
    render::appendDebugTextCentered(snapshot, line, center.x, y, ui, kWhite);
    y -= ui * 16.0f;
    std::snprintf(line, sizeof(line), "ZONE: %s", game.zone().name);
    render::appendDebugTextCentered(snapshot, line, center.x, y, ui, kWhite);
    y -= ui * 16.0f;
    std::snprintf(line, sizeof(line), "HP %d/%d   MANA %d/%d   GOLD %d",
                  static_cast<int>(std::ceil(player.health)),
                  static_cast<int>(player.maxHealth), static_cast<int>(std::ceil(player.mana)),
                  static_cast<int>(player.maxMana), player.loadout.gold);
    render::appendDebugTextCentered(snapshot, line, center.x, y, ui * 0.85f, kDim);
    y -= ui * 28.0f;

    for (core::usize i = 0; i < game.menuOptionCount(); ++i) {
        const bool selected = static_cast<core::i32>(i) == game.menuSelection();
        std::string text = selected ? "> " : "  ";
        text += std::string(game.menuOption(i));
        if (selected) {
            text += " <";
        }
        render::appendDebugTextCentered(snapshot, text, center.x, y, ui, selected ? kGold : kDim);
        y -= ui * 20.0f;
    }

    const char* help = game.victory()
                           ? "W/S SELECT   ENTER CONFIRM"
                           : game.dead() ? "W/S SELECT   ENTER CONFIRM"
                                         : "W/S SELECT   ENTER CONFIRM   ESC RESUME";
    render::appendDebugTextCentered(snapshot, help, center.x,
                                    center.y - panelH * 0.5f + ui * 10.0f, ui * 0.8f, kDim);
}

void appendSettings(scene::SceneSnapshot& snapshot, const logic::RpgGame& game,
                    const GraphicsSettings& settings, const render::IRenderDevice& device,
                    core::i32 selectedRow, core::f32 width, core::f32 height) {
    (void)game;
    const core::f32 ui = std::max(2.0f, height / 300.0f);
    const core::Vec2 center{width * 0.5f, height * 0.5f};
    const core::f32 panelW = std::min(width - ui * 40.0f, ui * 440.0f);
    const core::f32 panelH = std::min(height - ui * 40.0f, ui * 300.0f);

    render::appendOverlayRect(snapshot, center, {width, height}, {0.0f, 0.0f, 0.0f, 0.66f});
    render::appendOverlayRect(snapshot, center, {panelW, panelH}, kPanel);
    appendBorder(snapshot, center, {panelW, panelH}, ui * 1.2f, kBorder);

    const core::f32 top = center.y + panelH * 0.5f;
    render::appendDebugTextCentered(snapshot, "GRAPHICS", center.x, top - ui * 22.0f, ui * 1.6f,
                                    kGold);

    char values[6][48] = {};
    std::snprintf(values[0], sizeof(values[0]), "%s", kAaNames[settings.aaIndex]);
    std::snprintf(values[1], sizeof(values[1]), "%s", kQualityNames[settings.qualityIndex]);
    std::snprintf(values[2], sizeof(values[2]), "%s", kFpsNames[settings.fpsIndex]);
    std::snprintf(values[3], sizeof(values[3]), "%s", settings.vsync ? "ON" : "OFF");
    std::snprintf(values[4], sizeof(values[4]), "%s", settings.invertY ? "ON" : "OFF");

    core::f32 y = top - ui * 62.0f;
    for (core::i32 i = 0; i < 6; ++i) {
        const bool selected = i == selectedRow;
        std::string label = selected ? "> " : "  ";
        label += kSettingsLabels[i];
        render::appendDebugText(snapshot, label, {center.x - panelW * 0.5f + ui * 24.0f, y}, ui,
                                selected ? kGold : kDim);

        if (i < 5) {
            std::string value = selected ? ("< " + std::string(values[i]) + " >")
                                         : std::string(values[i]);
            const core::f32 textW = render::debugTextWidth(value, ui);
            render::appendDebugText(snapshot, value,
                                    {center.x + panelW * 0.5f - ui * 24.0f - textW, y}, ui,
                                    selected ? kWhite : kDim);
        }
        y -= ui * 24.0f;
    }

    char footer[128];
    std::snprintf(footer, sizeof(footer), "MSAA MAX %d  |  UP/DOWN SELECT  LEFT/RIGHT CHANGE",
                  static_cast<int>(device.maxMultisampleSamples()));
    render::appendDebugTextCentered(snapshot, footer, center.x,
                                    center.y - panelH * 0.5f + ui * 26.0f, ui * 0.72f, kDim);
    render::appendDebugTextCentered(snapshot, "F2 / ESC CLOSE", center.x,
                                    center.y - panelH * 0.5f + ui * 10.0f, ui * 0.72f, kDim);
}

void appendCoopMenu(scene::SceneSnapshot& snapshot, const logic::RpgGame& game,
                    core::u64 sessionTick, bool desynced, bool disconnected,
                    core::i32 selection, core::f32 width, core::f32 height) {
    const core::f32 ui = std::max(2.0f, height / 300.0f);
    const core::Vec2 center{width * 0.5f, height * 0.5f};
    const core::f32 panelW = std::min(width - ui * 40.0f, ui * 380.0f);
    const core::f32 panelH = std::min(height - ui * 40.0f, ui * 220.0f);
    render::appendOverlayRect(snapshot, center, {width, height}, {0.0f, 0.0f, 0.0f, 0.55f});
    render::appendOverlayRect(snapshot, center, {panelW, panelH}, kPanel);
    appendBorder(snapshot, center, {panelW, panelH}, ui * 1.2f, kBorder);

    const char* title = desynced ? "DESYNC" : disconnected ? "PEER LEFT" : "CO-OP MENU";
    render::appendDebugTextCentered(snapshot, title, center.x,
                                    center.y + panelH * 0.5f - ui * 24.0f, ui * 1.6f,
                                    desynced ? kEnemyHealth : kGold);

    char line[192];
    const logic::PlayerState& local = game.player(game.localViewerId());
    const core::PlayerId allyId = game.localViewerId() == core::PlayerId::P0
                                      ? core::PlayerId::P1
                                      : core::PlayerId::P0;
    const logic::PlayerState& ally = game.player(allyId);
    std::snprintf(line, sizeof(line), "%s  HP %d/%d    TICK %llu",
                  logic::classDef(local.classId).name,
                  static_cast<int>(std::ceil(local.health)),
                  static_cast<int>(local.maxHealth),
                  static_cast<unsigned long long>(sessionTick));
    render::appendDebugTextCentered(snapshot, line, center.x,
                                    center.y + panelH * 0.5f - ui * 52.0f, ui * 0.8f, kWhite);
    std::snprintf(line, sizeof(line), "ALLY %s  HP %d/%d",
                  logic::classDef(ally.classId).name,
                  static_cast<int>(std::ceil(ally.health)),
                  static_cast<int>(ally.maxHealth));
    render::appendDebugTextCentered(snapshot, line, center.x,
                                    center.y + panelH * 0.5f - ui * 68.0f, ui * 0.8f, kDim);

    if (desynced || disconnected) {
        render::appendDebugTextCentered(snapshot, "> LEAVE <", center.x, center.y, ui, kGold);
    } else {
        const char* options[] = {"RESUME", "LEAVE"};
        core::f32 y = center.y + ui * 10.0f;
        for (core::i32 i = 0; i < 2; ++i) {
            const bool selected = i == selection;
            std::string text = selected ? "> " : "  ";
            text += options[i];
            if (selected) text += " <";
            render::appendDebugTextCentered(snapshot, text, center.x, y, ui,
                                            selected ? kGold : kDim);
            y -= ui * 22.0f;
        }
    }
    render::appendDebugTextCentered(snapshot, "ESC RESUME   ENTER CONFIRM", center.x,
                                    center.y - panelH * 0.5f + ui * 12.0f, ui * 0.75f, kDim);
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

    const bool headless = options.headless || options.backend == "null";

    eee::platform::WindowDesc windowDesc;
    windowDesc.title = "EEEsSA — low-poly ARPG";
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

    GraphicsSettings settings;
    settings.vsync = options.vsync;
    for (core::i32 i = 0; i < 4; ++i) {
        if (kAaValues[i] == options.aa) {
            settings.aaIndex = i;
        }
    }
    settings.qualityIndex = options.quality;
    for (core::i32 i = 0; i < 7; ++i) {
        if (kFpsValues[i] == options.fpsCap) {
            settings.fpsIndex = i;
        }
    }

    render::Renderer renderer(*device);
    if (!renderer.initialize()) {
        core::logError("renderer initialization failed");
        device->shutdown();
        return 1;
    }
    renderer.resize(framebufferWidth, framebufferHeight);
    applyGraphics(settings, *device, renderer);
    if (device->multisampleSamples() <= 1) {
        settings.aaIndex = 0;
    } else {
        for (core::i32 i = 0; i < 4; ++i) {
            if (kAaValues[i] == device->multisampleSamples()) {
                settings.aaIndex = i;
            }
        }
    }

    if (!headless) {
        platform::initializeAudio();
    }

    logic::ClassId localClass = logic::ClassId::Warrior;
    if (!parseClassId(options.startClass, localClass)) {
        core::logf(core::LogLevel::Error, "unknown class '%s'", options.startClass.c_str());
        platform::shutdownAudio();
        renderer.shutdown();
        device->shutdown();
        return 2;
    }

    logic::RpgGame game;
    const bool coop = options.host || options.join;
    std::unique_ptr<net::TcpTransport> coopNet;
    net::ConnectionId coopConnection = 0;
    std::unique_ptr<session::LockstepSession> coopSession;
    std::unique_ptr<session::AuthoritativeClient> authoritativeClient;
    bool authoritativeMode = false;
    core::PlayerId localPlayerId = core::PlayerId::P0;
    bool coopMenuOpen = false;
    core::i32 coopMenuSelection = 0;
    bool coopInventoryOpen = false;
    core::i32 coopInventorySelection = 0;
    bool coopDesync = false;

    auto fail = [&](int code) {
        platform::shutdownAudio();
        renderer.shutdown();
        device->shutdown();
        return code;
    };

    if (coop) {
        coopNet = std::make_unique<net::TcpTransport>();
        logic::ClassId remoteClass = logic::ClassId::Warrior;
        bool authoritativeServer = false;

        if (options.host) {
            const int port = std::atoi(options.hostPort.c_str());
            if (port <= 0 || port > 65535 || !coopNet->listen(static_cast<core::u16>(port))) {
                core::logError("co-op: failed to listen");
                return fail(1);
            }
            core::logf(core::LogLevel::Info, "co-op: hosting on port %d, waiting for peer...",
                       coopNet->localPort());
            auto connection = acceptWithTimeout(*coopNet, 30000);
            if (!connection.has_value()) {
                core::logError("co-op: no peer connected");
                return fail(1);
            }
            coopConnection = *connection;

            net::Message welcome;
            welcome.type = net::MessageType::Welcome;
            welcome.payload = net::Welcome{1, 1, 60};
            sendMessage(*coopNet, coopConnection, welcome);

            net::Message choice;
            choice.type = net::MessageType::ClassChoice;
            choice.payload = net::ClassChoice{static_cast<core::u8>(localClass)};
            sendMessage(*coopNet, coopConnection, choice);

            auto peerChoice = waitForMessage(*coopNet, coopConnection, 8000);
            if (!peerChoice.has_value() || peerChoice->type != net::MessageType::ClassChoice) {
                core::logError("co-op: peer did not send a class choice");
                return fail(1);
            }
            remoteClass = static_cast<logic::ClassId>(
                std::get<net::ClassChoice>(peerChoice->payload).classId);
            if (remoteClass != logic::ClassId::Warrior &&
                remoteClass != logic::ClassId::Archer && remoteClass != logic::ClassId::Mage) {
                remoteClass = logic::ClassId::Warrior;
            }

            net::Message startMessage;
            startMessage.type = net::MessageType::StartRun;
            startMessage.payload = net::StartRun{0xC0FFEEull, 0, 2, 0,
                                                 static_cast<core::u8>(localClass),
                                                 static_cast<core::u8>(remoteClass), false};
            if (!sendMessage(*coopNet, coopConnection, startMessage)) {
                return fail(1);
            }
            game.beginCoop(localClass, remoteClass);
            localPlayerId = core::PlayerId::P0;
            core::logf(core::LogLevel::Info,
                       "co-op: host started (host=%s client=%s)",
                       logic::classDef(localClass).name, logic::classDef(remoteClass).name);
        } else {
            std::string host = options.joinAddress;
            core::u16 port = 27015;
            const auto colon = host.find(':');
            if (colon != std::string::npos) {
                port = static_cast<core::u16>(std::atoi(host.substr(colon + 1).c_str()));
                host = host.substr(0, colon);
            }
            if (host.empty() || port == 0) {
                core::logError("co-op: invalid --join address");
                return fail(2);
            }

            std::optional<net::ConnectionId> connection;
            for (int attempt = 0; attempt < 100 && !connection.has_value(); ++attempt) {
                connection = coopNet->connect(host, port);
                if (!connection.has_value()) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }
            }
            if (!connection.has_value()) {
                core::logf(core::LogLevel::Error, "co-op: could not connect to %s:%u", host.c_str(),
                           static_cast<unsigned>(port));
                return fail(1);
            }
            coopConnection = *connection;

            net::Message choice;
            choice.type = net::MessageType::ClassChoice;
            choice.payload = net::ClassChoice{static_cast<core::u8>(localClass)};
            if (!sendMessage(*coopNet, coopConnection, choice)) {
                return fail(1);
            }

            std::optional<net::StartRun> startRun;
            core::u8 assignedPlayer = 1; // listen-host default
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
            while (!startRun.has_value() && std::chrono::steady_clock::now() < deadline) {
                auto message = waitForMessage(*coopNet, coopConnection, 500);
                if (!message.has_value()) {
                    continue;
                }
                if (message->type == net::MessageType::Welcome) {
                    assignedPlayer = std::get<net::Welcome>(message->payload).playerId;
                } else if (message->type == net::MessageType::StartRun) {
                    startRun = std::get<net::StartRun>(message->payload);
                    authoritativeServer = startRun->authoritative;
                }
            }
            if (!startRun.has_value()) {
                core::logError("co-op: host did not start the run");
                return fail(1);
            }
            game.beginCoop(static_cast<logic::ClassId>(startRun->hostClass),
                           static_cast<logic::ClassId>(startRun->clientClass));
            localPlayerId =
                assignedPlayer == 0 ? core::PlayerId::P0 : core::PlayerId::P1;
            core::logf(core::LogLevel::Info,
                       "co-op: joined (host=%s client=%s)",
                       logic::classDef(static_cast<logic::ClassId>(startRun->hostClass)).name,
                       logic::classDef(static_cast<logic::ClassId>(startRun->clientClass)).name);
        }

        game.setLocalViewerId(localPlayerId);
        game.setActivePlayers(2);
        authoritativeMode = options.join && authoritativeServer;
        if (authoritativeMode) {
            authoritativeClient = std::make_unique<session::AuthoritativeClient>(
                *coopNet, coopConnection, localPlayerId, 3);
            core::logInfo("co-op: authoritative server mode (snapshots)");
        } else {
            session::LockstepConfig config;
            config.localPlayer = localPlayerId;
            config.activePlayers = 2;
            config.inputDelay = 3;
            config.authoritative = false;
            coopSession = std::make_unique<session::LockstepSession>(*coopNet, coopConnection, config);
        }
    } else if (!options.startClass.empty()) {
        game.begin(localClass);
    }

    eee::core::InputState input;
    using Clock = std::chrono::steady_clock;
    auto previous = Clock::now();
    const auto start = previous;

    session::SessionDriver sessionDriver;

    core::i64 renderedFrames = 0;
    bool fpsVisible = options.showFps;
    core::f32 smoothedFrameTime = 0.0f;
    bool settingsOpen = false;
    core::i32 settingsRow = 0;
    bool mouseCaptured = false;

    while (!window->shouldClose()) {
        const auto now = Clock::now();
        const float dt = std::chrono::duration<float>(now - previous).count();
        previous = now;

        if (!window->pollEvents(input)) {
            break;
        }

        const auto frameStart = now;

        if (input.wasPressed(core::Key::F1) || input.wasPressed(core::Key::Backquote)) {
            fpsVisible = !fpsVisible;
        }
        if (!coop && (input.wasPressed(core::Key::F2) || input.wasPressed('G'))) {
            settingsOpen = !settingsOpen;
            settingsRow = 0;
            platform::playSound(core::SoundId::MenuSelect);
        }

        core::i32 width = framebufferWidth;
        core::i32 height = framebufferHeight;
        window->getFramebufferSize(width, height);
        if (width != framebufferWidth || height != framebufferHeight) {
            framebufferWidth = width;
            framebufferHeight = height;
            renderer.resize(framebufferWidth, framebufferHeight);
        }

        input.viewportSize = {static_cast<core::f32>(framebufferWidth),
                              static_cast<core::f32>(framebufferHeight)};
        if (headless || (input.mousePosition.x == 0.0f && input.mousePosition.y == 0.0f)) {
            input.mousePosition = {static_cast<core::f32>(framebufferWidth) * 0.5f,
                                   static_cast<core::f32>(framebufferHeight) * 0.5f};
        }

        if (settingsOpen) {
            if (input.wasPressed(core::Key::Escape)) {
                settingsOpen = false;
                platform::playSound(core::SoundId::MenuSelect);
            } else {
                if (input.wasPressed(core::Key::Up) || input.wasPressed('W')) {
                    settingsRow = (settingsRow + 5) % 6;
                    platform::playSound(core::SoundId::MenuMove);
                }
                if (input.wasPressed(core::Key::Down) || input.wasPressed('S')) {
                    settingsRow = (settingsRow + 1) % 6;
                    platform::playSound(core::SoundId::MenuMove);
                }

                core::i32 delta = 0;
                if (input.wasPressed(core::Key::Left) || input.wasPressed('A')) {
                    delta = -1;
                }
                if (input.wasPressed(core::Key::Right) || input.wasPressed('D')) {
                    delta = 1;
                }
                if (delta != 0) {
                    switch (settingsRow) {
                    case 0:
                        settings.aaIndex = std::clamp(settings.aaIndex + delta, 0, 3);
                        applyGraphics(settings, *device, renderer);
                        if (device->multisampleSamples() <= 1) {
                            settings.aaIndex = 0;
                        } else {
                            for (core::i32 i = 0; i < 4; ++i) {
                                if (kAaValues[i] == device->multisampleSamples()) {
                                    settings.aaIndex = i;
                                }
                            }
                        }
                        break;
                    case 1:
                        settings.qualityIndex =
                            std::clamp(settings.qualityIndex + delta, 0, 3);
                        renderer.setQuality(
                            static_cast<render::QualityLevel>(settings.qualityIndex));
                        break;
                    case 2:
                        settings.fpsIndex = (settings.fpsIndex + 7 + delta) % 7;
                        break;
                    case 3:
                        settings.vsync = !settings.vsync;
                        device->setVSync(settings.vsync);
                        break;
                    case 4:
                        settings.invertY = !settings.invertY;
                        break;
                    default:
                        break;
                    }
                    platform::playSound(core::SoundId::MenuSelect);
                }

                if (input.wasPressed(core::Key::Enter) && settingsRow == 5) {
                    settingsOpen = false;
                    platform::playSound(core::SoundId::MenuSelect);
                }
            }
        } else if (coop) {
            if (input.wasPressed('V')) {
                settings.vsync = !settings.vsync;
                device->setVSync(settings.vsync);
            }

            bool leave = false;
            if (coopInventoryOpen) {
                const core::i32 count =
                    static_cast<core::i32>(game.inventory().size());
                if (input.wasPressed(core::Key::Escape) || input.wasPressed('I')) {
                    coopInventoryOpen = false;
                } else if (authoritativeMode && count > 0) {
                    if (input.wasPressed(core::Key::Left) || input.wasPressed('A')) {
                        coopInventorySelection = (coopInventorySelection + count - 1) % count;
                    }
                    if (input.wasPressed(core::Key::Right) || input.wasPressed('D')) {
                        coopInventorySelection = (coopInventorySelection + 1) % count;
                    }
                    if (input.wasPressed(core::Key::Up) || input.wasPressed('W')) {
                        coopInventorySelection =
                            (coopInventorySelection - 6 + count) % count;
                    }
                    if (input.wasPressed(core::Key::Down) || input.wasPressed('S')) {
                        coopInventorySelection = (coopInventorySelection + 6) % count;
                    }
                    coopInventorySelection = core::clamp(coopInventorySelection, 0, count - 1);
                    if (input.wasPressed(core::Key::Enter)) {
                        authoritativeClient->sendAction(0, static_cast<core::u16>(coopInventorySelection));
                    }
                    if (input.wasPressed('X')) {
                        authoritativeClient->sendAction(1, static_cast<core::u16>(coopInventorySelection));
                    }
                }
                if (authoritativeClient && authoritativeClient->disconnected()) {
                    coopInventoryOpen = false;
                    coopMenuOpen = true;
                }
            } else {
                if (input.wasPressed(core::Key::Escape)) {
                    coopMenuOpen = !coopMenuOpen;
                    coopMenuSelection = 0;
                }
                if (authoritativeMode && !coopMenuOpen && input.wasPressed('I')) {
                    coopInventoryOpen = true;
                    coopInventorySelection = 0;
                }
            }
            if (coopMenuOpen) {
                const bool failed = coopDesync ||
                                   (authoritativeMode ? authoritativeClient->disconnected()
                                                     : coopSession->disconnected());
                if (!failed) {
                    if (input.wasPressed(core::Key::Up) || input.wasPressed('W')) {
                        coopMenuSelection = (coopMenuSelection + 1) % 2;
                    }
                    if (input.wasPressed(core::Key::Down) || input.wasPressed('S')) {
                        coopMenuSelection = (coopMenuSelection + 1) % 2;
                    }
                    if (input.wasPressed(core::Key::Enter)) {
                        if (coopMenuSelection == 0) {
                            coopMenuOpen = false;
                        } else {
                            leave = true;
                        }
                    }
                } else if (input.wasPressed(core::Key::Enter)) {
                    leave = true;
                }
            }
            if (leave) {
                break;
            }

            const logic::PlayerState& local = game.player(localPlayerId);
            core::PlayerInput command;
            command.cameraYaw = local.camera.yaw;
            command.cameraPitch = local.camera.pitch;
            command.aimYaw = local.camera.yaw;
            if (!coopMenuOpen && !coopInventoryOpen) {
                command.moveX = (input.isDown('D') ? 1.0f : 0.0f) -
                                (input.isDown('A') ? 1.0f : 0.0f);
                command.moveZ = (input.isDown('W') ? 1.0f : 0.0f) -
                                (input.isDown('S') ? 1.0f : 0.0f);
                command.jump = input.wasPressed(core::Key::Space);
                command.attack = input.mouseDown[0];
                command.interact = input.mousePressed[1];
                command.skillQ = input.wasPressed('Q');
                command.skillE = input.wasPressed('E');
                command.sprint = input.isDown(core::Key::LeftShift) ||
                                 input.isDown(core::Key::RightShift);
            }
            if (authoritativeMode) {
                authoritativeClient->poll(game);
                authoritativeClient->sendInput(command);
                authoritativeClient->updatePrediction(game, command, dt);
                authoritativeClient->updateInterpolation(game, dt);
                if (authoritativeClient->disconnected()) {
                    core::logWarning("co-op: authoritative server disconnected");
                    coopMenuOpen = true;
                }
            } else {
                coopSession->advance(game, command, dt);
                if (coopSession->desynced()) {
                    coopDesync = true;
                    coopMenuOpen = true;
                }
                if (coopSession->disconnected()) {
                    core::logWarning("co-op: peer disconnected");
                    coopMenuOpen = true;
                }
            }
            for (const core::SoundId sound : game.takeSoundEvents()) {
                platform::playSound(sound);
            }
            if (!coopMenuOpen && !coopInventoryOpen) {
                game.applyLocalCameraInput(dt, input, localPlayerId);
            }
        } else {
            if (input.wasPressed('V')) {
                settings.vsync = !settings.vsync;
                device->setVSync(settings.vsync);
            }
            if (settings.invertY) {
                input.mouseDelta.y = -input.mouseDelta.y;
            }

            sessionDriver.advance(game, input, dt);
            for (const core::SoundId sound : game.takeSoundEvents()) {
                platform::playSound(sound);
            }
            if (game.takeGraphicsRequest()) {
                settingsOpen = true;
                settingsRow = 0;
            }
            if (game.quitRequested()) {
                break;
            }
        }

        const bool wantCapture = !headless && !settingsOpen && !coopMenuOpen &&
                                 !coopInventoryOpen &&
                                 game.uiMode() == logic::UiMode::Playing;
        if (wantCapture != mouseCaptured) {
            window->setMouseCapture(wantCapture);
            mouseCaptured = wantCapture;
        }

        scene::SceneSnapshot snapshot = game.buildSnapshot(game.localViewerId());

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
                          settings.vsync ? "ON" : "OFF",
                          static_cast<unsigned>(renderer.lastMeshCount()));
            const core::f32 widthPixels = render::debugTextWidth(text, pixelSize);
            render::appendDebugText(
                snapshot, text,
                {static_cast<core::f32>(framebufferWidth) - margin - widthPixels,
                 static_cast<core::f32>(framebufferHeight) - margin},
                pixelSize, core::Vec4::rgb(0.98f, 0.95f, 0.55f));
        }

        if (game.classSelect()) {
            appendMenu(snapshot, game, static_cast<core::f32>(framebufferWidth),
                       static_cast<core::f32>(framebufferHeight));
        } else {
            appendHud(snapshot, game, static_cast<core::f32>(framebufferWidth),
                      static_cast<core::f32>(framebufferHeight));
            if (game.inventoryOpen() || coopInventoryOpen) {
                if (coopInventoryOpen) {
                    const core::i32 count = static_cast<core::i32>(game.inventory().size());
                    coopInventorySelection = count <= 0
                                                 ? 0
                                                 : core::clamp(coopInventorySelection, 0,
                                                               count - 1);
                    game.setInventorySelection(coopInventorySelection);
                }
                appendInventory(snapshot, game, static_cast<core::f32>(framebufferWidth),
                                static_cast<core::f32>(framebufferHeight));
            } else if (game.paused() || game.dead() || game.victory()) {
                appendMenu(snapshot, game, static_cast<core::f32>(framebufferWidth),
                           static_cast<core::f32>(framebufferHeight));
            }
        }

        if (settingsOpen) {
            appendSettings(snapshot, game, settings, *device, settingsRow,
                           static_cast<core::f32>(framebufferWidth),
                           static_cast<core::f32>(framebufferHeight));
        }
        if (coop && (coopMenuOpen || coopDesync ||
                     (authoritativeMode ? authoritativeClient->disconnected()
                                        : coopSession->disconnected()))) {
            const core::u64 sessionTick = authoritativeMode ? authoritativeClient->serverTick()
                                                            : coopSession->tickCount();
            const bool sessionDisconnected =
                authoritativeMode ? authoritativeClient->disconnected()
                                  : coopSession->disconnected();
            appendCoopMenu(snapshot, game, sessionTick, coopDesync, sessionDisconnected,
                           coopMenuSelection,
                           static_cast<core::f32>(framebufferWidth),
                           static_cast<core::f32>(framebufferHeight));
        }

        renderer.renderFrame(snapshot);

        input.endFrame();
        ++renderedFrames;

        if (settings.fpsIndex < 6) {
            const auto target = std::chrono::duration<double>(1.0 / kFpsValues[settings.fpsIndex]);
            const auto elapsed = Clock::now() - frameStart;
            if (elapsed < target) {
                std::this_thread::sleep_for(target - elapsed);
            }
        }

        if (options.frames > 0 && renderedFrames >= options.frames) {
            break;
        }
    }

    if (mouseCaptured) {
        window->setMouseCapture(false);
    }

    const float seconds = std::chrono::duration<float>(Clock::now() - start).count();
    core::logf(core::LogLevel::Info,
               "rendered %lld frames in %.2fs (%.1f fps) using '%s', %u meshes + %u overlay quads",
               static_cast<long long>(renderedFrames), seconds,
               seconds > 0.0f ? static_cast<double>(renderedFrames) / seconds : 0.0,
               device->name(), renderer.lastMeshCount(), renderer.lastOverlayCount());
    if (coop) {
        if (authoritativeMode && authoritativeClient) {
            core::logf(core::LogLevel::Info, "co-op: authoritative server tick %llu, %u snapshots",
                       static_cast<unsigned long long>(authoritativeClient->serverTick()),
                       authoritativeClient->snapshotsApplied());
        } else if (coopSession) {
            core::logf(coopDesync ? core::LogLevel::Error : core::LogLevel::Info,
                       "co-op: %llu ticks, %s",
                       static_cast<unsigned long long>(coopSession->tickCount()),
                       coopDesync ? "DESYNC" : "clean");
        }
    }

    platform::shutdownAudio();
    renderer.shutdown();
    device->shutdown();
    return coopDesync ? 1 : 0;
}
