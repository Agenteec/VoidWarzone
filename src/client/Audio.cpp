#include "Audio.h"

#include <algorithm>
#include <cmath>

#include <SDL3/SDL.h>
#include <spdlog/spdlog.h>

namespace vw::client {

bool Audio::init() {
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        spdlog::warn("audio: {}", SDL_GetError());
        return false;
    }
    const SDL_AudioSpec spec{SDL_AUDIO_F32, 2, kRate};
    stream_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, &Audio::callback, this);
    if (!stream_) {
        spdlog::warn("audio device: {}", SDL_GetError());
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        return false;
    }
    SDL_ResumeAudioStreamDevice(stream_);
    if (const char* dump = SDL_getenv("VW_AUDIO_DUMP"))
        dumpPath_ = dump;
    return true;
}

void Audio::shutdown() {
    if (!dumpPath_.empty() && !dump_.empty()) {
        // debug: VW_AUDIO_DUMP=file records the mixed output as raw float32 stereo, 48 kHz
        SDL_SaveFile(dumpPath_.c_str(), dump_.data(), dump_.size() * sizeof(float));
    }
    if (stream_) {
        SDL_DestroyAudioStream(stream_);
        stream_ = nullptr;
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
    }
}

void Audio::setPaused(bool paused) {
    if (!stream_)
        return;
    if (paused)
        SDL_PauseAudioStreamDevice(stream_);
    else
        SDL_ResumeAudioStreamDevice(stream_);
}

bool Audio::load(const std::string& name, const void* data, std::size_t size) {
    SDL_AudioSpec spec{};
    Uint8* buf = nullptr;
    Uint32 len = 0;
    if (!SDL_LoadWAV_IO(SDL_IOFromConstMem(data, size), true, &spec, &buf, &len))
        return false;
    const SDL_AudioSpec mono{SDL_AUDIO_F32, 1, spec.freq};
    Uint8* out = nullptr;
    int outLen = 0;
    const bool ok = SDL_ConvertAudioSamples(&spec, buf, static_cast<int>(len), &mono, &out, &outLen);
    SDL_free(buf);
    if (!ok)
        return false;
    Sample s;
    s.rate = spec.freq;
    s.data.assign(reinterpret_cast<float*>(out), reinterpret_cast<float*>(out) + outLen / static_cast<int>(sizeof(float)));
    SDL_free(out);
    std::lock_guard lock(mutex_);
    samples_[name] = std::move(s);
    const auto underscore = name.rfind('_');
    if (underscore != std::string::npos && underscore + 1 < name.size() &&
        std::all_of(name.begin() + static_cast<std::ptrdiff_t>(underscore) + 1, name.end(), [](char c) { return c >= '0' && c <= '9'; })) {
        auto& count = variants_[name.substr(0, underscore)];
        count = std::max(count, std::stoi(name.substr(underscore + 1)) + 1);
    }
    return true;
}

bool Audio::has(const std::string& base) const {
    return samples_.contains(base) || variants_.contains(base);
}

void Audio::play(const std::string& base, SoundBus bus, float gain, float pan, float pitch) {
    if (!stream_ || gain <= 0.003f)
        return;
    std::lock_guard lock(mutex_);
    const Sample* sample = nullptr;
    if (const auto v = variants_.find(base); v != variants_.end()) {
        rng_ = rng_ * 1664525u + 1013904223u;
        const auto it = samples_.find(base + "_" + std::to_string((rng_ >> 16) % static_cast<std::uint32_t>(v->second)));
        sample = it != samples_.end() ? &it->second : nullptr;
    } else if (const auto it = samples_.find(base); it != samples_.end()) {
        sample = &it->second;
    }
    if (!sample)
        return;
    if (voices_.size() >= kMaxVoices) {
        auto quietest = std::min_element(voices_.begin(), voices_.end(), [](const Voice& a, const Voice& b) {
            return (a.looped ? 1e9f : a.gain) < (b.looped ? 1e9f : b.gain);
        });
        if (quietest == voices_.end() || quietest->looped || quietest->gain > gain)
            return;
        voices_.erase(quietest);
    }
    voices_.push_back({sample, 0.0, gain, std::clamp(pan, -1.0f, 1.0f), pitch, bus, false, -1, gain});
}

