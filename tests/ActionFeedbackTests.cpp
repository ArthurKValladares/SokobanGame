#include "TestAssetRoot.hpp"
#include "TestHarness.hpp"

#include "engine/ActionFeedbackConfig.hpp"
#include "engine/ActionFeedbackEffects.hpp"
#include "engine/AssetManifest.hpp"
#include "engine/GameplayLoop.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace {

using namespace sokoban;

Level makeLevel(const std::vector<std::vector<std::string>>& layers)
{
    std::vector<std::string> lines;
    for (std::size_t layer = 0; layer < layers.size(); ++layer) {
        lines.push_back("@layer " + std::to_string(layer));
        lines.insert(lines.end(), layers[layer].begin(), layers[layer].end());
        lines.emplace_back();
    }
    return Level::loadFromLines(lines, "action feedback test");
}

// One board, its session and its presentation, stepped the way the game steps
// them.
struct Board {
    explicit Board(Level loaded)
        : level(std::move(loaded))
    {
        session.reset(level);
        session.setStepDurationSeconds(0.25f);
        presentation.resetEntities(session.state());
    }

    std::vector<GameplayFeedbackCue> update(GameplayLoop::InputFrame input, float dt)
    {
        presentation.advanceClocks(dt, false);
        return GameplayLoop::update(level, session, presentation, input, dt, true).feedback;
    }

    [[nodiscard]] Vec2 bumpOffset() const
    {
        return presentation.stepBumpOffset(presentation.players()[0].motion.target.id);
    }

    Level level;
    GameplaySession session;
    GameplayPresentation presentation;
};

constexpr GameplayLoop::InputFrame pressRight { .right = { .pressed = true, .down = true } };
constexpr GameplayLoop::InputFrame holdRight { .right = { .down = true } };

std::size_t count(const std::vector<GameplayFeedbackCue>& cues, GameplayFeedback kind)
{
    return static_cast<std::size_t>(std::ranges::count(cues, kind, &GameplayFeedbackCue::kind));
}

void testPushedStoneRaisesDustWhereItSetsOff()
{
    TEST("pushedStoneRaisesDustWhereItSetsOff");
    Board board(makeLevel({ { "......" }, { "CR    " } }));
    const auto pushed = board.update({ .right = { .pressed = true } }, 0.05f);
    CHECK(pushed.size() == 1);
    if (pushed.size() == 1) {
        CHECK(pushed[0].kind == GameplayFeedback::PushOff);
        // Under the stone, on the surface it stood on, heading its way.
        checkNear(pushed[0].position.x, 1.5f, "push dust x");
        checkNear(pushed[0].position.y, 0.5f, "push dust y");
        checkNear(pushed[0].position.z, 1.0f, "push dust z");
        CHECK(pushed[0].direction == Vec2({ 1.0f, 0.0f }));
    }
    // Once, however many frames the push takes, and nothing where it stops.
    CHECK(board.update({}, 0.1f).empty());
    CHECK(board.update({}, 0.3f).empty());
    CHECK(!board.session.moving());
    CHECK(board.session.state().movables[0].cell == GridPosition3({ 2, 0, 1 }));

    // Undo is the world running backward, and raises nothing.
    CHECK(board.update({ .undoPressed = true }, 0.05f).empty());
    CHECK(board.update({}, 0.5f).empty());
    CHECK(board.session.state().movables[0].cell == GridPosition3({ 1, 0, 1 }));

    // A long frame that swallows the whole push still delivers its dust.
    CHECK(count(board.update({ .right = { .pressed = true } }, 0.6f),
        GameplayFeedback::PushOff) == 1);
}

void testWalkingRaisesNothing()
{
    TEST("walkingRaisesNothing");
    Board board(makeLevel({ { "......" }, { "C     " } }));
    CHECK(board.update({ .right = { .pressed = true } }, 0.05f).empty());
    CHECK(board.update({}, 0.3f).empty());
    CHECK(board.session.state().players[0].cell == GridPosition3({ 1, 0, 1 }));
}

