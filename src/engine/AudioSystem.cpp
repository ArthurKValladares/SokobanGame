#include "engine/AudioSystem.hpp"

#include "engine/Log.hpp"

#include "miniaudio.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <string>
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
    struct OneShotSoundSet {
        std::string name;
        float volume = 1.0f;
        std::vector<ma_sound> sounds;
        std::vector<int> loaded;
        int lastPlayed = -1;
        int activeLoop = -1;
        bool loopRequested = false;
        float loopGain = 1.0f;
        std::optional<AssetManifest::Atmosphere> atmosphere;
    };

    ma_engine engine {};
    bool engineInitialized = false;
    // Fully decoded at load; addresses must stay stable after init, so the
    // vector is sized once and never reallocated.
    std::vector<ma_sound> footstepSounds;
    std::vector<int> loadedFootsteps;
    std::vector<ma_sound> stoneDragSounds;
    std::vector<int> loadedStoneDrags;
    int activeStoneDrag = -1;
    std::vector<OneShotSoundSet> oneShotSoundSets;
    std::vector<ma_sound> musicSounds;
    std::vector<int> loadedMusic;
    int activeMusic = -1;
#if SOKOBAN_ENABLE_DEBUG_UI
    ma_sound previewSound {};
    bool previewInitialized = false;
    float previewVolume = 1.0f;
#endif
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
    const auto startupStarted = std::chrono::steady_clock::now();
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
    engine_->oneShotSoundSets.reserve(manifest.soundSets().size());
    for (const AssetManifest::SoundSet& set : manifest.soundSets()) {
        if (set.name == "footsteps" || set.name == "stone-drag") {
            continue;
        }
        EngineHandle::OneShotSoundSet& oneShot =
            engine_->oneShotSoundSets.emplace_back();
        oneShot.name = set.name;
        oneShot.volume = std::clamp(set.volume, 0.0f, 1.0f);
        oneShot.atmosphere = set.atmosphere;
        loadSoundSet(
            engine_->engine,
            audioRoot_,
            set.files,
            oneShot.sounds,
            oneShot.loaded,
            MA_SOUND_FLAG_DECODE);
    }
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
    log::info(log::Category::Audio)
        << "Audio startup phase (us): total="
        << std::chrono::duration_cast<std::chrono::microseconds>(
               std::chrono::steady_clock::now() - startupStarted).count();
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
    for (EngineHandle::OneShotSoundSet& set : engine_->oneShotSoundSets) {
        for (int index : set.loaded) {
            ma_sound_uninit(&set.sounds[static_cast<size_t>(index)]);
        }
    }
    for (int index : engine_->loadedMusic) {
        ma_sound_uninit(&engine_->musicSounds[static_cast<size_t>(index)]);
    }
#if SOKOBAN_ENABLE_DEBUG_UI
    stopSoundPreview();
#endif
    ma_engine_uninit(&engine_->engine);
}

#if SOKOBAN_ENABLE_DEBUG_UI
bool AudioSystem::previewSoundFile(
    const std::filesystem::path& file, float volume, std::string& error)
{
    stopSoundPreview();
    if (!engine_->engineInitialized) {
        error = "Audio engine is unavailable.";
        return false;
    }
#ifdef _WIN32
    const ma_result result = ma_sound_init_from_file_w(
        &engine_->engine, file.c_str(), MA_SOUND_FLAG_STREAM,
        nullptr, nullptr, &engine_->previewSound);
#else
    const ma_result result = ma_sound_init_from_file(
        &engine_->engine, file.c_str(), MA_SOUND_FLAG_STREAM,
        nullptr, nullptr, &engine_->previewSound);
#endif
    if (result != MA_SUCCESS) {
        error = "Cannot preview sound: " + std::string(ma_result_description(result));
        return false;
    }
    engine_->previewInitialized = true;
    engine_->previewVolume = std::clamp(volume, 0.0f, 1.0f);
    ma_sound_set_looping(&engine_->previewSound, MA_FALSE);
    ma_sound_set_volume(&engine_->previewSound, soundVolume_ * engine_->previewVolume);
    const ma_result started = ma_sound_start(&engine_->previewSound);
    if (started != MA_SUCCESS) {
        error = "Cannot play sound: " + std::string(ma_result_description(started));
        stopSoundPreview();
        return false;
    }
    error.clear();
    return true;
}

void AudioSystem::stopSoundPreview()
{
    if (engine_->previewInitialized) {
        ma_sound_uninit(&engine_->previewSound);
        engine_->previewInitialized = false;
    }
}

bool AudioSystem::soundPreviewPlaying() const
{
    return engine_->previewInitialized &&
        ma_sound_is_playing(&engine_->previewSound) == MA_TRUE;
}
#endif

