#include "engine/Application.hpp"

#include "engine/Config.hpp"
#include "engine/DebugUi.hpp"

#include <SDL3/SDL.h>

#if SOKOBAN_ENABLE_DEBUG_UI
#include <imgui.h>
#endif

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <utility>

namespace sokoban {
namespace {

Vec3 toVec3(GridPosition3 position)
{
    return {
        static_cast<float>(position.x),
        static_cast<float>(position.y),
        static_cast<float>(position.z),
    };
}

Vec3 entityRenderTarget(GridPosition3 position, bool fallen)
{
    Vec3 target = toVec3(position);
    if (fallen) {
        target.z -= config::waterDepthBelowGround;
    }
    return target;
}

float gridDistance(Vec3 from, Vec3 to)
{
    return std::abs(to.x - from.x) +
        std::abs(to.y - from.y) +
        std::abs(to.z - from.z);
}

float degreesToRadians(float degrees)
{
    constexpr float pi = 3.14159265358979323846f;
    return degrees * pi / 180.0f;
}

Vec3 sunDirectionFromSpherical(float azimuthDegrees, float tiltDegrees)
{
    const float azimuth = degreesToRadians(azimuthDegrees);
    const float tilt = degreesToRadians(tiltDegrees);
    const float horizontalLength = std::sin(tilt);
    return {
        horizontalLength * std::cos(azimuth),
        horizontalLength * std::sin(azimuth),
        std::cos(tilt),
    };
}

#if SOKOBAN_ENABLE_DEBUG_UI
void drawSunDirectionPreview(Vec3 direction, float tiltDegrees)
{
    constexpr ImVec2 previewSize { 240.0f, 116.0f };
    ImGui::InvisibleButton("sun_direction_preview", previewSize);

    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 max = ImGui::GetItemRectMax();
    ImDrawList* drawList = ImGui::GetWindowDrawList();

    const ImU32 background = ImGui::GetColorU32(ImGuiCol_FrameBg);
    const ImU32 border = ImGui::GetColorU32(ImGuiCol_Border);
    const ImU32 text = ImGui::GetColorU32(ImGuiCol_Text);
    const ImU32 muted = ImGui::GetColorU32(ImGuiCol_TextDisabled);
    const ImU32 sun = ImGui::GetColorU32(ImVec4 { 1.0f, 0.88f, 0.25f, 1.0f });

    drawList->AddRectFilled(min, max, background, 4.0f);
    drawList->AddRect(min, max, border, 4.0f);

    const float radius = 34.0f;
    const ImVec2 topCenter { min.x + 62.0f, min.y + 66.0f };
    const ImVec2 sideCenter { min.x + 178.0f, min.y + 66.0f };

    drawList->AddText(ImVec2 { topCenter.x - 16.0f, min.y + 10.0f }, text, "XY");
    drawList->AddCircle(topCenter, radius, border, 48, 1.5f);
    drawList->AddLine(ImVec2 { topCenter.x - radius, topCenter.y }, ImVec2 { topCenter.x + radius, topCenter.y }, muted);
    drawList->AddLine(ImVec2 { topCenter.x, topCenter.y - radius }, ImVec2 { topCenter.x, topCenter.y + radius }, muted);
    const ImVec2 topEnd {
        topCenter.x + direction.x * radius,
        topCenter.y + direction.y * radius,
    };
    drawList->AddLine(topCenter, topEnd, sun, 2.0f);
    drawList->AddCircleFilled(topEnd, 4.0f, sun);

    drawList->AddText(ImVec2 { sideCenter.x - 16.0f, min.y + 10.0f }, text, "Side");
    drawList->AddCircle(sideCenter, radius, border, 48, 1.5f);
    drawList->AddLine(ImVec2 { sideCenter.x - radius, sideCenter.y }, ImVec2 { sideCenter.x + radius, sideCenter.y }, muted);
    drawList->AddLine(ImVec2 { sideCenter.x, sideCenter.y - radius }, ImVec2 { sideCenter.x, sideCenter.y + radius }, muted);

    const float signedHorizontalLength = std::sin(degreesToRadians(tiltDegrees));
    const ImVec2 sideEnd {
        sideCenter.x + signedHorizontalLength * radius,
        sideCenter.y - direction.z * radius,
    };
    drawList->AddLine(sideCenter, sideEnd, sun, 2.0f);
    drawList->AddCircleFilled(sideEnd, 4.0f, sun);
}
#endif

Vec3 interpolateGridMotion(Vec3 from, Vec3 to, float elapsedSeconds, float secondsPerTile)
{
    const float distance = gridDistance(from, to);
    if (distance <= 0.0001f || secondsPerTile <= 0.0f) {
        return to;
    }

    float remaining = std::min(
        elapsedSeconds / secondsPerTile,
        distance);
    Vec3 result = from;

    auto travelAxis = [&](float target, float& value) {
        const float delta = target - value;
        const float step = std::min(std::abs(delta), remaining);
        if (step > 0.0f) {
            value += std::copysign(step, delta);
            remaining -= step;
        }
    };

    if (to.z > from.z) {
        travelAxis(to.z, result.z);
    }
    travelAxis(to.x, result.x);
    travelAxis(to.y, result.y);
    if (to.z <= from.z) {
        travelAxis(to.z, result.z);
    }
    return result;
}

struct StaticRenderCell {
    TileType tile = TileType::Ground;
    bool active = true;
    bool showGrid = true;
    Vec2 size { 1.0f, 1.0f };
    Vec2 positionOffset {};
    float baseElevation = 0.0f;
    float height = 0.0f;
    uint32_t modelRotationQuarterTurns = 0;
};

RenderModel renderModelForTile(TileType tile)
{
    switch (tile) {
    case TileType::Wall:
        return RenderModel::BricksA;
    case TileType::Rock:
        return RenderModel::Stone;
    case TileType::Water:
        return RenderModel::Water;
    case TileType::Ice:
        return RenderModel::Glass;
    case TileType::ConveyorUp:
    case TileType::ConveyorDown:
    case TileType::ConveyorRight:
    case TileType::ConveyorLeft:
        return RenderModel::Conveyor;
    case TileType::Player:
        return RenderModel::Rogue;
    default:
        return RenderModel::Cube;
    }
}

float clampedTileScale(float scale)
{
    return std::clamp(scale, config::minTileScale, config::maxTileScale);
}

void applyTileScale(RenderFrameData::Tile& tile, float scale)
{
    scale = clampedTileScale(scale);
    if (std::abs(scale - 1.0f) < 0.0001f) {
        return;
    }

    const Vec2 center {
        tile.position.x + tile.size.x * 0.5f,
        tile.position.y + tile.size.y * 0.5f,
    };
    tile.size = {
        tile.size.x * scale,
        tile.size.y * scale,
    };
    tile.position = {
        center.x - tile.size.x * 0.5f,
        center.y - tile.size.y * 0.5f,
    };
    tile.height *= scale;
}

uint32_t facingQuarterTurns(MoveDirection direction)
{
    switch (direction) {
    case MoveDirection::Down:
        return 0;
    case MoveDirection::Left:
        return 1;
    case MoveDirection::Up:
        return 2;
    case MoveDirection::Right:
        return 3;
    }
    return 0;
}

std::optional<MoveDirection> movementDirection(GridPosition3 from, GridPosition3 to)
{
    const int deltaX = to.x - from.x;
    const int deltaY = to.y - from.y;
    if (deltaX < 0) {
        return MoveDirection::Left;
    }
    if (deltaX > 0) {
        return MoveDirection::Right;
    }
    if (deltaY < 0) {
        return MoveDirection::Up;
    }
    if (deltaY > 0) {
        return MoveDirection::Down;
    }
    return std::nullopt;
}

StaticRenderCell staticRenderCellFor(
    const Level& level,
    uint32_t x,
    uint32_t y,
    uint32_t z,
    bool endUnlocked,
    std::optional<TileType> fallenTile,
    float surfaceEntityHeight,
    float surfaceEntitySize,
    uint32_t playerFacingQuarterTurns)
{
    const TileType tile = fallenTile.value_or(level.tileAt(x, y, z));
    const bool surfaceEntity = tileTypeIsSurfaceEntity(tile);
    const bool conveyor = tileTypeIsConveyor(tile);
    const bool submergedEntity = fallenTile.has_value();
    const float centeredOffset = (1.0f - surfaceEntitySize) * 0.5f;
    return {
        .tile = tile,
        .active = tile != TileType::End || endUnlocked,
        .showGrid = tile != TileType::Player,
        .size = surfaceEntity ? Vec2 { surfaceEntitySize, surfaceEntitySize } : Vec2 { 1.0f, 1.0f },
        .positionOffset = surfaceEntity ? Vec2 { centeredOffset, centeredOffset } : Vec2 {},
        .baseElevation = static_cast<float>(z) +
            (tile == TileType::Water ? 1.0f - 2.0f * config::waterDepthBelowGround : 0.0f) -
            (submergedEntity ? config::waterDepthBelowGround : 0.0f),
        .height = surfaceEntity
            ? surfaceEntityHeight
            : (tile == TileType::Water
                    ? config::waterDepthBelowGround
                    : (conveyor
                            ? config::conveyorTileHeight
                            : (tileTypeIsSolidBlock(tile) || tileTypeOccupiesLevelCell(tile) ? 1.0f : 0.0f))),
        .modelRotationQuarterTurns = tile == TileType::Player
            ? playerFacingQuarterTurns
            : (rules::conveyorDirectionForTile(tile)
                    ? facingQuarterTurns(*rules::conveyorDirectionForTile(tile))
                    : 0),
    };
}

Vec4 shade(Vec4 color, float multiplier)
{
    return {
        color.x * multiplier,
        color.y * multiplier,
        color.z * multiplier,
        color.w,
    };
}

void appendLadderRungFace(
    RenderFrameData& frame,
    GridPosition3 groundCell,
    GridPosition3 ladderCell,
    float rungCenter,
    bool preview)
{
    constexpr float rungLengthInset = 0.10f;
    constexpr float rungHalfThickness = 0.07f;
    constexpr float faceOffset = 0.003f;

    const Vec4 color = preview
        ? Vec4 { 0.43f, 0.22f, 0.08f, 0.62f }
        : tileColor(TileType::Ladder);
    const float bottom = static_cast<float>(groundCell.z) + rungCenter - rungHalfThickness;
    const float top = static_cast<float>(groundCell.z) + rungCenter + rungHalfThickness;
    const float gx = static_cast<float>(groundCell.x);
    const float gy = static_cast<float>(groundCell.y);

    auto appendFace = [&](std::array<Vec3, 4> vertices, Vec3 normal) {
        frame.isoFaces.push_back({
            .vertices = vertices,
            .normal = normal,
            .color = color,
        });
    };

    if (ladderCell.x < groundCell.x) {
        const float x = gx - faceOffset;
        const float y0 = gy + rungLengthInset;
        const float y1 = gy + 1.0f - rungLengthInset;
        appendFace({
            Vec3 { x, y1, bottom },
            Vec3 { x, y0, bottom },
            Vec3 { x, y0, top },
            Vec3 { x, y1, top },
        }, { -1.0f, 0.0f, 0.0f });
        return;
    }

    if (ladderCell.x > groundCell.x) {
        const float x = gx + 1.0f + faceOffset;
        const float y0 = gy + rungLengthInset;
        const float y1 = gy + 1.0f - rungLengthInset;
        appendFace({
            Vec3 { x, y0, bottom },
            Vec3 { x, y1, bottom },
            Vec3 { x, y1, top },
            Vec3 { x, y0, top },
        }, { 1.0f, 0.0f, 0.0f });
        return;
    }

    if (ladderCell.y < groundCell.y) {
        const float y = gy - faceOffset;
        const float x0 = gx + rungLengthInset;
        const float x1 = gx + 1.0f - rungLengthInset;
        appendFace({
            Vec3 { x0, y, bottom },
            Vec3 { x1, y, bottom },
            Vec3 { x1, y, top },
            Vec3 { x0, y, top },
        }, { 0.0f, -1.0f, 0.0f });
        return;
    }

    if (ladderCell.y > groundCell.y) {
        const float y = gy + 1.0f + faceOffset;
        const float x0 = gx + rungLengthInset;
        const float x1 = gx + 1.0f - rungLengthInset;
        appendFace({
            Vec3 { x1, y, bottom },
            Vec3 { x0, y, bottom },
            Vec3 { x0, y, top },
            Vec3 { x1, y, top },
        }, { 0.0f, 1.0f, 0.0f });
    }
}

void appendLadderRungs(RenderFrameData& frame, GridPosition3 ladderCell, GridPosition3 groundCell, bool preview = false)
{
    appendLadderRungFace(frame, groundCell, ladderCell, 0.32f, preview);
    appendLadderRungFace(frame, groundCell, ladderCell, 0.68f, preview);
}

template <typename TileAt>
void appendLadderRungsForCell(RenderFrameData& frame, GridPosition3 ladderCell, TileAt tileAt, bool preview = false)
{
    if (tileAt(ladderCell) != TileType::Ladder) {
        return;
    }

    constexpr std::array<GridPosition, 4> offsets {
        GridPosition { 0, -1 },
        GridPosition { 1, 0 },
        GridPosition { 0, 1 },
        GridPosition { -1, 0 },
    };

    for (GridPosition offset : offsets) {
        const GridPosition3 groundCell {
            ladderCell.x + offset.x,
            ladderCell.y + offset.y,
            ladderCell.z,
        };
        if (tileAt(groundCell) == TileType::Ground) {
            appendLadderRungs(frame, ladderCell, groundCell, preview);
        }
    }
}

void appendWaterEdgeFaces(
    RenderFrameData& frame,
    uint32_t width,
    uint32_t height,
    float layerElevation,
    const auto& isUnfilledWaterAt)
{
    const float bottom = layerElevation - config::waterDepthBelowGround;
    const float top = layerElevation;
    const Vec4 color = shade(tileColor(TileType::Ground), 0.78f);

    auto appendEdge = [&](std::array<Vec3, 4> vertices, Vec3 normal, Vec4 edgeColor) {
        frame.isoFaces.push_back({
            .vertices = vertices,
            .normal = normal,
            .color = edgeColor,
        });
    };

    auto neighborIsOpenWater = [&](GridPosition position) {
        return position.x >= 0 &&
            position.y >= 0 &&
            position.x < static_cast<int>(width) &&
            position.y < static_cast<int>(height) &&
            isUnfilledWaterAt(position);
    };

    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            const GridPosition position { static_cast<int>(x), static_cast<int>(y) };
            if (!isUnfilledWaterAt(position)) {
                continue;
            }

            const float left = static_cast<float>(x);
            const float right = left + 1.0f;
            const float nearY = static_cast<float>(y);
            const float farY = nearY + 1.0f;

            if (!neighborIsOpenWater({ position.x, position.y - 1 })) {
                appendEdge({
                    Vec3 { left, nearY, bottom },
                    Vec3 { right, nearY, bottom },
                    Vec3 { right, nearY, top },
                    Vec3 { left, nearY, top },
                }, { 0.0f, -1.0f, 0.0f }, shade(color, 0.92f));
            }
            if (!neighborIsOpenWater({ position.x + 1, position.y })) {
                appendEdge({
                    Vec3 { right, nearY, bottom },
                    Vec3 { right, farY, bottom },
                    Vec3 { right, farY, top },
                    Vec3 { right, nearY, top },
                }, { 1.0f, 0.0f, 0.0f }, shade(color, 0.82f));
            }
            if (!neighborIsOpenWater({ position.x, position.y + 1 })) {
                appendEdge({
                    Vec3 { right, farY, bottom },
                    Vec3 { left, farY, bottom },
                    Vec3 { left, farY, top },
                    Vec3 { right, farY, top },
                }, { 0.0f, 1.0f, 0.0f }, shade(color, 0.70f));
            }
            if (!neighborIsOpenWater({ position.x - 1, position.y })) {
                appendEdge({
                    Vec3 { left, farY, bottom },
                    Vec3 { left, nearY, bottom },
                    Vec3 { left, nearY, top },
                    Vec3 { left, farY, top },
                }, { -1.0f, 0.0f, 0.0f }, shade(color, 0.82f));
            }
        }
    }
}

