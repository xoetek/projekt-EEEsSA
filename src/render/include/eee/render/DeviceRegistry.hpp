#pragma once

#include "eee/render/IRenderDevice.hpp"

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace eee::render {

using DeviceFactory = std::function<std::unique_ptr<IRenderDevice>()>;

// Backends register themselves here; the host application selects one by name.
// This keeps `main` free of #ifdefs for every graphics API.
class DeviceRegistry {
public:
    static DeviceRegistry& instance();

    void registerDevice(std::string name, DeviceFactory factory);
    [[nodiscard]] std::unique_ptr<IRenderDevice> create(std::string_view name) const;
    [[nodiscard]] std::vector<std::string> names() const;
    [[nodiscard]] bool contains(std::string_view name) const;

private:
    struct Entry {
        std::string name;
        DeviceFactory factory;
    };
    std::vector<Entry> entries_;
};

// Registers every backend that was compiled into this binary (see CMake
// options EEE_BACKEND_OPENGL / EEE_BACKEND_METAL / EEE_BACKEND_VULKAN).
void registerBuiltinDevices();

} // namespace eee::render
