#include "eee/render/DebugText.hpp"

#include <array>

namespace eee::render {
namespace {

// 5x7 bitmap font. Each row uses 5 bits, MSB = leftmost column.
struct Glyph {
    char code;
    std::array<core::u8, 7> rows;
};

constexpr Glyph kGlyphs[] = {
    {' ', {0, 0, 0, 0, 0, 0, 0}},
    {'%', {0b11001, 0b11010, 0b00010, 0b00100, 0b01000, 0b01011, 0b10011}},
    {'(', {0b00010, 0b00100, 0b01000, 0b01000, 0b01000, 0b00100, 0b00010}},
    {')', {0b01000, 0b00100, 0b00010, 0b00010, 0b00010, 0b00100, 0b01000}},
    {'+', {0, 0b00100, 0b00100, 0b11111, 0b00100, 0b00100, 0}},
    {'-', {0, 0, 0, 0b11111, 0, 0, 0}},
    {'.', {0, 0, 0, 0, 0, 0b00110, 0b00110}},
    {'/', {0b00001, 0b00010, 0b00010, 0b00100, 0b01000, 0b01000, 0b10000}},
    {':', {0, 0b00110, 0b00110, 0, 0b00110, 0b00110, 0}},
    {'?', {0b01110, 0b10001, 0b00001, 0b00010, 0b00100, 0, 0b00100}},
    {'_', {0, 0, 0, 0, 0, 0, 0b11111}},
    {'[', {0b01110, 0b01000, 0b01000, 0b01000, 0b01000, 0b01000, 0b01110}},
    {']', {0b01110, 0b00010, 0b00010, 0b00010, 0b00010, 0b00010, 0b01110}},

    {'0', {0b01110, 0b10001, 0b10011, 0b10101, 0b11001, 0b10001, 0b01110}},
    {'1', {0b00100, 0b01100, 0b00100, 0b00100, 0b00100, 0b00100, 0b01110}},
    {'2', {0b01110, 0b10001, 0b00001, 0b00010, 0b00100, 0b01000, 0b11111}},
    {'3', {0b11111, 0b00010, 0b00100, 0b00010, 0b00001, 0b10001, 0b01110}},
    {'4', {0b00010, 0b00110, 0b01010, 0b10010, 0b11111, 0b00010, 0b00010}},
    {'5', {0b11111, 0b10000, 0b11110, 0b00001, 0b00001, 0b10001, 0b01110}},
    {'6', {0b00110, 0b01000, 0b10000, 0b11110, 0b10001, 0b10001, 0b01110}},
    {'7', {0b11111, 0b00001, 0b00010, 0b00100, 0b01000, 0b01000, 0b01000}},
    {'8', {0b01110, 0b10001, 0b10001, 0b01110, 0b10001, 0b10001, 0b01110}},
    {'9', {0b01110, 0b10001, 0b10001, 0b01111, 0b00001, 0b00010, 0b01100}},

    {'A', {0b01110, 0b10001, 0b10001, 0b11111, 0b10001, 0b10001, 0b10001}},
    {'B', {0b11110, 0b10001, 0b10001, 0b11110, 0b10001, 0b10001, 0b11110}},
    {'C', {0b01110, 0b10001, 0b10000, 0b10000, 0b10000, 0b10001, 0b01110}},
    {'D', {0b11110, 0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b11110}},
    {'E', {0b11111, 0b10000, 0b10000, 0b11110, 0b10000, 0b10000, 0b11111}},
    {'F', {0b11111, 0b10000, 0b10000, 0b11110, 0b10000, 0b10000, 0b10000}},
    {'G', {0b01110, 0b10001, 0b10000, 0b10111, 0b10001, 0b10001, 0b01111}},
    {'H', {0b10001, 0b10001, 0b10001, 0b11111, 0b10001, 0b10001, 0b10001}},
    {'I', {0b01110, 0b00100, 0b00100, 0b00100, 0b00100, 0b00100, 0b01110}},
    {'J', {0b00111, 0b00010, 0b00010, 0b00010, 0b00010, 0b10010, 0b01100}},
    {'K', {0b10001, 0b10010, 0b10100, 0b11000, 0b10100, 0b10010, 0b10001}},
    {'L', {0b10000, 0b10000, 0b10000, 0b10000, 0b10000, 0b10000, 0b11111}},
    {'M', {0b10001, 0b11011, 0b10101, 0b10101, 0b10001, 0b10001, 0b10001}},
    {'N', {0b10001, 0b11001, 0b10101, 0b10011, 0b10001, 0b10001, 0b10001}},
    {'O', {0b01110, 0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b01110}},
    {'P', {0b11110, 0b10001, 0b10001, 0b11110, 0b10000, 0b10000, 0b10000}},
    {'Q', {0b01110, 0b10001, 0b10001, 0b10001, 0b10101, 0b10010, 0b01101}},
    {'R', {0b11110, 0b10001, 0b10001, 0b11110, 0b10100, 0b10010, 0b10001}},
    {'S', {0b01111, 0b10000, 0b10000, 0b01110, 0b00001, 0b00001, 0b11110}},
    {'T', {0b11111, 0b00100, 0b00100, 0b00100, 0b00100, 0b00100, 0b00100}},
    {'U', {0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b01110}},
    {'V', {0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b01010, 0b00100}},
    {'W', {0b10001, 0b10001, 0b10001, 0b10101, 0b10101, 0b11011, 0b10001}},
    {'X', {0b10001, 0b10001, 0b01010, 0b00100, 0b01010, 0b10001, 0b10001}},
    {'Y', {0b10001, 0b10001, 0b01010, 0b00100, 0b00100, 0b00100, 0b00100}},
    {'Z', {0b11111, 0b00001, 0b00010, 0b00100, 0b01000, 0b10000, 0b11111}},
};

constexpr core::f32 kAdvance = 6.0f;    // glyph width + 1px spacing
constexpr core::f32 kLineHeight = 9.0f; // glyph height + 2px leading

const Glyph* findGlyph(char code) {
    if (code >= 'a' && code <= 'z') {
        code = static_cast<char>(code - ('a' - 'A'));
    }
    for (const Glyph& glyph : kGlyphs) {
        if (glyph.code == code) {
            return &glyph;
        }
    }
    return nullptr;
}

} // namespace

void appendDebugText(scene::SceneSnapshot& snapshot, std::string_view text,
                     core::Vec2 topLeft, core::f32 pixelSize, core::Vec4 color) {
    if (pixelSize <= 0.0f) {
        return;
    }

    const core::f32 startX = topLeft.x;
    core::f32 penX = topLeft.x;
    core::f32 penY = topLeft.y;

    for (const char code : text) {
        if (code == '\n') {
            penX = startX;
            penY -= pixelSize * kLineHeight;
            continue;
        }

        const Glyph* glyph = findGlyph(code);
        if (glyph == nullptr) {
            penX += pixelSize * kAdvance;
            continue;
        }

        for (int row = 0; row < 7; ++row) {
            const core::u8 bits = glyph->rows[static_cast<core::usize>(row)];
            for (int col = 0; col < 5; ++col) {
                if ((bits & (0x10u >> col)) == 0) {
                    continue;
                }
                scene::OverlaySprite sprite;
                sprite.transform = core::Mat4::trs(
                    {penX + pixelSize * (static_cast<core::f32>(col) + 0.5f),
                     penY - pixelSize * (static_cast<core::f32>(row) + 0.5f)},
                    0.0f, {pixelSize, pixelSize});
                sprite.color = color;
                snapshot.overlay.push_back(sprite);
            }
        }

        penX += pixelSize * kAdvance;
    }
}

void appendOverlayRect(scene::SceneSnapshot& snapshot, core::Vec2 center, core::Vec2 size,
                       core::Vec4 color) {
    if (size.x <= 0.0f || size.y <= 0.0f) {
        return;
    }
    scene::OverlaySprite sprite;
    sprite.transform = core::Mat4::trs(center, 0.0f, size);
    sprite.color = color;
    snapshot.overlay.push_back(sprite);
}

core::f32 debugTextWidth(std::string_view text, core::f32 pixelSize) {
    core::f32 widest = 0.0f;
    core::usize lineLength = 0;
    for (const char code : text) {
        if (code == '\n') {
            const core::f32 width =
                lineLength > 0 ? (static_cast<core::f32>(lineLength) * kAdvance - 1.0f) * pixelSize
                               : 0.0f;
            widest = width > widest ? width : widest;
            lineLength = 0;
        } else {
            ++lineLength;
        }
    }
    const core::f32 width =
        lineLength > 0 ? (static_cast<core::f32>(lineLength) * kAdvance - 1.0f) * pixelSize : 0.0f;
    return width > widest ? width : widest;
}

void appendDebugTextCentered(scene::SceneSnapshot& snapshot, std::string_view text,
                             core::f32 centerX, core::f32 topY, core::f32 pixelSize,
                             core::Vec4 color) {
    appendDebugText(snapshot, text, {centerX - debugTextWidth(text, pixelSize) * 0.5f, topY},
                    pixelSize, color);
}

} // namespace eee::render