template <typename CellAt, typename ScaleForTile>
void appendStaticTiles(RenderFrameData& frame, const Level& level, CellAt cellAt, ScaleForTile scaleForTile)
{
    for (uint32_t z = 0; z < level.depth(); ++z) {
        for (uint32_t y = 0; y < level.height(); ++y) {
            for (uint32_t x = 0; x < level.width(); ++x) {
                const StaticRenderCell cell = cellAt(x, y, z);
                if (cell.tile == TileType::Air || cell.tile == TileType::Ladder) {
                    continue;
                }
                RenderFrameData::Tile renderTile {
                    .cell = {
                        static_cast<int>(x),
                        static_cast<int>(y),
                        static_cast<int>(z),
                    },
                    .position = {
                        static_cast<float>(x) + cell.positionOffset.x,
                        static_cast<float>(y) + cell.positionOffset.y,
                    },
                    .size = cell.size,
                    .color = cell.tile == TileType::Player
                        ? Vec4 { 1.0f, 1.0f, 1.0f, 1.0f }
                        : tileColor(cell.tile, cell.active),
                    .baseElevation = cell.baseElevation,
                    .height = cell.height,
                    .showGrid = cell.showGrid,
                    .model = renderModelForTile(cell.tile),
                    .modelRotationQuarterTurns = cell.modelRotationQuarterTurns,
                };
                applyTileScale(renderTile, scaleForTile(cell.tile));
                frame.tiles.push_back(renderTile);
            }
        }
    }
}

} // namespace

