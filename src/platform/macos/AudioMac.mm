#import <Cocoa/Cocoa.h>

#include "eee/platform/Audio.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace {

constexpr int kSampleRate = 22050;
constexpr int kSoundCount = static_cast<int>(eee::core::SoundId::Count);

using Samples = std::vector<int16_t>;

// Deterministic noise source.
uint32_t gNoiseState = 0x12345678u;
float noise() {
    gNoiseState ^= gNoiseState << 13;
    gNoiseState ^= gNoiseState >> 17;
    gNoiseState ^= gNoiseState << 5;
    return static_cast<float>(gNoiseState & 0xFFFFu) / 32768.0f - 1.0f;
}

enum class Wave { Sine, Square, Saw };

void addTone(Samples& samples, float f0, float f1, float duration, float volume,
             Wave wave = Wave::Sine) {
    const int count = static_cast<int>(duration * kSampleRate);
    const float startPhase = static_cast<float>(samples.size());
    for (int i = 0; i < count; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(count);
        const float frequency = f0 + (f1 - f0) * t;
        const float phase = startPhase + frequency * static_cast<float>(i) / kSampleRate;
        float value = 0.0f;
        switch (wave) {
        case Wave::Sine: value = std::sin(phase * 6.2831853f); break;
        case Wave::Square: value = std::sin(phase * 6.2831853f) >= 0.0f ? 1.0f : -1.0f; break;
        case Wave::Saw: value = 2.0f * (phase - std::floor(phase + 0.5f)); break;
        }
        const float envelope = 1.0f - t;
        samples.push_back(static_cast<int16_t>(value * envelope * volume * 32767.0f));
    }
}

void addNoise(Samples& samples, float duration, float volume) {
    const int count = static_cast<int>(duration * kSampleRate);
    for (int i = 0; i < count; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(count);
        const float envelope = 1.0f - t;
        samples.push_back(static_cast<int16_t>(noise() * envelope * volume * 32767.0f));
    }
}

void addSilence(Samples& samples, float duration) {
    const int count = static_cast<int>(duration * kSampleRate);
    samples.insert(samples.end(), static_cast<size_t>(count), 0);
}

Samples buildSound(eee::core::SoundId id) {
    Samples samples;
    switch (id) {
    case eee::core::SoundId::Attack:
        addTone(samples, 260.0f, 120.0f, 0.10f, 0.18f, Wave::Square);
        break;
    case eee::core::SoundId::RangedAttack:
        addTone(samples, 880.0f, 420.0f, 0.11f, 0.14f);
        addNoise(samples, 0.04f, 0.05f);
        break;
    case eee::core::SoundId::Skill:
        addTone(samples, 180.0f, 960.0f, 0.26f, 0.20f);
        addNoise(samples, 0.15f, 0.08f);
        break;
    case eee::core::SoundId::Hit:
        addNoise(samples, 0.08f, 0.16f);
        addTone(samples, 180.0f, 90.0f, 0.06f, 0.10f, Wave::Square);
        break;
    case eee::core::SoundId::EnemyDeath:
        addTone(samples, 360.0f, 70.0f, 0.34f, 0.18f, Wave::Saw);
        addNoise(samples, 0.18f, 0.10f);
        break;
    case eee::core::SoundId::PlayerHurt:
        addTone(samples, 160.0f, 75.0f, 0.20f, 0.22f, Wave::Square);
        break;
    case eee::core::SoundId::Pickup:
        addTone(samples, 880.0f, 880.0f, 0.08f, 0.13f);
        addTone(samples, 1320.0f, 1320.0f, 0.10f, 0.13f);
        break;
    case eee::core::SoundId::LevelUp:
        addTone(samples, 523.0f, 523.0f, 0.10f, 0.14f);
        addTone(samples, 659.0f, 659.0f, 0.10f, 0.14f);
        addTone(samples, 784.0f, 784.0f, 0.10f, 0.14f);
        addTone(samples, 1046.0f, 1046.0f, 0.22f, 0.16f);
        break;
    case eee::core::SoundId::MenuMove:
        addTone(samples, 420.0f, 420.0f, 0.035f, 0.10f, Wave::Square);
        break;
    case eee::core::SoundId::MenuSelect:
        addTone(samples, 660.0f, 880.0f, 0.07f, 0.14f, Wave::Square);
        break;
    case eee::core::SoundId::ZoneEnter:
        addTone(samples, 110.0f, 165.0f, 0.55f, 0.16f);
        addTone(samples, 220.0f, 330.0f, 0.45f, 0.10f);
        break;
    case eee::core::SoundId::BossRoar:
        addTone(samples, 85.0f, 55.0f, 0.65f, 0.24f, Wave::Saw);
        addNoise(samples, 0.4f, 0.12f);
        break;
    case eee::core::SoundId::Portal:
        addTone(samples, 200.0f, 1400.0f, 0.40f, 0.18f);
        addTone(samples, 300.0f, 900.0f, 0.30f, 0.08f, Wave::Sine);
        break;
    case eee::core::SoundId::Jump:
        addTone(samples, 380.0f, 720.0f, 0.11f, 0.12f, Wave::Square);
        break;
    case eee::core::SoundId::Heal:
        addTone(samples, 500.0f, 1000.0f, 0.26f, 0.14f);
        addTone(samples, 750.0f, 1500.0f, 0.18f, 0.06f);
        break;
    case eee::core::SoundId::Error:
        addTone(samples, 200.0f, 120.0f, 0.16f, 0.16f, Wave::Square);
        break;
    case eee::core::SoundId::Victory:
        addTone(samples, 523.0f, 523.0f, 0.12f, 0.14f);
        addTone(samples, 659.0f, 659.0f, 0.12f, 0.14f);
        addTone(samples, 784.0f, 784.0f, 0.12f, 0.14f);
        addTone(samples, 1046.0f, 1046.0f, 0.35f, 0.18f);
        addTone(samples, 1318.0f, 1318.0f, 0.35f, 0.12f);
        break;
    default:
        break;
    }
    if (samples.empty()) {
        addSilence(samples, 0.05f);
    }
    return samples;
}

