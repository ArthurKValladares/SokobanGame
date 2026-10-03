#include "engine/FrameArena.hpp"
#include "engine/PerformanceAnalysis.hpp"
#include "engine/PerformanceFixtures.hpp"
#include "engine/LevelEditor.hpp"
#include "engine/AssetManifest.hpp"
#include "engine/ProcessMemory.hpp"
#include "engine/Profiler.hpp"
#include "engine/TaskSystem.hpp"
#include "engine/render/FrameTimeTelemetry.hpp"
#include "engine/render/IsoScenePreparer.hpp"
#include "engine/render/OpaqueDrawSorter.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <future>
#include <iomanip>
#include <iostream>
#include <memory>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

struct Settings {
    bool full = true;
    std::size_t samples = 40;
    std::size_t warmups = 5;
    std::filesystem::path outputDirectory = "performance-results";
    std::string filter;
};

struct BenchmarkResult {
    std::string group;
    std::string name;
    std::string description;
    std::size_t operationsPerInvocation = 1;
    sokoban::FrameTimeSummary timing;
    uint64_t checksum = 0;

    [[nodiscard]] double medianNanosecondsPerOperation() const
    {
        return timing.medianMilliseconds * 1'000'000.0 /
            static_cast<double>(operationsPerInvocation);
    }
};

std::atomic<uint64_t> benchmarkSink { 0 };

std::string jsonEscape(std::string_view text)
{
    std::string escaped;
    escaped.reserve(text.size() + 8);
    for (char character : text) {
        switch (character) {
        case '\\':
            escaped += "\\\\";
            break;
        case '"':
            escaped += "\\\"";
            break;
        case '\n':
            escaped += "\\n";
            break;
        case '\r':
            escaped += "\\r";
            break;
        case '\t':
            escaped += "\\t";
            break;
        default:
            escaped += character;
            break;
        }
    }
    return escaped;
}

Settings parseSettings(int argc, char** argv)
{
    Settings settings;
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument = argv[index];
        if (argument == "--quick") {
            settings.full = false;
            settings.samples = 12;
            settings.warmups = 2;
        } else if (argument == "--full") {
            settings.full = true;
            settings.samples = 40;
            settings.warmups = 5;
        } else if (argument == "--output" && index + 1 < argc) {
            settings.outputDirectory = argv[++index];
        } else if (argument == "--filter" && index + 1 < argc) {
            settings.filter = argv[++index];
        } else if (argument == "--help") {
            std::cout
                << "Usage: sokoban_performance_tests [--quick|--full] "
                   "[--filter text] [--output directory]\n";
            std::exit(0);
        } else {
            throw std::invalid_argument(
                "Unknown or incomplete argument: " + std::string(argument));
        }
    }
    return settings;
}

class Suite {
public:
    explicit Suite(Settings settings)
        : settings_(std::move(settings))
    {
        sokoban::CpuProfiler& profiler = sokoban::CpuProfiler::instance();
        profiler.clearCapture();
        profiler.setPaused(false);
        profiler.setEnabled(true);
        profiler.setCurrentThreadName("Performance suite");
    }

    template <typename Function>
    void run(
        std::string group,
        std::string name,
        std::string description,
        std::size_t operationsPerInvocation,
        std::size_t repetitionsPerSample,
        Function&& function)
    {
        if (!settings_.filter.empty() &&
            group.find(settings_.filter) == std::string::npos &&
            name.find(settings_.filter) == std::string::npos) {
            return;
        }
        for (std::size_t warmup = 0; warmup < settings_.warmups; ++warmup) {
            benchmarkSink.fetch_add(function(), std::memory_order_relaxed);
        }

        sokoban::FrameTimeTelemetry telemetry;
        uint64_t checksum = 0;
        for (std::size_t sample = 0; sample < settings_.samples; ++sample) {
            sokoban::CpuProfiler& profiler = sokoban::CpuProfiler::instance();
            profiler.beginFrame(nextProfileFrame_++);
            const auto start = Clock::now();
            {
                sokoban::CpuProfileScope scope(name.c_str());
                for (std::size_t repetition = 0;
                     repetition < repetitionsPerSample;
                     ++repetition) {
                    checksum = checksum * 0x9E3779B185EBCA87ULL +
                        function() + repetition + 1;
                }
            }
            const auto end = Clock::now();
            profiler.endFrame();
            const double milliseconds =
                std::chrono::duration<double, std::milli>(end - start).count() /
                static_cast<double>(repetitionsPerSample);
            telemetry.record(milliseconds);
        }
        benchmarkSink.fetch_add(checksum, std::memory_order_relaxed);
        results_.push_back({
            .group = std::move(group),
            .name = std::move(name),
            .description = std::move(description),
            .operationsPerInvocation = operationsPerInvocation,
            .timing = telemetry.summary(),
            .checksum = checksum,
        });
        const BenchmarkResult& result = results_.back();
        std::cout << std::left << std::setw(38) << result.name << std::right
                  << " median " << std::setw(9) << std::fixed
                  << std::setprecision(3) << result.timing.medianMilliseconds
                  << " ms  p95 " << std::setw(9)
                  << result.timing.p95Milliseconds << " ms  "
                  << std::setprecision(1)
                  << result.medianNanosecondsPerOperation() << " ns/op\n";
    }