bool AudioSystem::available() const
{
    return engine_->engineInitialized && !engine_->loadedFootsteps.empty();
}

void AudioSystem::setAtmosphericLevel(const Level& level)
{
    for (const auto& set : engine_->oneShotSoundSets) {
        if (set.atmosphere) {
            setLoopingSound(set.name, false);
        }
    }
    atmosphere_.reset(level);
}

void AudioSystem::update(float dt, bool playerWalking, bool pushingStone,
    bool minecartMoving, bool elevatorMoving, std::optional<Vec3> atmosphereListener)
{
    setLoopingSound("minecart-travel", minecartMoving && dt > 0.0f);
    setLoopingSound("elevator-moving", elevatorMoving && dt > 0.0f);
    for (const auto& set : engine_->oneShotSoundSets) {
        if (!set.atmosphere) {
            continue;
        }
        const float target = atmosphereListener && dt > 0.0f && std::isfinite(dt)
            ? atmosphere_.gain(*atmosphereListener, *set.atmosphere) : 0.0f;
        if (target <= 0.0f) {
            setLoopingSound(set.name, false);
            continue;
        }
        const float current = set.loopRequested ? set.loopGain : 0.0f;
        const float response = 1.0f - std::exp(-dt / config::atmosphereResponseSeconds);
        setLoopingSound(set.name, true, current + (target - current) * response);
    }
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

void AudioSystem::playOneShot(std::string_view soundSetName, float delaySeconds)
{
    if (!engine_->engineInitialized) {
        return;
    }

    const auto found = std::ranges::find_if(
        engine_->oneShotSoundSets,
        [&](const EngineHandle::OneShotSoundSet& set) {
            return set.name == soundSetName;
        });
    if (found == engine_->oneShotSoundSets.end() || found->loaded.empty() || found->atmosphere) {
        return;
    }

    int pick = found->loaded[random_() % found->loaded.size()];
    if (found->loaded.size() > 1) {
        while (pick == found->lastPlayed) {
            pick = found->loaded[random_() % found->loaded.size()];
        }
    }
    found->lastPlayed = pick;

    ma_sound& sound = found->sounds[static_cast<size_t>(pick)];
    ma_sound_reset_stop_time_and_fade(&sound);
    ma_sound_set_fade_in_milliseconds(&sound, 1.0f, 1.0f, 0);
    ma_sound_set_looping(&sound, MA_FALSE);
    ma_sound_set_volume(&sound, soundVolume_ * found->volume);
    ma_sound_seek_to_pcm_frame(&sound, 0);
    ma_sound_set_start_time_in_milliseconds(
        &sound, ma_engine_get_time_in_milliseconds(&engine_->engine) +
            static_cast<ma_uint64>(std::max(delaySeconds, 0.0f) * 1000.0f));
    ma_sound_start(&sound);
}

void AudioSystem::setLoopingSound(std::string_view soundSetName, bool playing, float gain)
{
    if (!engine_->engineInitialized) {
        return;
    }
    const auto found = std::ranges::find_if(engine_->oneShotSoundSets,
        [&](const auto& set) { return set.name == soundSetName; });
    if (found == engine_->oneShotSoundSets.end()) {
        return;
    }
    if (playing) {
        found->loopGain = std::clamp(gain, 0.0f, 1.0f);
        if (found->activeLoop >= 0) {
            ma_sound_set_volume(&found->sounds[static_cast<size_t>(found->activeLoop)],
                soundVolume_ * found->volume * found->loopGain);
        }
    }
    if (found->loopRequested == playing) {
        return;
    }
    found->loopRequested = playing;
    if (!playing) {
        if (found->activeLoop >= 0) {
            ma_sound_stop_with_fade_in_milliseconds(
                &found->sounds[static_cast<size_t>(found->activeLoop)], dragFadeOutMilliseconds);
            found->activeLoop = -1;
        }
        return;
    }
    if (found->loaded.empty()) {
        return;
    }
    const int pick = found->loaded[random_() % found->loaded.size()];
    found->activeLoop = pick;
    ma_sound& sound = found->sounds[static_cast<size_t>(pick)];
    ma_sound_reset_stop_time_and_fade(&sound);
    ma_sound_set_start_time_in_milliseconds(&sound, 0);
    ma_sound_set_looping(&sound, MA_TRUE);
    ma_sound_seek_to_pcm_frame(&sound, 0);
    ma_sound_set_volume(&sound, soundVolume_ * found->volume * found->loopGain);
    if (found->atmosphere) {
        // Attenuation is calculated in tile units, not miniaudio world units.
        ma_sound_set_spatialization_enabled(&sound, MA_FALSE);
    }
    ma_sound_set_fade_in_milliseconds(&sound, 0.0f, 1.0f, dragFadeInMilliseconds);
    ma_sound_start(&sound);
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

void AudioSystem::playMusicForLevel(int level, std::optional<CharacterType> activeCharacter)
{
    if (!engine_->engineInitialized) {
        return;
    }

    const auto& tracks = manifest_->musicTracks();
    const auto* track = manifest_->musicTrackFor(level, activeCharacter);
    const auto& loaded = engine_->loadedMusic;
    const auto loadedIndex = [&](const AssetManifest::MusicTrack* candidate) {
        if (candidate == nullptr) {
            return -1;
        }
        const int index = static_cast<int>(candidate - tracks.data());
        return std::ranges::find(loaded, index) == loaded.end() ? -1 : index;
    };
    int target = loadedIndex(track);
    if (target < 0 && track && track->character) {
        // Loading already reports missing/undecodable files once. A broken
        // character track keeps the level music rather than muting it.
        target = loadedIndex(manifest_->musicTrackFor(level));
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
    const bool alreadyPlaying = ma_sound_is_playing(&sound) == MA_TRUE;
    const float startGain = alreadyPlaying ? ma_sound_get_current_fade_volume(&sound) : 0.0f;
    ma_sound_reset_stop_time_and_fade(&sound);
    ma_sound_set_start_time_in_milliseconds(&sound, 0);
    ma_sound_set_looping(&sound, MA_TRUE);
    if (!alreadyPlaying) {
        ma_sound_seek_to_pcm_frame(&sound, 0);
    }
    ma_sound_set_volume(&sound, musicVolume_ * tracks[static_cast<size_t>(target)].volume);
    ma_sound_set_fade_in_milliseconds(&sound, startGain, 1.0f, musicCrossfadeMilliseconds);
    ma_sound_start(&sound);
}

void AudioSystem::setMusicVolume(float volume)
{
    musicVolume_ = std::clamp(volume, 0.0f, 1.0f);
    if (engine_->engineInitialized) {
        for (const int index : engine_->loadedMusic) {
            const auto slot = static_cast<std::size_t>(index);
            ma_sound_set_volume(&engine_->musicSounds[slot],
                musicVolume_ * manifest_->musicTracks()[slot].volume);
        }
    }
}

void AudioSystem::setFootstepVolume(float volume)
{
    footstepVolume_ = std::clamp(volume, 0.0f, 1.0f);
}

void AudioSystem::setSoundVolume(float volume)
{
    soundVolume_ = std::clamp(volume, 0.0f, 1.0f);
#if SOKOBAN_ENABLE_DEBUG_UI
    if (engine_->previewInitialized) {
        ma_sound_set_volume(&engine_->previewSound, soundVolume_ * engine_->previewVolume);
    }
#endif
    if (engine_->engineInitialized && engine_->activeStoneDrag >= 0) {
        ma_sound_set_volume(
            &engine_->stoneDragSounds[static_cast<size_t>(engine_->activeStoneDrag)],
            soundVolume_ * stoneDragVolume_);
    }
    if (engine_->engineInitialized) {
        for (EngineHandle::OneShotSoundSet& set : engine_->oneShotSoundSets) {
            for (int index : set.loaded) {
                ma_sound& sound = set.sounds[static_cast<size_t>(index)];
                ma_sound_set_volume(
                    &sound,
                    soundVolume_ * set.volume *
                        (ma_sound_is_looping(&sound) ? set.loopGain : 1.0f));
            }
        }
    }
}

void AudioSystem::applyManifestVolumes()
{
    if (manifest_ == nullptr) {
        return;
    }
    footstepVolume_ =
        std::clamp(manifest_->soundSetVolume("footsteps"), 0.0f, 1.0f);
    stoneDragVolume_ =
        std::clamp(manifest_->soundSetVolume("stone-drag"), 0.0f, 1.0f);
    for (EngineHandle::OneShotSoundSet& set : engine_->oneShotSoundSets) {
        set.volume =
            std::clamp(manifest_->soundSetVolume(set.name), 0.0f, 1.0f);
        const auto updated = std::ranges::find(manifest_->soundSets(), set.name,
            &AssetManifest::SoundSet::name);
        const auto atmosphere = updated == manifest_->soundSets().end()
            ? std::nullopt : updated->atmosphere;
        if (set.atmosphere && !atmosphere) {
            setLoopingSound(set.name, false);
        }
        set.atmosphere = atmosphere;
    }
    setSoundVolume(soundVolume_);
    setMusicVolume(musicVolume_);
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
