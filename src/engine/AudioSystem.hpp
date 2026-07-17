#pragma once

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
    explicit AudioSystem(std::filesystem::path audioRoot);
    ~AudioSystem();

    AudioSystem(const AudioSystem&) = delete;
    AudioSystem& operator=(const AudioSystem&) = delete;

    // Advances gameplay audio. While the player is walking (or pushing), a
    // footstep variation plays on the cadence interval.
    void update(float dt, bool playerWalking);

    void setMasterVolume(float volume);
    [[nodiscard]] float masterVolume() const { return masterVolume_; }
    void setFootstepIntervalSeconds(float seconds);
    [[nodiscard]] float footstepIntervalSeconds() const { return cadence_.intervalSeconds; }
    [[nodiscard]] bool available() const;

private:
    struct EngineHandle;

    void playFootstep();

    std::unique_ptr<EngineHandle> engine_;
    std::filesystem::path audioRoot_;
    std::mt19937 random_;
    FootstepCadence cadence_;
    float masterVolume_ = config::masterVolume;
    int lastFootstepIndex_ = -1;
};

} // namespace sokoban
