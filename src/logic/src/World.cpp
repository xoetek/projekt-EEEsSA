#include "eee/logic/World.hpp"

namespace eee::logic {

Entity World::create() {
    core::u32 index = 0;
    if (!freeList_.empty()) {
        index = freeList_.back();
        freeList_.pop_back();
    } else {
        index = static_cast<core::u32>(records_.size());
        records_.emplace_back();
    }

    Record& rec = records_[index];
    rec = Record{};
    rec.alive = true;
    ++aliveCount_;
    return index + 1;
}

void World::destroy(Entity entity) {
    Record* rec = record(entity);
    if (rec == nullptr || !rec->alive) {
        return;
    }
    rec->alive = false;
    freeList_.push_back(entity - 1);
    --aliveCount_;
}

bool World::alive(Entity entity) const {
    const Record* rec = record(entity);
    return rec != nullptr && rec->alive;
}

World::Record* World::record(Entity entity) {
    if (entity == kInvalidEntity) {
        return nullptr;
    }
    const core::usize index = entity - 1;
    if (index >= records_.size()) {
        return nullptr;
    }
    return &records_[index];
}

const World::Record* World::record(Entity entity) const {
    if (entity == kInvalidEntity) {
        return nullptr;
    }
    const core::usize index = entity - 1;
    if (index >= records_.size()) {
        return nullptr;
    }
    return &records_[index];
}

Transform* World::transform(Entity entity) {
    Record* rec = record(entity);
    return (rec != nullptr && rec->alive) ? &rec->transform : nullptr;
}

Motion* World::motion(Entity entity) {
    Record* rec = record(entity);
    return (rec != nullptr && rec->alive) ? &rec->motion : nullptr;
}

Visual* World::visual(Entity entity) {
    Record* rec = record(entity);
    return (rec != nullptr && rec->alive) ? &rec->visual : nullptr;
}

const Transform* World::transform(Entity entity) const {
    const Record* rec = record(entity);
    return (rec != nullptr && rec->alive) ? &rec->transform : nullptr;
}

const Motion* World::motion(Entity entity) const {
    const Record* rec = record(entity);
    return (rec != nullptr && rec->alive) ? &rec->motion : nullptr;
}

const Visual* World::visual(Entity entity) const {
    const Record* rec = record(entity);
    return (rec != nullptr && rec->alive) ? &rec->visual : nullptr;
}

} // namespace eee::logic