    [[nodiscard]] const Settings& settings() const { return settings_; }
    [[nodiscard]] const std::vector<BenchmarkResult>& results() const
    {
        return results_;
    }

private:
    Settings settings_;
    std::vector<BenchmarkResult> results_;
    uint64_t nextProfileFrame_ = 1;
};

const BenchmarkResult* findResult(
    const std::vector<BenchmarkResult>& results,
    std::string_view name)
{
    const auto found = std::ranges::find(results, name, &BenchmarkResult::name);
    return found == results.end() ? nullptr : &*found;
}

sokoban::OpaqueDrawSortKey drawKey(std::size_t index)
{
    sokoban::OpaqueDrawSortKey key {
        .pipeline = static_cast<uint32_t>((index / 1024) % 3),
        .material = static_cast<uint32_t>((index / 64) % 16),
        .mesh = static_cast<uint32_t>((index / 8) % 64),
    };
    key.fragmentState[0] = static_cast<uint32_t>((index / 512) % 4);
    return key;
}

std::vector<sokoban::OpaqueDrawSortItem> makeDraws(std::size_t count)
{
    std::vector<sokoban::OpaqueDrawSortItem> draws;
    draws.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        draws.push_back({
            .key = drawKey(index),
            .drawIndex = index,
            .instancable = index % 19 != 0,
        });
    }
    std::mt19937 random(0x5A17U);
    std::ranges::shuffle(draws, random);
    return draws;
}

sokoban::RenderFrameData makeScene(uint32_t edge)
{
    sokoban::RenderFrameData frame;
    frame.viewMode = sokoban::RenderViewMode::Isometric3D;
    frame.levelWidth = edge;
    frame.levelHeight = edge;
    frame.levelDepth = 1;
    frame.lighting.shadows.enabled = true;
    frame.lighting.shadows.opacity = 0.8f;
    frame.lighting.pointLightCount = 4;
    for (std::size_t index = 0;
         index < frame.lighting.pointLightCount;
         ++index) {
        frame.lighting.pointLights[index] = {
            .position = {
                static_cast<float>((index + 1) * edge) / 5.0f,
                static_cast<float>((4 - index) * edge) / 5.0f,
                2.5f,
            },
            .color = { 1.0f, 0.8f, 0.6f },
            .intensity = 1.0f,
            .range = static_cast<float>(edge) * 0.28f,
            .castsShadows = true,
        };
    }
    frame.tiles.reserve(static_cast<std::size_t>(edge) * edge);
    for (uint32_t y = 0; y < edge; ++y) {
        for (uint32_t x = 0; x < edge; ++x) {
            frame.tiles.push_back({
                .cell = {
                    static_cast<int>(x), static_cast<int>(y), 0 },
                .position = {
                    static_cast<float>(x), static_cast<float>(y) },
                .color = {
                    0.25f + static_cast<float>((x + y) % 3) * 0.12f,
                    0.48f,
                    0.32f,
                    1.0f,
                },
                .height = 0.75f +
                    static_cast<float>((x * 3 + y * 5) % 4) * 0.15f,
                .renderableId = static_cast<uint64_t>(y) * edge + x + 1,
            });
        }
    }
    return frame;
}

