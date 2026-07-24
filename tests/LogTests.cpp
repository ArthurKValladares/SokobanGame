#include "engine/Log.hpp"
#include "engine/LogQueue.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <thread>
#include <vector>

namespace {

int failures = 0;
int checks = 0;

void checkImpl(bool condition, const char* expression, int line)
{
    ++checks;
    if (!condition) {
        ++failures;
        std::cerr << "FAIL line " << line << ": "
                  << expression << '\n';
    }
}

#define CHECK(expression) checkImpl((expression), #expression, __LINE__)

struct TemporaryDirectory {
    TemporaryDirectory()
    {
        path = std::filesystem::temp_directory_path() /
            ("sokoban-log-tests-" + std::to_string(
                std::chrono::steady_clock::now()
                    .time_since_epoch().count()));
        std::filesystem::create_directories(path);
    }

    ~TemporaryDirectory()
    {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }

    std::filesystem::path path;
};

std::string readFile(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    return {
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>(),
    };
}

bool waitForText(
    const std::filesystem::path& path,
    std::string_view expected)
{
    const auto deadline =
        std::chrono::steady_clock::now() +
        std::chrono::seconds(2);
    while (std::chrono::steady_clock::now() < deadline) {
        if (readFile(path).find(expected) != std::string::npos) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return false;
}

void testBoundedQueuePreservesErrors()
{
    using sokoban::log::Category;
    using sokoban::log::Level;
    using sokoban::log::detail::BoundedQueue;
    using sokoban::log::detail::Record;

    BoundedQueue queue(2);
    CHECK(queue.push(Record {
        .level = Level::Info,
        .category = Category::Audio,
        .message = "one",
    }).accepted);
    CHECK(queue.push(Record {
        .level = Level::Warning,
        .category = Category::Assets,
        .message = "two",
    }).accepted);

    const auto dropped = queue.push(Record {
        .level = Level::Info,
        .category = Category::Gameplay,
        .message = "three",
    });
    CHECK(!dropped.accepted);
    CHECK(dropped.droppedCategory == Category::Gameplay);

    const auto replaced = queue.push(Record {
        .level = Level::Error,
        .category = Category::Persistence,
        .message = "critical",
    });
    CHECK(replaced.accepted);
    CHECK(replaced.droppedCategory == Category::Audio);

    const std::vector<Record> records = queue.takeAll();
    CHECK(records.size() == 2);
    CHECK(std::ranges::any_of(records, [](const Record& record) {
        return record.level == Level::Error &&
            record.category == Category::Persistence;
    }));
}

void testFilteringCategoriesAndExplicitFlush()
{
    TemporaryDirectory directory;
    const std::filesystem::path path = directory.path / "log.txt";

    sokoban::log::reset();
    sokoban::log::configure({
        .queueCapacity = 64,
        .flushInterval = std::chrono::seconds(30),
        .stderrEnabled = false,
    });
    sokoban::log::setMinimumLevel(sokoban::log::Level::Warning);
    sokoban::log::addFileSink(path);
    sokoban::log::info(sokoban::log::Category::Application)
        << "filtered";
    sokoban::log::warning(sokoban::log::Category::Audio)
        << "device unavailable";
    sokoban::log::error(sokoban::log::Category::Persistence)
        << "save failed";
    sokoban::log::flush();

    const std::string contents = readFile(path);
    CHECK(contents.find("filtered") == std::string::npos);
    CHECK(contents.find("[WARN] [AUDIO] device unavailable") !=
        std::string::npos);
    CHECK(contents.find("[ERROR] [SAVE] save failed") !=
        std::string::npos);

    const sokoban::log::Diagnostics diagnostics =
        sokoban::log::diagnostics();
    CHECK(diagnostics.filteredMessages == 1);
    CHECK(diagnostics.enqueuedMessages == 2);
    CHECK(diagnostics.writtenMessages == 2);
    CHECK(diagnostics.queuedMessages == 0);
    CHECK(diagnostics.fileSinkOpen);
}

void testPeriodicAndErrorFlushWithoutCallerFlush()
{
    TemporaryDirectory directory;
    const std::filesystem::path periodic =
        directory.path / "periodic.txt";

    sokoban::log::reset();
    sokoban::log::configure({
        .queueCapacity = 64,
        .flushInterval = std::chrono::milliseconds(40),
        .stderrEnabled = false,
    });
    sokoban::log::addFileSink(periodic);
    sokoban::log::info(sokoban::log::Category::Tasks)
        << "periodic marker";
    CHECK(waitForText(periodic, "periodic marker"));

    const std::filesystem::path errors =
        directory.path / "errors.txt";
    sokoban::log::configure({
        .queueCapacity = 64,
        .flushInterval = std::chrono::seconds(30),
        .stderrEnabled = false,
    });
    sokoban::log::addFileSink(errors);
    sokoban::log::error(sokoban::log::Category::Assets)
        << "error marker";
    CHECK(waitForText(errors, "error marker"));
}

void testConcurrentProducersAndDropDiagnostics()
{
    sokoban::log::reset();
    sokoban::log::configure({
        .queueCapacity = 1,
        .flushInterval = std::chrono::seconds(30),
        .stderrEnabled = false,
    });
    sokoban::log::setMinimumLevel(sokoban::log::Level::Debug);

    constexpr int producerCount = 6;
    constexpr int messagesPerProducer = 4000;
    std::vector<std::thread> producers;
    for (int producer = 0; producer < producerCount; ++producer) {
        producers.emplace_back([producer] {
            for (int message = 0;
                message < messagesPerProducer;
                ++message) {
                sokoban::log::debug(sokoban::log::Category::Tasks)
                    << producer << ':' << message;
            }
        });
    }
    for (std::thread& producer : producers) {
        producer.join();
    }
    sokoban::log::flush();

    const sokoban::log::Diagnostics diagnostics =
        sokoban::log::diagnostics();
    CHECK(diagnostics.enqueuedMessages +
            diagnostics.droppedMessages ==
        static_cast<uint64_t>(
            producerCount * messagesPerProducer));
    CHECK(diagnostics.droppedMessages != 0);
    CHECK(diagnostics.droppedMessageReports != 0);
    CHECK(diagnostics.droppedByCategory[
        static_cast<std::size_t>(
            sokoban::log::Category::Tasks)] ==
        diagnostics.droppedMessages);
    CHECK(diagnostics.writtenMessages ==
        diagnostics.enqueuedMessages);
    CHECK(diagnostics.queuedMessages == 0);
}

void testShutdownDrainsWithoutExplicitFlush()
{
    TemporaryDirectory directory;
    const std::filesystem::path path =
        directory.path / "shutdown.txt";

    sokoban::log::reset();
    sokoban::log::configure({
        .queueCapacity = 16,
        .flushInterval = std::chrono::seconds(30),
        .stderrEnabled = false,
    });
    sokoban::log::addFileSink(path);
    sokoban::log::info(sokoban::log::Category::Application)
        << "shutdown marker";
    sokoban::log::shutdown();

    CHECK(readFile(path).find("shutdown marker") !=
        std::string::npos);
    const sokoban::log::Diagnostics diagnostics =
        sokoban::log::diagnostics();
    CHECK(!diagnostics.writerRunning);
    CHECK(!diagnostics.writerActive);
    CHECK(diagnostics.queuedMessages == 0);
    CHECK(diagnostics.writtenMessages == 1);
}

} // namespace

int main()
{
    testBoundedQueuePreservesErrors();
    testFilteringCategoriesAndExplicitFlush();
    testPeriodicAndErrorFlushWithoutCallerFlush();
    testConcurrentProducersAndDropDiagnostics();
    testShutdownDrainsWithoutExplicitFlush();
    sokoban::log::reset();

    if (failures == 0) {
        std::cout << "LogTests: " << checks
                  << " checks passed\n";
        return 0;
    }
    std::cerr << "LogTests: " << failures << " of "
              << checks << " checks failed\n";
    return 1;
}
