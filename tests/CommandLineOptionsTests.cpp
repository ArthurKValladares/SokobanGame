#include "engine/CommandLineOptions.hpp"

#include <array>
#include <iostream>
#include <string_view>
#include <vector>

namespace {

int failures = 0;

void check(bool condition, const char* label)
{
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << label << '\n';
    }
}

sokoban::CommandLineOptions parse(
    std::initializer_list<std::string_view> arguments)
{
    const std::vector<std::string_view> values(arguments);
    return sokoban::parseCommandLine(values);
}

void testEmptyIsANormalRun()
{
    const sokoban::CommandLineOptions options = parse({});
    check(!options.malformed, "no arguments parses");
    check(!options.smokeRun(), "no arguments is not a smoke run");
    check(options.smokeFrames == 0, "no frame count without the flag");
    check(!options.requireValidation, "validation not required by default");
    check(!options.bakeTileThumbnails, "no bake by default");
    check(options.saveDirectory.empty(), "no save override by default");
    check(options.evidenceOutputDirectory.empty(),
        "no evidence output by default");
    check(options.evidenceRenderScalePercent == 100,
        "evidence scale defaults to 100 percent");
    check(options.evidenceAmbientOcclusionEnabled,
        "ambient occlusion is enabled in evidence runs by default");
    check(options.evidenceFrustumCullingEnabled,
        "frustum culling is enabled in evidence runs by default");
    check(options.parallelScenePreparationEnabled,
        "scene preparation is parallel by default");
    check(!options.evidencePointLightEnabled,
        "evidence point light is disabled by default");
    check(options.pointShadowOptimizationsEnabled,
        "point-shadow optimizations are enabled by default");
    check(options.textureResidencyBudgetKiB == 0,
        "texture residency uses its production default");
}

void testFlags()
{
    const sokoban::CommandLineOptions bake = parse({ "--bake-tile-thumbnails" });
    check(!bake.malformed && bake.bakeTileThumbnails, "bake flag parses");

    const sokoban::CommandLineOptions smoke = parse(
        { "--smoke-frames", "240", "--require-validation",
            "--save-directory", "/tmp/profile" });
    check(!smoke.malformed, "full smoke invocation parses");
    check(smoke.smokeFrames == 240, "frame count is read");
    check(smoke.smokeRun(), "a non-zero count is a smoke run");
    check(smoke.requireValidation, "validation requirement is read");
    check(smoke.saveDirectory == "/tmp/profile", "save directory is read");

    const sokoban::CommandLineOptions residency = parse(
        { "--smoke-frames", "120", "--texture-residency-kib", "64" });
    check(!residency.malformed, "residency stress invocation parses");
    check(residency.textureResidencyBudgetKiB == 64,
        "texture residency override is read");

    const sokoban::CommandLineOptions serialPreparation = parse(
        { "--smoke-frames", "120", "--serial-scene-preparation" });
    check(!serialPreparation.malformed,
        "serial scene preparation invocation parses");
    check(!serialPreparation.parallelScenePreparationEnabled,
        "serial scene preparation override is read");

    const sokoban::CommandLineOptions pointShadowEvidence = parse(
        { "--smoke-frames", "120", "--evidence-output", "/tmp/evidence",
            "--evidence-point-light",
            "--disable-point-shadow-optimizations" });
    check(!pointShadowEvidence.malformed,
        "point-shadow evidence invocation parses");
    check(pointShadowEvidence.evidencePointLightEnabled,
        "point-shadow evidence light is enabled");
    check(!pointShadowEvidence.pointShadowOptimizationsEnabled,
        "point-shadow legacy control is read");

    const sokoban::CommandLineOptions evidence = parse(
        { "--smoke-frames", "180", "--evidence-output", "/tmp/evidence",
            "--evidence-render-scale", "50" });
    check(!evidence.malformed, "evidence invocation parses");
    check(evidence.evidenceOutputDirectory == "/tmp/evidence",
        "evidence directory is read");
    check(evidence.evidenceRenderScalePercent == 50,
        "evidence scale is read");

    const sokoban::CommandLineOptions noAoEvidence = parse(
        { "--smoke-frames", "180", "--evidence-output", "/tmp/evidence",
            "--evidence-disable-ao" });
    check(!noAoEvidence.malformed, "AO-off evidence invocation parses");
    check(!noAoEvidence.evidenceAmbientOcclusionEnabled,
        "AO-off evidence invocation disables ambient occlusion");

    const sokoban::CommandLineOptions noCullingEvidence = parse(
        { "--smoke-frames", "180", "--evidence-output", "/tmp/evidence",
            "--evidence-disable-frustum-culling" });
    check(!noCullingEvidence.malformed,
        "culling-off evidence invocation parses");
    check(!noCullingEvidence.evidenceFrustumCullingEnabled,
        "culling-off evidence invocation disables frustum culling");

    // Order must not matter: CI writes these in whatever order reads best.
    const sokoban::CommandLineOptions reordered = parse(
        { "--save-directory", "/tmp/profile", "--require-validation",
            "--smoke-frames", "240" });
    check(!reordered.malformed, "argument order does not matter");
    check(reordered.smokeFrames == 240, "reordered frame count is read");
    check(reordered.saveDirectory == "/tmp/profile",
        "reordered save directory is read");

    // A path that looks like a flag is still a path. Rejecting it would make
    // any directory starting with two dashes unusable.
    const sokoban::CommandLineOptions oddPath =
        parse({ "--save-directory", "--strange" });
    check(!oddPath.malformed, "a flag-shaped path is accepted as a value");
    check(oddPath.saveDirectory == "--strange", "flag-shaped path is read");
}

