#include "engine/Application.hpp"

#include "engine/BoardLayout.hpp"
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

Vec2 toVec2(GridPosition position)
{
    return { static_cast<float>(position.x), static_cast<float>(position.y) };
}

Vec2 lerp(Vec2 from, Vec2 to, float t)
{
    return {
        from.x + (to.x - from.x) * t,
        from.y + (to.y - from.y) * t,
    };
}

int gridDistance(GridPosition from, GridPosition to)
{
    return std::abs(to.x - from.x) + std::abs(to.y - from.y);
}

float gridDistance(Vec2 from, Vec2 to)
{
    return std::abs(to.x - from.x) + std::abs(to.y - from.y);
}

// Text level files store rows top-to-bottom, while the 2D editor renderer uses
// grid coordinates with y=0 at the bottom of the board.
int documentRowToRenderY(uint32_t row, uint32_t height)
{
    return static_cast<int>(height - 1U - row);
}

int renderYToDocumentRow(int y, uint32_t height)
{
    return static_cast<int>(height) - 1 - y;
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

Vec2 interpolateGridMotion(Vec2 from, Vec2 to, float elapsedSeconds)
{
    const float distance = gridDistance(from, to);
    if (distance <= 0.0001f) {
        return to;
    }

    if constexpr (config::playerMoveDurationSeconds <= 0.0f) {
        return to;
    }

    const float traveledCells = std::min(
        elapsedSeconds / config::playerMoveDurationSeconds,
        distance);
    const float dx = to.x - from.x;
    const float dy = to.y - from.y;

    if (std::abs(dx) > 0.0001f && std::abs(dy) <= 0.0001f) {
        return {
            from.x + std::copysign(traveledCells, dx),
            from.y,
        };
    }

    if (std::abs(dy) > 0.0001f && std::abs(dx) <= 0.0001f) {
        return {
            from.x,
            from.y + std::copysign(traveledCells, dy),
        };
    }

    return lerp(from, to, traveledCells / distance);
}

Vec2 interpolateGridMotion(GridPosition from, GridPosition to, float elapsedSeconds)
{
    return interpolateGridMotion(toVec2(from), toVec2(to), elapsedSeconds);
}

struct StaticRenderCell {
    TileType tile = TileType::Empty;
    bool active = true;
    float baseElevation = 0.0f;
    float height = 0.0f;
};

bool canMergeStaticCells(const StaticRenderCell& left, const StaticRenderCell& right)
{
    return left.tile == right.tile &&
        left.active == right.active &&
        left.baseElevation == right.baseElevation &&
        left.height == right.height;
}

StaticRenderCell staticRenderCellFor(const Level& level, uint32_t x, uint32_t y, bool endUnlocked, std::optional<TileType> fallenTile)
{
    const TileType tile = fallenTile.value_or(level.tileAt(x, y));
    return {
        .tile = tile,
        .active = tile != TileType::End || endUnlocked,
        .baseElevation = tile == TileType::Water ? -config::waterDepthBelowGround : 0.0f,
        .height = tile == TileType::Wall ? 1.0f : 0.0f,
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

void appendWaterEdgeFaces(RenderFrameData& frame, const Level& level, const auto& isUnfilledWaterAt)
{
    const float bottom = -config::waterDepthBelowGround;
    const float top = 0.0f;
    const Vec4 color = shade(tileColor(TileType::Empty), 0.78f);

    auto appendEdge = [&](std::array<Vec3, 4> vertices, Vec3 normal, Vec4 edgeColor) {
        frame.isoFaces.push_back({
            .vertices = vertices,
            .normal = normal,
            .color = edgeColor,
        });
    };

    auto neighborIsOpenWater = [&](GridPosition position) {
        return level.inBounds(position) && isUnfilledWaterAt(position);
    };

    for (uint32_t y = 0; y < level.height(); ++y) {
        for (uint32_t x = 0; x < level.width(); ++x) {
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

template <typename CellAt>
void appendGreedyMergedStaticTiles(RenderFrameData& frame, const Level& level, CellAt cellAt)
{
    const uint32_t width = level.width();
    const uint32_t height = level.height();
    std::vector<bool> consumed(static_cast<size_t>(width) * height, false);

    auto index = [width](uint32_t x, uint32_t y) {
        return static_cast<size_t>(y) * width + x;
    };

    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            if (consumed[index(x, y)]) {
                continue;
            }

            const StaticRenderCell cell = cellAt(x, y);

            uint32_t mergeWidth = 1;
            while (x + mergeWidth < width &&
                !consumed[index(x + mergeWidth, y)] &&
                canMergeStaticCells(cell, cellAt(x + mergeWidth, y))) {
                ++mergeWidth;
            }

            uint32_t mergeHeight = 1;
            bool canGrowHeight = true;
            while (y + mergeHeight < height && canGrowHeight) {
                for (uint32_t offsetX = 0; offsetX < mergeWidth; ++offsetX) {
                    if (consumed[index(x + offsetX, y + mergeHeight)] ||
                        !canMergeStaticCells(cell, cellAt(x + offsetX, y + mergeHeight))) {
                        canGrowHeight = false;
                        break;
                    }
                }

                if (canGrowHeight) {
                    ++mergeHeight;
                }
            }

            for (uint32_t offsetY = 0; offsetY < mergeHeight; ++offsetY) {
                for (uint32_t offsetX = 0; offsetX < mergeWidth; ++offsetX) {
                    consumed[index(x + offsetX, y + offsetY)] = true;
                }
            }

            frame.tiles.push_back({
                .position = { static_cast<float>(x), static_cast<float>(y) },
                .size = { static_cast<float>(mergeWidth), static_cast<float>(mergeHeight) },
                .color = tileColor(cell.tile, cell.active),
                .baseElevation = cell.baseElevation,
                .height = cell.height,
            });
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
            const bool allowKeyboardInput = !isKeyboardEvent || !renderer_.wantsKeyboardCapture() || event.type == SDL_EVENT_KEY_UP;
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
    ImGui::Text("Player (%d, %d)", playerCell_.x, playerCell_.y);
    ImGui::Text("Player %s", playerDead_ ? "dead" : "alive");
    ImGui::Text("Movables %zu", rocks_.size());
    ImGui::Text("History %zu", moveHistory_.size());
    ImGui::Text("End %s", isEndUnlocked() ? "unlocked" : "locked");
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
    const BoardPixelLayout layout = calculateBoardPixelLayout(pixelSize, documentWidth, documentHeight);
    if (const std::optional<GridPosition> position = pixelToGridPosition(mousePixels, layout, documentWidth, documentHeight)) {
        editorHoverCell_ = position;
        if (input_.mouseButtonDown(SDL_BUTTON_LEFT)) {
            levelEditor_.paintCell({
                position->x,
                renderYToDocumentRow(position->y, documentHeight),
            });
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
    playerCell_ = level_.playerStart();
    playerRenderPosition_ = toVec2(playerCell_);
    playerDead_ = false;
    rocks_.clear();
    rocks_.reserve(level_.movableTiles().size());
    for (const Level::MovableTile& movable : level_.movableTiles()) {
        rocks_.push_back({
            .type = movable.type,
            .cell = movable.position,
            .renderPosition = toVec2(movable.position),
            .animationStart = toVec2(movable.position),
            .animationEnd = toVec2(movable.position),
        });
    }
    pendingCommands_.clear();
    moveHistory_.clear();
    undoCursor_.reset();
    activeAction_ = {};
    moving_ = false;
    moveElapsed_ = 0.0f;
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

        if constexpr (config::playerMoveDurationSeconds <= 0.0f) {
            if (completeActiveAction()) {
                return;
            }
            continue;
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

        playerRenderPosition_ = interpolateGridMotion(activeAction_.before.player, activeAction_.after.player, moveElapsed_);

        if (moveElapsed_ >= duration) {
            if (completeActiveAction()) {
                return;
            }
        }
    }
}

void Application::advanceMovableAnimations(float dt)
{
    for (Rock& rock : rocks_) {
        if (!rock.moving) {
            continue;
        }

        rock.animationElapsed = std::min(rock.animationElapsed + dt, rock.animationDuration);
        if (rock.animationDuration <= 0.0f || rock.animationElapsed >= rock.animationDuration) {
            rock.renderPosition = rock.animationEnd;
            rock.moving = false;
            continue;
        }

        rock.renderPosition = interpolateGridMotion(rock.animationStart, rock.animationEnd, rock.animationElapsed);
    }
}

void Application::startMovableAnimations(const ActionRecord& action)
{
    const size_t rockCount = std::min(action.before.rocks.size(), action.after.rocks.size());
    for (size_t i = 0; i < rockCount && i < rocks_.size(); ++i) {
        const Vec2 target = toVec2(action.after.rocks[i].cell);
        if (gridDistance(rocks_[i].renderPosition, target) <= 0.0001f) {
            rocks_[i].renderPosition = target;
            rocks_[i].animationStart = target;
            rocks_[i].animationEnd = target;
            rocks_[i].animationElapsed = 0.0f;
            rocks_[i].animationDuration = 0.0f;
            rocks_[i].moving = false;
            continue;
        }

        rocks_[i].animationStart = rocks_[i].renderPosition;
        rocks_[i].animationEnd = target;
        rocks_[i].animationElapsed = 0.0f;
        rocks_[i].animationDuration = gridDistance(rocks_[i].animationStart, rocks_[i].animationEnd) * config::playerMoveDurationSeconds;
        rocks_[i].moving = true;
    }
}

bool Application::completeActiveAction()
{
    applyMoveRecord(activeAction_.after);

    moveHistory_.push_back(activeAction_);
    moving_ = false;
    moveElapsed_ = 0.0f;

    if (level_.isEnd(playerCell_) && isEndUnlocked()) {
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
    return static_cast<float>(gridDistance(activeAction_.before.player, activeAction_.after.player)) * config::playerMoveDurationSeconds;
}

bool Application::tryStartNextMove()
{
    if (playerDead_) {
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

    return tryStartHeldMove();
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

bool Application::tryStartMove(MoveDirection direction)
{
    if (playerDead_) {
        return false;
    }

    const GridPosition target = movementTarget(direction);
    if (!isPlayerWalkable(target)) {
        return false;
    }

    activeAction_ = {
        .before = captureMoveRecord(),
        .after = captureMoveRecord(),
    };
    activeAction_.after.player = target;

    if (Rock* rock = rockAt(target)) {
        if (!canMoveRock(target, direction)) {
            return false;
        }

        const size_t rockIndex = static_cast<size_t>(rock - rocks_.data());
        activeAction_.after.rocks[rockIndex] = {
            .cell = movementTarget(rock->cell, direction),
            .fallen = false,
        };
        activeAction_.after.rocks[rockIndex].cell = movableSlidingTarget(rockIndex, direction, activeAction_.after);
        activeAction_.after.rocks[rockIndex].fallen = isUnfilledWater(
            activeAction_.after.rocks[rockIndex].cell,
            activeAction_.after);
    }
    activeAction_.after.player = playerSlidingTarget(activeAction_.after.player, direction, activeAction_.after);
    activeAction_.after.playerDead = isUnfilledWater(activeAction_.after.player, activeAction_.after);

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
    moveElapsed_ = 0.0f;
    moving_ = true;
    startMovableAnimations(activeAction_);
    return true;
}

bool Application::tryStartRestart()
{
    if (playerDead_) {
        return false;
    }

    MoveRecord restarted {
        .player = level_.playerStart(),
        .playerDead = false,
    };
    restarted.rocks.reserve(level_.movableTiles().size());
    for (const Level::MovableTile& movable : level_.movableTiles()) {
        restarted.rocks.push_back({
            .cell = movable.position,
            .fallen = false,
        });
    }

    const MoveRecord current = captureMoveRecord();
    if (current.player == restarted.player &&
        current.playerDead == restarted.playerDead &&
        current.rocks == restarted.rocks) {
        return false;
    }

    activeAction_ = {
        .before = current,
        .after = std::move(restarted),
    };
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
    if (!tryStartMove(direction)) {
        return false;
    }

    if (queuedDirection && !hasPendingMove(*queuedDirection)) {
        pendingCommands_.push_back({ .type = MoveCommandType::Move, .direction = *queuedDirection });
    }

    return true;
}

Application::MoveRecord Application::captureMoveRecord() const
{
    MoveRecord record {
        .player = playerCell_,
        .playerDead = playerDead_,
    };
    record.rocks.reserve(rocks_.size());
    for (const Rock& rock : rocks_) {
        record.rocks.push_back({
            .cell = rock.cell,
            .fallen = rock.fallen,
        });
    }

    return record;
}

void Application::applyMoveRecord(const MoveRecord& record)
{
    playerCell_ = record.player;
    playerRenderPosition_ = toVec2(playerCell_);
    playerDead_ = record.playerDead;

    for (size_t i = 0; i < rocks_.size(); ++i) {
        rocks_[i].cell = record.rocks[i].cell;
        if (!rocks_[i].moving) {
            rocks_[i].renderPosition = toVec2(rocks_[i].cell);
            rocks_[i].animationStart = rocks_[i].renderPosition;
            rocks_[i].animationEnd = rocks_[i].renderPosition;
            rocks_[i].animationElapsed = 0.0f;
            rocks_[i].animationDuration = 0.0f;
        }
        rocks_[i].fallen = record.rocks[i].fallen;
    }
}

Application::ActionRecord Application::invertActionRecord(const ActionRecord& record) const
{
    return {
        .before = record.after,
        .after = record.before,
    };
}

GridPosition Application::movementTarget(MoveDirection direction) const
{
    return movementTarget(playerCell_, direction);
}

GridPosition Application::movementTarget(GridPosition origin, MoveDirection direction) const
{
    GridPosition target = origin;

    switch (direction) {
    case MoveDirection::Up:
        target.y -= 1;
        break;
    case MoveDirection::Down:
        target.y += 1;
        break;
    case MoveDirection::Left:
        target.x -= 1;
        break;
    case MoveDirection::Right:
        target.x += 1;
        break;
    }

    return target;
}

Application::Rock* Application::rockAt(GridPosition position)
{
    const auto rock = std::ranges::find_if(rocks_, [position](const Rock& candidate) {
        return !candidate.fallen && candidate.cell == position;
    });

    return rock != rocks_.end() ? &*rock : nullptr;
}

const Application::Rock* Application::rockAt(GridPosition position) const
{
    const auto rock = std::ranges::find_if(rocks_, [position](const Rock& candidate) {
        return !candidate.fallen && candidate.cell == position;
    });

    return rock != rocks_.end() ? &*rock : nullptr;
}

const Application::Rock* Application::fallenRockAt(GridPosition position) const
{
    const auto rock = std::ranges::find_if(rocks_, [position](const Rock& candidate) {
        return candidate.fallen && candidate.cell == position;
    });

    return rock != rocks_.end() ? &*rock : nullptr;
}

std::optional<TileType> Application::fallenTileAt(GridPosition position) const
{
    if (const Rock* rock = fallenRockAt(position)) {
        return rock->type;
    }

    return std::nullopt;
}

std::optional<TileType> Application::fallenTileAt(const MoveRecord& record, GridPosition position) const
{
    for (size_t i = 0; i < record.rocks.size() && i < rocks_.size(); ++i) {
        if (record.rocks[i].fallen && record.rocks[i].cell == position) {
            return rocks_[i].type;
        }
    }

    return std::nullopt;
}

const Application::MovableRecord* Application::movableRecordAt(const MoveRecord& record, GridPosition position) const
{
    const auto rock = std::ranges::find_if(record.rocks, [position](const MovableRecord& candidate) {
        return !candidate.fallen && candidate.cell == position;
    });

    return rock != record.rocks.end() ? &*rock : nullptr;
}

const Application::MovableRecord* Application::fallenMovableRecordAt(const MoveRecord& record, GridPosition position) const
{
    const auto rock = std::ranges::find_if(record.rocks, [position](const MovableRecord& candidate) {
        return candidate.fallen && candidate.cell == position;
    });

    return rock != record.rocks.end() ? &*rock : nullptr;
}

bool Application::isUnfilledWater(GridPosition position) const
{
    if (!level_.inBounds(position)) {
        return false;
    }

    const TileType tile = level_.tileAt(static_cast<uint32_t>(position.x), static_cast<uint32_t>(position.y));
    return tile == TileType::Water &&
        fallenRockAt(position) == nullptr &&
        !(playerDead_ && playerCell_ == position);
}

bool Application::isUnfilledWater(GridPosition position, const MoveRecord& record) const
{
    if (!level_.inBounds(position)) {
        return false;
    }

    const TileType tile = level_.tileAt(static_cast<uint32_t>(position.x), static_cast<uint32_t>(position.y));
    return tile == TileType::Water &&
        fallenMovableRecordAt(record, position) == nullptr &&
        !(record.playerDead && record.player == position);
}

bool Application::isIceFloor(GridPosition position, const MoveRecord& record) const
{
    if (!level_.inBounds(position)) {
        return false;
    }

    if (level_.tileAt(static_cast<uint32_t>(position.x), static_cast<uint32_t>(position.y)) == TileType::Ice) {
        return true;
    }

    return fallenTileAt(record, position) == TileType::Ice;
}

bool Application::isPlayerWalkable(GridPosition position) const
{
    if (!level_.inBounds(position)) {
        return false;
    }

    const TileType tile = level_.tileAt(static_cast<uint32_t>(position.x), static_cast<uint32_t>(position.y));
    return tile != TileType::Wall;
}

bool Application::isPlayerWalkable(GridPosition position, const MoveRecord& record) const
{
    if (!level_.inBounds(position) || movableRecordAt(record, position) != nullptr) {
        return false;
    }

    const TileType tile = level_.tileAt(static_cast<uint32_t>(position.x), static_cast<uint32_t>(position.y));
    return tile != TileType::Wall;
}

bool Application::canMoveRock(GridPosition position, MoveDirection direction) const
{
    const GridPosition target = movementTarget(position, direction);
    if (!level_.inBounds(target) || rockAt(target) != nullptr) {
        return false;
    }

    return level_.tileAt(static_cast<uint32_t>(target.x), static_cast<uint32_t>(target.y)) != TileType::Wall;
}

bool Application::canMovableOccupy(GridPosition position, const MoveRecord& record, size_t movableIndex) const
{
    if (!level_.inBounds(position)) {
        return false;
    }

    for (size_t i = 0; i < record.rocks.size(); ++i) {
        if (i != movableIndex && !record.rocks[i].fallen && record.rocks[i].cell == position) {
            return false;
        }
    }

    return level_.tileAt(static_cast<uint32_t>(position.x), static_cast<uint32_t>(position.y)) != TileType::Wall;
}

GridPosition Application::movableSlidingTarget(size_t movableIndex, MoveDirection direction, const MoveRecord& record) const
{
    if (movableIndex >= record.rocks.size() || movableIndex >= rocks_.size()) {
        return {};
    }

    GridPosition current = record.rocks[movableIndex].cell;
    const bool movableIsIce = rocks_[movableIndex].type == TileType::Ice;
    while (movableIsIce || isIceFloor(current, record)) {
        const GridPosition next = movementTarget(current, direction);
        if (!canMovableOccupy(next, record, movableIndex)) {
            return current;
        }

        current = next;
        if (isUnfilledWater(current, record)) {
            return current;
        }
    }

    return current;
}

GridPosition Application::playerSlidingTarget(GridPosition position, MoveDirection direction, const MoveRecord& record) const
{
    GridPosition current = position;
    while (isIceFloor(current, record)) {
        const GridPosition next = movementTarget(current, direction);
        if (!isPlayerWalkable(next, record)) {
            return current;
        }

        current = next;
    }

    return current;
}

bool Application::allPressurePlatesActive() const
{
    return std::ranges::all_of(level_.pressurePlates(), [this](GridPosition plate) {
        return playerCell_ == plate || rockAt(plate) != nullptr;
    });
}

bool Application::isEndUnlocked() const
{
    return allPressurePlatesActive();
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
    };
    frame.levelWidth = level_.width();
    frame.levelHeight = level_.height();
    frame.playerPosition = playerRenderPosition_;
    const bool endUnlocked = isEndUnlocked();

    frame.tiles.reserve(static_cast<size_t>(level_.width()) * level_.height());
    const bool playerMovingOutOfWater = moving_ && activeAction_.before.playerDead && !activeAction_.after.playerDead;
    auto staticCellAt = [this, endUnlocked, playerMovingOutOfWater](uint32_t x, uint32_t y) {
        const GridPosition position { static_cast<int>(x), static_cast<int>(y) };
        std::optional<TileType> fallenTile;
        if (const Rock* fallenRock = fallenRockAt(position)) {
            if (!fallenRock->moving) {
                fallenTile = fallenRock->type;
            }
        } else if (playerDead_ && !playerMovingOutOfWater && position == playerCell_) {
            fallenTile = TileType::Player;
        }

        return staticRenderCellFor(level_, x, y, endUnlocked, fallenTile);
    };
    appendGreedyMergedStaticTiles(frame, level_, staticCellAt);
    appendWaterEdgeFaces(frame, level_, [this](GridPosition position) {
        return isUnfilledWater(position);
    });

    if (!playerDead_ || playerMovingOutOfWater) {
        frame.tiles.push_back({
            .position = playerRenderPosition_,
            .color = tileColor(TileType::Player),
            .height = 1.0f,
        });
    }

    for (size_t rockIndex = 0; rockIndex < rocks_.size(); ++rockIndex) {
        const Rock& rock = rocks_[rockIndex];
        const bool movingOutOfWater = moving_ &&
            rockIndex < activeAction_.before.rocks.size() &&
            rockIndex < activeAction_.after.rocks.size() &&
            activeAction_.before.rocks[rockIndex].fallen &&
            !activeAction_.after.rocks[rockIndex].fallen;
        if (rock.fallen && !rock.moving && !movingOutOfWater) {
            continue;
        }

        frame.tiles.push_back({
            .position = rock.renderPosition,
            .color = tileColor(rock.type),
            .height = 1.0f,
        });
    }

    return frame;
}

RenderFrameData Application::buildEditorRenderFrame() const
{
    RenderFrameData frame;
    frame.viewMode = RenderViewMode::TopDown2D;
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
    };
    frame.levelWidth = levelEditor_.documentWidth();
    frame.levelHeight = levelEditor_.documentHeight();

    const std::vector<std::string>& rows = levelEditor_.documentRows();
    frame.tiles.reserve(static_cast<size_t>(frame.levelWidth) * frame.levelHeight + 1);
    for (uint32_t rowIndex = 0; rowIndex < frame.levelHeight; ++rowIndex) {
        const std::string& row = rows[static_cast<size_t>(rowIndex)];
        const int renderY = documentRowToRenderY(rowIndex, frame.levelHeight);
        for (uint32_t x = 0; x < frame.levelWidth; ++x) {
            const TileType tile = x < row.size()
                ? charToTileType(row[static_cast<size_t>(x)]).value_or(TileType::Count)
                : TileType::Empty;

            frame.tiles.push_back({
                .position = { static_cast<float>(x), static_cast<float>(renderY) },
                .color = tileColor(tile),
            });
        }
    }

    if (editorHoverCell_ &&
        editorHoverCell_->x >= 0 &&
        editorHoverCell_->y >= 0 &&
        editorHoverCell_->x < static_cast<int>(frame.levelWidth) &&
        editorHoverCell_->y < static_cast<int>(frame.levelHeight)) {
        Vec4 highlightColor = tileColor(levelEditor_.selectedTile());
        highlightColor.w = 0.72f;

        frame.tiles.push_back({
            .position = {
                static_cast<float>(editorHoverCell_->x) + 0.18f,
                static_cast<float>(editorHoverCell_->y) + 0.18f,
            },
            .size = { 0.64f, 0.64f },
            .color = highlightColor,
        });
    }

    return frame;
}

} // namespace sokoban
