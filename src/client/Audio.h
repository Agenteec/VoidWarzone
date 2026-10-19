#pragma once

#include <array>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

struct SDL_AudioStream;

namespace vw::client {

enum class SoundBus : std::uint8_t { Effects, Interface, Ambient, Count };

// Small software mixer on an SDL3 audio stream: mono samples, linear resampling, constant-power pan, per-bus gain.
class Audio {
public:
    bool init();
    void shutdown();
    bool ready() const { return stream_ != nullptr; }

    // Loads a WAV file (any format SDL understands) under `name`; variants are name_0, name_1, ...
    bool load(const std::string& name, const void* data, std::size_t size);
    bool has(const std::string& base) const;

    // One-shot; picks a random variant of `base` (or `base` itself). pan -1..1, pitch 1 = original.
    void play(const std::string& base, SoundBus bus, float gain, float pan = 0.0f, float pitch = 1.0f);
    // Looped voice that stays alive while it's set every frame; gain 0 fades it out.
    void loop(const std::string& name, SoundBus bus, float gain, float pan = 0.0f, float pitch = 1.0f);

    void setVolume(SoundBus bus, float v) { bus_[static_cast<std::size_t>(bus)] = v; }
    void setMaster(float v) { master_ = v; }
    void setPaused(bool paused);

private:
    struct Sample {
        std::vector<float> data;
        int rate = 22050;
    };
    struct Voice {
        const Sample* sample = nullptr;
        double pos = 0.0;
        float gain = 1.0f;
        float pan = 0.0f;
        float pitch = 1.0f;
        SoundBus bus = SoundBus::Effects;
        bool looped = false;
        int loopId = -1;
        float current = 0.0f;
    };
    struct Loop {
        float gain = 0.0f;
        float pan = 0.0f;
        float pitch = 1.0f;
        bool touched = false;
    };

    static void callback(void* user, SDL_AudioStream* stream, int additional, int total);
    void mix(float* out, int frames);

    SDL_AudioStream* stream_ = nullptr;
    std::mutex mutex_;
    std::unordered_map<std::string, Sample> samples_;
    std::unordered_map<std::string, int> variants_;
    std::vector<Voice> voices_;
    std::unordered_map<std::string, int> loopIds_;
    std::vector<Loop> loops_;
    std::array<float, static_cast<std::size_t>(SoundBus::Count)> bus_{1.0f, 1.0f, 1.0f};
    float master_ = 0.8f;
    std::vector<float> scratch_;
    std::vector<float> dump_;
    std::string dumpPath_;
    std::uint32_t rng_ = 0x9e3779b9u;
    static constexpr int kRate = 48000;
    static constexpr std::size_t kMaxVoices = 48;
};

}
