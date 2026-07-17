#pragma once

#include "engine/GameplaySession.hpp"
#include "engine/Level.hpp"
#include "engine/PresentationSettings.hpp"
#include "engine/render/VulkanRenderer.hpp"

namespace sokoban {

class AudioSystem;

class ApplicationDebugUi {
public:
    struct Context {
        int currentLevel = 0;
        int currentScreen = 0;
        const Level& level;
        GameplaySession& gameplaySession;
        VulkanRenderer& renderer;
        PresentationSettings& settings;
        AudioSystem& audio;
    };

    void draw(const Context& context) const;
};

} // namespace sokoban