void runInstrumentationBenchmarks(Suite& suite)
{
    suite.run(
        "instrumentation",
        "cpu-profiler-scope-capture",
        "Cost of recording 4096 nested-free CPU scopes inside a captured frame.",
        4096,
        4,
        [] {
            uint64_t checksum = 0;
            for (uint64_t index = 0; index < 4096; ++index) {
                sokoban::CpuProfileScope scope("Benchmark.inner scope");
                checksum += index * 17U;
            }
            return checksum;
        });

    suite.run(
        "instrumentation",
        "process-memory-sampling",
        "Cost of collecting the process resident, peak, and private counters.",
        1,
        32,
        [] {
            const sokoban::ProcessMemoryStatistics memory =
                sokoban::processMemoryStatistics();
            return memory.residentBytes ^ memory.privateBytes;
        });
}

void runTelemetryBenchmarks(Suite& suite)
{
    std::vector<sokoban::FrameTimeTelemetry> streams(27);
    for (std::size_t stream = 0; stream < streams.size(); ++stream) {
        for (std::size_t sample = 0;
             sample < sokoban::FrameTimeTelemetry::historyCapacity;
             ++sample) {
            streams[stream].record(
                8.0 + static_cast<double>((stream + sample) % 19) * 0.17);
        }
    }
    uint64_t frame = 0;
    suite.run(
        "telemetry",
        "frame-telemetry-27-streams",
        "Update and summarize all renderer timing streams used by the profiler panel.",
        streams.size(),
        32,
        [&] {
            double checksum = 0.0;
            for (std::size_t index = 0; index < streams.size(); ++index) {
                streams[index].record(
                    8.0 + static_cast<double>((frame + index) % 23) * 0.11);
                checksum += streams[index].summary().p95Milliseconds;
            }
            ++frame;
            return static_cast<uint64_t>(checksum * 1000.0);
        });
}

void runMemoryBenchmarks(Suite& suite)
{
    constexpr std::size_t allocationCount = 4096;
    constexpr std::size_t blockBytes = 48;
    sokoban::FrameArena arena(
        "performance arena", allocationCount * (blockBytes + 16));
    suite.run(
        "memory",
        "frame-arena-allocate-reset",
        "Allocate 4096 small frame-lifetime blocks and release them with one reset.",
        allocationCount,
        32,
        [&] {
            arena.reset();
            uint64_t checksum = 0;
            for (std::size_t index = 0; index < allocationCount; ++index) {
                void* const allocation = arena.allocate(blockBytes, 16);
                checksum ^= reinterpret_cast<std::uintptr_t>(allocation);
            }
            return checksum ^ arena.bytesUsed();
        });

    suite.run(
        "memory",
        "heap-contiguous-frame-allocation",
        "Allocate and initialize one equivalent contiguous transient buffer from the general heap.",
        allocationCount,
        8,
        [] {
            std::vector<std::byte> bytes(allocationCount * blockBytes);
            for (std::size_t index = 0; index < bytes.size(); index += blockBytes) {
                bytes[index] = static_cast<std::byte>(index & 0xFFU);
            }
            return static_cast<uint64_t>(bytes.size()) ^
                static_cast<uint64_t>(bytes[0]);
        });

    suite.run(
        "memory",
        "heap-small-frame-allocations",
        "Allocate and release 4096 independent small frame-lifetime blocks from the general heap.",
        allocationCount,
        2,
        [] {
            std::vector<std::unique_ptr<std::byte[]>> blocks;
            blocks.reserve(allocationCount);
            uint64_t checksum = 0;
            for (std::size_t index = 0; index < allocationCount; ++index) {
                auto block = std::make_unique<std::byte[]>(blockBytes);
                block[0] = static_cast<std::byte>(index & 0xFFU);
                checksum += static_cast<uint64_t>(block[0]);
                blocks.push_back(std::move(block));
            }
            return checksum ^ blocks.size();
        });
}

void runDrawBenchmarks(Suite& suite)
{
    const std::vector<std::size_t> sizes = suite.settings().full
        ? std::vector<std::size_t> { 512, 4096, 16384 }
        : std::vector<std::size_t> { 512, 4096 };
    for (const std::size_t size : sizes) {
        const std::vector<sokoban::OpaqueDrawSortItem> source = makeDraws(size);
        std::vector<sokoban::OpaqueDrawSortItem> scratch;
        std::vector<sokoban::OpaqueDrawBatch> batches;
        scratch.reserve(size);
        batches.reserve(size);
        suite.run(
            "draw-submission",
            "opaque-sort-batch-" + std::to_string(size),
            "Copy representative draw metadata, sort state keys, and form instance batches.",
            size,
            size >= 16384 ? 1 : 4,
            [&] {
                scratch.assign(source.begin(), source.end());
                sokoban::sortOpaqueDraws(scratch, batches);
                return static_cast<uint64_t>(batches.size()) ^
                    static_cast<uint64_t>(scratch.front().drawIndex);
            });
    }
}

