#include "engine/GameplayLoop.hpp"
#include "engine/SaveStore.hpp"
#include "engine/Log.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>

using namespace sokoban;

int main(int argc, char** argv)
{
    const Level level = Level::loadFromLines({
        "@layer 0", "..........", "..........", "",
        "@layer 1", "CI      # ", "          ", ""
    }, "review probe");
    GameplaySession session;
    session.reset(level);
    session.setStepDurationSeconds(0.125f);
    GameplayPresentation presentation;
    presentation.resetEntities(session.state());
    const auto update = [&](GameplayLoop::InputFrame input, float dt) {
        (void)GameplayLoop::update(level, session, presentation, input, dt, false);
        std::cout << "slide: dt=" << dt
            << " player=" << session.state().players.front().cell.x << ','
            << session.state().players.front().cell.y
            << " visualBlockX=" << presentation.movables().front().renderPosition.x
            << " committedBlockX=" << session.state().movables.front().cell.x
            << " inFlight=" << session.inFlight().size() << '\n';
    };
    // Binary-exact steps stay below SimulationTiming's 0.1-second frame cap.
    // The player action ends at the end of a frame while the ice slide remains.
    update({.right = {.pressed = true, .down = true}}, 0.0625f);
    update({}, 0.0625f);
    update({}, 0.0625f);
    update({}, 0.0625f);
    update({.down = {.pressed = true, .down = true}}, 0.0625f);
    update({}, 0.0625f);
    update({}, 0.015625f);

    if (argc > 1) {
        const std::filesystem::path root = argv[1];
        std::filesystem::create_directories(root);
        SaveStore store(root);
        PlayerProfile expected;
        expected.unlockedLevel = 7;
        expected.currentLevel = 7;
        if (!store.save(expected)) return 2;
        std::filesystem::create_directories(store.backupPath());
        const auto loaded = store.load();
        std::cout << "recovery: expectedLevel=" << expected.currentLevel
            << " actualLevel=" << loaded.profile.currentLevel
            << " disposition=" << static_cast<int>(loaded.disposition)
            << " status=" << loaded.message << '\n';
        std::ifstream primary(store.primaryPath());
        const std::string contents((std::istreambuf_iterator<char>(primary)), {});
        std::cout << "recovery: primaryStillDecodesAs="
            << decodePlayerProfile(contents).profile.currentLevel << '\n';
    }
    log::shutdown();
}
