#include "engine/ui/LecternDialog.hpp"

#include "engine/ui/LecternConfig.hpp"
#include "engine/ui/UiControls.hpp"

#include <algorithm>
#include <cmath>

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
            std::size_t fit = 0;
            while (fit < remaining.size() &&
                measure(remaining.substr(0, fit + 1)) <= width) ++fit;
            fit = std::max(fit, std::size_t { 1 });
            std::size_t count = fit;
            if (fit < remaining.size()) {
                const std::size_t space = remaining.substr(0, fit + 1).find_last_of(" \t");
                if (space != std::string_view::npos && space > 0) count = space;
            }
            lines.emplace_back(remaining.substr(0, count));
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

bool LecternDialog::draw(UiContext& ui, Vec2 viewport,
    std::string_view text, std::string_view closeBinding)
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
    if (contents != cachedText_ || width != cachedWidth_ || fontSize != cachedFontSize_) {
        cachedText_ = contents;
        cachedWidth_ = width;
        cachedFontSize_ = fontSize;
        lines_ = wrapText(contents, width,
            [&](std::string_view line) { return ui.measureText(line, fontSize).x; });
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
        ui.text({ panel.position.x + padding,
            panel.position.y + 77.0f * scale + static_cast<float>(i) * lineHeight },
            lines_[page_ * perPage + i], { 0.98f, 0.95f, 0.87f, 1.0f }, fontSize);
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
    const std::string close = closeBinding.empty() ? "Close" : "Close  [" + std::string(closeBinding) + "]";
    return uiControls::button(ui,
        { { panel.position.x + size.x - padding - 175.0f * scale, footer },
            { 175.0f * scale, 36.0f * scale } }, close,
        { .tone = uiControls::ButtonTone::Accent, .contentScale = scale });
}

} // namespace sokoban
