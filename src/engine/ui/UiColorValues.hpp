#pragma once

#include "engine/Math.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>

namespace sokoban::ui::color {

enum class Format : uint8_t { Hex, Rgb, Cmyk, Hsv, Hsl };

struct Hsv {
    float hue = 0.0f; // Degrees.
    float saturation = 0.0f;
    float value = 0.0f;
};

namespace detail {

inline float unit(float value)
{
    return std::isfinite(value) ? std::clamp(value, 0.0f, 1.0f) : 0.0f;
}

inline Vec3 unitRgb(Vec3 rgb)
{
    return { unit(rgb.x), unit(rgb.y), unit(rgb.z) };
}

inline float wrapHue(float hue)
{
    if (!std::isfinite(hue)) return 0.0f;
    const float remainder = std::fmod(hue, 360.0f);
    if (remainder >= 0.0f) return remainder;
    const float wrapped = remainder + 360.0f;
    return wrapped < 360.0f ? wrapped : 0.0f;
}

inline Vec3 hueChroma(float hue, float chroma)
{
    const float sector = wrapHue(hue) / 60.0f;
    const float other =
        chroma * (1.0f - std::abs(std::fmod(sector, 2.0f) - 1.0f));
    switch (static_cast<int>(sector)) {
    case 0:
        return { chroma, other, 0.0f };
    case 1:
        return { other, chroma, 0.0f };
    case 2:
        return { 0.0f, chroma, other };
    case 3:
        return { 0.0f, other, chroma };
    case 4:
        return { other, 0.0f, chroma };
    default:
        return { chroma, 0.0f, other };
    }
}

inline void trimStart(std::string_view& text)
{
    const auto start = text.find_first_not_of(" \t\r\n");
    text.remove_prefix(start == std::string_view::npos ? text.size() : start);
}

inline int byte(float channel)
{
    return static_cast<int>(std::lround(channel * 255.0f));
}

inline void appendNumber(
    std::string& result,
    float value,
    std::string_view suffix = {})
{
    if (!result.empty()) result += ", ";
    // Fixed precision keeps display and parsing independent of the host locale.
    char buffer[16];
    const auto converted = std::to_chars(
        buffer, buffer + sizeof(buffer), value, std::chars_format::fixed, 3);
    const std::string_view digits(
        buffer, static_cast<std::size_t>(converted.ptr - buffer));
    const auto end = digits.find_last_not_of('0');
    result += digits.substr(0, end + (digits[end] == '.' ? 0 : 1));
    result += suffix;
}

inline constexpr std::string_view degrees = "\xc2\xb0";

} // namespace detail

// UI rectangles are blended in linear space before the display encodes sRGB.
[[nodiscard]] inline Vec3 displayLinear(Vec3 rgb)
{
    const auto decode = [](float encoded) {
        const float value = detail::unit(encoded);
        return value <= 0.04045f ? value / 12.92f
                                 : std::pow((value + 0.055f) / 1.055f, 2.4f);
    };
    return { decode(rgb.x), decode(rgb.y), decode(rgb.z) };
}

[[nodiscard]] inline Hsv toHsv(Vec3 rgb)
{
    rgb = detail::unitRgb(rgb);
    const float maximum = std::max({ rgb.x, rgb.y, rgb.z });
    const float chroma = maximum - std::min({ rgb.x, rgb.y, rgb.z });
    if (chroma == 0.0f) return { .value = maximum };

    float sector;
    if (maximum == rgb.x) {
        sector = (rgb.y - rgb.z) / chroma;
    } else if (maximum == rgb.y) {
        sector = (rgb.z - rgb.x) / chroma + 2.0f;
    } else {
        sector = (rgb.x - rgb.y) / chroma + 4.0f;
    }
    return { detail::wrapHue(sector * 60.0f), chroma / maximum, maximum };
}

[[nodiscard]] inline Vec3 fromHsv(Hsv hsv)
{
    const float value = detail::unit(hsv.value);
    const float chroma = value * detail::unit(hsv.saturation);
    const float base = value - chroma;
    return detail::unitRgb(
        detail::hueChroma(hsv.hue, chroma) + Vec3 { base, base, base });
}

[[nodiscard]] inline std::string format(Vec3 rgb, Format mode)
{
    rgb = detail::unitRgb(rgb);
    std::string result;
    switch (mode) {
    case Format::Hex: {
        constexpr std::string_view hex = "0123456789ABCDEF";
        result = "#";
        for (float channel : { rgb.x, rgb.y, rgb.z }) {
            const int value = detail::byte(channel);
            result += hex[static_cast<std::size_t>(value / 16)];
            result += hex[static_cast<std::size_t>(value % 16)];
        }
        break;
    }
    case Format::Rgb:
        result = std::to_string(detail::byte(rgb.x)) + ", " +
            std::to_string(detail::byte(rgb.y)) + ", " +
            std::to_string(detail::byte(rgb.z));
        break;
    case Format::Cmyk: {
        const float maximum = std::max({ rgb.x, rgb.y, rgb.z });
        for (float channel : { rgb.x, rgb.y, rgb.z }) {
            detail::appendNumber(
                result,
                maximum == 0.0f ? 0.0f : (1.0f - channel / maximum) * 100.0f,
                "%");
        }
        detail::appendNumber(result, (1.0f - maximum) * 100.0f, "%");
        break;
    }
    case Format::Hsv: {
        const Hsv hsv = toHsv(rgb);
        detail::appendNumber(result, hsv.hue, detail::degrees);
        detail::appendNumber(result, hsv.saturation * 100.0f, "%");
        detail::appendNumber(result, hsv.value * 100.0f, "%");
        break;
    }
    case Format::Hsl: {
        const Hsv hsv = toHsv(rgb);
        const float minimum = std::min({ rgb.x, rgb.y, rgb.z });
        const float lightness = (hsv.value + minimum) / 2.0f;
        // min(max + min, 2 - (max + min)) without cancellation near black.
        const float range = std::min(
            hsv.value + minimum, (1.0f - hsv.value) + (1.0f - minimum));
        const float saturation =
            range > 0.0f ? (hsv.value - minimum) / range : 0.0f;
        detail::appendNumber(result, hsv.hue, detail::degrees);
        detail::appendNumber(result, saturation * 100.0f, "%");
        detail::appendNumber(result, lightness * 100.0f, "%");
        break;
    }
    }
    return result;
}

[[nodiscard]] inline std::optional<Vec3> parse(
    std::string_view text,
    Format mode)
{
    detail::trimStart(text);
    if (text.empty()) return std::nullopt;
    text = text.substr(0, text.find_last_not_of(" \t\r\n") + 1);
    if (mode == Format::Hex) {
        if (text.starts_with('#')) text.remove_prefix(1);
        if (text.size() != 3 && text.size() != 6) return std::nullopt;

        unsigned value = 0;
        const auto end = text.data() + text.size();
        const auto parsed = std::from_chars(text.data(), end, value, 16);
        if (parsed.ec != std::errc {} || parsed.ptr != end) return std::nullopt;
        if (text.size() == 3) {
            return Vec3 { static_cast<float>(value >> 8) / 15.0f,
                          static_cast<float>((value >> 4) & 15U) / 15.0f,
                          static_cast<float>(value & 15U) / 15.0f };
        }
        return Vec3 { static_cast<float>(value >> 16) / 255.0f,
                      static_cast<float>((value >> 8) & 255U) / 255.0f,
                      static_cast<float>(value & 255U) / 255.0f };
    }

    std::array<float, 4> values {};
    const std::size_t count = mode == Format::Cmyk ? 4 : 3;
    for (std::size_t index = 0; index < count; ++index) {
        if (text.empty()) return std::nullopt;
        const auto end = text.data() + text.size();
        double value = 0.0;
        const auto parsed =
            std::from_chars(text.data(), end, value, std::chars_format::fixed);
        const bool hue =
            index == 0 && (mode == Format::Hsv || mode == Format::Hsl);
        const double limit = hue ? 360.0 : mode == Format::Rgb ? 255.0 : 100.0;
        if (parsed.ec != std::errc {} || !std::isfinite(value) || value < 0.0 ||
            value > limit ||
            (mode == Format::Rgb && std::trunc(value) != value)) {
            return std::nullopt;
        }
        values[index] = static_cast<float>(value);
        text.remove_prefix(static_cast<std::size_t>(parsed.ptr - text.data()));
        if (hue && text.starts_with(detail::degrees)) {
            text.remove_prefix(detail::degrees.size());
        } else if (hue && text.starts_with("deg")) {
            text.remove_prefix(3);
        } else if (!hue && mode != Format::Rgb && text.starts_with('%')) {
            text.remove_prefix(1);
        }
        const auto remaining = text.size();
        detail::trimStart(text);
        if (index + 1 == count) {
            if (!text.empty()) return std::nullopt;
        } else if (text.starts_with(',')) {
            text.remove_prefix(1);
            detail::trimStart(text);
        } else if (remaining == text.size()) {
            return std::nullopt;
        }
    }

    switch (mode) {
    case Format::Rgb:
        return Vec3 { values[0] / 255.0f,
                      values[1] / 255.0f,
                      values[2] / 255.0f };
    case Format::Cmyk: {
        const float brightness = 1.0f - values[3] / 100.0f;
        return Vec3 { (1.0f - values[0] / 100.0f) * brightness,
                      (1.0f - values[1] / 100.0f) * brightness,
                      (1.0f - values[2] / 100.0f) * brightness };
    }
    case Format::Hsv:
        return fromHsv({ values[0], values[1] / 100.0f, values[2] / 100.0f });
    case Format::Hsl: {
        const float lightness = values[2] / 100.0f;
        const float chroma =
            2.0f * std::min(lightness, 1.0f - lightness) * values[1] / 100.0f;
        const float base = lightness - chroma / 2.0f;
        return detail::unitRgb(
            detail::hueChroma(values[0], chroma) + Vec3 { base, base, base });
    }
    case Format::Hex:
        break;
    }
    return std::nullopt;
}

} // namespace sokoban::ui::color
