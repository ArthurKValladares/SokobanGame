#include "engine/ui/UiLayout.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace sokoban {
namespace {

float resolvedMeasuredSize(UiLayoutSize specification, float contentSize)
{
    switch (specification.kind) {
    case UiLayoutSizeKind::Fixed: return std::max(specification.value, 0.0f);
    case UiLayoutSizeKind::Content: return std::max(contentSize, 0.0f);
    case UiLayoutSizeKind::Fill: return std::max(contentSize, 0.0f);
    }
    return 0.0f;
}

float axisValue(Vec2 value, UiLayoutAxis axis)
{
    return axis == UiLayoutAxis::Horizontal ? value.x : value.y;
}

UiLayoutSize axisSize(
    UiLayoutSize width,
    UiLayoutSize height,
    UiLayoutAxis axis)
{
    return axis == UiLayoutAxis::Horizontal ? width : height;
}

} // namespace

UiLayoutTree::UiLayoutTree(
    UiLayoutAxis rootAxis,
    UiLayoutInsets rootInsets,
    float rootGap)
{
    Node rootNode;
    rootNode.axis = rootAxis;
    rootNode.width = UiLayoutSize::fill();
    rootNode.height = UiLayoutSize::fill();
    rootNode.insets = rootInsets;
    rootNode.gap = std::max(rootGap, 0.0f);
    rootNode.container = true;
    nodes_.push_back(rootNode);
}

UiLayoutNode UiLayoutTree::column(
    UiLayoutNode parent,
    UiLayoutSize height,
    float gap,
    UiLayoutInsets insets)
{
    Node node;
    node.axis = UiLayoutAxis::Vertical;
    node.width = UiLayoutSize::fill();
    node.height = height;
    node.insets = insets;
    node.gap = std::max(gap, 0.0f);
    node.container = true;
    return addNode(parent, node);
}

UiLayoutNode UiLayoutTree::row(
    UiLayoutNode parent,
    UiLayoutSize height,
    float gap,
    UiLayoutInsets insets)
{
    Node node;
    node.axis = UiLayoutAxis::Horizontal;
    node.width = UiLayoutSize::fill();
    node.height = height;
    node.insets = insets;
    node.gap = std::max(gap, 0.0f);
    node.container = true;
    return addNode(parent, node);
}

UiLayoutNode UiLayoutTree::item(UiLayoutNode parent, float mainAxisPixels)
{
    const UiLayoutAxis parentAxis = checkedNode(parent).axis;
    return parentAxis == UiLayoutAxis::Horizontal
        ? item(parent, UiLayoutSize::fixed(mainAxisPixels), UiLayoutSize::fill())
        : item(parent, UiLayoutSize::fill(), UiLayoutSize::fixed(mainAxisPixels));
}

UiLayoutNode UiLayoutTree::item(
    UiLayoutNode parent,
    UiLayoutSize width,
    UiLayoutSize height)
{
    Node node;
    node.width = width;
    node.height = height;
    return addNode(parent, node);
}

void UiLayoutTree::spacer(UiLayoutNode parent, float mainAxisPixels)
{
    (void)item(parent, mainAxisPixels);
}

void UiLayoutTree::flexibleSpacer(UiLayoutNode parent, float weight)
{
    const UiLayoutAxis parentAxis = checkedNode(parent).axis;
    if (parentAxis == UiLayoutAxis::Horizontal) {
        (void)item(parent, UiLayoutSize::fill(weight), UiLayoutSize::fill());
    } else {
        (void)item(parent, UiLayoutSize::fill(), UiLayoutSize::fill(weight));
    }
}

void UiLayoutTree::arrange(UiRect bounds)
{
    overflowed_ = false;
    arrangeNode(root(), bounds);
}

UiRect UiLayoutTree::rect(UiLayoutNode node) const
{
    return checkedNode(node).arrangedRect;
}

UiLayoutNode UiLayoutTree::addNode(UiLayoutNode parent, Node node)
{
    Node& parentNode = checkedNode(parent);
    if (!parentNode.container) {
        throw std::invalid_argument("UI layout children require a container parent");
    }
    const UiLayoutNode id = static_cast<UiLayoutNode>(nodes_.size());
    nodes_.push_back(std::move(node));
    checkedNode(parent).children.push_back(id);
    return id;
}