void Audio::loop(const std::string& name, SoundBus bus, float gain, float pan, float pitch) {
    if (!stream_)
        return;
    std::lock_guard lock(mutex_);
    auto [it, inserted] = loopIds_.try_emplace(name, static_cast<int>(loops_.size()));
    if (inserted)
        loops_.push_back({});
    auto& l = loops_[static_cast<std::size_t>(it->second)];
    l.gain = gain;
    l.pan = std::clamp(pan, -1.0f, 1.0f);
    l.pitch = pitch;
    const bool playing = std::any_of(voices_.begin(), voices_.end(), [&](const Voice& v) { return v.loopId == it->second; });
    if (!playing && gain > 0.003f)
        if (const auto s = samples_.find(name); s != samples_.end())
            voices_.push_back({&s->second, 0.0, gain, l.pan, pitch, bus, true, it->second, 0.0f});
}

void Audio::callback(void* user, SDL_AudioStream* stream, int additional, int) {
    auto* self = static_cast<Audio*>(user);
    const int frames = additional / static_cast<int>(sizeof(float) * 2);
    if (frames <= 0)
        return;
    self->scratch_.assign(static_cast<std::size_t>(frames) * 2, 0.0f);
    self->mix(self->scratch_.data(), frames);
    if (!self->dumpPath_.empty() && self->dump_.size() < static_cast<std::size_t>(kRate) * 2 * 120)
        self->dump_.insert(self->dump_.end(), self->scratch_.begin(), self->scratch_.end());
    SDL_PutAudioStreamData(stream, self->scratch_.data(), frames * static_cast<int>(sizeof(float) * 2));
}

void Audio::mix(float* out, int frames) {
    std::lock_guard lock(mutex_);
    const float smoothing = 1.0f - std::exp(-static_cast<float>(frames) / (0.08f * kRate));
    for (auto& v : voices_) {
        float target = v.gain;
        if (v.looped) {
            const auto& l = loops_[static_cast<std::size_t>(v.loopId)];
            target = l.gain;
            v.pan += (l.pan - v.pan) * smoothing;
            v.pitch += (l.pitch - v.pitch) * smoothing;
            v.current += (target - v.current) * smoothing;
        } else {
            v.current = target;
        }
        const float g = v.current * bus_[static_cast<std::size_t>(v.bus)] * master_;
        const float angle = (v.pan + 1.0f) * 0.25f * 3.14159265f;
        const float gl = g * std::cos(angle) * 1.41421356f;
        const float gr = g * std::sin(angle) * 1.41421356f;
        const auto& data = v.sample->data;
        const double step = static_cast<double>(v.sample->rate) / kRate * v.pitch;
        const double size = static_cast<double>(data.size());
        for (int i = 0; i < frames; ++i) {
            if (v.pos >= size - 1.0) {
                if (!v.looped) {
                    v.pos = size;
                    break;
                }
                v.pos -= size - 1.0;
            }
            const auto k = static_cast<std::size_t>(v.pos);
            const float f = static_cast<float>(v.pos - static_cast<double>(k));
            const float s = data[k] + (data[k + 1] - data[k]) * f;
            out[i * 2] += s * gl;
            out[i * 2 + 1] += s * gr;
            v.pos += step;
        }
    }
    std::erase_if(voices_, [&](const Voice& v) {
        if (v.looped)
            return loops_[static_cast<std::size_t>(v.loopId)].gain <= 0.003f && v.current < 0.003f;
        return v.pos >= static_cast<double>(v.sample->data.size()) - 1.0;
    });
    for (int i = 0; i < frames * 2; ++i)
        out[i] = std::tanh(out[i]);
}

}
