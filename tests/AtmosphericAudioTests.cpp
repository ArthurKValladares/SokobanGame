#include "TestHarness.hpp"

#include "engine/AtmosphericAudio.hpp"
#include "engine/GameplayLoop.hpp"

#include <cmath>
#include <iostream>
#include <limits>

namespace {

using namespace sokoban;

void testDistanceAndTuning()
{
    TEST("distanceAndTuning");
    AssetManifest::Atmosphere settings;
    CHECK(AtmosphericAudio::distanceGain(0.0f, settings) == 1.0f);
    CHECK(AtmosphericAudio::distanceGain(1.0f, settings) == 0.5f);
    CHECK(AtmosphericAudio::distanceGain(1.99f, settings) > 0.0f);
    CHECK(AtmosphericAudio::distanceGain(2.0f, settings) == 0.0f);
    CHECK(AtmosphericAudio::distanceGain(3.0f, settings) == 0.0f);
    CHECK(AtmosphericAudio::distanceGain(std::numeric_limits<float>::quiet_NaN(), settings) == 0.0f);
    settings.audibleDistanceTiles = 3.0f;
    settings.fullVolumeDistanceTiles = 1.0f;
    settings.falloffExponent = 2.0f;
    CHECK(AtmosphericAudio::distanceGain(0.5f, settings) == 1.0f);
    CHECK(AtmosphericAudio::distanceGain(1.0f, settings) == 1.0f);
    CHECK(AtmosphericAudio::distanceGain(2.0f, settings) == 0.25f);
    CHECK(AtmosphericAudio::distanceGain(3.0f, settings) == 0.0f);
}

void testNearestTileMixingAndReset()
{
    TEST("nearestTileMixingAndReset");
    const Level::Definition shortBelt {
        .layers = { { "............" }, { "C >         " } },
    };
    auto longBelt = shortBelt;
    for (int x = 3; x < 12; ++x) {
        longBelt.layers[1][0][static_cast<std::size_t>(x)] = '<';
    }
    longBelt.layers[1][0][10] = '^';
    // A second nearby belt must not stack another copy of the same loop.
    longBelt.layers[0].push_back("............");
    longBelt.layers[1].push_back("  v         ");
    AtmosphericAudio mixer;
    AssetManifest::Atmosphere conveyor { .source = AssetManifest::AtmosphericSource::Conveyor };
    mixer.reset(Level::loadFromDefinition(shortBelt, "short belt audio"));
    const float single = mixer.gain({ 2.5f, -1.0f, 1.0f }, conveyor);
    CHECK(single > 0.0f && single < 0.5f);
    mixer.reset(Level::loadFromDefinition(longBelt, "long belt audio"));
    CHECK(approximately(mixer.gain({ 2.5f, -1.0f, 1.0f }, conveyor), single));
    CHECK(mixer.gain({ 8, 0, 1 }, conveyor) == 1.0f);
    CHECK(mixer.gain({ 2, 0, 3 }, conveyor) == 0.0f);
    CHECK(mixer.gain({ 2, 0, 2 }, conveyor) == 0.5f);
    CHECK(mixer.gain({ 8, 0, 1 }, {}) == 0.0f);
    mixer.reset(Level::loadFromDefinition({
        .layers = { { "............" }, { "C           " } },
    }, "empty audio"));
    CHECK(mixer.gain({ 8, 0, 1 }, conveyor) == 0.0f);
}

void testCoveredSourcesAndPortals()
{
    TEST("coveredSourcesAndPortals");
    const auto level = Level::loadFromDefinition({
        .layers = { { "...." }, { "Co>R" } },
        .plates = { { { 0, 0, 1 }, TileType::PortalNorth },
                    { { 3, 0, 1 }, TileType::PortalWest } },
    }, "covered audio emitters");
    AtmosphericAudio mixer;
    mixer.reset(level);
    CHECK(mixer.gain({ 2, 0, 1 },
        { .source = AssetManifest::AtmosphericSource::Conveyor }) == 1.0f);
    CHECK(mixer.gain({ 0, 0, 1 }, {}) == 1.0f);
    CHECK(mixer.gain({ 1, 0, 1 }, {}) == 1.0f);
    CHECK(mixer.gain({ 3, 0, 1 }, {}) == 1.0f);
    CHECK(mixer.gain({ 2, 0, 1 }, {}) == 0.5f);
    CHECK(approximately(mixer.gain({ 1.5f, 0.5f, 1 }, {}),
        1.0f - std::sqrt(0.5f) / 2.0f));
}

void testListenerUsesAnimatedControlledHero()
{
    TEST("listenerUsesAnimatedControlledHero");
    const auto level = Level::loadFromDefinition({
        .layers = { { "...." }, { "C   " } },
    }, "listener motion");
    GameplaySession session;
    session.reset(level);
    session.setStepDurationSeconds(0.2f);
    GameplayPresentation presentation;
    presentation.resetEntities(session.state());
    static_cast<void>(GameplayLoop::update(level, session, presentation,
        { .right = { .pressed = true } }, 0.1f, false));
    const auto listener = AtmosphericAudio::listenerPosition(
        session.state(), presentation, session.activeHeroController());
    CHECK(listener.has_value());
    CHECK(listener->x > 0.0f && listener->x < 1.0f);
    CHECK(*listener == presentation.players()[0].motion.renderPosition);
    CHECK(session.state().players[0].cell.x == 0);

    GameState heroes;
    heroes.players = {
        { .id = 33, .cell = { 7, 0, 1 }, .controller = 11 },
        { .id = 22, .cell = { 5, 0, 1 }, .controller = 22 },
        { .id = 11, .cell = { 1, 0, 1 }, .controller = 11 },
    };
    presentation.resetEntities(heroes);
    CHECK(AtmosphericAudio::listenerPosition(heroes, presentation, 11) == Vec3({ 1, 0, 1 }));
    CHECK(AtmosphericAudio::listenerPosition(heroes, presentation, 22) == Vec3({ 5, 0, 1 }));
    heroes.players[2].dead = true;
    CHECK(AtmosphericAudio::listenerPosition(heroes, presentation, 11) == Vec3({ 7, 0, 1 }));
    heroes.players[0].dead = true;
    CHECK(!AtmosphericAudio::listenerPosition(heroes, presentation, 11));
    CHECK(!AtmosphericAudio::listenerPosition({}, presentation, invalidEntityId));
}

} // namespace

int main()
{
    testDistanceAndTuning();
    testNearestTileMixingAndReset();
    testCoveredSourcesAndPortals();
    testListenerUsesAnimatedControlledHero();
    if (failures != 0) {
        return 1;
    }
    std::cout << "AtmosphericAudioTests: " << checks << " checks passed\n";
    return 0;
}
