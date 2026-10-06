#pragma once

#include "engine/ArenaArray.hpp"
#include "engine/FrameArena.hpp"
#include "engine/Math.hpp"
#include "engine/render/RenderTypes.hpp"
#include "engine/ui/UiConfig.hpp"
#include "engine/ui/TextLayout.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <span>
#include <cstdint>

namespace sokoban {

class FontAtlas;

struct UiRect {
    Vec2 position {};
    Vec2 size {};
};

enum class UiDrawKind : uint8_t {
    Solid,
    FontGlyph,
    Image,
    TextureImage,
    SceneImage,
};

struct UiDrawCommand {
    UiDrawKind kind = UiDrawKind::Solid;
    UiRect rect {};
    UiRect uvRect {};
    Vec4 color {};
    Vec4 effectOptions {};
    NineSlice nineSlice {};
    RenderTexture texture = noTexture;
    uint32_t glyphCurveOffset = 0;
    bool outlineGlyph = false;
    float fontSize = 0.0f; // Requested display size, for UI diagnostics.
};

// A mixed line uses the same advance, baseline and draw order for text and
// icons. Input bindings resolve to these identities before layout.
struct UiInlineRun {
    std::string_view text;
    uint32_t vectorIcon = 0;
    RenderTexture texture = noTexture;
    UiRect uvRect { {}, { 1.0f, 1.0f } };
    float iconAspectRatio = 1.0f;
};

struct UiDrawData {
    UiDrawData() = default;

    // The frame's whole budget in one bump, so a frame of UI is exactly one
    // arena allocation.
    explicit UiDrawData(FrameArena& arena)
        : commands(arena, config::uiFrameCommandBudget)
    {
    }

    Vec2 viewportSize {};
    ArenaArray<UiDrawCommand> commands;
};

class UiContext {
public:
    explicit UiContext(const FontAtlas& font);

    void beginFrame(Vec2 viewportSize, Vec2 mousePosition, bool mouseDown, bool mousePressed);
    void endFrame();

    [[nodiscard]] const UiDrawData& drawData() const { return drawData_; }
    // Scratch data is consumed during this frame and dies at beginFrame().
    [[nodiscard]] FrameArena& frameArena() { return frameArena_; }
    // Arena tuning, for the Engine debug tab. bytesUsed() is this frame,
    // highWaterBytes() is the number the capacity should be derived from, and
    // droppedCommands() is what the budget cost the frame just drawn.
    [[nodiscard]] std::size_t frameArenaBytesUsed() const
    {
        return frameArena_.bytesUsed();
    }
    [[nodiscard]] std::size_t frameArenaHighWaterBytes() const
    {
        return frameArena_.highWaterBytes();
    }
    [[nodiscard]] std::size_t droppedCommands() const
    {
        return drawData_.commands.droppedCount();
    }
    [[nodiscard]] Vec2 mousePosition() const { return mousePosition_; }
    [[nodiscard]] bool mouseDown() const { return mouseDown_; }
    [[nodiscard]] bool mousePressed() const { return mousePressed_; }
    [[nodiscard]] bool contains(UiRect rect, Vec2 point) const;
    [[nodiscard]] bool hovered(UiRect rect) const;
    [[nodiscard]] bool clicked(UiRect rect) const;
    [[nodiscard]] bool drag(std::string_view id, UiRect rect);

    void rect(UiRect rect, Vec4 color);
    void image(
        UiRect rect,
        UiRect uvRect = { {}, { 1.0f, 1.0f } },
        Vec4 color = { 1.0f, 1.0f, 1.0f, 1.0f });
    void textureImage(
        UiRect rect,
        RenderTexture texture,
        UiRect uvRect = { {}, { 1.0f, 1.0f } },
        Vec4 color = { 1.0f, 1.0f, 1.0f, 1.0f },
        // Use NineSlice::preserveSourcePixels(...) for UI panels whose
        // corners must retain their authored pixel size.
        NineSlice nineSlice = {});
    // Samples the renderer's preserved main-scene image. Used when UI needs
    // to composite the live world back over a later scene pass.
    void sceneImage(
        UiRect rect,
        UiRect uvRect = { {}, { 1.0f, 1.0f } },
        Vec4 color = { 1.0f, 1.0f, 1.0f, 1.0f },
        Vec4 effectOptions = {});
    void panel(UiRect rect);
    void divider(UiRect rect);
    void text(Vec2 position, std::string_view text, Vec4 color, float size = 24.0f,
        GlyphRendering rendering = GlyphRendering::Automatic);
    void inlineText(Vec2 position, std::span<const UiInlineRun> runs, Vec4 color, float size = 24.0f);
    [[nodiscard]] Vec2 measureInlineText(std::span<const UiInlineRun> runs, float size = 24.0f) const;
    [[nodiscard]] Vec2 measureText(std::string_view text, float size = 24.0f) const;
    void centeredText(UiRect rect, std::string_view text, Vec4 color, float size = 24.0f);

private:
    void drawGlyph(Vec2 baseline, const struct FontGlyph& glyph, Vec4 color, float size);
    const FontAtlas* font_ = nullptr;
    // UI commands are consumed synchronously by drawFrame(), so none may
    // survive beginFrame(). Sized from the same budget the command array
    // asks for, so the one allocation a frame makes cannot fail to fit.
    FrameArena frameArena_;
    // Holds no storage of its own, so beginFrame() overwrites it rather than
    // having to destroy it before the arena underneath can be reset.
    UiDrawData drawData_;
    Vec2 mousePosition_ {};
    bool mouseDown_ = false;
    bool mousePressed_ = false;
    // One warning per context, like the arena's own.
    bool reportedDroppedCommands_ = false;
    std::string activeControl_;
};

} // namespace sokoban
