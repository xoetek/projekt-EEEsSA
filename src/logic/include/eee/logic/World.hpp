#pragma once

#include "eee/core/Math.hpp"
#include "eee/core/Types.hpp"

#include <utility>
#include <vector>

namespace eee::logic {

using Entity = core::u32;
inline constexpr Entity kInvalidEntity = 0;

struct Transform {
    core::Vec3 position{};
    core::Vec3 rotation{}; // Euler XYZ, radians
    core::Vec3 scale{1.0f, 1.0f, 1.0f};
};

struct Motion {
    core::Vec3 velocity{};
    core::Vec3 angularVelocity{};

    // Optional procedural orbit around the world Y axis.
    bool orbiting = false;
    core::f32 orbitRadius = 0.0f;
    core::f32 orbitHeight = 0.0f;
    core::f32 orbitTilt = 0.0f;
    core::f32 orbitSpeed = 0.0f;
    core::f32 orbitAngle = 0.0f;
};

struct Visual {
    core::Vec4 color{1.0f, 1.0f, 1.0f, 1.0f};
};

// Minimal entity/component store. Entities are stable ids (index + 1), records
// are kept in a dense array with a free list so iteration stays cache friendly.
class World {
public:
    Entity create();
    void destroy(Entity entity);
    [[nodiscard]] bool alive(Entity entity) const;

    [[nodiscard]] Transform* transform(Entity entity);
    [[nodiscard]] Motion* motion(Entity entity);
    [[nodiscard]] Visual* visual(Entity entity);

    [[nodiscard]] const Transform* transform(Entity entity) const;
    [[nodiscard]] const Motion* motion(Entity entity) const;
    [[nodiscard]] const Visual* visual(Entity entity) const;

    [[nodiscard]] core::usize entityCount() const { return aliveCount_; }

    // Visits every live entity. `fn(Entity, Transform&/const Transform&, Motion*, Visual*)`.
    template <typename Fn>
    void forEach(Fn&& fn) {
        for (core::usize i = 0; i < records_.size(); ++i) {
            if (!records_[i].alive) {
                continue;
            }
            fn(static_cast<Entity>(i + 1), records_[i].transform, &records_[i].motion,
               &records_[i].visual);
        }
    }

    template <typename Fn>
    void forEach(Fn&& fn) const {
        for (core::usize i = 0; i < records_.size(); ++i) {
            if (!records_[i].alive) {
                continue;
            }
            fn(static_cast<Entity>(i + 1), std::as_const(records_[i].transform),
               &records_[i].motion, &records_[i].visual);
        }
    }

private:
    struct Record {
        bool alive = false;
        Transform transform{};
        Motion motion{};
        Visual visual{};
    };

    [[nodiscard]] Record* record(Entity entity);
    [[nodiscard]] const Record* record(Entity entity) const;

    std::vector<Record> records_;
    std::vector<core::u32> freeList_;
    core::usize aliveCount_ = 0;
};

} // namespace eee::logic