void testStoneAndHeroLandWithDustFromADrop()
{
    TEST("stoneAndHeroLandWithDustFromADrop");
    // A ledge two cells long, one layer above the rest of the floor.
    Board board(makeLevel({ { "......" }, { "..    " }, { "CR    " } }));
    const auto started = board.update({ .right = { .pressed = true } }, 0.05f);
    CHECK(count(started, GameplayFeedback::PushOff) == 1);
    CHECK(count(started, GameplayFeedback::Landing) == 0);
    std::vector<GameplayFeedbackCue> landed;
    for (int frame = 0; frame < 40 && board.session.moving(); ++frame) {
        const auto cues = board.update({}, 1.0f / 60.0f);
        landed.insert(landed.end(), cues.begin(), cues.end());
    }
    CHECK(board.session.state().movables[0].cell == GridPosition3({ 2, 0, 1 }));
    CHECK(landed.size() == 1);
    float stoneStrength = 0.0f;
    if (landed.size() == 1) {
        CHECK(landed[0].kind == GameplayFeedback::Landing);
        checkNear(landed[0].position.x, 2.5f, "landing x");
        checkNear(landed[0].position.y, 0.5f, "landing y");
        checkNear(landed[0].position.z, 1.0f, "landing z");
        stoneStrength = landed[0].strength;
        CHECK(stoneStrength > 0.5f && stoneStrength <= 1.0f);
    }

    // A hero is lighter than a stone and comes down softer.
    Board walker(makeLevel({ { "......" }, { "..    " }, { " C    " } }));
    std::vector<GameplayFeedbackCue> stepped = walker.update({ .right = { .pressed = true } }, 0.05f);
    for (int frame = 0; frame < 40 && walker.session.moving(); ++frame) {
        const auto cues = walker.update({}, 1.0f / 60.0f);
        stepped.insert(stepped.end(), cues.begin(), cues.end());
    }
    CHECK(walker.session.state().players[0].cell == GridPosition3({ 2, 0, 1 }));
    CHECK(stepped.size() == 1);
    if (stepped.size() == 1) {
        CHECK(stepped[0].kind == GameplayFeedback::Landing);
        CHECK(stepped[0].strength > 0.0f && stepped[0].strength < stoneStrength);
    }
}

void testStoneSinkingInWaterRaisesNoLandingDust()
{
    TEST("stoneSinkingInWaterRaisesNoLandingDust");
    Board board(makeLevel({ { "..W..." }, { "CR    " } }));
    std::vector<GameplayFeedbackCue> cues = board.update({ .right = { .pressed = true } }, 0.05f);
    for (int frame = 0; frame < 40 && board.session.moving(); ++frame) {
        const auto more = board.update({}, 1.0f / 60.0f);
        cues.insert(cues.end(), more.begin(), more.end());
    }
    CHECK(board.session.state().movables[0].fallen);
    // It ground the earth it left, and then met water: the ripples' business.
    CHECK(count(cues, GameplayFeedback::PushOff) == 1);
    CHECK(count(cues, GameplayFeedback::Landing) == 0);
}

void testCarriedAndClimbingUnitsDoNotLand()
{
    TEST("carriedAndClimbingUnitsDoNotLand");
    const Level level = makeLevel({ { "....." }, { ".  L." }, { "C    " } });
    GameplayPresentation presentation;
    GameplaySession::Action action;
    action.before = rules::initialState(level);
    action.after = action.before;
    const EntityTarget hero { EntityKind::Player,
        resolvedEntityId(EntityKind::Player, action.before.players[0].id, 0) };
    const ActionMotionTrack drop {
        .target = hero,
        .from = { 0.0f, 0.0f, 2.0f },
        .to = { 1.0f, 0.0f, 1.0f },
        .startSeconds = 0.0f,
        .durationSeconds = 0.25f,
    };
    action.presentation.motions = { drop };
    action.presentation.durationSeconds = 0.25f;
    auto cues = presentation.buildActionFeedbackCues(level, action);
    CHECK(cues.size() == 1);
    if (cues.size() == 1) {
        CHECK(cues[0].kind == GameplayFeedback::Landing);
        checkNear(cues[0].triggerSeconds, 0.25f, "a landing is due as the drop ends");
        CHECK(cues[0].direction == Vec2({ 1.0f, 0.0f }));
    }

    // The same travel at the same times as a minecart: the hero rode it down.
    action.presentation.motions.push_back({
        .target = { EntityKind::Minecart, resolvedEntityId(EntityKind::Minecart, invalidEntityId, 0) },
        .from = { 0.0f, 0.0f, 2.0f },
        .to = { 1.0f, 0.0f, 1.0f },
        .startSeconds = 0.0f,
        .durationSeconds = 0.25f,
    });
    CHECK(presentation.buildActionFeedbackCues(level, action).empty());

    // Down onto a ladder is a climb.
    ActionMotionTrack climb = drop;
    climb.from = { 4.0f, 0.0f, 2.0f };
    climb.to = { 3.0f, 0.0f, 1.0f };
    action.presentation.motions = { climb };
    CHECK(presentation.buildActionFeedbackCues(level, action).empty());

    // Below the board, or half sunk, is no ground to raise dust from.
    ActionMotionTrack sunk = drop;
    sunk.to.z = 0.4f;
    action.presentation.motions = { sunk };
    CHECK(presentation.buildActionFeedbackCues(level, action).empty());

    // And an action running backward has no moments of its own.
    action.presentation.motions = { drop };
    action.reversed = true;
    CHECK(presentation.buildActionFeedbackCues(level, action).empty());
}