Application::Application()
    : window_("Sokoban 3D", 1280, 720)
    , renderer_(window_.nativeHandle(), SOKOBAN_ASSET_DIR)
    , assetRoot_(SOKOBAN_ASSET_DIR)
{
    loadCurrentScreen();

#if SOKOBAN_ENABLE_DEBUG_UI
    levelEditor_.initialize(SOKOBAN_SOURCE_LEVEL_DIR, assetRoot_ / "levels", currentLevel_, currentScreen_);
    DebugUi::addWindow("Engine", [this] {
        drawDebugUi();
    });
    DebugUi::addWindow("Level Editor", [this] {
        levelEditor_.draw({
            .playDraft = [this](Level level) {
                applyLevel(std::move(level));
                levelEditor_.setPlayingDraft(true);
            },
            .returnToCurrentScreen = [this] {
                loadCurrentScreen();
            },
        });
    }, true);
#endif
}

Application::~Application()
{
    DebugUi::clearWindows();
    renderer_.waitIdle();
}

void Application::run()
{
    while (running_) {
        input_.beginFrame();

        SDL_Event event {};
        while (SDL_PollEvent(&event)) {
            renderer_.handleEvent(event);

            const bool isKeyboardEvent = event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP;
            const bool isMouseEvent = event.type == SDL_EVENT_MOUSE_MOTION ||
                event.type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
                event.type == SDL_EVENT_MOUSE_BUTTON_UP;
            bool isEditorEditModifier = false;
#if SOKOBAN_ENABLE_DEBUG_UI
            isEditorEditModifier = isKeyboardEvent &&
                levelEditor_.editingDocument() &&
                (event.key.scancode == SDL_SCANCODE_R ||
                    event.key.scancode == SDL_SCANCODE_D);
#endif
            const bool allowKeyboardInput =
                !isKeyboardEvent ||
                !renderer_.wantsKeyboardCapture() ||
                event.type == SDL_EVENT_KEY_UP ||
                isEditorEditModifier;
            const bool allowMouseInput = !isMouseEvent || !renderer_.wantsMouseCapture() || event.type == SDL_EVENT_MOUSE_BUTTON_UP;
            if (allowKeyboardInput && allowMouseInput) {
                input_.handleEvent(event);
            }

            if (event.type == SDL_EVENT_QUIT) {
                running_ = false;
            }

            if (event.type == SDL_EVENT_KEY_DOWN && event.key.scancode == SDL_SCANCODE_ESCAPE) {
#if SOKOBAN_ENABLE_DEBUG_UI
                if (levelEditor_.playingDraft()) {
                    draftExitConfirmationOpen_ = true;
                } else {
                    quitConfirmationOpen_ = true;
                }
#else
                quitConfirmationOpen_ = true;
#endif
            }
        }

        const float dt = frameTimer_.tick();
        update(dt);

        const Vec2 windowSize = window_.size();
        const Vec2 pixelSize = window_.sizeInPixels();
        const Vec2 mouse = input_.mousePosition();
        const Vec2 mousePixels {
            windowSize.x > 0.0f ? mouse.x * pixelSize.x / windowSize.x : mouse.x,
            windowSize.y > 0.0f ? mouse.y * pixelSize.y / windowSize.y : mouse.y,
        };

        ui_.beginFrame(
            pixelSize,
            mousePixels,
            input_.mouseButtonDown(SDL_BUTTON_LEFT),
            input_.mouseButtonPressed(SDL_BUTTON_LEFT));

        renderer_.beginDebugUiFrame();
        DebugUi::draw();
        drawQuitConfirmation();
        drawDraftExitConfirmation();
        drawEditorModeIndicator();
        ui_.endFrame();
        renderer_.drawFrame(buildRenderFrame(), ui_.drawData());
    }
}

void Application::update(float dt)
{
    playerAnimationTimeSeconds_ += dt;

    if (quitConfirmationOpen_) {
        return;
    }

#if SOKOBAN_ENABLE_DEBUG_UI
    if (draftExitConfirmationOpen_) {
        return;
    }

    if (levelEditor_.editingDocument()) {
        updateEditorPainting();
        return;
    }
#else
    (void)dt;
#endif

    queuePressedCommands();
    advancePlayerMovement(dt);
}

