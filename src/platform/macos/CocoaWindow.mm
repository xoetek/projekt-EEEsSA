#import <Cocoa/Cocoa.h>

#include "eee/core/Log.hpp"
#include "eee/platform/Window.hpp"

#include <string>

namespace eee::platform {
namespace {

using eee::core::InputState;
using eee::core::Key;

void updateMousePosition(InputState* input, NSEvent* event, NSView* view) {
    if (input == nullptr) {
        return;
    }
    const NSPoint point = [view convertPoint:event.locationInWindow fromView:nil];
    input->mousePosition = {static_cast<float>(point.x),
                            static_cast<float>(view.bounds.size.height - point.y)};
}

void applyKey(InputState* input, NSEvent* event, bool isDown) {
    if (input == nullptr) {
        return;
    }

    NSString* characters = [event charactersIgnoringModifiers];
    if (characters.length > 0) {
        const unichar c = [characters characterAtIndex:0];
        if (c >= 'a' && c <= 'z') {
            input->setKey(static_cast<Key>(c - ('a' - 'A')), isDown);
            return;
        }
        if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == ' ') {
            input->setKey(static_cast<Key>(c), isDown);
            return;
        }
        if (c >= 33 && c <= 126) {
            input->setKey(static_cast<Key>(c), isDown);
            return;
        }
    }

    switch ([event keyCode]) {
    case 123: input->setKey(Key::Left, isDown); break;
    case 124: input->setKey(Key::Right, isDown); break;
    case 125: input->setKey(Key::Down, isDown); break;
    case 126: input->setKey(Key::Up, isDown); break;
    case 49: input->setKey(Key::Space, isDown); break;
    case 53: input->setKey(Key::Escape, isDown); break;
    case 36:
    case 76: input->setKey(Key::Enter, isDown); break;
    case 48: input->setKey(Key::Tab, isDown); break;
    case 51: input->setKey(Key::Backspace, isDown); break;
    case 56: input->setKey(Key::LeftShift, isDown); break;
    case 60: input->setKey(Key::RightShift, isDown); break;
    case 122: input->setKey(Key::F1, isDown); break;
    default: break;
    }
}

} // namespace
} // namespace eee::platform

// ---------------------------------------------------------------------------
// Objective-C pieces
// ---------------------------------------------------------------------------

@interface EEEInputView : NSView
@property (nonatomic, assign) eee::core::InputState* inputState;
@end

@implementation EEEInputView

- (BOOL)acceptsFirstResponder {
    return YES;
}

- (BOOL)isOpaque {
    return YES;
}

- (void)keyDown:(NSEvent*)event {
    eee::platform::applyKey(self.inputState, event, true);
}

- (void)keyUp:(NSEvent*)event {
    eee::platform::applyKey(self.inputState, event, false);
}

- (void)mouseDown:(NSEvent*)event {
    eee::platform::updateMousePosition(self.inputState, event, self);
    if (self.inputState != nullptr) {
        self.inputState->setMouseButton(0, true);
    }
}

- (void)mouseUp:(NSEvent*)event {
    eee::platform::updateMousePosition(self.inputState, event, self);
    if (self.inputState != nullptr) {
        self.inputState->setMouseButton(0, false);
    }
}

- (void)rightMouseDown:(NSEvent*)event {
    if (self.inputState != nullptr) {
        self.inputState->setMouseButton(1, true);
    }
}

- (void)rightMouseUp:(NSEvent*)event {
    if (self.inputState != nullptr) {
        self.inputState->setMouseButton(1, false);
    }
}

- (void)otherMouseDown:(NSEvent*)event {
    if (self.inputState != nullptr) {
        self.inputState->setMouseButton(2, true);
    }
}

- (void)otherMouseUp:(NSEvent*)event {
    if (self.inputState != nullptr) {
        self.inputState->setMouseButton(2, false);
    }
}

- (void)mouseMoved:(NSEvent*)event {
    eee::platform::updateMousePosition(self.inputState, event, self);
}

- (void)mouseDragged:(NSEvent*)event {
    eee::platform::updateMousePosition(self.inputState, event, self);
}

- (void)rightMouseDragged:(NSEvent*)event {
    eee::platform::updateMousePosition(self.inputState, event, self);
}

- (void)otherMouseDragged:(NSEvent*)event {
    eee::platform::updateMousePosition(self.inputState, event, self);
}

- (void)scrollWheel:(NSEvent*)event {
    if (self.inputState != nullptr) {
        self.inputState->scrollDelta += static_cast<float>(event.scrollingDeltaY);
    }
}

@end

