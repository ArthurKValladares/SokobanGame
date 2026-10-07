#include "TestHarness.hpp"

#include "engine/TaskSystem.hpp"
#include "engine/render/IsoScenePreparer.hpp"
#include "engine/render/GroundChunkGeometry.hpp"
#include "engine/render/ProcessedGroundArtifact.hpp"
#include "engine/render/AnimationController.hpp"
#include "engine/AssetManifest.hpp"
#include "engine/GameplayLoop.hpp"
#include "engine/RenderFrameBuilder.hpp"
#include "engine/OverworldView.hpp"
#include "engine/Profiler.hpp"
#include "engine/TurretRayTrace.hpp"
#include "engine/AnimationPreviewScene.hpp"
#include "engine/LevelEditor.hpp"
#include "engine/InputRouter.hpp"
#include "engine/render/RenderAssetRequirements.hpp"
#include "engine/ui/FontAtlas.hpp"
#include "engine/ui/TitleScreen.hpp"
#include "engine/ui/OptionsMenu.hpp"
#include "engine/ui/InputPrompts.hpp"
#include "engine/ui/SelectorPrompt.hpp"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>

namespace allocationTracking {

struct Header {
    void* base = nullptr;
};

std::atomic_bool measuring = false;
std::atomic_uint64_t allocationCount = 0;
std::atomic_uint64_t allocatedBytes = 0;

void* allocate(std::size_t size, std::size_t alignment)
{
    const std::size_t storedSize = std::max(size, std::size_t { 1 });
    if (storedSize > std::numeric_limits<std::size_t>::max() -
            sizeof(Header) - alignment) {
        throw std::bad_alloc();
    }
    void* const base = std::malloc(
        storedSize + sizeof(Header) + alignment - 1);
    if (!base) {
        throw std::bad_alloc();
    }
    const std::uintptr_t first = reinterpret_cast<std::uintptr_t>(base) +
        sizeof(Header);
    const std::uintptr_t aligned =
        (first + alignment - 1) & ~(alignment - 1);
    auto* const header = reinterpret_cast<Header*>(aligned) - 1;
    header->base = base;
    if (measuring.load(std::memory_order_relaxed)) {
        allocationCount.fetch_add(1, std::memory_order_relaxed);
        allocatedBytes.fetch_add(size, std::memory_order_relaxed);
    }
    return reinterpret_cast<void*>(aligned);
}

void deallocate(void* pointer) noexcept
{
    if (!pointer) {
        return;
    }
    const auto* const header =
        reinterpret_cast<const Header*>(pointer) - 1;
    std::free(header->base);
}

struct Sample {
    uint64_t allocations = 0;
    uint64_t bytes = 0;
};

template <typename Function>
Sample measure(Function&& function)
{
    allocationCount.store(0, std::memory_order_relaxed);
    allocatedBytes.store(0, std::memory_order_relaxed);
    measuring.store(true, std::memory_order_release);
    try {
        function();
    } catch (...) {
        measuring.store(false, std::memory_order_release);
        throw;
    }
    measuring.store(false, std::memory_order_release);
    return {
        .allocations = allocationCount.load(std::memory_order_relaxed),
        .bytes = allocatedBytes.load(std::memory_order_relaxed),
    };
}

} // namespace allocationTracking

void* operator new(std::size_t size)
{
    return allocationTracking::allocate(size, alignof(std::max_align_t));
}

void* operator new[](std::size_t size)
{
    return allocationTracking::allocate(size, alignof(std::max_align_t));
}

void* operator new(std::size_t size, std::align_val_t alignment)
{
    return allocationTracking::allocate(
        size, static_cast<std::size_t>(alignment));
}

void* operator new[](std::size_t size, std::align_val_t alignment)
{
    return allocationTracking::allocate(
        size, static_cast<std::size_t>(alignment));
}

void operator delete(void* pointer) noexcept
{
    allocationTracking::deallocate(pointer);
}

void operator delete[](void* pointer) noexcept
{
    allocationTracking::deallocate(pointer);
}

void operator delete(void* pointer, std::size_t) noexcept
{
    allocationTracking::deallocate(pointer);
}

void operator delete[](void* pointer, std::size_t) noexcept
{
    allocationTracking::deallocate(pointer);
}

void operator delete(void* pointer, std::align_val_t) noexcept
{
    allocationTracking::deallocate(pointer);
}

void operator delete[](void* pointer, std::align_val_t) noexcept
{
    allocationTracking::deallocate(pointer);
}

void operator delete(void* pointer, std::size_t, std::align_val_t) noexcept
{
    allocationTracking::deallocate(pointer);
}

