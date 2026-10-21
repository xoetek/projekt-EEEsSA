#pragma once

#include "eee/core/Types.hpp"

#include <cmath>

namespace eee::core {

// ---------------------------------------------------------------------------
// Dependency-free math. Mat4 is COLUMN-MAJOR, matching GLSL, MSL and Vulkan
// conventions, so matrices upload without transposition.
// ---------------------------------------------------------------------------

struct Vec2 {
    f32 x = 0.0f;
    f32 y = 0.0f;

    constexpr Vec2() = default;
    constexpr Vec2(f32 x_, f32 y_) : x(x_), y(y_) {}

    constexpr Vec2 operator+(const Vec2& o) const { return {x + o.x, y + o.y}; }
    constexpr Vec2 operator-(const Vec2& o) const { return {x - o.x, y - o.y}; }
    constexpr Vec2 operator*(f32 s) const { return {x * s, y * s}; }
    constexpr Vec2& operator+=(const Vec2& o) { x += o.x; y += o.y; return *this; }
    constexpr Vec2& operator-=(const Vec2& o) { x -= o.x; y -= o.y; return *this; }
    [[nodiscard]] constexpr f32 lengthSquared() const { return x * x + y * y; }
    [[nodiscard]] f32 length() const { return std::sqrt(lengthSquared()); }
};

struct Vec3 {
    f32 x = 0.0f;
    f32 y = 0.0f;
    f32 z = 0.0f;

    constexpr Vec3() = default;
    constexpr Vec3(f32 x_, f32 y_, f32 z_) : x(x_), y(y_), z(z_) {}

