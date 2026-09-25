#include "engine/Tuning.hpp"

#include "engine/AtomicFile.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <system_error>
#include <utility>

namespace sokoban::tuning {
namespace {

std::vector<Entry>& registry()
{
    // Function-local so registrations from any translation unit's static
    // initialization find it constructed.
    static std::vector<Entry> entries;
    return entries;
}

std::vector<std::string> literalsFor(const Entry& entry)
{
    switch (entry.kind) {
    case Kind::Float:
        return { floatLiteral(*static_cast<const float*>(entry.value)) };
    case Kind::UInt:
        return { std::to_string(*static_cast<const uint32_t*>(entry.value)) };
    case Kind::Color3: {
        const Vec3& color = *static_cast<const Vec3*>(entry.value);
        return { floatLiteral(color.x), floatLiteral(color.y),
            floatLiteral(color.z) };
    }
    }
    return {};
}

Vec3 currentValue(const Entry& entry)
{
    switch (entry.kind) {
    case Kind::Float:
        return { *static_cast<const float*>(entry.value), 0.0f, 0.0f };
    case Kind::UInt:
        return {
            static_cast<float>(*static_cast<const uint32_t*>(entry.value)),
            0.0f,
            0.0f,
        };
    case Kind::Color3:
        return *static_cast<const Vec3*>(entry.value);
    }
    return {};
}

bool isIdentifierCharacter(char character)
{
    return std::isalnum(static_cast<unsigned char>(character)) != 0 ||
        character == '_';
}

struct Argument {
    std::size_t begin = 0;
    std::size_t end = 0;
};

// The comma-separated arguments of the invocation whose '(' is at `open`,
// trimmed of surrounding whitespace, and the position of its ')'.
std::pair<std::vector<Argument>, std::size_t> invocationArguments(
    const std::string& text,
    std::size_t open)
{
    std::vector<Argument> arguments;
    int depth = 0;
    std::size_t start = open + 1;
    for (std::size_t index = open + 1; index < text.size(); ++index) {
        const char character = text[index];
        if (character == '(' || character == '{') {
            ++depth;
        } else if ((character == ')' || character == '}') && depth > 0) {
            --depth;
        } else if ((character == ',' && depth == 0) ||
                   (character == ')' && depth == 0)) {
            std::size_t begin = start;
            std::size_t end = index;
            while (begin < end &&
                   std::isspace(static_cast<unsigned char>(text[begin])) != 0) {
                ++begin;
            }
            while (end > begin &&
                   std::isspace(static_cast<unsigned char>(text[end - 1])) != 0) {
                --end;
            }
            arguments.push_back({ begin, end });
            if (character == ')') {
                return { std::move(arguments), index };
            }
            start = index + 1;
        }
    }
    throw std::runtime_error("unterminated tunable declaration");
}

// `text` with comments and string literals blanked to spaces, so offsets
// match the original and a declaration quoted in a comment is never taken
// for the real one.
std::string codeOnly(const std::string& text)
{
    std::string code = text;
    enum class State { Code, LineComment, BlockComment, String, Character };
    State state = State::Code;
    for (std::size_t index = 0; index < code.size(); ++index) {
        const char character = text[index];
        const char next = index + 1 < text.size() ? text[index + 1] : '\0';
        switch (state) {
        case State::Code:
            if (character == '/' && next == '/') {
                state = State::LineComment;
            } else if (character == '/' && next == '*') {
                state = State::BlockComment;
            } else if (character == '"') {
                state = State::String;
            } else if (character == '\'') {
                state = State::Character;
            } else {
                continue;
            }
            code[index] = ' ';
            break;
        case State::LineComment:
            if (character == '\n') {
                state = State::Code;
                continue;
            }
            code[index] = ' ';
            break;
        case State::BlockComment:
            code[index] = ' ';
            if (character == '*' && next == '/') {
                code[++index] = ' ';
                state = State::Code;
            }
            break;
        case State::String:
        case State::Character:
            code[index] = ' ';
            if (character == '\\' && index + 1 < code.size()) {
                code[++index] = ' ';
            } else if ((state == State::String && character == '"') ||
                       (state == State::Character && character == '\'')) {
                state = State::Code;
            }
            break;
        }
    }
    return code;
}

std::string readText(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("cannot read " + path.string());
    }
    return { std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>() };
}

} // namespace

Registration::Registration(
    const Section& section,
    std::string_view name,
    float* value,
    float minimum,
    float maximum)
{
    registry().push_back({
        .section = &section,
        .name = name,
        .kind = Kind::Float,
        .value = value,
        .saved = { *value, 0.0f, 0.0f },
        .minimum = minimum,
        .maximum = maximum,
    });
}

