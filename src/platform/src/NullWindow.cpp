#include "eee/platform/Window.hpp"

namespace eee::platform {
namespace {

class NullWindow final : public IWindow {
public:
    explicit NullWindow(const WindowDesc& desc)
        : width_(desc.width), height_(desc.height), title_(desc.title) {}

    bool pollEvents(core::InputState& /*input*/) override { return !closeRequested_; }

    void getFramebufferSize(core::i32& width, core::i32& height) const override {
        width = width_;
        height = height_;
    }

    [[nodiscard]] NativeWindow native() const override { return {}; }

    void requestClose() override { closeRequested_ = true; }
    [[nodiscard]] bool shouldClose() const override { return closeRequested_; }
    void setTitle(const char* title) override { title_ = title; }

private:
    core::i32 width_ = 0;
    core::i32 height_ = 0;
    const char* title_ = "";
    bool closeRequested_ = false;
};

} // namespace

std::unique_ptr<IWindow> createNullWindow(const WindowDesc& desc) {
    return std::make_unique<NullWindow>(desc);
}

} // namespace eee::platform
