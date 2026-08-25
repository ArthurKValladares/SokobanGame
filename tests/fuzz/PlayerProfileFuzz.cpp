#include "engine/PlayerProfile.hpp"

#include <cstddef>
#include <cstdint>
#include <exception>
#include <string>
#include <string_view>

namespace {

void decodeIgnoringMalformedInput(std::string_view input)
{
    try {
        (void)sokoban::decodePlayerProfile(input);
    } catch (const std::exception&) {
        // Invalid saves are expected and are handled by the save-recovery path.
    }
}

} // namespace

// Player profiles are player-controlled durable input. The decoder must reject
// arbitrary bytes without escaping an exception, exhausting memory, or causing
// undefined behavior. libFuzzer supplies the input and owns main().
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    constexpr size_t maxProfileBytes = 64 * 1024;
    if (size > maxProfileBytes) {
        return 0;
    }

    decodeIgnoringMalformedInput(std::string_view(
        reinterpret_cast<const char*>(data), size));

    // A raw fuzzer almost never invents a complete versioned JSON save. Apply
    // its bytes to a valid current-format profile too, so every execution also
    // exercises the strict semantic and migration paths near real input.
    std::string mutated = sokoban::PlayerProfile {}.serialize();
    for (size_t index = 0; index < size && index < 512; index += 2) {
        const size_t offset = static_cast<size_t>(data[index]) * 257U %
            mutated.size();
        const uint8_t value = index + 1 < size ? data[index + 1] : data[index];
        mutated[offset] ^= static_cast<char>(value);
    }
    decodeIgnoringMalformedInput(mutated);
    return 0;
}
