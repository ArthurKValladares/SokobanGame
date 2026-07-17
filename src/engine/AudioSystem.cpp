#include "engine/AudioSystem.hpp"

#include "miniaudio.h"

#include <array>
#include <iostream>
#include <vector>

namespace sokoban {
namespace {

constexpr std::array<const char*, 2> footstepFileNames {
    "footstep02.ogg",
    "footstep09.ogg",
};

} // namespace

struct AudioSystem::EngineHandle {
    ma_engine engine {};
    bool engineInitialized = false;
    // Fully decoded at load; addresses must stay stable after init, so the
    // vector is sized once and never reallocated.
    std::vector<ma_sound> footstepSounds;
    std::vector<int> loadedFootsteps;
};

AudioSystem::AudioSystem(std::filesystem::path audioRoot)
    : engine_(std::make_unique<EngineHandle>())
    , audioRoot_(std::move(audioRoot))
    , random_(std::random_device {}())
{
    if (ma_engine_init(nullptr, &engine_->engine) != MA_SUCCESS) {
        std::cerr << "Audio disabled: audio engine initialization failed\n";
        return;
    }
    engine_->engineInitialized = true;
    ma_engine_set_volume(&engine_->engine, masterVolume_);

    engine_->footstepSounds.resize(footstepFileNames.size());
    for (size_t i = 0; i < footstepFileNames.size(); ++i) {
        const std::filesystem::path path = audioRoot_ / footstepFileNames[i];
        const ma_result result = ma_sound_init_from_file(
            &engine_->engine,
            path.string().c_str(),
            MA_SOUND_FLAG_DECODE,
            nullptr,
            nullptr,
            &engine_->footstepSounds[i]);
        if (result == MA_SUCCESS) {
            engine_->loadedFootsteps.push_back(static_cast<int>(i));
        } else {
            std::cerr << "Audio: failed to load " << path.string() << "\n";
        }
    }
}

AudioSystem::~AudioSystem()
{
    if (!engine_->engineInitialized) {
        return;
    }
    for (int index : engine_->loadedFootsteps) {
        ma_sound_uninit(&engine_->footstepSounds[static_cast<size_t>(index)]);
    }
    ma_engine_uninit(&engine_->engine);
}

bool AudioSystem::available() const
{
    return engine_->engineInitialized && !engine_->loadedFootsteps.empty();
}

void AudioSystem::update(float dt, bool playerWalking)
{
    const int due = cadence_.update(dt, playerWalking);
    if (due > 0 && available()) {
        // Multiple due steps in one frame collapse into a single sound;
        // stacking identical samples only changes loudness.
        playFootstep();
    }
}

void AudioSystem::playFootstep()
{
    const auto& loaded = engine_->loadedFootsteps;
    int pick = loaded[random_() % loaded.size()];
    if (static_cast<int>(loaded.size()) > 1) {
        while (pick == lastFootstepIndex_) {
            pick = loaded[random_() % loaded.size()];
        }
    }
    lastFootstepIndex_ = pick;

    ma_sound& sound = engine_->footstepSounds[static_cast<size_t>(pick)];
    ma_sound_seek_to_pcm_frame(&sound, 0);
    ma_sound_start(&sound);
}

void AudioSystem::setMasterVolume(float volume)
{
    masterVolume_ = std::clamp(volume, 0.0f, 1.0f);
    if (engine_->engineInitialized) {
        ma_engine_set_volume(&engine_->engine, masterVolume_);
    }
}

void AudioSystem::setFootstepIntervalSeconds(float seconds)
{
    cadence_.intervalSeconds = std::clamp(seconds, 0.05f, 2.0f);
}

} // namespace sokoban