void Application::drawDebugUi()
{
#if SOKOBAN_ENABLE_DEBUG_UI
    ImGui::Text("Level %d Screen %d", currentLevel_, currentScreen_);
    ImGui::Text("Player (%d, %d, %d)", state_.player.x, state_.player.y, state_.player.z);
    ImGui::Text("Player %s", state_.playerDead ? "dead" : "alive");
    ImGui::Text("Movables %zu", state_.movables.size());
    ImGui::Text("History %zu", moveHistory_.size());
    ImGui::Text("End %s", rules::isEndUnlocked(level_, state_) ? "unlocked" : "locked");
    ImGui::Separator();

    constexpr const char* antiAliasingLabels[] { "None", "MSAA 2x", "MSAA 4x", "MSAA 8x" };
    int antiAliasingIndex = static_cast<int>(renderer_.antiAliasingMode());
    if (ImGui::Combo("Anti-aliasing", &antiAliasingIndex, antiAliasingLabels, static_cast<int>(std::size(antiAliasingLabels)))) {
        renderer_.setAntiAliasingMode(static_cast<AntiAliasingMode>(antiAliasingIndex));
    }
    bool wireframeEnabled = renderer_.wireframeEnabled();
    if (ImGui::Checkbox("Wireframe", &wireframeEnabled)) {
        renderer_.setWireframeEnabled(wireframeEnabled);
    }
    ImGui::SameLine();
    float wireframeLineWidth = renderer_.wireframeLineWidth();
    const auto lineWidthRange = renderer_.wireframeLineWidthRange();
    ImGui::BeginDisabled(!renderer_.wideLinesSupported());
    ImGui::SetNextItemWidth(120.0f);
    if (ImGui::SliderFloat("Line Width", &wireframeLineWidth, lineWidthRange[0], lineWidthRange[1], "%.1f")) {
        renderer_.setWireframeLineWidth(wireframeLineWidth);
    }
    ImGui::EndDisabled();

    if (ImGui::CollapsingHeader("Tile Grid")) {
        float gridColor[3] { tileGridLineColor_.x, tileGridLineColor_.y, tileGridLineColor_.z };
        if (ImGui::ColorEdit3("Grid Color", gridColor)) {
            tileGridLineColor_.x = gridColor[0];
            tileGridLineColor_.y = gridColor[1];
            tileGridLineColor_.z = gridColor[2];
        }
        ImGui::DragFloat("Grid Alpha", &tileGridLineColor_.w, 0.01f, 0.0f, 1.0f, "%.2f");
        tileGridLineColor_.w = std::clamp(tileGridLineColor_.w, 0.0f, 1.0f);
        ImGui::DragFloat("Grid Width", &tileGridLineWidth_, 0.05f, 0.0f, 12.0f, "%.2f px");
        tileGridLineWidth_ = std::clamp(tileGridLineWidth_, 0.0f, 12.0f);
    }

    if (ImGui::CollapsingHeader("Tile Geometry")) {
        ImGui::DragFloat(
            "Step Duration",
            &stepDurationSeconds_,
            0.005f,
            0.05f,
            1.0f,
            "%.3f s");
        stepDurationSeconds_ = std::clamp(stepDurationSeconds_, 0.05f, 1.0f);
        if (ImGui::TreeNode("Step Rates (tiles/step)")) {
            ImGui::SliderInt("Player", &stepRates_.playerMove, 0, 5);
            ImGui::SliderInt("Slide", &stepRates_.slide, 0, 5);
            ImGui::SliderInt("Conveyor", &stepRates_.conveyor, 0, 5);
            ImGui::TextDisabled("Movement rates by source; default 1.");
            ImGui::TreePop();
        }
        ImGui::DragFloat(
            "Surface Entity Height",
            &surfaceEntityHeight_,
            0.005f,
            0.01f,
            0.5f,
            "%.3f");
        surfaceEntityHeight_ = std::clamp(surfaceEntityHeight_, 0.01f, 0.5f);
        ImGui::DragFloat(
            "Surface Entity Width / Depth",
            &surfaceEntityWidthDepth_,
            0.01f,
            0.1f,
            1.0f,
            "%.2f");
        surfaceEntityWidthDepth_ = std::clamp(surfaceEntityWidthDepth_, 0.1f, 1.0f);
        ImGui::TextDisabled("End and pressure plate geometry");

        if (ImGui::TreeNode("Tile Scale")) {
            for (const TileTypeDefinition& definition : tileTypeDefinitions()) {
                if (definition.type == TileType::Air) {
                    continue;
                }

                const auto index = static_cast<std::size_t>(definition.type);
                ImGui::DragFloat(
                    definition.name.data(),
                    &tileScales_[index],
                    0.01f,
                    config::minTileScale,
                    config::maxTileScale,
                    "%.2f");
                tileScales_[index] = clampedTileScale(tileScales_[index]);
            }
            ImGui::TextDisabled("Visual scale around each tile's bottom-center.");
            ImGui::TreePop();
        }
    }

    if (ImGui::CollapsingHeader("Lighting")) {
        ImGui::DragFloat("Sun Azimuth", &sunAzimuthDegrees_, 0.5f, -180.0f, 180.0f, "%.1f deg");
        sunAzimuthDegrees_ = std::clamp(sunAzimuthDegrees_, -180.0f, 180.0f);
        ImGui::DragFloat("Sun Tilt", &sunTiltDegrees_, 0.5f, -90.0f, 90.0f, "%.1f deg");
        sunTiltDegrees_ = std::clamp(sunTiltDegrees_, -90.0f, 90.0f);

        const Vec3 sunDirection = sunDirectionFromSpherical(sunAzimuthDegrees_, sunTiltDegrees_);
        ImGui::Text("Unit Vector %.2f, %.2f, %.2f", sunDirection.x, sunDirection.y, sunDirection.z);
        drawSunDirectionPreview(sunDirection, sunTiltDegrees_);

        float color[3] { sunColor_.x, sunColor_.y, sunColor_.z };
        if (ImGui::ColorEdit3("Sun Color", color)) {
            sunColor_ = { color[0], color[1], color[2] };
        }

        ImGui::DragFloat("Sun Intensity", &sunIntensity_, 0.02f, 0.0f, 4.0f, "%.2f");

        float ambientColor[3] { ambientLightColor_.x, ambientLightColor_.y, ambientLightColor_.z };
        if (ImGui::ColorEdit3("Ambient Color", ambientColor)) {
            ambientLightColor_ = { ambientColor[0], ambientColor[1], ambientColor[2] };
        }

        ImGui::DragFloat("Ambient Intensity", &ambientLightIntensity_, 0.01f, 0.0f, 2.0f, "%.2f");

        ImGui::DragFloat("Specular Strength", &specularStrength_, 0.01f, 0.0f, 1.0f, "%.2f");
        specularStrength_ = std::clamp(specularStrength_, 0.0f, 1.0f);
        ImGui::DragFloat("Specular Power", &specularPower_, 0.5f, 1.0f, 128.0f, "%.1f");
        specularPower_ = std::clamp(specularPower_, 1.0f, 128.0f);
        ImGui::DragFloat("Model Shadow Receive", &modelShadowReceive_, 0.01f, 0.0f, 1.0f, "%.2f");
        modelShadowReceive_ = std::clamp(modelShadowReceive_, 0.0f, 1.0f);
        ImGui::TextDisabled("Lower model shadow receive reduces harsh self-shadowing.");

        ImGui::Checkbox("Shadows", &shadowsEnabled_);
        ImGui::BeginDisabled(!shadowsEnabled_);
        ImGui::DragFloat("Shadow Opacity", &shadowOpacity_, 0.01f, 0.0f, 0.85f, "%.2f");
        ImGui::DragFloat("Shadow Bias", &shadowBias_, 0.0005f, 0.0f, 0.05f, "%.4f");
        ImGui::EndDisabled();
    }

    if (ImGui::CollapsingHeader("Rendering Stats")) {
        const RenderStats renderStats = renderer_.renderStats();
        const ImGuiIO& io = ImGui::GetIO();
        ImGui::Text("Frame %.3f ms (%.1f FPS)", io.DeltaTime * 1000.0f, io.Framerate);
        ImGui::Text("Recorded frame %llu", static_cast<unsigned long long>(renderStats.frameIndex));
        ImGui::Text("Swapchain %u x %u, %u images", renderStats.swapchainWidth, renderStats.swapchainHeight, renderStats.swapchainImages);
        ImGui::Text("Active samples %ux", renderStats.activeSamples);
        ImGui::Text("Wireframe %s", renderStats.wireframeEnabled ? "on" : "off");
        ImGui::Text("Wireframe line width %.1f", renderStats.wireframeLineWidth);
        ImGui::Text("Render tiles %u", renderStats.totalTiles);
        ImGui::Text("Visible faces %u", renderStats.visibleFaces);
        ImGui::Text("Draw calls %u", renderStats.drawCalls);
        ImGui::Text("Triangles %u", renderStats.triangles);
        ImGui::Text("Vertices %u", renderStats.vertices);
        ImGui::Text("Pipelines bound %u", renderStats.pipelineBinds);
        ImGui::Text("Render passes %u", renderStats.renderPasses);
        ImGui::Text("Image barriers %u", renderStats.imageBarriers);
        ImGui::Text("Pipeline rebuilds %llu", static_cast<unsigned long long>(renderStats.pipelineRebuilds));
        ImGui::Text("Swapchain recreations %llu", static_cast<unsigned long long>(renderStats.swapchainRecreations));
    }
#endif
}

void Application::drawEditorModeIndicator()
{
#if SOKOBAN_ENABLE_DEBUG_UI
    const char* label = nullptr;
    ImVec4 color {};
    if (levelEditor_.editingDocument()) {
        label = "Editing Draft";
        color = ImVec4(0.24f, 0.58f, 0.95f, 1.0f);
    } else if (levelEditor_.playingDraft()) {
        label = "Testing Draft";
        color = ImVec4(0.20f, 0.72f, 0.38f, 1.0f);
    }

    if (!label) {
        return;
    }

    constexpr float margin = 12.0f;
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(
        ImVec2(viewport->WorkPos.x + margin, viewport->WorkPos.y + viewport->WorkSize.y - margin),
        ImGuiCond_Always,
        ImVec2(0.0f, 1.0f));
    ImGui::SetNextWindowBgAlpha(0.82f);

    constexpr ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoDecoration |
        ImGuiWindowFlags_AlwaysAutoResize |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoFocusOnAppearing |
        ImGuiWindowFlags_NoNav |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoInputs;

    if (ImGui::Begin("EditorModeIndicator", nullptr, flags)) {
        ImGui::TextColored(color, "%s", label);
    }
    ImGui::End();
#endif
}

void Application::drawQuitConfirmation()
{
    if (!quitConfirmationOpen_) {
        return;
    }

    const Vec2 viewport = window_.sizeInPixels();
    const UiRect panel {
        .position = {
            (viewport.x - 360.0f) * 0.5f,
            (viewport.y - 180.0f) * 0.5f,
        },
        .size = { 360.0f, 180.0f },
    };

    ui_.panel(panel);
    ui_.text({ panel.position.x + 78.0f, panel.position.y + 38.0f }, "QUIT GAME?", { 0.96f, 0.88f, 0.72f, 1.0f }, 4.0f);

    if (ui_.button("quit_game_confirm", { { panel.position.x + 62.0f, panel.position.y + 108.0f }, { 104.0f, 42.0f } }, "QUIT")) {
        running_ = false;
        quitConfirmationOpen_ = false;
    }

    if (ui_.button("quit_game_cancel", { { panel.position.x + 194.0f, panel.position.y + 108.0f }, { 104.0f, 42.0f } }, "CANCEL")) {
        quitConfirmationOpen_ = false;
    }
}

