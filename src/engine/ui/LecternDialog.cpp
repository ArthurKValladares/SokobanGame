#include "engine/ui/LecternDialog.hpp"

#include "engine/ui/LecternConfig.hpp"
#include "engine/ui/SelectorPrompt.hpp"
#include "engine/ui/UiControls.hpp"
#include "engine/ui/TextLayout.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace sokoban {

void LecternDialog::turnPage(bool previous, bool next)
{
    if (previous == next) return;
    if (previous && page_ > 0) --page_;
    if (next && page_ + 1 < pageCount_) ++page_;
}

std::vector<std::string> LecternDialog::wrapText(
    std::string_view text, float width,
    const std::function<float(std::string_view)>& measure)
{
    std::vector<std::string> lines;
    std::size_t paragraph = 0;
    do {
        const std::size_t end = text.find('\n', paragraph);
        std::string_view remaining = text.substr(paragraph,
            end == std::string_view::npos ? end : end - paragraph);
        if (!remaining.empty() && remaining.back() == '\r') remaining.remove_suffix(1);
        if (remaining.empty()) lines.emplace_back();
        while (!remaining.empty()) {
            const auto boundaries = textBoundaries(remaining);
            std::size_t fit = 0, opportunity = 0;
            for (const TextBoundary boundary : boundaries) {
                if (measure(remaining.substr(0, boundary.end)) > width) break;
                fit = boundary.end;
                if (boundary.lineBreak) opportunity = fit;
            }
            if (!fit) fit = boundaries.front().end;
            std::size_t count = fit;
            if (fit < remaining.size()) {
                if (opportunity) count = opportunity;
            }
            auto line = remaining.substr(0, count);
            while (!line.empty() && (line.back() == ' ' || line.back() == '\t')) line.remove_suffix(1);
            lines.emplace_back(line);
            remaining.remove_prefix(count);
            while (!remaining.empty() && (remaining.front() == ' ' || remaining.front() == '\t')) {
                remaining.remove_prefix(1);
            }
        }
        if (end == std::string_view::npos) break;
        paragraph = end + 1;
    } while (paragraph <= text.size());
    return lines;
}