void testMalformedInput()
{
    // Each of these once produced a run that silently did nothing useful,
    // which is the failure mode a validation gate can least afford.
    check(parse({ "--smoke-frames" }).malformed,
        "missing frame count is rejected");
    check(parse({ "--save-directory" }).malformed,
        "missing save directory is rejected");
    check(parse({ "--evidence-output" }).malformed,
        "missing evidence directory is rejected");
    check(parse({ "--evidence-output", "" }).malformed,
        "empty evidence directory is rejected");
    check(parse({ "--evidence-render-scale" }).malformed,
        "missing evidence scale is rejected");
    check(parse({ "--texture-residency-kib" }).malformed,
        "missing residency budget is rejected");
    check(parse({ "--texture-residency-kib", "0" }).malformed,
        "zero residency budget is rejected");
    check(parse({ "--texture-residency-kib", "12x" }).malformed,
        "malformed residency budget is rejected");
    check(parse({ "--smoke-frames", "0" }).malformed,
        "zero frames is rejected");
    check(parse({ "--smoke-frames", "-1" }).malformed,
        "a negative frame count is rejected");
    check(parse({ "--smoke-frames", "abc" }).malformed,
        "a non-numeric frame count is rejected");
    check(parse({ "--smoke-frames", "12x" }).malformed,
        "trailing garbage after the count is rejected");
    check(parse({ "--smoke-frames", "" }).malformed,
        "an empty frame count is rejected");
    check(parse({ "--unknown-flag" }).malformed,
        "an unknown flag is rejected rather than ignored");
    check(parse({ "--smoke-frames", "2", "--evidence-output", "/tmp/e" })
            .malformed,
        "evidence capture requires two distinct capture frames");
    check(parse({ "--smoke-frames", "3", "--evidence-output", "/tmp/e",
                    "--evidence-render-scale", "24" })
            .malformed,
        "evidence scale below the renderer range is rejected");
    check(parse({ "--smoke-frames", "3", "--evidence-output", "/tmp/e",
                    "--evidence-render-scale", "101" })
            .malformed,
        "evidence scale above the renderer range is rejected");
    check(parse({ "--evidence-render-scale", "50" }).malformed,
        "evidence scale without an output directory is rejected");
    check(parse({ "--evidence-render-scale", "100" }).malformed,
        "explicit default evidence scale still requires an output directory");
    check(parse({ "--evidence-disable-ao" }).malformed,
        "AO-off evidence mode requires an output directory");
    check(parse({ "--evidence-disable-frustum-culling" }).malformed,
        "culling-off evidence mode requires an output directory");
    check(parse({ "--evidence-point-light" }).malformed,
        "point-light evidence mode requires an output directory");

    const sokoban::CommandLineOptions rejected = parse({ "--smoke-frames", "abc" });
    check(!rejected.error.empty(), "a rejection explains itself");
    check(rejected.smokeFrames == 0, "a rejected run has no frame count");
}

void testLargeCountFits()
{
    const sokoban::CommandLineOptions options =
        parse({ "--smoke-frames", "4294967296" });
    check(!options.malformed, "a count past 32 bits parses");
    check(options.smokeFrames == 4294967296ULL, "the count is not truncated");
}

} // namespace

int main()
{
    testEmptyIsANormalRun();
    testFlags();
    testMalformedInput();
    testLargeCountFits();
    if (failures != 0) {
        std::cerr << "CommandLineOptionsTests: " << failures
                  << " check(s) failed\n";
        return 1;
    }
    std::cout << "CommandLineOptionsTests passed\n";
    return 0;
}