void Application::drawDraftExitConfirmation()
{
#if SOKOBAN_ENABLE_DEBUG_UI
    constexpr const char* popupName = "Stop Testing Draft?";
    if (draftExitConfirmationOpen_) {
        ImGui::OpenPopup(popupName);
    }

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(
        ImVec2(viewport->WorkPos.x + viewport->WorkSize.x * 0.5f, viewport->WorkPos.y + viewport->WorkSize.y * 0.5f),
        ImGuiCond_Appearing,
        ImVec2(0.5f, 0.5f));

    if (ImGui::BeginPopupModal(popupName, &draftExitConfirmationOpen_, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Stop testing this draft and return to the editor?");
        ImGui::Separator();

        if (ImGui::Button("Stop Testing", ImVec2(120.0f, 0.0f))) {
            levelEditor_.setEditingDocument(true);
            editorHoverCell_.reset();
            draftExitConfirmationOpen_ = false;
            ImGui::CloseCurrentPopup();
        }

        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(90.0f, 0.0f))) {
            draftExitConfirmationOpen_ = false;
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
#endif
}

void Application::updateEditorPainting()
{
#if SOKOBAN_ENABLE_DEBUG_UI
    editorHoverCell_.reset();

    if (input_.keyPressed(SDL_SCANCODE_Z)) {
        const bool undone = levelEditor_.tryUndoEdit();
        (void)undone;
        return;
    }

    if (renderer_.wantsMouseCapture()) {
        return;
    }

    const uint32_t documentWidth = levelEditor_.documentWidth();
    const uint32_t documentHeight = levelEditor_.documentHeight();
    if (documentWidth == 0 || documentHeight == 0) {
        return;
    }

    const Vec2 windowSize = window_.size();
    const Vec2 pixelSize = window_.sizeInPixels();
    if (windowSize.x <= 0.0f || windowSize.y <= 0.0f || pixelSize.x <= 0.0f || pixelSize.y <= 0.0f) {
        return;
    }

    const Vec2 mouse = input_.mousePosition();
    const Vec2 mousePixels {
        mouse.x * pixelSize.x / windowSize.x,
        mouse.y * pixelSize.y / windowSize.y,
    };
    const RenderFrameData editorFrame = buildEditorRenderFrame();
    if (const std::optional<GridPosition3> clicked = renderer_.pickIsoGridCell(editorFrame, mousePixels)) {
        GridPosition3 target = *clicked;
        const bool deleting = input_.keyDown(SDL_SCANCODE_D);
        auto topmostOccupiedLayer = [&](GridPosition3 position) -> std::optional<int> {
            const Level::LayerRows& layers = levelEditor_.documentLayers();
            for (int z = static_cast<int>(layers.size()) - 1; z >= 0; --z) {
                if (position.y < 0 ||
                    position.x < 0 ||
                    position.y >= static_cast<int>(layers[static_cast<size_t>(z)].size()) ||
                    position.x >= static_cast<int>(layers[static_cast<size_t>(z)][static_cast<size_t>(position.y)].size())) {
                    continue;
                }
                if (charToTileType(
                        layers[static_cast<size_t>(z)]
                            [static_cast<size_t>(position.y)]
                            [static_cast<size_t>(position.x)]).value_or(TileType::Air) != TileType::Air) {
                    return z;
                }
            }
            return std::nullopt;
        };

        if (levelEditor_.layerLocked()) {
            target.z = static_cast<int>(levelEditor_.activeLayer());
        } else if (deleting) {
            target.z = topmostOccupiedLayer(target).value_or(target.z);
        } else if (!input_.keyDown(SDL_SCANCODE_R)) {
            // Add above the topmost occupied layer; fully empty columns paint
            // the bottom layer itself so they never become unreachable.
            const std::optional<int> occupied = topmostOccupiedLayer(target);
            target.z = occupied ? *occupied + 1 : 0;
        }

        editorHoverCell_ = target;
        if (input_.mouseButtonPressed(SDL_BUTTON_LEFT)) {
            if (deleting) {
                levelEditor_.eraseCell(target);
            } else {
                levelEditor_.paintCell(target);
            }
        }
    }
#endif
}

void Application::loadCurrentScreen()
{
    applyLevel(Level::loadFromFile(screenPath(currentLevel_, currentScreen_)));
    levelEditor_.setPlayingDraft(false);
    levelEditor_.setEditingDocument(false);
    editorHoverCell_.reset();

    std::cerr << "player started level " << currentLevel_ << " screen " << currentScreen_ << '\n';
}

void Application::applyLevel(Level level)
{
    level_ = std::move(level);
    state_ = rules::initialState(level_);
    playerRenderPosition_ = toVec3(state_.player);
    playerFacingQuarterTurns_ = facingQuarterTurns(MoveDirection::Down);
    movableVisuals_.clear();
    movableVisuals_.reserve(state_.movables.size());
    for (const GameState::Movable& movable : state_.movables) {
        movableVisuals_.push_back({
            .renderPosition = toVec3(movable.cell),
            .animationStart = toVec3(movable.cell),
            .animationEnd = toVec3(movable.cell),
        });
    }
    pendingCommands_.clear();
    moveHistory_.clear();
    undoCursor_.reset();
    activeAction_ = {};
    moving_ = false;
    moveElapsed_ = 0.0f;
    autoMotionPaused_ = false;
}

void Application::advanceScreen()
{
    if (screenExists(currentLevel_, currentScreen_ + 1)) {
        ++currentScreen_;
    } else if (screenExists(currentLevel_ + 1, 0)) {
        ++currentLevel_;
        currentScreen_ = 0;
    } else {
        currentLevel_ = 0;
        currentScreen_ = 0;
    }

    loadCurrentScreen();
}

void Application::queuePressedCommands()
{
    if (input_.keyPressed(SDL_SCANCODE_Z)) {
        pendingCommands_.push_back({ .type = MoveCommandType::Undo });
    }
    if (input_.keyPressed(SDL_SCANCODE_R)) {
        pendingCommands_.push_back({ .type = MoveCommandType::Restart });
    }

    const std::optional<MoveDirection> vertical = pressedVerticalDirection();
    const std::optional<MoveDirection> horizontal = pressedHorizontalDirection();
    if (vertical) {
        pendingCommands_.push_back({ .type = MoveCommandType::Move, .direction = *vertical });
    }
    if (horizontal) {
        pendingCommands_.push_back({ .type = MoveCommandType::Move, .direction = *horizontal });
    }
}

void Application::advancePlayerMovement(float dt)
{
    advanceMovableAnimations(dt);

    float remainingTime = dt;

    while (remainingTime > 0.0f) {
        if (!moving_ && !tryStartNextMove()) {
            return;
        }

        const float duration = activeActionDuration();
        if (duration <= 0.0f) {
            if (completeActiveAction()) {
                return;
            }
            continue;
        }

        const float timeToFinish = duration - moveElapsed_;
        const float step = std::min(remainingTime, timeToFinish);
        remainingTime -= step;
        moveElapsed_ += step;

        const Vec3 playerFrom = entityRenderTarget(activeAction_.before.player, activeAction_.before.playerDead);
        const Vec3 playerTo = entityRenderTarget(activeAction_.after.player, activeAction_.after.playerDead);
        const float playerDistance = gridDistance(playerFrom, playerTo);
        playerRenderPosition_ = interpolateGridMotion(
            playerFrom,
            playerTo,
            moveElapsed_,
            playerDistance > 0.0001f ? duration / playerDistance : 0.0f);

        if (moveElapsed_ >= duration) {
            if (completeActiveAction()) {
                return;
            }
        }
    }
}

void Application::advanceMovableAnimations(float dt)
{
    for (MovableVisual& visual : movableVisuals_) {
        if (!visual.moving) {
            continue;
        }

        visual.animationElapsed = std::min(visual.animationElapsed + dt, visual.animationDuration);
        if (visual.animationDuration <= 0.0f || visual.animationElapsed >= visual.animationDuration) {
            visual.renderPosition = visual.animationEnd;
            visual.moving = false;
            continue;
        }

        visual.renderPosition = interpolateGridMotion(
            visual.animationStart,
            visual.animationEnd,
            visual.animationElapsed,
            visual.animationSecondsPerTile);
    }
}

void Application::startMovableAnimations(const ActionRecord& action)
{
    const size_t movableCount = std::min(action.before.movables.size(), action.after.movables.size());
    for (size_t i = 0; i < movableCount && i < movableVisuals_.size(); ++i) {
        MovableVisual& visual = movableVisuals_[i];
        const Vec3 target = entityRenderTarget(action.after.movables[i].cell, action.after.movables[i].fallen);
        if (gridDistance(visual.renderPosition, target) <= 0.0001f) {
            visual.renderPosition = target;
            visual.animationStart = target;
            visual.animationEnd = target;
            visual.animationElapsed = 0.0f;
            visual.animationDuration = 0.0f;
            visual.animationSecondsPerTile = 0.0f;
            visual.moving = false;
            continue;
        }

        visual.animationStart = visual.renderPosition;
        visual.animationEnd = target;
        visual.animationElapsed = 0.0f;
        const float distance = gridDistance(visual.animationStart, visual.animationEnd);
        visual.animationDuration = action.durationSeconds;
        visual.animationSecondsPerTile = distance > 0.0001f ? action.durationSeconds / distance : 0.0f;
        visual.moving = true;
    }
}

bool Application::completeActiveAction()
{
    applyGameState(activeAction_.after);

    moveHistory_.push_back(activeAction_);
    moving_ = false;
    moveElapsed_ = 0.0f;

    if (rules::isAtUnlockedEnd(level_, state_)) {
        if (levelEditor_.playingDraft()) {
            levelEditor_.markDraftSolved();
            return false;
        }

        advanceScreen();
        return true;
    }

    return false;
}

float Application::activeActionDuration() const
{
    return activeAction_.durationSeconds;
}

bool Application::tryStartNextMove()
{
    if (state_.playerDead) {
        while (!pendingCommands_.empty()) {
            const MoveCommand command = pendingCommands_.front();
            pendingCommands_.pop_front();
            if (command.type == MoveCommandType::Undo && tryStartUndoMove()) {
                return true;
            }
        }

        return input_.keyDown(SDL_SCANCODE_Z) && tryStartUndoMove();
    }

    while (!pendingCommands_.empty()) {
        const MoveCommand command = pendingCommands_.front();
        pendingCommands_.pop_front();

        if (command.type == MoveCommandType::Undo && tryStartUndoMove()) {
            return true;
        }

        if (command.type == MoveCommandType::Restart && tryStartRestart()) {
            return true;
        }

        if (command.type == MoveCommandType::Move && tryStartHeldDirection(command.direction, heldPerpendicularDirection(command.direction))) {
            return true;
        }
    }

    if (tryStartHeldMove()) {
        return true;
    }

    return !autoMotionPaused_ &&
        rules::hasPendingMotion(level_, state_) &&
        tryStartWorldStep(std::nullopt);
}

bool Application::tryStartHeldMove()
{
    if (input_.keyDown(SDL_SCANCODE_Z)) {
        return tryStartUndoMove();
    }

    const std::optional<MoveDirection> vertical = heldVerticalDirection();
    const std::optional<MoveDirection> horizontal = heldHorizontalDirection();
    if (vertical && tryStartHeldDirection(*vertical, horizontal)) {
        return true;
    }
    if (horizontal && tryStartHeldDirection(*horizontal, vertical)) {
        return true;
    }

    return false;
}

bool Application::tryStartWorldStep(std::optional<MoveDirection> playerInput)
{
    GameState after = rules::step(level_, state_, playerInput, stepRates_);
    if (after == state_) {
        return false;
    }

    activeAction_ = {
        .before = state_,
        .after = std::move(after),
        .durationSeconds = stepDurationSeconds_,
    };

    // The step was a push if input displaced a movable out of the cell the
    // player stepped toward.
    if (playerInput && !(activeAction_.before.player == activeAction_.after.player)) {
        const GridPosition3 pushCell = rules::movementTarget(activeAction_.before.player, *playerInput);
        for (size_t i = 0; i < activeAction_.before.movables.size() && i < activeAction_.after.movables.size(); ++i) {
            if (activeAction_.before.movables[i].cell == pushCell &&
                !(activeAction_.after.movables[i].cell == pushCell)) {
                activeAction_.playerPushing = true;
                break;
            }
        }
    }

    if (playerInput) {
        playerFacingQuarterTurns_ = facingQuarterTurns(*playerInput);
        autoMotionPaused_ = false;
    } else if (const std::optional<MoveDirection> direction =
                   movementDirection(activeAction_.before.player, activeAction_.after.player)) {
        playerFacingQuarterTurns_ = facingQuarterTurns(*direction);
    }
    undoCursor_.reset();
    moveElapsed_ = 0.0f;
    moving_ = true;
    startMovableAnimations(activeAction_);
    return true;
}

bool Application::tryStartUndoMove()
{
    if (moveHistory_.empty()) {
        return false;
    }

    if (!undoCursor_) {
        // A contiguous undo run walks backward through the pre-existing history while
        // each completed undo still appends its inverse move for future branching.
        undoCursor_ = moveHistory_.size();
    }

    if (*undoCursor_ == 0) {
        return false;
    }

    --(*undoCursor_);
    activeAction_ = invertActionRecord(moveHistory_[*undoCursor_]);
    activeAction_.durationSeconds = stepDurationSeconds_;
    autoMotionPaused_ = true;
    if (const auto direction = movementDirection(activeAction_.before.player, activeAction_.after.player)) {
        playerFacingQuarterTurns_ = facingQuarterTurns(*direction);
    }
    moveElapsed_ = 0.0f;
    moving_ = true;
    startMovableAnimations(activeAction_);
    return true;
}

bool Application::tryStartRestart()
{
    if (state_.playerDead) {
        return false;
    }

    GameState restarted = rules::initialState(level_);
    if (state_ == restarted) {
        return false;
    }

    activeAction_ = {
        .before = state_,
        .after = std::move(restarted),
        .durationSeconds = stepDurationSeconds_,
    };
    autoMotionPaused_ = false;
    undoCursor_.reset();
    moveElapsed_ = 0.0f;
    moving_ = true;
    startMovableAnimations(activeAction_);
    return true;
}

std::optional<MoveDirection> Application::pressedVerticalDirection() const
{
    const bool upPressed = input_.keyPressed(SDL_SCANCODE_W);
    const bool downPressed = input_.keyPressed(SDL_SCANCODE_S);
    if (upPressed == downPressed) {
        return std::nullopt;
    }

    if ((upPressed && input_.keyDown(SDL_SCANCODE_S)) || (downPressed && input_.keyDown(SDL_SCANCODE_W))) {
        return std::nullopt;
    }

    return upPressed ? MoveDirection::Up : MoveDirection::Down;
}

std::optional<MoveDirection> Application::pressedHorizontalDirection() const
{
    const bool leftPressed = input_.keyPressed(SDL_SCANCODE_A);
    const bool rightPressed = input_.keyPressed(SDL_SCANCODE_D);
    if (leftPressed == rightPressed) {
        return std::nullopt;
    }

    if ((leftPressed && input_.keyDown(SDL_SCANCODE_D)) || (rightPressed && input_.keyDown(SDL_SCANCODE_A))) {
        return std::nullopt;
    }

    return leftPressed ? MoveDirection::Left : MoveDirection::Right;
}

std::optional<MoveDirection> Application::heldVerticalDirection() const
{
    const bool up = input_.keyDown(SDL_SCANCODE_W);
    const bool down = input_.keyDown(SDL_SCANCODE_S);
    if (up == down) {
        return std::nullopt;
    }

    return up ? MoveDirection::Up : MoveDirection::Down;
}

std::optional<MoveDirection> Application::heldHorizontalDirection() const
{
    const bool left = input_.keyDown(SDL_SCANCODE_A);
    const bool right = input_.keyDown(SDL_SCANCODE_D);
    if (left == right) {
        return std::nullopt;
    }

    return left ? MoveDirection::Left : MoveDirection::Right;
}

std::optional<MoveDirection> Application::heldPerpendicularDirection(MoveDirection direction) const
{
    switch (direction) {
    case MoveDirection::Up:
    case MoveDirection::Down:
        return heldHorizontalDirection();
    case MoveDirection::Left:
    case MoveDirection::Right:
        return heldVerticalDirection();
    }

    return std::nullopt;
}

bool Application::hasPendingMove(MoveDirection direction) const
{
    return std::ranges::any_of(pendingCommands_, [direction](const MoveCommand& command) {
        return command.type == MoveCommandType::Move && command.direction == direction;
    });
}

bool Application::tryStartHeldDirection(MoveDirection direction, std::optional<MoveDirection> queuedDirection)
{
    if (!tryStartWorldStep(direction)) {
        return false;
    }

    if (queuedDirection && !hasPendingMove(*queuedDirection)) {
        pendingCommands_.push_back({ .type = MoveCommandType::Move, .direction = *queuedDirection });
    }

    return true;
}

void Application::applyGameState(const GameState& state)
{
    state_ = state;
    playerRenderPosition_ = entityRenderTarget(state_.player, state_.playerDead);

    for (size_t i = 0; i < movableVisuals_.size() && i < state_.movables.size(); ++i) {
        if (movableVisuals_[i].moving) {
            continue;
        }
        const Vec3 target = entityRenderTarget(state_.movables[i].cell, state_.movables[i].fallen);
        movableVisuals_[i].renderPosition = target;
        movableVisuals_[i].animationStart = target;
        movableVisuals_[i].animationEnd = target;
        movableVisuals_[i].animationElapsed = 0.0f;
        movableVisuals_[i].animationDuration = 0.0f;
        movableVisuals_[i].animationSecondsPerTile = 0.0f;
    }
}

Application::ActionRecord Application::invertActionRecord(const ActionRecord& record) const
{
    return {
        .before = record.after,
        .after = record.before,
    };
}

std::filesystem::path Application::screenPath(int levelIndex, int screenIndex) const
{
    return assetRoot_ /
        "levels" /
        ("level" + std::to_string(levelIndex)) /
        ("screen" + std::to_string(screenIndex) + ".scr");
}

bool Application::screenExists(int levelIndex, int screenIndex) const
{
    if (levelIndex < 0 || screenIndex < 0) {
        return false;
    }

    return std::filesystem::exists(screenPath(levelIndex, screenIndex));
}

RenderFrameData Application::buildRenderFrame() const
{
#if SOKOBAN_ENABLE_DEBUG_UI
    if (levelEditor_.editingDocument()) {
        return buildEditorRenderFrame();
    }
#endif

    return buildGameplayRenderFrame();
}

float Application::conveyorBeltScrollOffset() const
{
    if (stepDurationSeconds_ <= 0.0f) {
        return 0.0f;
    }

    // Belts move riders one tile per step and the belt texture spans one full
    // V cycle per tile, so one cycle per step keeps the surface in sync with
    // conveyed entities. Negate to flip the scroll direction if needed.
    return std::fmod(playerAnimationTimeSeconds_ / stepDurationSeconds_, 1.0f);
}

float Application::tileTypeToScale(TileType type) const
{
    const auto index = static_cast<std::size_t>(type);
    if (index >= tileScales_.size()) {
        return 1.0f;
    }

    return clampedTileScale(tileScales_[index]);
}

RenderFrameData Application::buildGameplayRenderFrame() const
{
    RenderFrameData frame;
    frame.viewMode = RenderViewMode::Isometric3D;
    frame.lighting = {
        .sun = {
            .direction = sunDirectionFromSpherical(sunAzimuthDegrees_, sunTiltDegrees_),
            .color = sunColor_,
            .intensity = sunIntensity_,
        },
        .ambient = {
            .color = ambientLightColor_,
            .intensity = ambientLightIntensity_,
        },
        .shadows = {
            .enabled = shadowsEnabled_,
            .opacity = shadowOpacity_,
            .bias = shadowBias_,
        },
        .specularStrength = specularStrength_,
        .specularPower = specularPower_,
        .modelShadowReceive = modelShadowReceive_,
    };
    frame.gridOverlay = {
        .color = tileGridLineColor_,
        .width = tileGridLineWidth_,
    };
    frame.levelWidth = level_.width();
    frame.levelHeight = level_.height();
    frame.levelDepth = level_.depth();
    frame.playerPosition = { playerRenderPosition_.x, playerRenderPosition_.y };
    const bool endUnlocked = rules::isEndUnlocked(level_, state_);

    frame.tiles.reserve(static_cast<size_t>(level_.width()) * level_.height() * level_.depth());
    const bool playerMovingOutOfWater = moving_ && activeAction_.before.playerDead && !activeAction_.after.playerDead;
    auto fallenMovableIsMoving = [this](const GameState::Movable* movable) {
        const auto index = static_cast<size_t>(movable - state_.movables.data());
        return index < movableVisuals_.size() && movableVisuals_[index].moving;
    };
    auto staticCellAt = [this, endUnlocked, playerMovingOutOfWater, fallenMovableIsMoving](uint32_t x, uint32_t y, uint32_t z) {
        const GridPosition3 position {
            static_cast<int>(x),
            static_cast<int>(y),
            static_cast<int>(z),
        };
        if (level_.tileAt(x, y, z) == TileType::Water) {
            const GridPosition3 entityPosition {
                position.x,
                position.y,
                position.z + 1,
            };
            if (const GameState::Movable* fallenMovable = rules::fallenMovableAt(state_, entityPosition)) {
                if (!fallenMovableIsMoving(fallenMovable)) {
                    return StaticRenderCell { .tile = TileType::Air };
                }
            }
        }

        std::optional<TileType> fallenTile;
        if (const GameState::Movable* fallenMovable = rules::fallenMovableAt(state_, position)) {
            if (!fallenMovableIsMoving(fallenMovable)) {
                return StaticRenderCell {
                    .tile = fallenMovable->type,
                    .showGrid = true,
                    .baseElevation = static_cast<float>(std::max(position.z - 1, 0)),
                    .height = 1.0f,
                };
            }
        } else if (state_.playerDead && !playerMovingOutOfWater && position == state_.player) {
            fallenTile = TileType::Player;
        }

        return staticRenderCellFor(
            level_,
            x,
            y,
            z,
            endUnlocked,
            fallenTile,
            surfaceEntityHeight_,
            surfaceEntityWidthDepth_,
            playerFacingQuarterTurns_);
    };
    appendStaticTiles(frame, level_, staticCellAt, [this](TileType tile) {
        return tileTypeToScale(tile);
    });
    auto levelTileAt = [this](GridPosition3 position) {
        if (!level_.inBounds(position)) {
            return TileType::Air;
        }
        return level_.tileAt(
            static_cast<uint32_t>(position.x),
            static_cast<uint32_t>(position.y),
            static_cast<uint32_t>(position.z));
    };
    for (uint32_t z = 0; z < level_.depth(); ++z) {
        for (uint32_t y = 0; y < level_.height(); ++y) {
            for (uint32_t x = 0; x < level_.width(); ++x) {
                appendLadderRungsForCell(
                    frame,
                    {
                        static_cast<int>(x),
                        static_cast<int>(y),
                        static_cast<int>(z),
                    },
                    levelTileAt);
            }
        }
    }
    for (uint32_t z = 0; z < level_.depth(); ++z) {
        appendWaterEdgeFaces(
            frame,
            level_.width(),
            level_.height(),
            static_cast<float>(z) + 1.0f,
            [this, z](GridPosition position) {
                return rules::isUnfilledWater(level_, state_, {
                    position.x,
                    position.y,
                    static_cast<int>(z) + 1,
                });
            });
    }

    if (!state_.playerDead || playerMovingOutOfWater) {
        RenderFrameData::Tile playerTile {
            .position = { playerRenderPosition_.x, playerRenderPosition_.y },
            .color = { 1.0f, 1.0f, 1.0f, 1.0f },
            .baseElevation = playerRenderPosition_.z,
            .height = 1.0f,
            .showGrid = false,
            .model = RenderModel::Rogue,
            .animation = moving_
                ? (activeAction_.playerPushing ? RenderAnimation::RoguePush : RenderAnimation::RogueMovement)
                : RenderAnimation::RogueIdle,
            .animationTimeSeconds = playerAnimationTimeSeconds_,
            .modelRotationQuarterTurns = playerFacingQuarterTurns_,
        };
        applyTileScale(playerTile, tileTypeToScale(TileType::Player));
        frame.tiles.push_back(playerTile);
    }

    for (size_t movableIndex = 0; movableIndex < state_.movables.size() && movableIndex < movableVisuals_.size(); ++movableIndex) {
        const GameState::Movable& movable = state_.movables[movableIndex];
        const MovableVisual& visual = movableVisuals_[movableIndex];
        const bool movingOutOfWater = moving_ &&
            movableIndex < activeAction_.before.movables.size() &&
            movableIndex < activeAction_.after.movables.size() &&
            activeAction_.before.movables[movableIndex].fallen &&
            !activeAction_.after.movables[movableIndex].fallen;
        if (movable.fallen && !visual.moving && !movingOutOfWater) {
            continue;
        }

        Vec4 color = tileColor(movable.type);
        if (movable.type == TileType::Ice) {
            color.w = config::iceTintAlpha;
        }

        RenderFrameData::Tile movableTile {
            .position = { visual.renderPosition.x, visual.renderPosition.y },
            .color = color,
            .baseElevation = visual.renderPosition.z,
            .height = 1.0f,
            .blurBehind = movable.type == TileType::Ice,
            .model = renderModelForTile(movable.type),
        };
        applyTileScale(movableTile, tileTypeToScale(movable.type));
        frame.tiles.push_back(movableTile);
    }

    const float beltScrollOffset = conveyorBeltScrollOffset();
    for (RenderFrameData::Tile& tile : frame.tiles) {
        if (tile.model == RenderModel::Conveyor) {
            tile.beltScrollOffset = beltScrollOffset;
        }
    }

    return frame;
}

RenderFrameData Application::buildEditorRenderFrame() const
{
    RenderFrameData frame;
    frame.viewMode = RenderViewMode::Isometric3D;
    frame.lighting = {
        .sun = {
            .direction = sunDirectionFromSpherical(sunAzimuthDegrees_, sunTiltDegrees_),
            .color = sunColor_,
            .intensity = sunIntensity_,
        },
        .ambient = {
            .color = ambientLightColor_,
            .intensity = ambientLightIntensity_,
        },
        .shadows = {
            .enabled = shadowsEnabled_,
            .opacity = shadowOpacity_,
            .bias = shadowBias_,
        },
        .specularStrength = specularStrength_,
        .specularPower = specularPower_,
        .modelShadowReceive = modelShadowReceive_,
    };
    frame.gridOverlay = {
        .color = tileGridLineColor_,
        .width = tileGridLineWidth_,
    };
    frame.levelWidth = levelEditor_.documentWidth();
    frame.levelHeight = levelEditor_.documentHeight();

    const Level::LayerRows& layers = levelEditor_.documentLayers();
    const uint32_t activeLayer = levelEditor_.activeLayer();
    const uint32_t layerCount = static_cast<uint32_t>(layers.size());
    const bool layerLocked = levelEditor_.layerLocked();
    frame.levelDepth = std::max(layerCount, 1U);
    frame.tiles.reserve(
        static_cast<size_t>(frame.levelWidth) *
        frame.levelHeight *
        layerCount *
        2 +
        2);

    auto documentTileAt = [&](uint32_t x, uint32_t y, uint32_t z) {
        if (z >= layers.size() || y >= layers[z].size() || x >= layers[z][y].size()) {
            return TileType::Air;
        }
        return charToTileType(layers[z][y][x]).value_or(TileType::Air);
    };
    auto documentTileAtPosition = [&](GridPosition3 position) {
        if (position.x < 0 || position.y < 0 || position.z < 0) {
            return TileType::Air;
        }
        return documentTileAt(
            static_cast<uint32_t>(position.x),
            static_cast<uint32_t>(position.y),
            static_cast<uint32_t>(position.z));
    };
    auto appendEditorTile = [&](uint32_t x, uint32_t y, uint32_t z, TileType tile, bool preview) {
        if (tile == TileType::Air) {
            return;
        }
        if (tile == TileType::Ladder) {
            auto tileAtForLadder = [&](GridPosition3 position) {
                if (preview &&
                    position.x == static_cast<int>(x) &&
                    position.y == static_cast<int>(y) &&
                    position.z == static_cast<int>(z)) {
                    return TileType::Ladder;
                }
                return documentTileAtPosition(position);
            };
            appendLadderRungsForCell(
                frame,
                {
                    static_cast<int>(x),
                    static_cast<int>(y),
                    static_cast<int>(z),
                },
                tileAtForLadder,
                preview);
            return;
        }

        const bool surfaceEntity = tileTypeIsSurfaceEntity(tile);
        const bool conveyor = tileTypeIsConveyor(tile);
        const float tileSize = surfaceEntity ? surfaceEntityWidthDepth_ : 1.0f;
        const float centeredOffset = (1.0f - tileSize) * 0.5f;
        Vec4 color = tileColor(tile);
        if (tile == TileType::Player) {
            color = { 1.0f, 1.0f, 1.0f, 1.0f };
        }
        if (tile == TileType::Ice) {
            color.w = config::iceTintAlpha;
        }
        const float previewOffset = preview ? 0.02f : 0.0f;
        RenderFrameData::Tile renderTile {
            .cell = {
                static_cast<int>(x),
                static_cast<int>(y),
                static_cast<int>(z),
            },
            .position = {
                static_cast<float>(x) + centeredOffset,
                static_cast<float>(y) + centeredOffset,
            },
            .size = { tileSize, tileSize },
            .color = color,
            .baseElevation = static_cast<float>(z) +
                (tile == TileType::Water ? 1.0f - 2.0f * config::waterDepthBelowGround : 0.0f) +
                previewOffset,
            .height = surfaceEntity
                ? surfaceEntityHeight_
                : (tile == TileType::Water
                        ? config::waterDepthBelowGround
                        : (conveyor
                                ? config::conveyorTileHeight
                                : (tileTypeIsSolidBlock(tile) || tileTypeOccupiesLevelCell(tile) ? 1.0f : 0.0f))),
            .blurBehind = tile == TileType::Ice,
            .showGrid = tile != TileType::Player,
            .isEditorPreview = preview,
            .model = renderModelForTile(tile),
            .animation = tile == TileType::Player ? RenderAnimation::RogueIdle : RenderAnimation::None,
            .animationTimeSeconds = tile == TileType::Player ? playerAnimationTimeSeconds_ : 0.0f,
            .modelRotationQuarterTurns = rules::conveyorDirectionForTile(tile)
                ? facingQuarterTurns(*rules::conveyorDirectionForTile(tile))
                : 0,
        };
        applyTileScale(renderTile, tileTypeToScale(tile));
        frame.tiles.push_back(renderTile);
    };

    for (uint32_t z = 0; z < layerCount; ++z) {
        if (layerLocked && z != activeLayer) {
            continue;
        }
        for (uint32_t y = 0; y < frame.levelHeight; ++y) {
            for (uint32_t x = 0; x < frame.levelWidth; ++x) {
                if (editorHoverCell_ &&
                    editorHoverCell_->z == static_cast<int>(z) &&
                    editorHoverCell_->x == static_cast<int>(x) &&
                    editorHoverCell_->y == static_cast<int>(y)) {
                    continue;
                }

                const TileType tile = documentTileAt(x, y, z);
                if (layerLocked && tile == TileType::Air) {
                    frame.tiles.push_back({
                        .cell = {
                            static_cast<int>(x),
                            static_cast<int>(y),
                            static_cast<int>(z),
                        },
                        .position = { static_cast<float>(x), static_cast<float>(y) },
                        .baseElevation = static_cast<float>(z),
                        .pickOnly = true,
                    });
                    continue;
                }
                if (!layerLocked && z == 0 && tile == TileType::Air) {
                    // Columns that are Air on every layer render nothing, which
                    // would leave the mouse picker with nothing to hit and make
                    // the column unpaintable. Emit a pick-only ground cell.
                    bool columnEmpty = true;
                    for (uint32_t layer = 1; layer < layerCount && columnEmpty; ++layer) {
                        columnEmpty = documentTileAt(x, y, layer) == TileType::Air;
                    }
                    if (columnEmpty) {
                        frame.tiles.push_back({
                            .cell = {
                                static_cast<int>(x),
                                static_cast<int>(y),
                                0,
                            },
                            .position = { static_cast<float>(x), static_cast<float>(y) },
                            .baseElevation = 0.0f,
                            .pickOnly = true,
                        });
                        continue;
                    }
                }
                appendEditorTile(x, y, z, tile, false);
            }
        }
    }

    auto documentHasOpenWater = [&](GridPosition position, uint32_t z) {
        if (position.x < 0 || position.y < 0 ||
            position.x >= static_cast<int>(frame.levelWidth) ||
            position.y >= static_cast<int>(frame.levelHeight)) {
            return false;
        }
        return documentTileAt(
                   static_cast<uint32_t>(position.x),
                   static_cast<uint32_t>(position.y),
                   z) == TileType::Water;
    };
    for (uint32_t z = 0; z < layerCount; ++z) {
        if (layerLocked && z != activeLayer) {
            continue;
        }
        appendWaterEdgeFaces(
            frame,
            frame.levelWidth,
            frame.levelHeight,
            static_cast<float>(z) + 1.0f,
            [&, z](GridPosition position) {
                return documentHasOpenWater(position, z);
            });
    }

    if (editorHoverCell_ &&
        editorHoverCell_->x >= 0 &&
        editorHoverCell_->y >= 0 &&
        editorHoverCell_->x < static_cast<int>(frame.levelWidth) &&
        editorHoverCell_->y < static_cast<int>(frame.levelHeight)) {
        const bool deleting = input_.keyDown(SDL_SCANCODE_D);
        const TileType selectedTile = deleting ? TileType::Air : levelEditor_.selectedTile();
        const TileType hoveredTile = documentTileAt(
            static_cast<uint32_t>(editorHoverCell_->x),
            static_cast<uint32_t>(editorHoverCell_->y),
            static_cast<uint32_t>(editorHoverCell_->z));
        const TileType previewTile = selectedTile == TileType::Air ? hoveredTile : selectedTile;
        appendEditorTile(
            static_cast<uint32_t>(editorHoverCell_->x),
            static_cast<uint32_t>(editorHoverCell_->y),
            static_cast<uint32_t>(editorHoverCell_->z),
            previewTile,
            true);
    }

    const float beltScrollOffset = conveyorBeltScrollOffset();
    for (RenderFrameData::Tile& tile : frame.tiles) {
        if (tile.model == RenderModel::Conveyor) {
            tile.beltScrollOffset = beltScrollOffset;
        }
    }

    return frame;
}

} // namespace sokoban