void runTaskBenchmarks(Suite& suite, sokoban::TaskSystem& tasks)
{
    constexpr std::size_t taskCount = 256;
    std::vector<std::future<uint64_t>> futures;
    futures.reserve(taskCount);
    suite.run(
        "tasks",
        "task-enqueue-roundtrip-256",
        "Enqueue small independent tasks and wait for all returned futures.",
        taskCount,
        2,
        [&] {
            futures.clear();
            for (uint64_t index = 0; index < taskCount; ++index) {
                futures.push_back(tasks.enqueue([index] {
                    sokoban::CpuProfileScope scope(
                        "Benchmark.task enqueue worker");
                    return (index * 2654435761ULL) ^ (index >> 3U);
                }));
            }
            uint64_t checksum = 0;
            for (std::future<uint64_t>& future : futures) {
                checksum ^= future.get();
            }
            return checksum;
        });

    suite.run(
        "tasks",
        "task-future-sequential-256",
        "Dispatch and join 256 one-shot tasks through packaged_task/future shared state.",
        taskCount,
        2,
        [&] {
            uint64_t checksum = 0;
            for (uint64_t index = 0; index < taskCount; ++index) {
                std::future<uint64_t> future = tasks.enqueue([index] {
                    return index * 2654435761ULL;
                });
                checksum ^= future.get();
            }
            return checksum;
        });
    suite.run(
        "tasks",
        "task-scoped-sequential-256",
        "Dispatch and join 256 stack-owned one-shot tasks without future shared state.",
        taskCount,
        2,
        [&] {
            uint64_t checksum = 0;
            for (uint64_t index = 0; index < taskCount; ++index) {
                auto task = tasks.scopedTask([&checksum, index] {
                    checksum ^= index * 2654435761ULL;
                });
                task.finish();
            }
            return checksum;
        });

    constexpr std::size_t valueCount = 1U << 18U;
    std::vector<uint64_t> input(valueCount);
    std::vector<uint64_t> output(valueCount);
    for (std::size_t index = 0; index < input.size(); ++index) {
        input[index] = index * 11400714819323198485ULL;
    }
    const auto transform = [&](std::size_t begin, std::size_t end) {
        sokoban::CpuProfileScope scope("Benchmark.compute chunk");
        for (std::size_t index = begin; index < end; ++index) {
            uint64_t value = input[index];
            for (int round = 0; round < 12; ++round) {
                value ^= value >> 29U;
                value *= 0x9E3779B185EBCA87ULL;
                value ^= value << 17U;
            }
            output[index] = value;
        }
    };
    suite.run(
        "tasks",
        "compute-transform-serial",
        "CPU-heavy transform over 262144 values on the calling thread.",
        valueCount,
        2,
        [&] {
            transform(0, valueCount);
            return output[17] ^ output[valueCount - 1];
        });
    suite.run(
        "tasks",
        "compute-transform-parallel",
        "The same transform through TaskSystem::parallelFor.",
        valueCount,
        2,
        [&] {
            tasks.parallelFor(valueCount, 4096, transform);
            return output[17] ^ output[valueCount - 1];
        });
}

