#pragma once

#include "engine/AssetManifest.hpp"
#include "engine/Config.hpp"

#include <algorithm>
#include <filesystem>
#include <memory>
#include <random>

namespace sokoban {

// Pure footstep timing: while walking, one footstep is due immediately and
// then one per interval. Stopping resets the phase so the next walk starts
// with an immediate step. Kept free of audio dependencies so it is testable.
struct FootstepCadence {
    float intervalSeconds = config::footstepIntervalSeconds;

    // Returns the number of footsteps due this frame (usually 0 or 1; capped
    // so a frame hitch cannot queue a burst).
    [[nodiscard]] int update(float dt, bool walking)
    {
        if (!walking) {
            walking_ = false;
            timer_ = 0.0f;
            return 0;
        }

        const float interval = std::max(intervalSeconds, 0.01f);
        if (!walking_) {
            walking_ = true;
            timer_ = interval;
            return 1;
        }

        timer_ -= dt;
        int due = 0;
        while (timer_ <= 0.0f && due < 4) {
            ++due;
            timer_ += interval;
        }
        if (timer_ <= 0.0f) {
            // A hitch larger than the burst cap covers must not become a
            // catch-up barrage afterwards; drop the remaining debt.
            timer_ = interval;
        }
        return due;
    }

private:
    bool walking_ = false;
    float timer_ = 0.0f;
};

// Sound-effect playback built on miniaudio. Owns the audio device/engine and
// the gameplay footstep orchestration. Construction never throws: if no audio
// device or sound file is available the system degrades to silence and
// reports available() == false.
class AudioSystem {
public:
    // audioRoot is the staged runtime content directory; the manifest's "footsteps"
    // and "stone-drag" sound sets and per-level music entries are loaded
    // relative to it. The manifest must outlive this object.
    AudioSystem(std::filesystem::path audioRoot, const AssetManifest& manifest);
    ~AudioSystem();

    AudioSystem(const AudioSystem&) = delete;
    AudioSystem& operator=(const AudioSystem&) = delete;

    // Advances gameplay audio. While the player is walking (or pushing), a
    // footstep variation plays on the cadence interval. While a stone is being
    // pushed, a randomly chosen stone-drag loop plays seamlessly and fades out
    // when the push ends.
    void update(float dt, bool playerWalking, bool pushingStone);

    // Starts the level's manifest soundtrack looping, crossfading from the
    // current track. Levels without music fade out. Re-requesting the level
    // that is already playing is a no-op, so per-screen reloads within a
    // level keep the soundtrack running seamlessly.
    void playMusicForLevel(int level);

    void setMasterVolume(float volume);
    [[nodiscard]] float masterVolume() const { return masterVolume_; }
    void setMusicVolume(float volume);
    [[nodiscard]] float musicVolume() const { return musicVolume_; }
    void setSoundVolume(float volume);
    [[nodiscard]] float soundVolume() const { return soundVolume_; }
    void setFootstepVolume(float volume);
    [[nodiscard]] float footstepVolume() const { return footstepVolume_; }
    void setStoneDragVolume(float volume);
    [[nodiscard]] float stoneDragVolume() const { return stoneDragVolume_; }
    void setFootstepIntervalSeconds(float seconds);
    [[nodiscard]] float footstepIntervalSeconds() const { return cadence_.intervalSeconds; }
    [[nodiscard]] bool available() const;

private:
    struct EngineHandle;

    void playFootstep();
    void startStoneDrag();
    void stopStoneDrag();

    std::unique_ptr<EngineHandle> engine_;
    std::filesystem::path audioRoot_;
    const AssetManifest* manifest_ = nullptr;
    std::mt19937 random_;
    FootstepCadence cadence_;
    float masterVolume_ = config::masterVolume;
    float musicVolume_ = config::musicVolume;
    float soundVolume_ = 1.0f;
    // Seeded from the manifest's sound-set volumes in the constructor.
    float footstepVolume_ = 1.0f;
    float stoneDragVolume_ = 1.0f;
    int lastFootstepIndex_ = -1;
    int lastDragIndex_ = -1;
    bool pushing_ = false;
};

} // namespace sokoban