    constexpr Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    constexpr Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    constexpr Vec3 operator*(f32 s) const { return {x * s, y * s, z * s}; }
    constexpr Vec3 operator-() const { return {-x, -y, -z}; }
    constexpr Vec3& operator+=(const Vec3& o) { x += o.x; y += o.y; z += o.z; return *this; }
    constexpr Vec3& operator-=(const Vec3& o) { x -= o.x; y -= o.y; z -= o.z; return *this; }
    [[nodiscard]] constexpr f32 lengthSquared() const { return x * x + y * y + z * z; }
    [[nodiscard]] f32 length() const { return std::sqrt(lengthSquared()); }
};

inline constexpr f32 dot(const Vec3& a, const Vec3& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

inline constexpr Vec3 cross(const Vec3& a, const Vec3& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

inline Vec3 normalize(const Vec3& v) {
    const f32 length = v.length();
    return length > 1e-8f ? v * (1.0f / length) : Vec3{0.0f, 0.0f, 0.0f};
}

struct Vec4 {
    f32 x = 0.0f;
    f32 y = 0.0f;
    f32 z = 0.0f;
    f32 w = 0.0f;

    constexpr Vec4() = default;
    constexpr Vec4(f32 x_, f32 y_, f32 z_, f32 w_) : x(x_), y(y_), z(z_), w(w_) {}

    static constexpr Vec4 rgb(f32 r, f32 g, f32 b) { return {r, g, b, 1.0f}; }

    constexpr Vec4 operator*(f32 s) const { return {x * s, y * s, z * s, w * s}; }
};

struct Mat4 {
    // Column-major: m[column * 4 + row].
    f32 m[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

    static constexpr Mat4 identity() { return Mat4{}; }

    [[nodiscard]] static Mat4 translation(Vec3 t) {
        Mat4 r;
        r.m[12] = t.x;
        r.m[13] = t.y;
        r.m[14] = t.z;
        return r;
    }

    [[nodiscard]] static Mat4 translation(Vec2 t) { return translation({t.x, t.y, 0.0f}); }

    [[nodiscard]] static Mat4 scaling(Vec3 s) {
        Mat4 r;
        r.m[0] = s.x;
        r.m[5] = s.y;
        r.m[10] = s.z;
        return r;
    }

    [[nodiscard]] static Mat4 scaling(Vec2 s) { return scaling({s.x, s.y, 1.0f}); }

    [[nodiscard]] static Mat4 rotationX(f32 radians) {
        const f32 c = std::cos(radians);
        const f32 s = std::sin(radians);
        Mat4 r;
        r.m[5] = c;
        r.m[6] = s;
        r.m[9] = -s;
        r.m[10] = c;
        return r;
    }

    [[nodiscard]] static Mat4 rotationY(f32 radians) {
        const f32 c = std::cos(radians);
        const f32 s = std::sin(radians);
        Mat4 r;
        r.m[0] = c;
        r.m[2] = -s;
        r.m[8] = s;
        r.m[10] = c;
        return r;
    }

    [[nodiscard]] static Mat4 rotationZ(f32 radians) {
        const f32 c = std::cos(radians);
        const f32 s = std::sin(radians);
        Mat4 r;
        r.m[0] = c;
        r.m[1] = s;
        r.m[4] = -s;
        r.m[5] = c;
        return r;
    }

    // Right-handed perspective projection, depth mapped to [-1, 1].
    [[nodiscard]] static Mat4 perspective(f32 fovYRadians, f32 aspect, f32 nearZ, f32 farZ) {
        const f32 f = 1.0f / std::tan(fovYRadians * 0.5f);
        Mat4 r;
        for (int i = 0; i < 16; ++i) {
            r.m[i] = 0.0f;
        }
        r.m[0] = f / (aspect > 0.0f ? aspect : 1.0f);
        r.m[5] = f;
        r.m[10] = (farZ + nearZ) / (nearZ - farZ);
        r.m[11] = -1.0f;
        r.m[14] = (2.0f * farZ * nearZ) / (nearZ - farZ);
        return r;
    }

    // Right-handed look-at view matrix.
    [[nodiscard]] static Mat4 lookAt(Vec3 eye, Vec3 target, Vec3 up) {
        const Vec3 forward = normalize(target - eye);
        const Vec3 right = normalize(cross(forward, up));
        const Vec3 trueUp = cross(right, forward);

        Mat4 r;
        r.m[0] = right.x;
        r.m[1] = trueUp.x;
        r.m[2] = -forward.x;
        r.m[3] = 0.0f;
        r.m[4] = right.y;
        r.m[5] = trueUp.y;
        r.m[6] = -forward.y;
        r.m[7] = 0.0f;
        r.m[8] = right.z;
        r.m[9] = trueUp.z;
        r.m[10] = -forward.z;
        r.m[11] = 0.0f;
        r.m[12] = -dot(right, eye);
        r.m[13] = -dot(trueUp, eye);
        r.m[14] = dot(forward, eye);
        r.m[15] = 1.0f;
        return r;
    }

    // Orthographic projection, depth mapped to [-1, 1].
    [[nodiscard]] static Mat4 orthographic(f32 left, f32 right, f32 bottom, f32 top,
                                           f32 nearZ, f32 farZ) {
        Mat4 r;
        r.m[0] = 2.0f / (right - left);
        r.m[5] = 2.0f / (top - bottom);
        r.m[10] = -2.0f / (farZ - nearZ);
        r.m[12] = -(right + left) / (right - left);
        r.m[13] = -(top + bottom) / (top - bottom);
        r.m[14] = -(farZ + nearZ) / (farZ - nearZ);
        return r;
    }

    // 3D model transform: T * Rz * Ry * Rx * S.
    [[nodiscard]] static Mat4 trs3(Vec3 position, Vec3 eulerRadians, Vec3 scale) {
        return translation(position) * rotationZ(eulerRadians.z) * rotationY(eulerRadians.y) *
               rotationX(eulerRadians.x) * scaling(scale);
    }

    // 2D convenience used by the debug-text overlay.
    [[nodiscard]] static Mat4 trs(Vec2 position, f32 rotationRadians, Vec2 scale) {
        return translation(position) * rotationZ(rotationRadians) * scaling(scale);
    }

    [[nodiscard]] Mat4 operator*(const Mat4& o) const {
        Mat4 r;
        for (int col = 0; col < 4; ++col) {
            for (int row = 0; row < 4; ++row) {
                f32 sum = 0.0f;
                for (int k = 0; k < 4; ++k) {
                    sum += m[k * 4 + row] * o.m[col * 4 + k];
                }
                r.m[col * 4 + row] = sum;
            }
        }
        return r;
    }
};

[[nodiscard]] inline Vec3 transformPoint(const Mat4& matrix, const Vec3& point) {
    const f32 x = matrix.m[0] * point.x + matrix.m[4] * point.y + matrix.m[8] * point.z + matrix.m[12];
    const f32 y = matrix.m[1] * point.x + matrix.m[5] * point.y + matrix.m[9] * point.z + matrix.m[13];
    const f32 z = matrix.m[2] * point.x + matrix.m[6] * point.y + matrix.m[10] * point.z + matrix.m[14];
    const f32 w = matrix.m[3] * point.x + matrix.m[7] * point.y + matrix.m[11] * point.z + matrix.m[15];
    return w != 0.0f ? Vec3{x / w, y / w, z / w} : Vec3{x, y, z};
}

[[nodiscard]] inline f32 clamp(f32 value, f32 lo, f32 hi) {
    return value < lo ? lo : (value > hi ? hi : value);
}

} // namespace eee::core