void runSceneBenchmarks(Suite& suite, sokoban::TaskSystem& tasks)
{
    const std::vector<uint32_t> edges = suite.settings().full
        ? std::vector<uint32_t> { 16, 32, 64 }
        : std::vector<uint32_t> { 16, 32 };
    for (uint32_t edge : edges) {
        const sokoban::RenderFrameData frame = makeScene(edge);
        const std::size_t tileCount = static_cast<std::size_t>(edge) * edge;
        suite.run(
            "scene-preparation",
            "scene-cold-serial-" + std::to_string(tileCount),
            "Cold scene preparation including cache and output capacity construction.",
            tileCount,
            1,
            [&] {
                sokoban::IsoScenePreparer preparer;
                sokoban::PreparedRenderScene scene;
                preparer.prepare(frame, { 1920.0f, 1080.0f }, scene);
                return static_cast<uint64_t>(scene.isoFaces.size()) ^
                    scene.pointShadowFaceCandidates;
            });

        sokoban::IsoScenePreparer serialPreparer;
        sokoban::PreparedRenderScene serialScene;
        serialPreparer.prepare(
            frame, { 1920.0f, 1080.0f }, serialScene);
        suite.run(
            "scene-preparation",
            "scene-warm-serial-" + std::to_string(tileCount),
            "Warm scene preparation with retained output capacity and bounds metadata.",
            tileCount,
            2,
            [&] {
                serialPreparer.prepare(
                    frame, { 1920.0f, 1080.0f }, serialScene);
                return static_cast<uint64_t>(serialScene.reusedRenderableBounds) ^
                    serialScene.pointShadowFaceCandidates;
            });

        sokoban::IsoScenePreparer parallelPreparer;
        sokoban::PreparedRenderScene parallelScene;
        parallelPreparer.prepare(
            frame, { 1920.0f, 1080.0f }, parallelScene, &tasks);
        suite.run(
            "scene-preparation",
            "scene-warm-parallel-" + std::to_string(tileCount),
            "Warm scene preparation with auxiliary geometry on the task system.",
            tileCount,
            2,
            [&] {
                parallelPreparer.prepare(
                    frame, { 1920.0f, 1080.0f }, parallelScene, &tasks);
                return static_cast<uint64_t>(parallelScene.reusedRenderableBounds) ^
                    parallelScene.pointShadowFaceCandidates;
            });
    }
}

void runEditorBenchmarks(Suite& suite)
{
    const std::filesystem::path root(SOKOBAN_PERFORMANCE_LEVEL_DIR);
    sokoban::LevelEditor editor;
    editor.initialize(root, root, 5, 3);
    if (editor.loadedDocumentPath().empty()) {
        throw std::runtime_error("Editor benchmark requires level 5 screen 3");
    }
    suite.run("editor", "editor-puzzle-overworld-identity",
        "Repeated palette/gameplay classification of a puzzle document with a composed overworld present.",
        128, 2, [&] {
            uint64_t result = 0;
            for (int index = 0; index < 64; ++index) {
                result += editor.editingOverworld() ? 1U : 0U;
                result += editor.overworldScreenId().has_value() ? 1U : 0U;
            }
            return result;
        });
    suite.run("editor", "editor-level-browser-scan",
        "Filesystem enumeration and metadata parsing for the Level Editor browser.",
        1, 1, [&] { return static_cast<uint64_t>(editor.collectLevelDirectories().size()); });
}

void runEffectBenchmarks(Suite& suite, sokoban::TaskSystem& tasks)
{
    const auto manifest = sokoban::AssetManifest::loadFromFile(
        std::filesystem::path(SOKOBAN_PERFORMANCE_ASSET_DIR) / "manifest.json");
    for (const std::string scenario : { "mirror-swap", "witch-swap", "turret-volley",
             "portals", "special-blocks", "mixed-stress" }) {
        for (const uint32_t emitters : { 1U, suite.settings().full ? 32U : 8U }) {
            sokoban::PerformanceEffectFixture fixture(manifest);
            sokoban::RenderFrameData emitted;
            emitted.levelWidth = emitted.levelHeight = 16;
            const std::string suffix = scenario + '-' + std::to_string(emitters);
            suite.run("effects", "effects-emission-" + suffix,
                "Production burst/ribbon/portal/special-block emission, simulation and render-data export at a seeded visible phase.",
                emitters, 2, [&] {
                    emitted.tiles.clear();
                    emitted.particles.clear();
                    fixture.append(emitted, manifest, scenario, emitters);
                    return static_cast<uint64_t>(emitted.particles.size() + emitted.tiles.size());
                });
            auto frame = makeScene(16);
            fixture.append(frame, manifest, scenario, emitters);
            sokoban::IsoScenePreparer preparer;
            sokoban::PreparedRenderScene scene;
            preparer.prepare(frame, { 1920.0f, 1080.0f }, scene, &tasks);
            suite.run("effects", "effects-preparation-" + suffix,
                "Warm particle billboard/ribbon projection, ordering, and special-surface scene preparation.",
                frame.particles.size() + frame.tiles.size(), 2, [&] {
                    preparer.prepare(frame, { 1920.0f, 1080.0f }, scene, &tasks);
                    return static_cast<uint64_t>(scene.particles.size() + scene.isoFaces.size());
                });
        }
    }
}

