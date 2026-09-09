#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>

// Creates a collision-safe directory below the platform temporary root and
// removes it on scope exit. A failed cleanup must not hide the assertion that
// caused stack unwinding, so destruction is intentionally best-effort.
class ScopedTestDirectory {
public:
    explicit ScopedTestDirectory(std::string_view prefix = "sokoban-tests")
    {
        static std::atomic_uint64_t nextId { 0 };
        const auto timestamp =
            std::chrono::steady_clock::now().time_since_epoch().count();
        const std::filesystem::path temporaryRoot =
            std::filesystem::temp_directory_path();

        for (int attempt = 0; attempt < 100; ++attempt) {
            path_ = temporaryRoot /
                (std::string(prefix) + '-' + std::to_string(timestamp) + '-' +
                    std::to_string(nextId.fetch_add(1)));
            std::error_code error;
            if (std::filesystem::create_directory(path_, error)) {
                return;
            }
            if (error) {
                throw std::runtime_error(
                    "cannot create test directory " + path_.string() + ": " +
                    error.message());
            }
        }
        throw std::runtime_error(
            "could not allocate a unique test directory for " +
            std::string(prefix));
    }

    ~ScopedTestDirectory()
    {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }

    ScopedTestDirectory(const ScopedTestDirectory&) = delete;
    ScopedTestDirectory& operator=(const ScopedTestDirectory&) = delete;
    ScopedTestDirectory(ScopedTestDirectory&&) = delete;
    ScopedTestDirectory& operator=(ScopedTestDirectory&&) = delete;

    [[nodiscard]] const std::filesystem::path& path() const noexcept
    {
        return path_;
    }

private:
    std::filesystem::path path_;
};