void LecternDialog::rebuildLines(UiContext& ui, std::string_view text,
    float width, float fontSize, const PromptContext& context)
{
    lines_.clear();
    std::vector<Run> line;
    float used = 0.0f;
    std::string whitespace;
    const auto finishLine = [&] {
        lines_.push_back(std::move(line));
        line.clear();
        used = 0.0f;
        whitespace.clear();
    };
    const auto appendRun = [&](Run run) {
        if (!run.glyph && !line.empty() && !line.back().glyph &&
            line.back().error == run.error && line.back().scale == run.scale) {
            used -= line.back().width;
            line.back().text += run.text;
            line.back().width = ui.measureText(line.back().text, fontSize * run.scale).x;
            used += line.back().width;
        } else {
            used += run.width;
            line.push_back(std::move(run));
        }
    };
    const auto appendWhitespace = [&] {
        if (!line.empty() && !whitespace.empty()) {
            const float spaceWidth = ui.measureText(whitespace, fontSize).x;
            appendRun({ .text = whitespace, .width = spaceWidth });
        }
        whitespace.clear();
    };
    const auto appendWord = [&](std::string_view word, bool error) {
        const float wordWidth = ui.measureText(word, fontSize).x;
        const float spaceWidth = line.empty() ? 0.0f : ui.measureText(whitespace, fontSize).x;
        if (!line.empty() && used + spaceWidth + std::min(wordWidth, width) > width) finishLine();
        appendWhitespace();
        if (wordWidth <= width) {
            appendRun({ .text = std::string(word), .width = wordWidth, .error = error });
            return;
        }
        // Oversized text and error messages can split; key combinations stay atomic.
        std::size_t offset = 0;
        const auto boundaries = textBoundaries(word);
        while (offset < word.size()) {
            std::size_t count = 0;
            float extent = 0.0f;
            for (const TextBoundary boundary : boundaries) {
                if (boundary.end <= offset) continue;
                const float measured = ui.measureText(word.substr(offset, boundary.end - offset), fontSize).x;
                if (used + measured > width) break;
                extent = measured;
                count = boundary.end - offset;
            }
            if (count == 0 && !line.empty()) {
                finishLine();
                continue;
            }
            if (count == 0) {
                const auto next = std::ranges::find_if(boundaries, [&](TextBoundary b) { return b.end > offset; });
                count = next->end - offset;
                extent = ui.measureText(word.substr(offset, count), fontSize).x;
            }
            appendRun({ .text = std::string(word.substr(offset, count)), .width = extent, .error = error });
            offset += count;
            if (offset < word.size()) finishLine();
        }
    };
    const auto appendText = [&](std::string_view value, bool error) {
        std::size_t position = 0;
        while (position < value.size()) {
            if (value[position] == '\n') {
                finishLine();
                ++position;
            } else if (value[position] == '\r') {
                ++position;
            } else if (value[position] == ' ' || value[position] == '\t') {
                whitespace += value[position++];
            } else {
                const std::size_t end = value.find_first_of(" \t\r\n", position);
                const std::size_t count = end == std::string_view::npos ? value.size() - position : end - position;
                appendWord(value.substr(position, count), error);
                position += count;
            }
        }
    };
    const auto appendBinding = [&](const InputBinding& binding) {
        std::vector<Run> parts;
        const auto appendPart = [&](const InputBinding& part) {
            const auto glyph = context.prompts
                ? context.prompts->glyphForBinding(part, context.gamepad) : std::nullopt;
            if (glyph) {
                parts.push_back({ .glyph = glyph, .width = fontSize * glyph->aspectRatio });
            } else {
                const std::string label = "[" + bindingDisplayName(part) + "]";
                parts.push_back({ .text = label, .width = ui.measureText(label, fontSize).x });
            }
        };
        if (const auto* key = std::get_if<KeyboardBinding>(&binding)) {
            for (const KeyModifier modifier : { keyModifierCtrl, keyModifierShift, keyModifierAlt }) {
                if ((key->modifiers & modifier) == 0U) continue;
                const std::string scancode = modifier == keyModifierCtrl ? "Left Ctrl"
                    : modifier == keyModifierShift ? "Left Shift" : "Left Alt";
                appendPart(KeyboardBinding { scancode });
                parts.push_back({ .text = "+", .width = ui.measureText("+", fontSize).x });
            }
            appendPart(KeyboardBinding { key->scancode });
        } else {
            appendPart(binding);
        }
        float extent = 0.0f;
        for (const Run& part : parts) extent += part.width;
        if (extent > width) {
            const float fit = width / extent;
            for (Run& part : parts) {
                part.width *= fit;
                part.scale = fit;
            }
            extent = width;
        }
        const float spaceWidth = line.empty() ? 0.0f : ui.measureText(whitespace, fontSize).x;
        if (!line.empty() && used + spaceWidth + extent > width) finishLine();
        appendWhitespace();
        for (Run& part : parts) appendRun(std::move(part));
    };

    std::size_t position = 0;
    while (position < text.size()) {
        const std::size_t begin = text.find("<!", position);
        if (begin == std::string_view::npos) {
            appendText(text.substr(position), false);
            break;
        }
        appendText(text.substr(position, begin - position), false);
        const std::size_t end = text.find("!>", begin + 2);
        const std::size_t newline = text.find('\n', begin + 2);
        if (end == std::string_view::npos || (newline != std::string_view::npos && newline < end)) {
            appendText("[Invalid keybind tag]", true);
            position = newline == std::string_view::npos ? text.size() : newline;
            continue;
        }
        const std::string_view name = text.substr(begin + 2, end - begin - 2);
        const auto action = findInputAction(name);
        const InputBinding* binding = action && context.bindings
            ? SelectorPrompt::bindingView(*context.bindings, *action, context.deviceClass) : nullptr;
        if (!action) appendText("[Unknown action: " + std::string(name) + "]", true);
        else if (!binding) appendText("[Unbound action: " + std::string(name) + "]", true);
        else appendBinding(*binding);
        position = end + 2;
    }
    finishLine();
}

void LecternDialog::draw(UiContext& ui, Vec2 viewport, std::string_view text)
{
    draw(ui, viewport, text, {});
}