std::vector<sokoban::PerformanceFinding> analyzeBenchmarks(
    const std::vector<BenchmarkResult>& results)
{
    std::vector<sokoban::PerformanceFinding> findings;
    const auto add = [&](std::string id,
                         std::string title,
                         std::string evidence,
                         std::string recommendation,
                         double score) {
        findings.push_back({
            .id = std::move(id),
            .title = std::move(title),
            .evidence = std::move(evidence),
            .recommendation = std::move(recommendation),
            .priority = score >= 90.0
                ? sokoban::PerformancePriority::High
                : score >= 55.0
                    ? sokoban::PerformancePriority::Medium
                    : sokoban::PerformancePriority::Low,
            .score = score,
        });
    };
    const auto ratioFinding = [&](std::string_view slowerName,
                                  std::string_view fasterName,
                                  double threshold,
                                  std::string id,
                                  std::string title,
                                  std::string recommendation) {
        const BenchmarkResult* slower = findResult(results, slowerName);
        const BenchmarkResult* faster = findResult(results, fasterName);
        if (!slower || !faster || faster->timing.medianMilliseconds <= 0.0) {
            return;
        }
        const double ratio = slower->timing.medianMilliseconds /
            faster->timing.medianMilliseconds;
        if (ratio < threshold) {
            return;
        }
        std::ostringstream evidence;
        evidence << slowerName << " is " << std::fixed << std::setprecision(2)
                 << ratio << "x the median time of " << fasterName << ".";
        add(
            std::move(id), std::move(title), evidence.str(),
            std::move(recommendation),
            50.0 + std::min(70.0, (ratio - 1.0) * 45.0));
    };

    ratioFinding(
        "compute-transform-serial",
        "compute-transform-parallel",
        1.15,
        "parallel-compute-opportunity",
        "The task system accelerates sufficiently large independent CPU work",
        "Apply parallelFor only to similarly sized, independent workloads and preserve a serial path below the measured crossover.");
    ratioFinding(
        "compute-transform-parallel",
        "compute-transform-serial",
        1.10,
        "parallel-scheduling-overhead",
        "Task scheduling outweighs parallel compute gains",
        "Increase chunk size, reduce helper count, or keep this workload serial on the measured machine.");
    ratioFinding(
        "task-future-sequential-256",
        "task-scoped-sequential-256",
        1.10,
        "scoped-task-dispatch",
        "Future shared state is expensive for join-before-return work",
        "Use TaskSystem::scopedTask when work is guaranteed to join in the creating scope; retain futures for results that escape that scope.");

    const BenchmarkResult* smallDraw =
        findResult(results, "opaque-sort-batch-512");
    const BenchmarkResult* largeDraw =
        findResult(results, "opaque-sort-batch-4096");
    if (smallDraw && largeDraw &&
        smallDraw->medianNanosecondsPerOperation() > 0.0) {
        const double ratio = largeDraw->medianNanosecondsPerOperation() /
            smallDraw->medianNanosecondsPerOperation();
        // n log n naturally raises the per-item cost. Only promote it to an
        // optimization candidate when the representative large case is also
        // material to a frame; the table still reports cheaper scaling.
        if (ratio >= 1.5 &&
            largeDraw->timing.medianMilliseconds >= 1.0) {
            std::ostringstream evidence;
            evidence << "Per-draw sort/batch cost grows " << std::fixed
                     << std::setprecision(2) << ratio
                     << "x between 512 and 4096 draws.";
            add(
                "draw-sort-scaling",
                "Opaque draw sorting cost rises at high submission counts",
                evidence.str(),
                "Reduce source draw count first; then compare the current comparison sort with a packed-key radix or bucket strategy on representative keys.",
                58.0 + std::min(40.0, (ratio - 1.0) * 25.0));
        }
    }

    for (const std::size_t tileCount : { 256U, 1024U, 4096U }) {
        const std::string suffix = std::to_string(tileCount);
        ratioFinding(
            "scene-cold-serial-" + suffix,
            "scene-warm-serial-" + suffix,
            1.20,
            "scene-cache-value-" + suffix,
            "Retained scene caches materially reduce preparation cost",
            "Protect stable renderable IDs, bounds revisions, and vector capacity so production frames stay on the warm path.");
        ratioFinding(
            "scene-warm-parallel-" + suffix,
            "scene-warm-serial-" + suffix,
            1.10,
            "scene-parallel-overhead-" + suffix,
            "Parallel scene preparation is slower at this scene size",
            "Raise the scene-size crossover for auxiliary task dispatch or combine its work into a coarser job.");
        ratioFinding(
            "scene-warm-serial-" + suffix,
            "scene-warm-parallel-" + suffix,
            1.15,
            "scene-parallel-benefit-" + suffix,
            "Auxiliary scene preparation benefits from parallel execution",
            "Keep this workload on the task system and profile whether another independent scene-preparation phase can overlap at this scene size.");
    }

    const BenchmarkResult* smallScene =
        findResult(results, "scene-warm-serial-256");
    const BenchmarkResult* largeScene =
        findResult(results, "scene-warm-serial-4096");
    if (smallScene && largeScene) {
        const double smallPerTile = smallScene->medianNanosecondsPerOperation();
        const double largePerTile = largeScene->medianNanosecondsPerOperation();
        if (smallPerTile > 0.0 && largePerTile > smallPerTile * 1.5) {
            const double ratio = largePerTile / smallPerTile;
            std::ostringstream evidence;
            evidence << "Per-tile warm preparation cost grows " << std::fixed
                     << std::setprecision(2) << ratio
                     << "x between 256 and 4096 tiles.";
            add(
                "scene-scaling",
                "Scene preparation scales worse than linearly",
                evidence.str(),
                "Profile sorting, cache reconciliation, point-shadow candidate construction, and repeated reserve/copy work at the largest scene.",
                62.0 + std::min(40.0, (ratio - 1.0) * 30.0));
        }
    }

    std::ranges::sort(
        findings,
        [](const auto& left, const auto& right) {
            if (left.score != right.score) {
                return left.score > right.score;
            }
            return left.id < right.id;
        });
    return findings;
}

