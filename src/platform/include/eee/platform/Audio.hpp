#pragma once

#include "eee/core/Sound.hpp"

namespace eee::platform {

// Small procedural SFX player. No asset files: the macOS implementation
// synthesises short WAV buffers at startup and plays them with NSSound.
void initializeAudio();
void playSound(core::SoundId id);
void shutdownAudio();

} // namespace eee::platform