void LecternDialog::draw(UiContext& ui, Vec2 viewport, std::string_view text,
    const PromptContext& context)
{
    const float scale = std::min(viewport.x / 760.0f, viewport.y / 540.0f);
    const float padding = 32.0f * scale;
    const Vec2 size { std::min(720.0f * scale, viewport.x - 24.0f * scale),
        std::min(480.0f * scale, viewport.y - 24.0f * scale) };
    const UiRect panel { { (viewport.x - size.x) * 0.5f, (viewport.y - size.y) * 0.5f }, size };
    const std::string_view contents = text.empty() ? "This book has no text yet." : text;
    const float width = size.x - padding * 2.0f;
    // Keep the reading text proportional to the box, including on larger displays.
    float fontSize = std::max(size.y / 15.0f, config::lecternMinimumFontSize);
    if (config::lecternMaximumFontSize > 0.0f) {
        fontSize = std::min(fontSize,
            std::max(config::lecternMinimumFontSize, config::lecternMaximumFontSize));
    }
    const float lineHeight = fontSize * 1.3f;
    const bool bindingsChanged = context.bindings
        ? !cachedBindings_ || *cachedBindings_ != *context.bindings : cachedBindings_.has_value();
    const auto theme = context.prompts
        ? context.prompts->themeForGamepad(context.gamepad) : InputPromptTheme::Generic;
    if (contents != cachedText_ || width != cachedWidth_ || fontSize != cachedFontSize_ ||
        bindingsChanged || context.prompts != cachedPrompts_ || context.deviceClass != cachedDeviceClass_ ||
        theme != cachedTheme_ || context.gamepad.type != cachedGamepadType_ ||
        context.gamepad.faceButtonLabels != cachedFaceButtonLabels_) {
        cachedText_ = contents;
        cachedWidth_ = width;
        cachedFontSize_ = fontSize;
        cachedBindings_ = context.bindings ? std::optional<InputBindings>(*context.bindings) : std::nullopt;
        cachedPrompts_ = context.prompts;
        cachedDeviceClass_ = context.deviceClass;
        cachedTheme_ = theme;
        cachedGamepadType_ = context.gamepad.type;
        cachedFaceButtonLabels_ = context.gamepad.faceButtonLabels;
        rebuildLines(ui, contents, width, fontSize, context);
    }
    const std::size_t perPage = static_cast<std::size_t>(std::max(1.0f,
        std::floor((size.y - 158.0f * scale) / lineHeight)));
    pageCount_ = std::max(std::size_t { 1 }, (lines_.size() + perPage - 1) / perPage);
    page_ = std::min(page_, pageCount_ - 1);

    ui.rect({ {}, viewport }, { 0.015f, 0.025f, 0.035f, 0.62f });
    ui.rect({ { panel.position.x + 6.0f * scale, panel.position.y + 8.0f * scale }, size },
        { 0.0f, 0.0f, 0.0f, 0.35f });
    ui.rect(panel, { 0.84f, 0.64f, 0.32f, 1.0f });
    ui.rect({ { panel.position.x + 3.0f * scale, panel.position.y + 3.0f * scale },
        { size.x - 6.0f * scale, size.y - 6.0f * scale } }, { 0.09f, 0.075f, 0.055f, 1.0f });
    ui.text({ panel.position.x + padding, panel.position.y + 22.0f * scale },
        "LECTERN", { 0.95f, 0.79f, 0.48f, 1.0f }, 20.0f * scale);
    ui.rect({ { panel.position.x + padding, panel.position.y + 57.0f * scale },
        { size.x - padding * 2.0f, scale } }, { 0.43f, 0.34f, 0.22f, 1.0f });
    for (std::size_t i = 0; i < perPage && page_ * perPage + i < lines_.size(); ++i) {
        float x = panel.position.x + padding;
        const float y = panel.position.y + 77.0f * scale + static_cast<float>(i) * lineHeight;
        for (const Run& run : lines_[page_ * perPage + i]) {
            const float runSize = fontSize * run.scale;
            const Vec2 position { x, y + (fontSize - runSize) * 0.5f };
            if (run.glyph) {
                const UiInlineRun inlineRun { .texture = run.glyph->texture,
                    .uvRect = run.glyph->uvRect, .iconAspectRatio = run.glyph->aspectRatio };
                ui.inlineText(position, std::span(&inlineRun, 1), { 1.0f, 1.0f, 1.0f, 1.0f }, runSize);
            } else {
                ui.text(position, run.text, run.error ? Vec4 { 1.0f, 0.32f, 0.30f, 1.0f }
                    : Vec4 { 0.98f, 0.95f, 0.87f, 1.0f }, runSize);
            }
            x += run.width;
        }
    }
    const float footer = panel.position.y + size.y - 60.0f * scale;
    if (pageCount_ > 1) {
        const bool previous = uiControls::button(ui,
            { { panel.position.x + padding, footer }, { 100.0f * scale, 36.0f * scale } },
            "Previous", { .enabled = page_ > 0, .contentScale = scale });
        const bool next = uiControls::button(ui,
            { { panel.position.x + padding + 110.0f * scale, footer }, { 90.0f * scale, 36.0f * scale } },
            "Next", { .enabled = page_ + 1 < pageCount_, .contentScale = scale });
        turnPage(previous, next);
        const std::string page = std::to_string(page_ + 1) + " / " + std::to_string(pageCount_);
        ui.text({ panel.position.x + padding + 215.0f * scale, footer + 7.0f * scale },
            page, { 0.79f, 0.73f, 0.61f, 1.0f }, 18.0f * scale);
    }
    constexpr std::string_view hint = "Move away to close";
    const float hintSize = 18.0f * scale;
    ui.text({ panel.position.x + size.x - padding - ui.measureText(hint, hintSize).x,
        footer + 7.0f * scale }, hint, { 0.79f, 0.73f, 0.61f, 1.0f }, hintSize);
}

} // namespace sokoban