void writeReports(
    const Suite& suite,
    unsigned workers,
    const sokoban::ProcessMemoryStatistics& initialMemory,
    const sokoban::ProcessMemoryStatistics& finalMemory)
{
    std::error_code error;
    std::filesystem::create_directories(
        suite.settings().outputDirectory, error);
    if (error) {
        throw std::runtime_error(
            "Could not create performance report directory: " +
            error.message());
    }
    const std::vector<sokoban::PerformanceFinding> findings =
        analyzeBenchmarks(suite.results());

    const std::filesystem::path markdownPath =
        suite.settings().outputDirectory / "performance-report.md";
    std::ofstream markdown(markdownPath, std::ios::trunc);
    if (!markdown) {
        throw std::runtime_error(
            "Could not write " + markdownPath.string());
    }
    markdown << "# Sokoban performance suite\n\n"
             << std::fixed << std::setprecision(2)
             << "- Mode: " << (suite.settings().full ? "full" : "quick")
             << "\n- Samples per case: " << suite.settings().samples
             << "\n- Task workers: " << workers
             << "\n- Hardware concurrency: " << std::thread::hardware_concurrency()
             << "\n- CPU trace: `cpu-trace.json`\n";
    if (initialMemory.available && finalMemory.available) {
        markdown << "- Initial resident memory: "
                 << initialMemory.residentBytes / (1024.0 * 1024.0)
                 << " MiB\n- Final resident memory: "
                 << finalMemory.residentBytes / (1024.0 * 1024.0)
                 << " MiB\n";
    }
    markdown << "\nHardware timings are diagnostic baselines, not correctness "
                "thresholds. Compare runs on the same machine and power state.\n\n"
             << "## Results\n\n"
             << "| Group | Case | Median ms | P95 ms | P99 ms | ns/op |\n"
             << "|---|---|---:|---:|---:|---:|\n";
    for (const BenchmarkResult& result : suite.results()) {
        markdown << "| " << result.group << " | " << result.name << " | "
                 << std::fixed << std::setprecision(3)
                 << result.timing.medianMilliseconds << " | "
                 << result.timing.p95Milliseconds << " | "
                 << result.timing.p99Milliseconds << " | "
                 << std::setprecision(1)
                 << result.medianNanosecondsPerOperation() << " |\n";
    }
    sokoban::PerformanceAnalysis analysis;
    analysis.findings = findings;
    sokoban::writePerformanceAnalysisMarkdown(markdown, analysis);
    markdown << "\n## Method\n\nEach case is warmed up, sampled into the "
                "engine's bounded `FrameTimeTelemetry`, and wrapped in a "
                "`CpuProfileScope`. The accompanying trace can be opened in "
                "Perfetto or Chrome tracing to inspect hot paths and worker "
                "overlap. Checksums keep benchmark work observable.\n";

    const std::filesystem::path jsonPath =
        suite.settings().outputDirectory / "performance-results.json";
    std::ofstream json(jsonPath, std::ios::trunc);
    if (!json) {
        throw std::runtime_error("Could not write " + jsonPath.string());
    }
    json << "{\n  \"mode\": \""
         << (suite.settings().full ? "full" : "quick")
         << "\",\n  \"samples\": " << suite.settings().samples
         << ",\n  \"workerCount\": " << workers
         << ",\n  \"benchmarks\": [\n";
    for (std::size_t index = 0; index < suite.results().size(); ++index) {
        const BenchmarkResult& result = suite.results()[index];
        json << "    {\"group\": \"" << jsonEscape(result.group)
             << "\", \"name\": \"" << jsonEscape(result.name)
             << "\", \"medianMs\": " << std::setprecision(9)
             << result.timing.medianMilliseconds
             << ", \"p95Ms\": " << result.timing.p95Milliseconds
             << ", \"p99Ms\": " << result.timing.p99Milliseconds
             << ", \"standardDeviationMs\": "
             << result.timing.standardDeviationMilliseconds
             << ", \"operationsPerInvocation\": "
             << result.operationsPerInvocation << ", \"checksum\": "
             << result.checksum << '}'
             << (index + 1 == suite.results().size() ? "\n" : ",\n");
    }
    json << "  ],\n  \"findings\": [\n";
    for (std::size_t index = 0; index < findings.size(); ++index) {
        const sokoban::PerformanceFinding& finding = findings[index];
        json << "    {\"id\": \"" << jsonEscape(finding.id)
             << "\", \"priority\": \""
             << sokoban::performancePriorityName(finding.priority)
             << "\", \"score\": " << finding.score
             << ", \"title\": \"" << jsonEscape(finding.title)
             << "\", \"evidence\": \"" << jsonEscape(finding.evidence)
             << "\", \"recommendation\": \""
             << jsonEscape(finding.recommendation) << "\"}"
             << (index + 1 == findings.size() ? "\n" : ",\n");
    }
    json << "  ]\n}\n";

    std::string traceError;
    const std::filesystem::path tracePath =
        suite.settings().outputDirectory / "cpu-trace.json";
    if (!sokoban::CpuProfiler::instance().exportChromeTrace(
            tracePath, &traceError)) {
        throw std::runtime_error(traceError);
    }
    std::cout << "\nReports: " << markdownPath.string() << "\n"
              << "Trace:   " << tracePath.string() << "\n";
}

} // namespace