Registration::Registration(
    const Section& section,
    std::string_view name,
    uint32_t* value,
    uint32_t minimum,
    uint32_t maximum)
{
    registry().push_back({
        .section = &section,
        .name = name,
        .kind = Kind::UInt,
        .value = value,
        .saved = { static_cast<float>(*value), 0.0f, 0.0f },
        .minimum = static_cast<float>(minimum),
        .maximum = static_cast<float>(maximum),
    });
}

Registration::Registration(
    const Section& section,
    std::string_view name,
    Vec3* value)
{
    registry().push_back({
        .section = &section,
        .name = name,
        .kind = Kind::Color3,
        .value = value,
        .saved = *value,
        .minimum = 0.0f,
        .maximum = 1.0f,
    });
}

std::span<Entry> entries()
{
    return registry();
}

bool modified(const Entry& entry)
{
    const Vec3 value = currentValue(entry);
    return value.x != entry.saved.x || value.y != entry.saved.y ||
        value.z != entry.saved.z;
}

void revert(Entry& entry)
{
    switch (entry.kind) {
    case Kind::Float:
        *static_cast<float*>(entry.value) = entry.saved.x;
        break;
    case Kind::UInt:
        *static_cast<uint32_t*>(entry.value) =
            static_cast<uint32_t>(entry.saved.x);
        break;
    case Kind::Color3:
        *static_cast<Vec3*>(entry.value) = entry.saved;
        break;
    }
}

std::string floatLiteral(float value)
{
    std::array<char, 64> buffer {};
    const auto [end, error] =
        std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
    if (error != std::errc {}) {
        throw std::runtime_error("cannot format a tunable value");
    }
    std::string text(buffer.data(), end);
    if (text.find_first_of(".eEn") == std::string::npos) {
        text += ".0";
    }
    return text + 'f';
}

std::string rewriteTunableValues(
    std::string text,
    const std::vector<std::pair<std::string, std::vector<std::string>>>& values)
{
    for (const auto& [name, literals] : values) {
        // Recomputed per name: each replacement shifts later offsets.
        const std::string code = codeOnly(text);
        bool found = false;
        std::size_t search = 0;
        while (!found) {
            const std::size_t macro = code.find("SOKOBAN_TUNABLE_", search);
            if (macro == std::string::npos) {
                break;
            }
            search = macro + 1;
            // Skip the macro definitions themselves and any other spelling
            // that is not an invocation.
            std::size_t open = macro + std::string_view("SOKOBAN_TUNABLE_").size();
            while (open < code.size() && isIdentifierCharacter(code[open])) {
                ++open;
            }
            if (open >= code.size() || code[open] != '(') {
                continue;
            }
            const auto [arguments, close] = invocationArguments(code, open);
            if (arguments.size() < 3 ||
                std::string_view(code).substr(
                    arguments[1].begin,
                    arguments[1].end - arguments[1].begin) != name) {
                continue;
            }
            if (arguments.size() < 2 + literals.size()) {
                throw std::runtime_error(
                    "tunable " + name + " has fewer value arguments than expected");
            }
            // Replace back to front so earlier offsets stay valid.
            for (std::size_t index = literals.size(); index-- > 0;) {
                const Argument& argument = arguments[2 + index];
                text.replace(
                    argument.begin,
                    argument.end - argument.begin,
                    literals[index]);
            }
            (void)close;
            found = true;
        }
        if (!found) {
            throw std::runtime_error(
                "no SOKOBAN_TUNABLE declaration of " + name + " found");
        }
    }
    return text;
}

SaveResult saveToSource(
    const std::filesystem::path& sourceRoot,
    const Section& section)
{
    SaveResult result;
    std::vector<std::pair<std::string, std::vector<std::string>>> values;
    std::vector<Entry*> saving;
    for (Entry& entry : registry()) {
        if (entry.section == &section && modified(entry)) {
            values.emplace_back(std::string(entry.name), literalsFor(entry));
            saving.push_back(&entry);
        }
    }
    if (values.empty()) {
        return result;
    }
    const std::filesystem::path path =
        sourceRoot / std::filesystem::path(section.sourceFile);
    try {
        const std::string original = readText(path);
        const std::string rewritten =
            rewriteTunableValues(original, values);
        atomicFile::write(path, rewritten);
        result.writtenFiles.push_back(path);
        for (Entry* entry : saving) {
            entry->saved = currentValue(*entry);
        }
    } catch (const std::exception& error) {
        result.errors.push_back(path.string() + ": " + error.what());
    }
    return result;
}

} // namespace sokoban::tuning
