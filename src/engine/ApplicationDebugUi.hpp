#pragma once

#include "engine/AsyncSaveStore.hpp"
#include "engine/GameplaySession.hpp"
#include "engine/Input.hpp"
#include "engine/Level.hpp"
#include "engine/PlayerProfile.hpp"
#include "engine/PresentationSettings.hpp"
#include "engine/render/VulkanRenderer.hpp"

#include <functional>

namespace sokoban {

class AudioSystem;

class ApplicationDebugUi {
public:
    struct Context {
        int currentLevel = 0;
        int currentScreen = 0;
        const Level& level;
        GameplaySession& gameplaySession;
        const InputState& input;
        VulkanRenderer& renderer;
        PresentationSettings& settings;
        AudioSystem& audio;
        AsyncSaveStore::Diagnostics saveDiagnostics;
        const PlayerProfile::AudioSettings& audioSettings;
        std::function<void(PlayerProfile::AudioSettings, bool)> updateAudioSettings;
    };

    void draw(const Context& context) const;
};

} // namespace sokoban
