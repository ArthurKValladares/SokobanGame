#pragma once

#include "engine/Math.hpp"

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#ifndef SOKOBAN_ENABLE_DEBUG_UI
// Decides whether a tunable is a constant or a registered variable. Every
// translation unit must agree, so the build has to say (see CMakeLists.txt).
#error "SOKOBAN_ENABLE_DEBUG_UI must be defined by the build (see CMakeLists.txt)"
#endif

// Live-tunable configuration values.
//
// A *Config.hpp header declares its values with these macros instead of
// `inline constexpr`. In builds without developer tools each one still is
// exactly an `inline constexpr` constant. In Debug builds with the tools it is
// an ordinary variable registered with sokoban::tuning, so the Tuning tab can
// edit it while the game runs and write the edited value back into the
// header's own source text. Call sites read `config::name` either way and do
// not change.
//
// The macros are the file format the writer understands, so keep to it:
//
//     SOKOBAN_TUNING_SECTION(sectionId, "Title", "src/path/Header.hpp", "prefix");
//     SOKOBAN_TUNABLE_FLOAT(sectionId, name, value, minimum, maximum);
//     SOKOBAN_TUNABLE_UINT(sectionId, name, value, minimum, maximum);
//     SOKOBAN_TUNABLE_COLOR3(sectionId, name, red, green, blue);
//
// `value` (or the colour components) must be a plain numeric literal; the
// writer replaces exactly that argument. The path is relative to the
// repository root, and the prefix is dropped from names in the UI.
//
// Only runtime reads are allowed: a tunable is not a constant expression in
// a tools build, so it cannot size an array or feed a static_assert. Values
// that shape pipelines or resources belong in plain constants.

namespace sokoban::tuning {

struct Section {
    std::string_view title;
    std::string_view sourceFile;
    std::string_view namePrefix;
};

enum class Kind {
    Float,
    UInt,
    Color3,
};

struct Entry {
    const Section* section = nullptr;
    std::string_view name;
    Kind kind = Kind::Float;
    // Points at the live variable: float, uint32_t, or Vec3 by kind.
    void* value = nullptr;
    // What the header currently says, so the UI can show and revert edits.
    // Saving to source advances it.
    Vec3 saved {};
    float minimum = 0.0f;
    float maximum = 0.0f;
};

class Registration {
public:
    Registration(
        const Section& section,
        std::string_view name,
        float* value,
        float minimum,
        float maximum);
    Registration(
        const Section& section,
        std::string_view name,
        uint32_t* value,
        uint32_t minimum,
        uint32_t maximum);
    Registration(const Section& section, std::string_view name, Vec3* value);
};

// Every registered tunable, in registration order.
[[nodiscard]] std::span<Entry> entries();

[[nodiscard]] bool modified(const Entry& entry);
void revert(Entry& entry);

// The spelling the writer uses for a float literal: the shortest text that
// reads back as the same float, always with a decimal point, suffixed `f`.
[[nodiscard]] std::string floatLiteral(float value);

struct SaveResult {
    std::vector<std::filesystem::path> writtenFiles;
    std::vector<std::string> errors;
};

// Rewrites the value arguments of every modified entry in `section` in its
// header under `sourceRoot`, then marks them saved. Only the literal
// arguments change; the rest of the file is kept byte for byte.
[[nodiscard]] SaveResult saveToSource(
    const std::filesystem::path& sourceRoot,
    const Section& section);

// The text-level rewrite saveToSource performs, exposed for testing. Replaces
// the value arguments of the named macro invocations in `text`; `values`
// holds (name, replacement literals) pairs. Throws if a name is missing.
[[nodiscard]] std::string rewriteTunableValues(
    std::string text,
    const std::vector<std::pair<std::string, std::vector<std::string>>>& values);

} // namespace sokoban::tuning

#if SOKOBAN_ENABLE_DEBUG_UI
#define SOKOBAN_TUNING_SECTION(id, title, file, prefix) \
    inline constexpr ::sokoban::tuning::Section id { title, file, prefix }
#define SOKOBAN_TUNABLE_FLOAT(section, name, value, minimum, maximum) \
    inline float name = value; \
    inline const ::sokoban::tuning::Registration name##TuningRegistration { \
        section, #name, &(name), minimum, maximum }
#define SOKOBAN_TUNABLE_UINT(section, name, value, minimum, maximum) \
    inline uint32_t name = value; \
    inline const ::sokoban::tuning::Registration name##TuningRegistration { \
        section, #name, &(name), uint32_t { minimum }, uint32_t { maximum } }
#define SOKOBAN_TUNABLE_COLOR3(section, name, red, green, blue) \
    inline ::sokoban::Vec3 name { red, green, blue }; \
    inline const ::sokoban::tuning::Registration name##TuningRegistration { \
        section, #name, &(name) }
#else
#define SOKOBAN_TUNING_SECTION(id, title, file, prefix) static_assert(true)
#define SOKOBAN_TUNABLE_FLOAT(section, name, value, minimum, maximum) \
    inline constexpr float name = value
#define SOKOBAN_TUNABLE_UINT(section, name, value, minimum, maximum) \
    inline constexpr uint32_t name = value
#define SOKOBAN_TUNABLE_COLOR3(section, name, red, green, blue) \
    inline constexpr ::sokoban::Vec3 name { red, green, blue }
#endif