NSData* makeWav(const Samples& samples) {
    const uint32_t dataSize = static_cast<uint32_t>(samples.size() * sizeof(int16_t));
    std::vector<uint8_t> bytes;
    bytes.reserve(44 + dataSize);

    auto append32 = [&](uint32_t v) {
        bytes.push_back(static_cast<uint8_t>(v & 0xFF));
        bytes.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
        bytes.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
        bytes.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
    };
    auto append16 = [&](uint16_t v) {
        bytes.push_back(static_cast<uint8_t>(v & 0xFF));
        bytes.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    };

    bytes.insert(bytes.end(), {'R', 'I', 'F', 'F'});
    append32(36 + dataSize);
    bytes.insert(bytes.end(), {'W', 'A', 'V', 'E'});
    bytes.insert(bytes.end(), {'f', 'm', 't', ' '});
    append32(16);
    append16(1); // PCM
    append16(1); // mono
    append32(kSampleRate);
    append32(kSampleRate * 2);
    append16(2);
    append16(16);
    bytes.insert(bytes.end(), {'d', 'a', 't', 'a'});
    append32(dataSize);
    const auto* raw = reinterpret_cast<const uint8_t*>(samples.data());
    bytes.insert(bytes.end(), raw, raw + dataSize);

    return [NSData dataWithBytes:bytes.data() length:bytes.size()];
}

std::array<void*, kSoundCount> gSoundData{};
NSMutableArray<NSSound*>* gActiveSounds = nil;

} // namespace

void eeeAudioInitialize() {
    static bool initialized = false;
    if (initialized) {
        return;
    }
    initialized = true;
    gActiveSounds = [NSMutableArray array];
    for (int i = 0; i < kSoundCount; ++i) {
        const eee::core::SoundId id = static_cast<eee::core::SoundId>(i);
        NSData* data = makeWav(buildSound(id));
        gSoundData[static_cast<size_t>(i)] = (__bridge_retained void*)data;
    }
}

void eeeAudioPlay(int sound) {
    if (sound < 0 || sound >= kSoundCount || gSoundData[static_cast<size_t>(sound)] == nullptr) {
        return;
    }
    if (gActiveSounds == nil) {
        gActiveSounds = [NSMutableArray array];
    }

    // Drop finished sounds so the array does not grow forever.
    for (NSInteger i = static_cast<NSInteger>(gActiveSounds.count) - 1; i >= 0; --i) {
        NSSound* active = gActiveSounds[static_cast<NSUInteger>(i)];
        if (!active.isPlaying) {
            [gActiveSounds removeObjectAtIndex:static_cast<NSUInteger>(i)];
        }
    }

    NSData* data = (__bridge NSData*)gSoundData[static_cast<size_t>(sound)];
    NSSound* player = [[NSSound alloc] initWithData:data];
    if (player == nil) {
        return;
    }
    [player play];
    [gActiveSounds addObject:player];
    if (gActiveSounds.count > 32) {
        [gActiveSounds removeObjectAtIndex:0];
    }
}

void eeeAudioShutdown() {
    if (gActiveSounds != nil) {
        for (NSSound* sound in gActiveSounds) {
            [sound stop];
        }
        [gActiveSounds removeAllObjects];
        gActiveSounds = nil;
    }
    for (size_t i = 0; i < gSoundData.size(); ++i) {
        if (gSoundData[i] != nullptr) {
            NSData* data = (__bridge_transfer NSData*)gSoundData[i];
            (void)data;
            gSoundData[i] = nullptr;
        }
    }
}
