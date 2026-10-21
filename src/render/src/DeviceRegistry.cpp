#include "eee/render/DeviceRegistry.hpp"

#include "eee/core/Log.hpp"

#include <algorithm>

namespace eee::render {

DeviceRegistry& DeviceRegistry::instance() {
    static DeviceRegistry registry;
    return registry;
}

void DeviceRegistry::registerDevice(std::string name, DeviceFactory factory) {
    const auto it = std::find_if(entries_.begin(), entries_.end(),
                                 [&](const Entry& e) { return e.name == name; });
    if (it != entries_.end()) {
        it->factory = std::move(factory);
        return;
    }
    entries_.push_back(Entry{std::move(name), std::move(factory)});
}

std::unique_ptr<IRenderDevice> DeviceRegistry::create(std::string_view name) const {
    for (const Entry& entry : entries_) {
        if (entry.name == name) {
            return entry.factory();
        }
    }
    return nullptr;
}

std::vector<std::string> DeviceRegistry::names() const {
    std::vector<std::string> result;
    result.reserve(entries_.size());
    for (const Entry& entry : entries_) {
        result.push_back(entry.name);
    }
    return result;
}

bool DeviceRegistry::contains(std::string_view name) const {
    return std::any_of(entries_.begin(), entries_.end(),
                       [&](const Entry& e) { return e.name == name; });
}

} // namespace eee::render