@interface EEEWindowDelegate : NSObject <NSWindowDelegate>
@property(nonatomic) BOOL closeRequested;
@end

@implementation EEEWindowDelegate
- (BOOL)windowShouldClose:(NSWindow*)sender {
    (void)sender;
    self.closeRequested = YES;
    return NO;
}
@end

@interface EEEAppDelegate : NSObject <NSApplicationDelegate>
@end

@implementation EEEAppDelegate
- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication*)sender {
    (void)sender;
    return NO;
}
@end

// ---------------------------------------------------------------------------
// C++ window
// ---------------------------------------------------------------------------

namespace eee::platform {
namespace {

class CocoaWindow final : public IWindow {
public:
    explicit CocoaWindow(const WindowDesc& desc) { create(desc); }

    ~CocoaWindow() override {
        if (view_ != nil) {
            view_.inputState = nullptr;
        }
        if (window_ != nil) {
            window_.delegate = nil;
            [window_ close];
            window_ = nil;
        }
        view_ = nil;
        windowDelegate_ = nil;
        appDelegate_ = nil;
    }

    CocoaWindow(const CocoaWindow&) = delete;
    CocoaWindow& operator=(const CocoaWindow&) = delete;

    bool pollEvents(InputState& input) override {
        view_.inputState = &input;

        @autoreleasepool {
            NSEvent* event = nil;
            while ((event = [NSApp nextEventMatchingMask:NSEventMaskAny
                                               untilDate:[NSDate distantPast]
                                                  inMode:NSDefaultRunLoopMode
                                                 dequeue:YES]) != nil) {
                [NSApp sendEvent:event];
            }
        }

        refreshFramebufferSize();
        return !windowDelegate_.closeRequested;
    }

    void getFramebufferSize(core::i32& width, core::i32& height) const override {
        width = width_;
        height = height_;
    }

    [[nodiscard]] NativeWindow native() const override {
        return {(__bridge void*)window_, (__bridge void*)view_};
    }

    void requestClose() override { windowDelegate_.closeRequested = YES; }

    [[nodiscard]] bool shouldClose() const override { return windowDelegate_.closeRequested; }

    void setTitle(const char* title) override {
        window_.title = [NSString stringWithUTF8String:title];
    }

private:
    void create(const WindowDesc& desc) {
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];

        appDelegate_ = [[EEEAppDelegate alloc] init];
        [NSApp setDelegate:appDelegate_];

        const NSRect contentRect =
            NSMakeRect(0, 0, static_cast<CGFloat>(desc.width), static_cast<CGFloat>(desc.height));
        const NSWindowStyleMask style = NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                                        NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable;

        window_ = [[NSWindow alloc] initWithContentRect:contentRect
                                              styleMask:style
                                                backing:NSBackingStoreBuffered
                                                  defer:NO];
        window_.title = [NSString stringWithUTF8String:desc.title != nullptr ? desc.title : "EEEsSA"];
        window_.releasedWhenClosed = NO;

        windowDelegate_ = [[EEEWindowDelegate alloc] init];
        window_.delegate = windowDelegate_;

        view_ = [[EEEInputView alloc] initWithFrame:contentRect];
        view_.wantsLayer = YES;
        window_.contentView = view_;
        [window_ makeFirstResponder:view_];
        [window_ setAcceptsMouseMovedEvents:YES];
        [window_ center];
        [window_ makeKeyAndOrderFront:nil];
        [NSApp activateIgnoringOtherApps:YES];

        refreshFramebufferSize();
        core::logf(core::LogLevel::Info, "cocoa window created: %dx%d (framebuffer %dx%d)",
                   desc.width, desc.height, width_, height_);
    }

    void refreshFramebufferSize() const {
        if (view_ == nil) {
            return;
        }
        const NSRect backing = [view_ convertRectToBacking:view_.bounds];
        width_ = static_cast<core::i32>(backing.size.width);
        height_ = static_cast<core::i32>(backing.size.height);
    }

    NSWindow* __strong window_ = nil;
    EEEInputView* __strong view_ = nil;
    EEEWindowDelegate* __strong windowDelegate_ = nil;
    EEEAppDelegate* __strong appDelegate_ = nil;
    mutable core::i32 width_ = 0;
    mutable core::i32 height_ = 0;
};

} // namespace
} // namespace eee::platform

// Declared at global scope in platform/src/Window.cpp; kept out of the eee::platform
// namespace so the declaration and definition agree.
std::unique_ptr<eee::platform::IWindow> eeeCreateCocoaWindow(
    const eee::platform::WindowDesc& desc) {
    return std::make_unique<eee::platform::CocoaWindow>(desc);
}
