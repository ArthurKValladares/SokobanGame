#include "engine/AtomicFile.hpp"

#include <fstream>
#include <stdexcept>
#include <system_error>

namespace sokoban::atomicFile {

void replace(
    const std::filesystem::path& destination,
    const std::filesystem::path& temporary)
{
    std::error_code error;
    std::filesystem::rename(temporary, destination, error);
    if (!error) {
        return;
    }

    const std::filesystem::path displaced =
        destination.string() + ".replace-old";
    std::filesystem::remove(displaced, error);
    error.clear();
    std::filesystem::rename(destination, displaced, error);
    if (error) {
        throw std::runtime_error(
            "cannot prepare replacement for " + destination.string() +
            ": " + error.message());
    }

    error.clear();
    std::filesystem::rename(temporary, destination, error);
    if (error) {
        std::error_code rollbackError;
        std::filesystem::rename(displaced, destination, rollbackError);
        throw std::runtime_error(
            "cannot replace " + destination.string() + ": " +
            error.message());
    }
    std::filesystem::remove(displaced, error);
}

void write(
    const std::filesystem::path& destination,
    std::string_view contents)
{
    const std::filesystem::path temporary = destination.string() + ".tmp";
    std::error_code cleanupError;
    std::filesystem::remove(temporary, cleanupError);

    try {
        std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
        if (!stream) {
            throw std::runtime_error("cannot open " + temporary.string());
        }
        stream.write(contents.data(), static_cast<std::streamsize>(contents.size()));
        stream.close();
        if (!stream) {
            throw std::runtime_error("cannot write " + temporary.string());
        }
        replace(destination, temporary);
    } catch (...) {
        std::filesystem::remove(temporary, cleanupError);
        throw;
    }
}

} // namespace sokoban::atomicFile
