#include "engine/AudioSystem.hpp"

#include "engine/Log.hpp"

#include "miniaudio.h"

#include <algorithm>
#include <array>
#include <string_view>
#include <vector>

namespace sokoban {
namespace {

// Short fades hide the click of starting/stopping the drag loop mid-waveform.
constexpr unsigned int dragFadeInMilliseconds = 15;
constexpr unsigned int dragFadeOutMilliseconds = 40;

constexpr unsigned int musicCrossfadeMilliseconds = 600;

} // namespace

struct AudioSystem::EngineHandle {
    ma_engine engine {};
    bool engineInitialized = false;
    // Fully decoded at load; addresses must stay stable after init, so the
    // vector is sized once and never reallocated.
    std::vector<ma_sound> footstepSounds;
    std::vector<int> loadedFootsteps;
    std::vector<ma_sound> stoneDragSounds;
    std::vector<int> loadedStoneDrags;
    int activeStoneDrag = -1;
    std::vector<ma_sound> musicSounds;
    std::vector<int> loadedMusic;
    int activeMusic = -1;
};

namespace {

void loadSoundSet(
    ma_engine& engine,
    const std::filesystem::path& audioRoot,
    const std::vector<std::string>& fileNames,
    std::vector<ma_sound>& sounds,
    std::vector<int>& loaded,
    ma_uint32 flags)
{
    // Addresses must stay stable after init, so the vector is sized once and
    // never reallocated.
    const size_t count = fileNames.size();
    sounds.resize(count);
    for (size_t i = 0; i < count; ++i) {
        const std::filesystem::path path = audioRoot / fileNames[i];
        const ma_result result = ma_sound_init_from_file(
            &engine,
            path.string().c_str(),
            flags,
            nullptr,
            nullptr,
            &sounds[i]);
        if (result == MA_SUCCESS) {
            loaded.push_back(static_cast<int>(i));
        } else {
            log::warning(log::Category::Audio)
                << "Audio: failed to load " << path.string();
        }
    }
}

} // namespace

AudioSystem::AudioSystem(std::filesystem::path audioRoot, const AssetManifest& manifest)
    : engine_(std::make_unique<EngineHandle>())
    , audioRoot_(std::move(audioRoot))
    , manifest_(&manifest)
    , random_(std::random_device {}())
{
    if (ma_engine_init(nullptr, &engine_->engine) != MA_SUCCESS) {
        log::warning(log::Category::Audio)
            << "Audio disabled: audio engine initialization failed";
        return;
    }
    engine_->engineInitialized = true;
    ma_engine_set_volume(&engine_->engine, masterVolume_);
    footstepVolume_ = std::clamp(manifest.soundSetVolume("footsteps"), 0.0f, 1.0f);
    stoneDragVolume_ = std::clamp(manifest.soundSetVolume("stone-drag"), 0.0f, 1.0f);

    loadSoundSet(
        engine_->engine, audioRoot_,
        manifest.soundSet("footsteps"),
        engine_->footstepSounds, engine_->loadedFootsteps,
        MA_SOUND_FLAG_DECODE);
    loadSoundSet(
        engine_->engine, audioRoot_,
        manifest.soundSet("stone-drag"),
        engine_->stoneDragSounds, engine_->loadedStoneDrags,
        MA_SOUND_FLAG_DECODE);
    // Music is streamed rather than fully decoded; tracks are long and only
    // one plays at a time. Slot i mirrors manifest.musicTracks()[i].
    std::vector<std::string> musicFiles;
    musicFiles.reserve(manifest.musicTracks().size());
    for (const AssetManifest::MusicTrack& track : manifest.musicTracks()) {
        musicFiles.push_back(track.file);
    }
    loadSoundSet(
        engine_->engine, audioRoot_,
        musicFiles,
        engine_->musicSounds, engine_->loadedMusic,
        MA_SOUND_FLAG_STREAM);
}

AudioSystem::~AudioSystem()
{
    if (!engine_->engineInitialized) {
        return;
    }
    for (int index : engine_->loadedFootsteps) {
        ma_sound_uninit(&engine_->footstepSounds[static_cast<size_t>(index)]);
    }
    for (int index : engine_->loadedStoneDrags) {
        ma_sound_uninit(&engine_->stoneDragSounds[static_cast<size_t>(index)]);
    }
    for (int index : engine_->loadedMusic) {
        ma_sound_uninit(&engine_->musicSounds[static_cast<size_t>(index)]);
    }
    ma_engine_uninit(&engine_->engine);
}

bool AudioSystem::available() const
{
    return engine_->engineInitialized && !engine_->loadedFootsteps.empty();
}

void AudioSystem::update(float dt, bool playerWalking, bool pushingStone)
{
    const int due = cadence_.update(dt, playerWalking);
    if (due > 0 && available()) {
        // Multiple due steps in one frame collapse into a single sound;
        // stacking identical samples only changes loudness.
        playFootstep();
    }

    if (pushingStone != pushing_) {
        pushing_ = pushingStone;
        if (pushing_) {
            startStoneDrag();
        } else {
            stopStoneDrag();
        }
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
    ma_sound_set_volume(&sound, soundVolume_ * footstepVolume_);
    ma_sound_seek_to_pcm_frame(&sound, 0);
    ma_sound_start(&sound);
}

void AudioSystem::startStoneDrag()
{
    if (!engine_->engineInitialized || engine_->loadedStoneDrags.empty()) {
        return;
    }

    const auto& loaded = engine_->loadedStoneDrags;
    int pick = loaded[random_() % loaded.size()];
    if (static_cast<int>(loaded.size()) > 1) {
        while (pick == lastDragIndex_) {
            pick = loaded[random_() % loaded.size()];
        }
    }
    lastDragIndex_ = pick;
    engine_->activeStoneDrag = pick;

    ma_sound& sound = engine_->stoneDragSounds[static_cast<size_t>(pick)];
    // A prior stop-with-fade leaves a scheduled stop time on the sound; if it
    // is not cleared the restarted sound is silently stopped again right away
    // (see the note on ma_sound_stop_with_fade_* in miniaudio.h).
    ma_sound_reset_stop_time_and_fade(&sound);
    ma_sound_set_looping(&sound, MA_TRUE);
    ma_sound_seek_to_pcm_frame(&sound, 0);
    ma_sound_set_volume(&sound, soundVolume_ * stoneDragVolume_);
    // The prior stop-with-fade also leaves the fade at zero; fade back in.
    ma_sound_set_fade_in_milliseconds(&sound, 0.0f, 1.0f, dragFadeInMilliseconds);
    ma_sound_start(&sound);
}

void AudioSystem::stopStoneDrag()
{
    if (engine_->activeStoneDrag < 0) {
        return;
    }
    ma_sound& sound =
        engine_->stoneDragSounds[static_cast<size_t>(engine_->activeStoneDrag)];
    ma_sound_stop_with_fade_in_milliseconds(&sound, dragFadeOutMilliseconds);
    engine_->activeStoneDrag = -1;
}

void AudioSystem::playMusicForLevel(int level)
{
    if (!engine_->engineInitialized) {
        return;
    }

    int target = -1;
    const auto& tracks = manifest_->musicTracks();
    for (size_t i = 0; i < tracks.size(); ++i) {
        if (tracks[i].level == level) {
            target = static_cast<int>(i);
            break;
        }
    }
    const auto& loaded = engine_->loadedMusic;
    if (target >= 0 &&
        std::find(loaded.begin(), loaded.end(), target) == loaded.end()) {
        log::warning(log::Category::Audio)
            << "Audio: music track not loaded for level " << level;
        target = -1;
    }

    if (target == engine_->activeMusic) {
        return;
    }

    if (engine_->activeMusic >= 0) {
        ma_sound_stop_with_fade_in_milliseconds(
            &engine_->musicSounds[static_cast<size_t>(engine_->activeMusic)],
            musicCrossfadeMilliseconds);
    }
    engine_->activeMusic = target;
    if (target < 0) {
        return;
    }

    ma_sound& sound = engine_->musicSounds[static_cast<size_t>(target)];
    ma_sound_reset_stop_time_and_fade(&sound);
    ma_sound_set_looping(&sound, MA_TRUE);
    ma_sound_seek_to_pcm_frame(&sound, 0);
    ma_sound_set_volume(&sound, musicVolume_ * tracks[static_cast<size_t>(target)].volume);
    ma_sound_set_fade_in_milliseconds(&sound, 0.0f, 1.0f, musicCrossfadeMilliseconds);
    ma_sound_start(&sound);
}

void AudioSystem::setMusicVolume(float volume)
{
    musicVolume_ = std::clamp(volume, 0.0f, 1.0f);
    if (engine_->engineInitialized && engine_->activeMusic >= 0) {
        const std::size_t active = static_cast<std::size_t>(engine_->activeMusic);
        ma_sound_set_volume(
            &engine_->musicSounds[active],
            musicVolume_ * manifest_->musicTracks()[active].volume);
    }
}

void AudioSystem::setFootstepVolume(float volume)
{
    footstepVolume_ = std::clamp(volume, 0.0f, 1.0f);
}

void AudioSystem::setSoundVolume(float volume)
{
    soundVolume_ = std::clamp(volume, 0.0f, 1.0f);
    if (engine_->engineInitialized && engine_->activeStoneDrag >= 0) {
        ma_sound_set_volume(
            &engine_->stoneDragSounds[static_cast<size_t>(engine_->activeStoneDrag)],
            soundVolume_ * stoneDragVolume_);
    }
}

void AudioSystem::setStoneDragVolume(float volume)
{
    stoneDragVolume_ = std::clamp(volume, 0.0f, 1.0f);
    if (engine_->engineInitialized && engine_->activeStoneDrag >= 0) {
        ma_sound_set_volume(
            &engine_->stoneDragSounds[static_cast<size_t>(engine_->activeStoneDrag)],
            soundVolume_ * stoneDragVolume_);
    }
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