void testWalkingIntoAWallBumpsOncePerPress()
{
    TEST("walkingIntoAWallBumpsOncePerPress");
    Board board(makeLevel({ { "...." }, { "C#  " } }));
    const GameState before = board.session.state();

    const auto pressed = board.update(pressRight, 1.0f / 60.0f);
    CHECK(pressed.size() == 1);
    if (pressed.size() == 1) {
        CHECK(pressed[0].kind == GameplayFeedback::Bump);
        // Where hero and wall meet, at the hero's feet.
        checkNear(pressed[0].position.x, 1.0f, "bump x");
        checkNear(pressed[0].position.y, 0.5f, "bump y");
        checkNear(pressed[0].position.z, 1.0f, "bump z");
        CHECK(pressed[0].direction == Vec2({ 1.0f, 0.0f }));
    }

    // The hero leans toward the wall and comes back; holding the direction
    // down neither repeats the bump nor keeps it leaning.
    float furthest = 0.0f;
    std::size_t extra = 0;
    for (int frame = 0; frame < 60; ++frame) {
        extra += board.update(holdRight, 1.0f / 60.0f).size();
        const Vec2 offset = board.bumpOffset();
        CHECK(offset.x >= 0.0f && offset.y == 0.0f);
        furthest = std::max(furthest, offset.x);
    }
    CHECK(extra == 0);
    CHECK(furthest > config::actionFeedbackBumpDistance * 0.8f);
    CHECK(furthest <= config::actionFeedbackBumpDistance + 0.0001f);
    CHECK(board.bumpOffset() == Vec2());

    // Nothing happened as far as the rules can tell.
    CHECK(board.session.state() == before);
    CHECK(board.session.playerMoveCount() == 0);
    CHECK(!board.session.moving());
    checkNear(board.presentation.players()[0].motion.renderPosition.x, 0.0f,
        "a bump leaves the hero's own position alone");

    // Letting go and pressing again is a new attempt.
    CHECK(board.update({}, 1.0f / 60.0f).empty());
    CHECK(count(board.update(pressRight, 1.0f / 60.0f), GameplayFeedback::Bump) == 1);

    // Restarting takes a bump in flight with it.
    CHECK(board.bumpOffset().x >= 0.0f);
    board.presentation.resetEntities(board.session.state());
    CHECK(board.bumpOffset() == Vec2());
}

void testHeldDirectionBumpsWhenTheWalkEndsAtAWall()
{
    TEST("heldDirectionBumpsWhenTheWalkEndsAtAWall");
    Board board(makeLevel({ { "....." }, { "C  # " } }));
    (void)board.update(pressRight, 1.0f / 60.0f);
    std::size_t bumps = 0;
    for (int frame = 0; frame < 150; ++frame) {
        const auto cues = board.update(holdRight, 1.0f / 60.0f);
        for (const GameplayFeedbackCue& cue : cues) {
            CHECK(cue.kind == GameplayFeedback::Bump);
            ++bumps;
            // Not before the hero has arrived in front of the wall.
            CHECK(board.session.state().players[0].cell == GridPosition3({ 2, 0, 1 }));
            CHECK(!board.presentation.players()[0].motion.moving);
            checkNear(cue.position.x, 3.0f, "the bump is against the wall");
        }
    }
    // Two and a half seconds against the wall, and one bump.
    CHECK(bumps == 1);
    CHECK(board.session.state().players[0].cell == GridPosition3({ 2, 0, 1 }));
}

void testTurningAwayBeforeTheWallCancelsItsBump()
{
    TEST("turningAwayBeforeTheWallCancelsItsBump");
    // The wall is two steps ahead; the hero is told to turn before reaching it.
    Board board(makeLevel({ { ".....", "....." }, { "C #  ", "     " } }));
    (void)board.update(pressRight, 1.0f / 60.0f);
    std::size_t bumps = 0;
    // Held until the step toward the wall is under way, then down instead.
    for (int frame = 0; frame < 10; ++frame) {
        bumps += board.update(holdRight, 1.0f / 60.0f).size();
    }
    CHECK(board.session.moving());
    for (int frame = 0; frame < 90; ++frame) {
        bumps += board.update({ .down = { .pressed = frame == 0, .down = true } },
            1.0f / 60.0f).size();
    }
    CHECK(bumps <= 1);
    // Whatever it bumped, it was not the wall it turned away from.
    CHECK(board.session.state().players[0].cell.y == 1);
}