void operator delete[](
    void* pointer,
    std::size_t,
    std::align_val_t) noexcept
{
    allocationTracking::deallocate(pointer);
}

namespace {

template <typename Function>
void checkNoFrameAllocations(const char* name, Function&& function)
{
    for (int index = 0; index < 8; ++index) {
        function();
    }
    const auto sample = allocationTracking::measure([&] {
        for (int index = 0; index < 64; ++index) {
            function();
        }
    });
    std::cout << name << " count=" << sample.allocations
              << " bytes=" << sample.bytes << '\n';
    CHECK_MESSAGE(sample.allocations == 0, name);
}

void testGameplayFrameAllocations()
{
    using namespace sokoban;
    const auto manifest = AssetManifest::loadFromFile(
        std::filesystem::path(SOKOBAN_TEST_ASSET_DIR) / "manifest.json");
    // A valid mirror preview exercises cached rules data and interpolated beams.
    const Level level = Level::loadFromLayers({
        { ".....", ".....", ".....", ".....", "....." },
        { "     ", "     ", "     ", "C 1  ", "     " },
    }, "allocation fixture");
    GameplaySession session;
    session.reset(level);
    GameplayPresentation presentation;
    presentation.resetEntities(session.state());
    PresentationSettings settings;
    FrameArena arena("test render", renderFrameArenaBytes());
    CHECK(session.activationPreview(level).has_value());
    const auto draw = [&] {
        arena.reset();
        const auto& preview = session.activationPreview(level);
        const auto& endPreview = session.activationPreview(level, true);
        const auto frame = RenderFrameBuilder::buildGameplay({
            .manifest = manifest,
            .level = level,
            .state = session.state(),
            .moving = session.moving(),
            .projectedState = session.projectedStateView(),
            .presentation = presentation,
            .settings = settings,
            .levelLocation = LevelLocation { 0, 0 },
            .cachedActivationPreviews = true,
            .activationPreview = preview ? &*preview : nullptr,
            .projectedActivationPreview = endPreview ? &*endPreview : nullptr,
        }, arena);
        CHECK(frame.tiles.arenaBacked());
        CHECK(!arena.exhausted());
    };
    checkNoFrameAllocations("gameplay_render", draw);
    checkNoFrameAllocations("idle_gameplay_update", [&] {
        (void)GameplayLoop::update(level, session, presentation, {}, 0.0001f, false);
    });

    session.queueMove(MoveDirection::Down);
    (void)GameplayLoop::update(level, session, presentation, {}, 0.0001f, false);
    CHECK(session.moving());
    checkNoFrameAllocations("moving_gameplay_update", [&] {
        (void)GameplayLoop::update(level, session, presentation, {}, 0.0001f, false);
        (void)session.projectedStateView();
        (void)session.activationPreview(level);
        (void)session.activationPreview(level, true);
    });
    checkNoFrameAllocations("moving_gameplay_render", draw);
    checkNoFrameAllocations("held_move_update", [&] {
        GameplayLoop::InputFrame input;
        input.down.down = true;
        (void)GameplayLoop::update(level, session, presentation, input, 0.0001f, false);
    });

    const auto previewModel = manifest.playerModel();
    checkNoFrameAllocations("animation_preview_scene", [&] {
        arena.reset();
        const auto frame = animationPreviewScene::build(previewModel, manifest, settings, &arena);
        CHECK(frame.tiles.arenaBacked());
    });
}

void testDualSceneFrameAllocations()
{
    using namespace sokoban;
    const auto manifest = AssetManifest::loadFromFile(
        std::filesystem::path(SOKOBAN_TEST_ASSET_DIR) / "manifest.json");
    const auto level = Level::loadFromLayers({ { "....." }, { "C 1  " } }, "preview fixture");
    GameplaySession session;
    session.reset(level);
    GameplayPresentation presentation;
    presentation.resetEntities(session.state());
    FrameArena arena("main and preview", 2 * renderFrameArenaBytes());
    IsoScenePreparer mainPreparer;
    IsoScenePreparer previewPreparer;
    PreparedRenderScene mainScene;
    PreparedRenderScene previewScene;
    RenderAssetRequirements mainRequirements;
    RenderAssetRequirements previewRequirements;
    TaskSystem tasks(2);
    checkNoFrameAllocations("main_and_selector_preview", [&] {
        arena.reset();
        const auto& activation = session.activationPreview(level);
        const auto build = [&] {
            return RenderFrameBuilder::buildGameplay({
                .manifest = manifest, .level = level, .state = session.state(),
                .projectedState = session.projectedStateView(), .presentation = presentation,
                .settings = {}, .cachedActivationPreviews = true,
                .activationPreview = activation ? &*activation : nullptr,
            }, arena);
        };
        const auto main = build();
        const auto preview = build();
        tasks.parallelFor(2, 1, [&](std::size_t begin, std::size_t end) {
            for (std::size_t index = begin; index < end; ++index) {
                if (index == 0) mainPreparer.prepare(main, { 1280, 720 }, mainScene);
                else previewPreparer.prepare(preview, { 960, 540 }, previewScene);
            }
        });
        renderAssetRequirementsForFrame(main, mainRequirements);
        renderAssetRequirementsForFrame(preview, previewRequirements);
        mainRequirements.merge(previewRequirements);
        CHECK(main.tiles.arenaBacked());
        CHECK(preview.tiles.arenaBacked());
        CHECK(!arena.exhausted());
    });
}

void testEditorFrameAllocations()
{
    using namespace sokoban;
    const auto manifest = AssetManifest::loadFromFile(
        std::filesystem::path(SOKOBAN_TEST_ASSET_DIR) / "manifest.json");
    LevelEditor editor;
    editor.newDocument(5, 3, false);
    CHECK(editor.setCell({ 1, 1, 1 }, TileType::TurretEast));
    CHECK(editor.setCell({ 2, 1, 1 }, TileType::PressurePlate));
    CHECK(editor.setCell({ 3, 1, 1 }, TileType::Gate));
    CHECK(editor.setCell({ 1, 0, 1 }, TileType::RailStopEastWest));
    CHECK(editor.setCell({ 1, 0, 1 }, TileType::Minecart));
    CHECK(editor.setCell({ 2, 0, 1 }, TileType::RailStraightEastWest));
    CHECK(editor.setCell({ 3, 0, 1 }, TileType::RailStopEastWest));
    CHECK(editor.minecartRoutesView().size() == 1);
    CHECK(editor.minecartRoutesView().front().has_value());
    const auto originalRoute = *editor.minecartRoutesView().front();
    CHECK(editor.setCell({ 2, 0, 1 }, TileType::Air));
    CHECK(editor.minecartRoutesView().front()->cells.size() == 1);
    CHECK(editor.tryUndoEdit());
    CHECK(editor.minecartRoutesView().front() == originalRoute);
    const auto originalGroups = editor.linkGroups();
    CHECK(editor.setLinkColor({ 3, 1, 1 }, { 0.1f, 0.2f, 0.3f }));
    CHECK(editor.linkGroupsView().size() == originalGroups.size() + 1);
    CHECK(editor.tryUndoEdit());
    CHECK(editor.linkGroupsView().size() == originalGroups.size());
    Level::Definition neighbor;
    neighbor.layers = { { "....." }, { "C    " } };
    neighbor.layers[1][0][3] = tileTypeToChar(TileType::Gate);
    neighbor.gates.push_back({ .cell = { 3, 0, 1 }, .pressurePlates = { { 2, 0, 1 } } });
    const auto decorationModel = std::ranges::find_if(manifest.models(),
        [](const auto& model) { return model.name.size() > 15; });
    CHECK(decorationModel != manifest.models().end());
    neighbor.decorations.push_back({ .model = decorationModel->name,
        .position = { 0.5f, 0.5f, 1.0f } });
    const std::array neighbors { RenderFrameBuilder::EditorInput::OverworldNeighbor {
        .screen = 1, .origin = { 5, 0 }, .width = 5, .height = 1, .definition = &neighbor,
    } };
    FrameArena arena("editor test", renderFrameArenaBytes());
    checkNoFrameAllocations("editor_render_with_neighbors", [&] {
        arena.reset();
        const auto frame = RenderFrameBuilder::buildEditor({
            .manifest = manifest, .editor = editor, .settings = {},
#if SOKOBAN_ENABLE_DEBUG_UI
            .showDebugView = true,
#endif
            .overworldNeighbors = neighbors,
        }, arena);
        CHECK(frame.tiles.arenaBacked());
        CHECK(!arena.exhausted());
    });
}

void testTurretQueryAllocations()
{
    using namespace sokoban;
    const auto openCell = [](GridPosition3) { return rules::TurretRayCell::Open; };
    const auto loopPortal = [](GridPosition3 cell, MoveDirection)
        -> std::optional<Level::PortalCrossing> {
        return cell.x == 1
            ? std::optional<Level::PortalCrossing> { { { 0, 0, 0 }, { 1, 0 }, 0 } }
            : std::nullopt;
    };
    checkNoFrameAllocations("turret_loop_detection", [&] {
        std::size_t segments = 0;
        CHECK(!rules::traceTurretRayWithSegments({ 0, 0, 0 }, MoveDirection::Right,
            std::nullopt, openCell, loopPortal, [&](const auto&) { ++segments; }));
        CHECK(segments < 16);
    });
    const std::string row = std::string("C ") +
        tileTypeToChar(TileType::TurretEast) + "  " + tileTypeToChar(TileType::TurretWest);
    const auto level = Level::loadFromLayers({ { "......" }, { row } }, "turret query");
    const auto state = rules::initialState(level);
    checkNoFrameAllocations("turret_pending_motion", [&] {
        CHECK(rules::hasPendingMotion(level, state));
    });
}

void testAnimationRequestAllocations()
{
    using namespace sokoban;
    AnimationController controller;
    controller.configure(RenderModel { 1 }, RenderAnimation { 1 });
    GltfAnimationClip clip;
    clip.durationSeconds = 1.0f;
    clip.channels.resize(1);
    controller.setClip(RenderAnimation { 1 }, std::move(clip));
    RenderFrameData frame;
    frame.tiles.push_back({
        .model = RenderModel { 1 },
        .animation = RenderAnimation { 1 },
        .animationInstanceId = 7,
    });
    FrameArena arena("animation test", 4096);
    checkNoFrameAllocations("animation_requests", [&] {
        arena.reset();
        frame.animationTransitionTimeSeconds += 0.01f;
        frame.tiles[0].animationTimeSeconds += 0.01f;
        const auto requests = controller.updateInstances(frame, arena);
        CHECK(requests.arenaBacked());
        CHECK(requests.size() == 1);
        CHECK(!arena.exhausted());
    });
}

void testOverworldFrameAllocations()
{
    using namespace sokoban;
    const auto map = OverworldMap::load(
        std::filesystem::path(SOKOBAN_TEST_ASSET_DIR).parent_path() /
            "levels/overworld");
    const auto state = rules::initialState(map.level());
    const auto cell = state.players.front().cell;
    FrameArena arena("overworld test", renderFrameArenaBytes());
    for (float overview : { 0.0f, 1.0f }) {
        checkNoFrameAllocations("overworld_view_and_fog", [&] {
            arena.reset();
            const auto view = calculateOverworldView(map, map.startScreen(), state,
                state, { static_cast<float>(cell.x), static_cast<float>(cell.y),
                    static_cast<float>(cell.z) }, overview, &arena);
            RenderFrameData frame(arena);
            appendOverworldFogVolumes(frame.overworldFogVolumes, map,
                view.visibleScreens, {});
            CHECK(view.visibleScreens.arenaBacked());
            CHECK(!view.visibleScreens.empty());
            CHECK(frame.overworldFogVolumes.size() == view.visibleScreens.size());
            CHECK(!arena.exhausted());
        });
    }
}

void testInputFrameAllocations()
{
    using namespace sokoban;
    InputState input(false);
    InputRouter router;
    SDL_Event event {};
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.scancode = SDL_SCANCODE_W;
    for (const auto context : { InputRouter::RoutingContext {},
            InputRouter::RoutingContext { .optionsOpen = true },
            InputRouter::RoutingContext { .editorEditing = true } }) {
        checkNoFrameAllocations("input_routing", [&] {
            input.beginFrame();
            input.handleEvent(event);
            (void)router.routeFrame(input, context);
            (void)input.activeGamepadPresentation();
        });
    }
}

void testMenuFrameAllocations()
{
    using namespace sokoban;
    const auto font = FontAtlas::load(
        std::filesystem::path(SOKOBAN_TEST_ASSET_DIR) / "ui/Karla-Regular.ttf");
    UiContext ui(font);
    OptionsMenuView view;
    UserSettings settings;
    for (const auto page : { OptionsMenuPage::Main, OptionsMenuPage::Graphics,
            OptionsMenuPage::Audio, OptionsMenuPage::Controls,
            OptionsMenuPage::EditorControls, OptionsMenuPage::QuitConfirmation }) {
        OptionsMenuState state { .open = true, .page = page };
        checkNoFrameAllocations("options_menu", [&] {
            ui.beginFrame({ 1280.0f, 720.0f }, {}, false, false);
            (void)view.draw(ui, { 1280.0f, 720.0f }, state, settings);
            ui.endFrame();
            CHECK(!ui.frameArena().exhausted());
        });
    }
    const auto manifest = AssetManifest::loadFromFile(
        std::filesystem::path(SOKOBAN_TEST_ASSET_DIR) / "manifest.json");
    InputPromptCatalog prompts(SOKOBAN_TEST_ASSET_DIR, manifest);
    for (const auto type : { SDL_GAMEPAD_TYPE_UNKNOWN, SDL_GAMEPAD_TYPE_XBOXONE,
            SDL_GAMEPAD_TYPE_PS5, SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO,
            SDL_GAMEPAD_TYPE_GAMECUBE }) {
        const GamepadPresentation gamepad {
            .type = type, .name = "A long unknown controller name for allocation testing",
        };
        OptionsMenuState state { .open = true, .page = OptionsMenuPage::Controls,
            .controlsBindingDevice = BindingDeviceClass::Gamepad };
        checkNoFrameAllocations("controller_binding_menu", [&] {
            ui.beginFrame({ 1280.0f, 720.0f }, {}, false, false);
            (void)view.draw(ui, { 1280.0f, 720.0f }, state, settings, &prompts, &gamepad);
            ui.endFrame();
            CHECK(!ui.frameArena().exhausted());
        });
    }
    OptionsMenuState controls { .open = true, .page = OptionsMenuPage::EditorControls };
    checkNoFrameAllocations("keyboard_binding_menu", [&] {
        ui.beginFrame({ 1280.0f, 720.0f }, {}, false, false);
        (void)view.draw(ui, { 1280.0f, 720.0f }, controls, settings, &prompts);
        ui.endFrame();
        CHECK(!ui.frameArena().exhausted());
    });
    InputBindings bindings = defaultInputBindings();
    bindings.forAction(InputAction::MenuConfirm) = {
        KeyboardBinding { "Keypad Enter", keyModifierAll },
    };
    const auto expectedEnter = SelectorPrompt::bindingLabel(bindings,
        InputAction::MenuConfirm, BindingDeviceClass::Keyboard).value();
    checkNoFrameAllocations("selector_prompts", [&] {
        ui.beginFrame({ 1280.0f, 720.0f }, {}, false, false);
        const auto enter = SelectorPrompt::bindingLabel(bindings,
            InputAction::MenuConfirm, BindingDeviceClass::Keyboard, ui.frameArena());
        const auto preview = SelectorPrompt::bindingLabel(bindings,
            InputAction::PreviewScreen, BindingDeviceClass::Keyboard, ui.frameArena());
        SelectorPrompt::draw(ui, { 400.0f, 400.0f }, enter, preview);
        CHECK(enter == expectedEnter);
        ui.endFrame();
    });

    TitleScreen title;
    title.setSaveSlots({ { .state = SaveSlotState::Ready, .completedLevels = 12 },
        { .state = SaveSlotState::Recoverable }, { .state = SaveSlotState::Empty } }, 0);
    title.open();
    const auto drawTitle = [&](const TitleScreenInput& input = {}) {
        ui.beginFrame({ 1280.0f, 720.0f }, {}, false, false);
        (void)title.draw(ui, { 1280.0f, 720.0f }, input);
        ui.endFrame();
        CHECK(!ui.frameArena().exhausted());
    };
    checkNoFrameAllocations("title_menu", [&] { drawTitle(); });
    drawTitle({ .down = true });
    drawTitle({ .confirm = true });
    CHECK(title.page() == TitleScreen::Page::SaveSlots);
    checkNoFrameAllocations("save_slots_menu", [&] { drawTitle(); });
    drawTitle({ .right = true });
    drawTitle({ .confirm = true });
    CHECK(title.page() == TitleScreen::Page::SlotDeleteConfirmation);
    checkNoFrameAllocations("delete_slot_menu", [&] { drawTitle(); });

}

void testProfilerFrameAllocations()
{
    using namespace sokoban;
    CpuProfiler& profiler = CpuProfiler::instance();
    profiler.setEnabled(true);
    profiler.setPaused(false);
    profiler.clearCapture();
    profiler.setCurrentThreadName("Allocation audit thread");
    uint64_t frameIndex = 0;
    const auto frame = [&] {
        profiler.beginFrame(++frameIndex);
        {
            CpuProfileScope outer("Allocation audit outer frame scope");
            CpuProfileScope inner("Allocation audit nested scope");
        }
        profiler.endFrame();
    };
    // Each ring slot owns a historical frame; warm every retained slot.
    for (std::size_t index = 0; index < CpuProfiler::capturedFrameCapacity * 2; ++index) {
        frame();
    }
    checkNoFrameAllocations("profiler_capture", frame);
    profiler.setEnabled(false);
}

sokoban::RenderFrameData makeScene(uint32_t edge)
{
    sokoban::RenderFrameData frame;
    frame.viewMode = sokoban::RenderViewMode::Isometric3D;
    frame.levelWidth = edge;
    frame.levelHeight = edge;
    frame.lighting.shadows.enabled = true;
    frame.lighting.pointLightCount = 4;
    frame.tiles.reserve(static_cast<std::size_t>(edge) * edge);
    for (uint32_t y = 0; y < edge; ++y) {
        for (uint32_t x = 0; x < edge; ++x) {
            frame.tiles.push_back({
                .cell = {
                    static_cast<int>(x), static_cast<int>(y), 0 },
                .position = {
                    static_cast<float>(x), static_cast<float>(y) },
                .color = { 0.4f, 0.5f, 0.3f, 1.0f },
                .height = 1.0f,
                .renderableId = static_cast<uint64_t>(y) * edge + x + 1,
            });
        }
    }
    return frame;
}

void testWarmPreparationAllocationCounts()
{
    constexpr std::size_t measuredFrames = 64;
    const sokoban::RenderFrameData frame = makeScene(32);
    sokoban::TaskSystem tasks(2);

    sokoban::IsoScenePreparer serialPreparer;
    sokoban::PreparedRenderScene serialScene;
    sokoban::IsoScenePreparer parallelPreparer;
    sokoban::PreparedRenderScene parallelScene;
    for (int warmup = 0; warmup < 8; ++warmup) {
        serialPreparer.prepare(
            frame, { 1920.0f, 1080.0f }, serialScene);
        parallelPreparer.prepare(
            frame, { 1920.0f, 1080.0f }, parallelScene, &tasks);
    }

    const allocationTracking::Sample serial =
        allocationTracking::measure([&] {
            for (std::size_t index = 0; index < measuredFrames; ++index) {
                serialPreparer.prepare(
                    frame, { 1920.0f, 1080.0f }, serialScene);
            }
        });
    const allocationTracking::Sample parallel =
        allocationTracking::measure([&] {
            for (std::size_t index = 0; index < measuredFrames; ++index) {
                parallelPreparer.prepare(
                    frame, { 1920.0f, 1080.0f }, parallelScene, &tasks);
            }
        });

    std::cout << "scene_preparation_allocations frames=" << measuredFrames
              << " serial_count=" << serial.allocations
              << " serial_bytes=" << serial.bytes
              << " parallel_count=" << parallel.allocations
              << " parallel_bytes=" << parallel.bytes << '\n';
    CHECK_MESSAGE(serial.allocations == 0,
        "warm serial scene preparation stays allocation free");
    CHECK_MESSAGE(parallel.allocations == 0,
        "warm parallel scene preparation stays allocation free");
}

void testWarmParallelForAllocationCounts()
{
    constexpr std::size_t measuredLoops = 64;
    constexpr std::size_t itemCount = 4096;
    sokoban::TaskSystem tasks(2);
    std::atomic_uint64_t checksum { 0 };
    const auto work = [&](std::size_t begin, std::size_t end) {
        uint64_t local = 0;
        for (std::size_t index = begin; index < end; ++index) {
            local += index * 2654435761ULL;
        }
        checksum.fetch_add(local, std::memory_order_relaxed);
    };
    for (int warmup = 0; warmup < 8; ++warmup) {
        tasks.parallelFor(itemCount, 128, work);
    }

    const allocationTracking::Sample parallelFor =
        allocationTracking::measure([&] {
            for (std::size_t index = 0; index < measuredLoops; ++index) {
                tasks.parallelFor(itemCount, 128, work);
            }
        });

    std::cout << "parallel_for_allocations loops=" << measuredLoops
              << " count=" << parallelFor.allocations
              << " bytes=" << parallelFor.bytes
              << " checksum="
              << checksum.load(std::memory_order_relaxed) << '\n';
    CHECK_MESSAGE(parallelFor.allocations == 0,
        "warm parallelFor coordination stays allocation free");
}

void testWarmGroundRimPreparationAllocations()
{
    using namespace sokoban;
    constexpr uint32_t edge = 8;
    RenderFrameData frame = makeScene(edge);
    frame.cameraExtent = RenderFrameData::CameraExtent { 0, 0, 0, edge, edge, 1 };
    for (auto& tile : frame.tiles) {
        tile.model = { 1 };
        tile.groundTop = true;
        tile.effect = RenderSurfaceEffect::GroundSplat;
        tile.groundRimSides = static_cast<uint8_t>(
            (tile.cell.x == 0 ? groundWestSide : 0) |
            (tile.cell.y == 0 ? groundNorthSide : 0) |
            (tile.cell.x == static_cast<int>(edge - 1) ? groundEastSide : 0) |
            (tile.cell.y == static_cast<int>(edge - 1) ? groundSouthSide : 0));
        tile.groundRimWidth = 0.12f;
        tile.groundRimDepth = 0.10f;
        tile.groundSplat = GroundSplatTextures {
            .base = { 1 }, .detail = { 2 }, .splatMap = { 3 }, .rimWall = { 4 },
        };
    }
    TaskSystem tasks(2);
    IsoScenePreparer serialPreparer;
    IsoScenePreparer parallelPreparer;
    PreparedRenderScene serialScene;
    PreparedRenderScene parallelScene;
    frame.processedGroundArtifact = std::make_shared<const ProcessedGroundArtifact>(
        buildProcessedGroundArtifact(frame.tiles, 123));
    uint32_t step = 0;
    const auto changePaintAndCamera = [&] {
        ++step;
        const bool alternate = (step % 2) != 0;
        frame.cameraOffset = alternate ? Vec2 { 0.1f, -0.2f } : Vec2 { -0.2f, 0.1f };
        frame.cameraYawDegrees = alternate ? 31.0f : 36.0f;
        frame.tiles[0].color = alternate ? Vec4 { 0.2f, 0.4f, 0.7f, 1 } : Vec4 { 0.6f, 0.3f, 0.2f, 1 };
        frame.tiles[0].groundSplat->splatMap = RenderTexture { alternate ? 5U : 6U };
    };
    const auto prepareSerial = [&] {
        serialPreparer.prepare(frame, { 1920, 1080 }, serialScene);
    };
    const auto prepareParallel = [&] {
        parallelPreparer.prepare(frame, { 1920, 1080 }, parallelScene, &tasks);
    };
    checkNoFrameAllocations("rim_scene_camera_and_paint_serial", [&] {
        changePaintAndCamera();
        prepareSerial();
    });
    CHECK(serialScene.reusedGroundRimSurfaces == 4 * edge - 4);
    CHECK(serialScene.generatedGroundRimSurfaces == 0);
    CHECK(serialScene.bakedGroundRimSurfaces == 4 * edge - 4);
    checkNoFrameAllocations("rim_scene_camera_and_paint_parallel", [&] {
        changePaintAndCamera();
        prepareParallel();
    });
    CHECK(parallelScene.reusedGroundRimSurfaces == 4 * edge - 4);
    CHECK(parallelScene.generatedGroundRimSurfaces == 0);
    CHECK(parallelScene.bakedGroundRimSurfaces == 4 * edge - 4);
    checkNoFrameAllocations("rim_scene_reorder_serial", [&] {
        std::rotate(frame.tiles.begin(), frame.tiles.begin() + 1, frame.tiles.end());
        prepareSerial();
    });
    CHECK(serialScene.reusedGroundRimSurfaces == 4 * edge - 4);
    CHECK(serialScene.generatedGroundRimSurfaces == 0);
    checkNoFrameAllocations("rim_scene_reorder_parallel", [&] {
        std::rotate(frame.tiles.begin(), frame.tiles.begin() + 1, frame.tiles.end());
        prepareParallel();
    });
    CHECK(parallelScene.reusedGroundRimSurfaces == 4 * edge - 4);
    CHECK(parallelScene.generatedGroundRimSurfaces == 0);
    const auto editOneProfile = [&] {
        const auto tile = std::find_if(frame.tiles.begin(), frame.tiles.end(),
            [](const auto& candidate) { return candidate.cell == GridPosition3 { 0, 0, 0 }; });
        CHECK(tile != frame.tiles.end());
        if (tile != frame.tiles.end()) {
            tile->groundRimWidth = tile->groundRimWidth == 0.12f ? 0.16f : 0.12f;
            tile->groundRimDepth = tile->groundRimDepth == 0.10f ? 0.08f : 0.10f;
        }
    };
    checkNoFrameAllocations("rim_scene_profile_edit_serial", [&] {
        editOneProfile();
        prepareSerial();
    });
    CHECK(serialScene.generatedGroundRimSurfaces + serialScene.importedGroundRimSurfaces == 1);
    CHECK(serialScene.reusedGroundRimSurfaces == 4 * edge - 5);
    checkNoFrameAllocations("rim_scene_profile_edit_parallel", [&] {
        editOneProfile();
        prepareParallel();
    });
    CHECK(parallelScene.generatedGroundRimSurfaces + parallelScene.importedGroundRimSurfaces == 1);
    CHECK(parallelScene.reusedGroundRimSurfaces == 4 * edge - 5);
}

void testWarmGroundChunkPreparationAllocations()
{
    using namespace sokoban;
    constexpr uint32_t edge = 16;
    constexpr Vec2 extent { 1920, 1080 };
    RenderFrameData frame = makeScene(edge);
    frame.lighting.pointLightCount = 0;
    frame.cameraExtent = RenderFrameData::CameraExtent { 0, 0, 0, edge, edge, 1 };
    frame.groundChunksRequested = true;
    frame.groundChunksReady = true;
    frame.groundSplat = { .base = { 1 }, .detail = { 2 }, .splatMap = { 3 } };
    for (auto& tile : frame.tiles) {
        tile.model = { 1 };
        tile.renderableId = 0;
        tile.groundTop = true;
        tile.groundGeometryEligible = true;
        tile.effect = RenderSurfaceEffect::GroundSplat;
        tile.groundRimSides = static_cast<uint8_t>(
            (tile.cell.x == 0 ? groundWestSide : 0) |
            (tile.cell.y == 0 ? groundNorthSide : 0) |
            (tile.cell.x == static_cast<int>(edge - 1) ? groundEastSide : 0) |
            (tile.cell.y == static_cast<int>(edge - 1) ? groundSouthSide : 0));
        tile.groundRimWidth = 0.12f;
        tile.groundRimDepth = 0.10f;
    }
    GroundChunkGeometryCache geometryCache;
    frame.groundChunks = geometryCache.update(frame.tiles);
    const auto originalGeometry = frame.groundChunks;
    TaskSystem tasks(2);
    IsoScenePreparer serialPreparer;
    IsoScenePreparer parallelPreparer;
    PreparedRenderScene serial;
    PreparedRenderScene parallel;
    uint32_t step = 0;
    const auto updateFrame = [&] {
        ++step;
        frame.cameraYawDegrees = (step % 2) == 0 ? 31.0f : 36.0f;
        frame.groundSplat.splatMap = RenderTexture { (step % 2) == 0 ? 3U : 4U };
        std::rotate(frame.tiles.begin(), frame.tiles.begin() + 1, frame.tiles.end());
        frame.groundChunks = geometryCache.update(frame.tiles);
    };
    const auto pick = [&](const PreparedRenderScene& scene, const IsoScenePreparer& preparer) {
        const Vec3 clip = IsoScenePreparer::projectIsoPoint(scene.isoLayout, extent, { 4.5f, 4.5f, 1 });
        const Vec2 pixel { (clip.x + 1.0f) * extent.x * 0.5f, (1.0f - clip.y) * extent.y * 0.5f };
        (void)preparer.pickGridCell(scene, pixel, extent, edge, edge);
        (void)preparer.pickGroundPoint(scene, pixel, extent);
    };
    checkNoFrameAllocations("chunk_scene_camera_paint_reorder_serial", [&] {
        updateFrame();
        serialPreparer.prepare(frame, extent, serial);
        pick(serial, serialPreparer);
    });
    CHECK(serial.groundChunks == originalGeometry);
    CHECK(serial.groundChunkDraws.size() == 4);
    CHECK(serial.shadowFaces.empty());
    CHECK(serial.opaqueFaceIndices.empty());
    checkNoFrameAllocations("chunk_scene_camera_paint_reorder_parallel", [&] {
        updateFrame();
        parallelPreparer.prepare(frame, extent, parallel, &tasks);
        pick(parallel, parallelPreparer);
    });
    CHECK(parallel.groundChunks == originalGeometry);
    CHECK(parallel.groundChunkDraws.size() == 4);
    CHECK(parallel.shadowFaces.empty());
    CHECK(parallel.opaqueFaceIndices.empty());
}

} // namespace

int main()
{
    std::cout << std::unitbuf;
    testWarmPreparationAllocationCounts();
    testWarmGroundRimPreparationAllocations();
    testWarmGroundChunkPreparationAllocations();
    testWarmParallelForAllocationCounts();
    testGameplayFrameAllocations();
    testTurretQueryAllocations();
    testEditorFrameAllocations();
    testDualSceneFrameAllocations();
    testAnimationRequestAllocations();
    testOverworldFrameAllocations();
    testInputFrameAllocations();
    testMenuFrameAllocations();
    testProfilerFrameAllocations();
    if (failures == 0) {
        std::cout << "ScenePreparationAllocationTests: " << checks
                  << " checks passed\n";
        return 0;
    }
    return 1;
}
