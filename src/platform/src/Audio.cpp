#include "eee/platform/Audio.hpp"

#if defined(__APPLE__)
void eeeAudioInitialize();
void eeeAudioPlay(int sound);
void eeeAudioShutdown();
#endif

namespace eee::platform {

void initializeAudio() {
#if defined(__APPLE__)
    eeeAudioInitialize();
#endif
}

void playSound(core::SoundId id) {
#if defined(__APPLE__)
    eeeAudioPlay(static_cast<int>(id));
#else
    (void)id;
#endif
}

void shutdownAudio() {
#if defined(__APPLE__)
    eeeAudioShutdown();
#endif
}

} // namespace eee::platform