void testLeaningOffTheBoardRaisesNoDust()
{
    TEST("leaningOffTheBoardRaisesNoDust");
    // The board ends at the hero's left. The step cannot be taken, the hero
    // leans all the same, and there is nothing there to raise dust against.
    Board board(makeLevel({ { ".." }, { "C " } }));
    const GameState before = board.session.state();
    const GameplayLoop::InputFrame pressLeft { .left = { .pressed = true, .down = true } };
    CHECK(board.update(pressLeft, 1.0f / 60.0f).empty());
    CHECK(board.session.state() == before);
    CHECK(!board.session.moving());
    (void)board.update({ .left = { .down = true } }, 2.0f / 60.0f);
    CHECK(board.bumpOffset().x < 0.0f);
}

void testSessionReportsBlockedStepsWithoutKeepingThem()
{
    TEST("sessionReportsBlockedStepsWithoutKeepingThem");
    const Level level = makeLevel({ { "...." }, { "C#  " } });
    GameplaySession session;
    session.reset(level);
    CHECK(session.takeBlockedSteps().empty());

    session.queueMove(MoveDirection::Right);
    CHECK(!session.tryStartNextAction(level, {}));
    auto blocked = session.takeBlockedSteps();
    CHECK(blocked.size() == 1);
    if (blocked.size() == 1) {
        CHECK(blocked[0].direction == MoveDirection::Right);
        CHECK(blocked[0].pressed);
        CHECK(blocked[0].controller == session.activeHeroController());
    }
    CHECK(session.takeBlockedSteps().empty());

    // Held, it is reported on every attempt, and a session nobody asks does
    // not pile the reports up.
    const GameplaySession::Controls held { .horizontalMove = MoveDirection::Right };
    for (int attempt = 0; attempt < 500; ++attempt) {
        CHECK(!session.tryStartNextAction(level, held));
    }
    blocked = session.takeBlockedSteps();
    CHECK(blocked.size() == 1);
    if (blocked.size() == 1) {
        CHECK(!blocked[0].pressed);
    }

    // A step that can be taken is not reported, and a reset forgets.
    session.queueMove(MoveDirection::Right);
    CHECK(!session.tryStartNextAction(level, {}));
    session.reset(level);
    CHECK(session.takeBlockedSteps().empty());
    const Level open = makeLevel({ { "...." }, { "C   " } });
    session.reset(open);
    session.queueMove(MoveDirection::Right);
    CHECK(session.tryStartNextAction(open, {}));
    CHECK(session.takeBlockedSteps().empty());
}

void testCuesBecomeDust(const AssetManifest& manifest)
{
    TEST("cuesBecomeDust");
    const ActionFeedbackEffects effects(manifest);
    const auto emitted = [&](GameplayFeedbackCue cue) {
        ParticleSystem particles(7U);
        effects.emit(particles, cue);
        particles.update(0.05f);
        RenderFrameData frame;
        particles.appendRenderData(frame);
        return frame;
    };
    const Vec3 at { 4.5f, 2.5f, 1.0f };
    for (const GameplayFeedback kind : { GameplayFeedback::PushOff,
             GameplayFeedback::Landing, GameplayFeedback::Bump }) {
        const RenderFrameData frame = emitted({
            .kind = kind, .position = at, .direction = { 1.0f, 0.0f } });
        CHECK(frame.particles.size() >= 4);
        for (const auto& particle : frame.particles) {
            // Dust: textured, above the ground it rose from, near its cause,
            // dimmer than white and hidden by whatever stands in front of it.
            CHECK(!particle.texture.isNone());
            CHECK(particle.position.z > at.z);
            CHECK(std::abs(particle.position.x - at.x) < 1.0f);
            CHECK(std::abs(particle.position.y - at.y) < 1.0f);
            CHECK(particle.size.x > 0.0f && particle.size.x < 1.0f);
            CHECK(particle.color.w > 0.0f && particle.color.w <= config::actionFeedbackDustOpacity);
            CHECK(!particle.drawOnTop);
            CHECK(particle.emissiveStrength <= 1.0f);
        }
    }

    // A pushed stone throws its dust back, never ahead of itself.
    {
        ParticleSystem particles(7U);
        effects.emit(particles, { .kind = GameplayFeedback::PushOff,
            .position = at, .direction = { 0.0f, -1.0f } });
        RenderFrameData early;
        particles.update(0.01f);
        particles.appendRenderData(early);
        RenderFrameData late;
        particles.update(0.3f);
        particles.appendRenderData(late);
        CHECK(early.particles.size() == late.particles.size());
        for (std::size_t index = 0;
             index < std::min(early.particles.size(), late.particles.size()); ++index) {
            CHECK(late.particles[index].position.y > early.particles[index].position.y);
            CHECK(late.particles[index].position.z > early.particles[index].position.z);
        }
    }

    // A harder landing raises more.
    const std::size_t soft = emitted({ .kind = GameplayFeedback::Landing,
        .position = at, .strength = 0.3f }).particles.size();
    const std::size_t hard = emitted({ .kind = GameplayFeedback::Landing,
        .position = at, .strength = 1.0f }).particles.size();
    CHECK(hard > soft);

    // Without its textures it draws nothing rather than white squares.
    ParticleSystem particles(7U);
    ActionFeedbackEffects().emit(particles, { .kind = GameplayFeedback::Landing, .position = at });
    CHECK(particles.activeParticleCount() == 0);
}

