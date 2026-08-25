#pragma once

namespace sokoban {

// Presentation is deliberately a policy rather than a platform mode. The
// Vulkan backend maps these choices to the modes advertised by the current
// surface, retaining FIFO as the portable safe fallback.
struct PresentationPolicy {
    bool vsync = true;
    bool allowTearing = false;

    bool operator==(const PresentationPolicy&) const = default;
};

enum class PresentationMode {
    Fifo,
    Mailbox,
    Immediate,
};

struct PresentationModeSupport {
    bool fifo = true;
    bool mailbox = false;
    bool immediate = false;
};

[[nodiscard]] constexpr PresentationMode choosePresentationMode(
    PresentationModeSupport support,
    PresentationPolicy policy) noexcept
{
    if (policy.vsync) {
        return PresentationMode::Fifo;
    }
    if (support.mailbox) {
        return PresentationMode::Mailbox;
    }
    if (policy.allowTearing && support.immediate) {
        return PresentationMode::Immediate;
    }
    return PresentationMode::Fifo;
}

} // namespace sokoban