int main(int argc, char** argv)
{
    try {
        const Settings settings = parseSettings(argc, argv);
        const sokoban::ProcessMemoryStatistics initialMemory =
            sokoban::processMemoryStatistics();
        const unsigned hardware = std::max(1U, std::thread::hardware_concurrency());
        const unsigned workerCount = std::min(4U, hardware > 1 ? hardware - 1 : 1U);
        sokoban::TaskSystem tasks(workerCount);
        Suite suite(settings);

        std::cout << "Sokoban performance suite ("
                  << (settings.full ? "full" : "quick") << ", "
                  << settings.samples << " samples, " << workerCount
                  << " task workers)\n\n";
        runInstrumentationBenchmarks(suite);
        runTelemetryBenchmarks(suite);
        runMemoryBenchmarks(suite);
        runDrawBenchmarks(suite);
        runTaskBenchmarks(suite, tasks);
        runSceneBenchmarks(suite, tasks);
        runEditorBenchmarks(suite);
        runEffectBenchmarks(suite, tasks);
        const sokoban::ProcessMemoryStatistics finalMemory =
            sokoban::processMemoryStatistics();
        writeReports(
            suite, tasks.workerCount(), initialMemory, finalMemory);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Performance suite failed: " << error.what() << '\n';
        return 1;
    }
}