Vec2 UiLayoutTree::measuredSize(UiLayoutNode nodeId) const
{
    const Node& node = checkedNode(nodeId);
    Vec2 content {};
    if (node.container) {
        float mainTotal = 0.0f;
        float crossMaximum = 0.0f;
        for (UiLayoutNode child : node.children) {
            const Vec2 childSize = measuredSize(child);
            mainTotal += axisValue(childSize, node.axis);
            crossMaximum = std::max(
                crossMaximum,
                axisValue(childSize,
                    node.axis == UiLayoutAxis::Horizontal
                        ? UiLayoutAxis::Vertical
                        : UiLayoutAxis::Horizontal));
        }
        if (node.children.size() > 1) {
            mainTotal += node.gap * static_cast<float>(node.children.size() - 1);
        }
        if (node.axis == UiLayoutAxis::Horizontal) {
            content = {
                mainTotal + node.insets.left + node.insets.right,
                crossMaximum + node.insets.top + node.insets.bottom,
            };
        } else {
            content = {
                crossMaximum + node.insets.left + node.insets.right,
                mainTotal + node.insets.top + node.insets.bottom,
            };
        }
    }
    return {
        resolvedMeasuredSize(node.width, content.x),
        resolvedMeasuredSize(node.height, content.y),
    };
}

void UiLayoutTree::arrangeNode(UiLayoutNode nodeId, UiRect bounds)
{
    Node& node = checkedNode(nodeId);
    node.arrangedRect = bounds;
    if (!node.container || node.children.empty()) {
        return;
    }

    const UiRect inner {
        {
            bounds.position.x + node.insets.left,
            bounds.position.y + node.insets.top,
        },
        {
            std::max(bounds.size.x - node.insets.left - node.insets.right, 0.0f),
            std::max(bounds.size.y - node.insets.top - node.insets.bottom, 0.0f),
        },
    };
    const float availableMain = axisValue(inner.size, node.axis);
    const float availableCross = axisValue(
        inner.size,
        node.axis == UiLayoutAxis::Horizontal
            ? UiLayoutAxis::Vertical
            : UiLayoutAxis::Horizontal);
    const float totalGap = node.children.size() > 1
        ? node.gap * static_cast<float>(node.children.size() - 1)
        : 0.0f;

    float occupiedMain = totalGap;
    float fillWeight = 0.0f;
    for (UiLayoutNode childId : node.children) {
        const Node& child = checkedNode(childId);
        const UiLayoutSize mainSpecification = axisSize(
            child.width, child.height, node.axis);
        if (mainSpecification.kind == UiLayoutSizeKind::Fill) {
            fillWeight += std::max(mainSpecification.value, 0.0f);
        } else {
            occupiedMain += axisValue(measuredSize(childId), node.axis);
        }
    }
    const float fillSpace = std::max(availableMain - occupiedMain, 0.0f);
    overflowed_ |= occupiedMain > availableMain + 0.01f;
    float cursor = node.axis == UiLayoutAxis::Horizontal
        ? inner.position.x
        : inner.position.y;

    for (UiLayoutNode childId : node.children) {
        const Node& child = checkedNode(childId);
        const Vec2 measured = measuredSize(childId);
        const UiLayoutSize mainSpecification = axisSize(
            child.width, child.height, node.axis);
        const UiLayoutSize crossSpecification = axisSize(
            child.width,
            child.height,
            node.axis == UiLayoutAxis::Horizontal
                ? UiLayoutAxis::Vertical
                : UiLayoutAxis::Horizontal);
        const float mainSize = mainSpecification.kind == UiLayoutSizeKind::Fill
            ? (fillWeight > 0.0f
                ? fillSpace * std::max(mainSpecification.value, 0.0f) / fillWeight
                : 0.0f)
            : axisValue(measured, node.axis);
        const float crossSize = crossSpecification.kind == UiLayoutSizeKind::Fill
            ? availableCross
            : std::min(axisValue(
                measured,
                node.axis == UiLayoutAxis::Horizontal
                    ? UiLayoutAxis::Vertical
                    : UiLayoutAxis::Horizontal), availableCross);

        UiRect childBounds;
        if (node.axis == UiLayoutAxis::Horizontal) {
            childBounds = { { cursor, inner.position.y }, { mainSize, crossSize } };
        } else {
            childBounds = { { inner.position.x, cursor }, { crossSize, mainSize } };
        }
        arrangeNode(childId, childBounds);
        cursor += mainSize + node.gap;
    }
}

const UiLayoutTree::Node& UiLayoutTree::checkedNode(UiLayoutNode node) const
{
    if (node >= nodes_.size()) {
        throw std::out_of_range("UI layout node does not belong to this tree");
    }
    return nodes_[node];
}

UiLayoutTree::Node& UiLayoutTree::checkedNode(UiLayoutNode node)
{
    return const_cast<Node&>(std::as_const(*this).checkedNode(node));
}

} // namespace sokoban
