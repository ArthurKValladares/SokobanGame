#pragma once

#include "engine/ui/Ui.hpp"

#include <cstdint>
#include <vector>

namespace sokoban {

enum class UiLayoutAxis {
    Horizontal,
    Vertical,
};

enum class UiLayoutSizeKind {
    Content,
    Fixed,
    Fill,
};

struct UiLayoutSize {
    UiLayoutSizeKind kind = UiLayoutSizeKind::Content;
    float value = 0.0f;

    [[nodiscard]] static UiLayoutSize content() { return {}; }
    [[nodiscard]] static UiLayoutSize fixed(float pixels)
    {
        return { UiLayoutSizeKind::Fixed, pixels };
    }
    [[nodiscard]] static UiLayoutSize fill(float weight = 1.0f)
    {
        return { UiLayoutSizeKind::Fill, weight };
    }
};

struct UiLayoutInsets {
    float left = 0.0f;
    float top = 0.0f;
    float right = 0.0f;
    float bottom = 0.0f;
};

using UiLayoutNode = uint32_t;

class UiLayoutTree {
public:
    explicit UiLayoutTree(
        UiLayoutAxis rootAxis = UiLayoutAxis::Vertical,
        UiLayoutInsets rootInsets = {},
        float rootGap = 0.0f);

    [[nodiscard]] UiLayoutNode root() const { return 0; }
    [[nodiscard]] UiLayoutNode column(
        UiLayoutNode parent,
        UiLayoutSize height = UiLayoutSize::content(),
        float gap = 0.0f,
        UiLayoutInsets insets = {});
    [[nodiscard]] UiLayoutNode row(
        UiLayoutNode parent,
        UiLayoutSize height,
        float gap = 0.0f,
        UiLayoutInsets insets = {});
    [[nodiscard]] UiLayoutNode item(UiLayoutNode parent, float mainAxisPixels);
    [[nodiscard]] UiLayoutNode item(
        UiLayoutNode parent,
        UiLayoutSize width,
        UiLayoutSize height);
    void spacer(UiLayoutNode parent, float mainAxisPixels);
    void flexibleSpacer(UiLayoutNode parent, float weight = 1.0f);

    void arrange(UiRect bounds);
    [[nodiscard]] UiRect rect(UiLayoutNode node) const;
    [[nodiscard]] bool overflowed() const { return overflowed_; }

private:
    struct Node {
        UiLayoutAxis axis = UiLayoutAxis::Vertical;
        UiLayoutSize width = UiLayoutSize::content();
        UiLayoutSize height = UiLayoutSize::content();
        UiLayoutInsets insets {};
        float gap = 0.0f;
        bool container = false;
        std::vector<UiLayoutNode> children;
        UiRect arrangedRect {};
    };

    [[nodiscard]] UiLayoutNode addNode(UiLayoutNode parent, Node node);
    [[nodiscard]] Vec2 measuredSize(UiLayoutNode node) const;
    void arrangeNode(UiLayoutNode node, UiRect bounds);
    [[nodiscard]] const Node& checkedNode(UiLayoutNode node) const;
    [[nodiscard]] Node& checkedNode(UiLayoutNode node);

    std::vector<Node> nodes_;
    bool overflowed_ = false;
};

} // namespace sokoban