void testDustUsesTheSpritesAManifestDeclares()
{
    TEST("dustUsesTheSpritesAManifestDeclares");
    const GameplayFeedbackCue landing {
        .kind = GameplayFeedback::Landing, .position = { 1.5f, 1.5f, 1.0f } };
    const auto textures = [&](const AssetManifest& manifest) {
        ParticleSystem particles(7U);
        ActionFeedbackEffects(manifest).emit(particles, landing);
        particles.update(0.05f);
        RenderFrameData frame;
        particles.appendRenderData(frame);
        std::vector<RenderTexture> used;
        for (const auto& particle : frame.particles) {
            if (std::ranges::find(used, particle.texture) == used.end()) {
                used.push_back(particle.texture);
            }
        }
        return used;
    };

    // The least a manifest may hold, around the textures given.
    const auto manifestWith = [](std::string_view declaredTextures) {
        return AssetManifest::parse(R"json({
          "format": 1,
          "textures": [)json" + std::string(declaredTextures) + R"json(],
          "models": [
            { "name": "Hero", "path": "hero.glb", "geometry": "skinned", "role": "player" },
            { "name": "Enemy", "path": "enemy.glb", "geometry": "skinned", "role": "enemy" }
          ],
          "animations": [
            { "name": "Idle", "path": "a.glb", "role": "player-idle" },
            { "name": "Move", "path": "a.glb", "role": "player-move" },
            { "name": "Push", "path": "a.glb", "role": "player-push" },
            { "name": "Pull", "path": "a.glb", "role": "player-pull" },
            { "name": "Death", "path": "a.glb", "role": "player-death" },
            { "name": "DeadIdle", "path": "a.glb", "role": "player-dead-idle" },
            { "name": "EnemyAttack", "path": "a.glb", "role": "enemy-attack" }
          ],
          "tiles": [
            { "tile": "Player", "model": "Hero" },
            { "tile": "Enemy", "model": "Enemy" }
          ]
        })json");
    };

    // A test fixture, or a manifest older than the dust, declares none of
    // its sprites, and that is no error.
    const AssetManifest bare = manifestWith(
        R"json({ "name": "Smoke01", "path": "smoke01.png" })json");
    CHECK(textures(bare).empty());

    const AssetManifest partial = manifestWith(R"json(
        { "name": "Smoke01", "path": "smoke01.png" },
        { "name": "DustPuff03", "path": "puff.png" })json");
    const std::vector<RenderTexture> used = textures(partial);
    CHECK(used.size() == 1);
    CHECK(!used.empty() && used.front() == partial.textureIdByName("DustPuff03"));
}

} // namespace

int main()
{
    testPushedStoneRaisesDustWhereItSetsOff();
    testWalkingRaisesNothing();
    testStoneAndHeroLandWithDustFromADrop();
    testStoneSinkingInWaterRaisesNoLandingDust();
    testCarriedAndClimbingUnitsDoNotLand();
    testWalkingIntoAWallBumpsOncePerPress();
    testHeldDirectionBumpsWhenTheWalkEndsAtAWall();
    testTurningAwayBeforeTheWallCancelsItsBump();
    testLeaningOffTheBoardRaisesNoDust();
    testSessionReportsBlockedStepsWithoutKeepingThem();
    testCuesBecomeDust(AssetManifest::loadFromFile(testAssetRoot() / "manifest.json"));
    testDustUsesTheSpritesAManifestDeclares();

    if (failures == 0) {
        std::cout << "ActionFeedbackTests: " << checks << " checks passed\n";
        return 0;
    }
    std::cerr << "ActionFeedbackTests: " << failures << " of " << checks
              << " checks failed\n";
    return 1;
}
