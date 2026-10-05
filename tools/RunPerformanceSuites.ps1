[CmdletBinding()]
param(
    [string]$BuildDirectory = "out/visual-studio",
    [ValidateSet("Debug", "RelWithDebInfo", "Release")]
    [string]$Configuration = "Release",
    [string]$OutputDirectory = "",
    [switch]$Quick,
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$buildRoot = if ([IO.Path]::IsPathRooted($BuildDirectory)) {
    [IO.Path]::GetFullPath($BuildDirectory)
} else {
    [IO.Path]::GetFullPath((Join-Path $projectRoot $BuildDirectory))
}
if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    $OutputDirectory = Join-Path $buildRoot "performance-results/$Configuration/comprehensive"
}
$outputRoot = if ([IO.Path]::IsPathRooted($OutputDirectory)) {
    [IO.Path]::GetFullPath($OutputDirectory)
} else {
    [IO.Path]::GetFullPath((Join-Path $projectRoot $OutputDirectory))
}

function Resolve-Executable([string]$Name) {
    $multiConfig = Join-Path $buildRoot "$Configuration/$Name.exe"
    if (Test-Path -LiteralPath $multiConfig) {
        return $multiConfig
    }
    $singleConfig = Join-Path $buildRoot "$Name.exe"
    if (Test-Path -LiteralPath $singleConfig) {
        return $singleConfig
    }
    throw "Could not find $Name under $buildRoot. Build the requested configuration first."
}

function Invoke-Checked(
    [string]$Executable,
    [string[]]$Arguments,
    [string]$LogPath = ""
) {
    Write-Host "> $Executable $($Arguments -join ' ')"
    if ([string]::IsNullOrWhiteSpace($LogPath)) {
        & $Executable @Arguments
    } else {
        & $Executable @Arguments 2>&1 | Tee-Object -FilePath $LogPath
    }
    if ($LASTEXITCODE -ne 0) {
        throw "$Executable exited with code $LASTEXITCODE"
    }
}

function Write-HardwareSnapshot([string]$Path) {
    # Optional, read-only metadata. Hardware throttling can dwarf a source
    # change during a sustained GPU matrix; preserve the driver's own reasons.
    if (Get-Command nvidia-smi -ErrorAction SilentlyContinue) {
        $benchmarkExitCode = $LASTEXITCODE
        try {
            & nvidia-smi -q -d PERFORMANCE,TEMPERATURE,POWER,CLOCK 2>&1 |
                Set-Content -LiteralPath $Path -Encoding utf8
        } finally {
            # Optional diagnostics must not change a benchmark's exit status.
            $global:LASTEXITCODE = $benchmarkExitCode
        }
    }
}

if (-not $SkipBuild) {
    $targets = @("sokoban_performance_tests", "sokoban")
    $cmakeCache = Join-Path $buildRoot "CMakeCache.txt"
    $vulkanTestsEnabled = (Test-Path -LiteralPath $cmakeCache) -and
        (Select-String -LiteralPath $cmakeCache `
            -Pattern "^SOKOBAN_BUILD_VULKAN_SMOKE_TESTS:BOOL=ON$" `
            -Quiet)
    if ($vulkanTestsEnabled) {
        $targets += "sokoban_vulkan_tests"
    }
    & cmake --build $buildRoot --config $Configuration --target $targets --parallel
    if ($LASTEXITCODE -ne 0) {
        throw "CMake build failed with code $LASTEXITCODE"
    }
}

New-Item -ItemType Directory -Path $outputRoot -Force | Out-Null
$cpuOutput = Join-Path $outputRoot "cpu"
New-Item -ItemType Directory -Path $cpuOutput -Force | Out-Null

$performanceExecutable = Resolve-Executable "sokoban_performance_tests"
$performanceMode = if ($Quick) { "--quick" } else { "--full" }
Invoke-Checked $performanceExecutable @(
    $performanceMode,
    "--output", $cpuOutput
)

$vulkanRunner = $null
try {
    $vulkanRunner = Resolve-Executable "sokoban_vulkan_tests"
} catch {
    Write-Warning "Vulkan test runner is unavailable; startup and prepared-asset benchmarks are skipped."
}
if ($null -ne $vulkanRunner) {
    $vulkanOutput = Join-Path $outputRoot "vulkan"
    New-Item -ItemType Directory -Path $vulkanOutput -Force | Out-Null
    Invoke-Checked $vulkanRunner @(
        "vulkan_smoke", "--benchmark-startup"
    ) (Join-Path $vulkanOutput "startup.txt")
    Invoke-Checked $vulkanRunner @(
        "vulkan_smoke", "--benchmark-prepared-assets"
    ) (Join-Path $vulkanOutput "prepared-assets.txt")
}

$gameExecutable = Resolve-Executable "sokoban"
# The report retains the final 120 samples. Leave a separate warm-up window
# for first-use model/texture uploads and shader compilation in every case.
$frameCount = if ($Quick) { 240 } else { 420 }
$scenarios = @(
    @{ Name = "baseline"; Arguments = @() },
    @{ Name = "vsync-disabled"; Arguments = @("--evidence-disable-vsync") },
    @{ Name = "point-light-stress"; Arguments = @("--evidence-point-light-stress") },
    @{ Name = "serial-scene-preparation"; Arguments = @("--serial-scene-preparation") }
)
$level5Arguments = @("--evidence-level", "5", "--evidence-animate", "--evidence-disable-vsync")
if ($Configuration -in @("Debug", "RelWithDebInfo")) {
    $level5Arguments += "--evidence-debug-ui"
}
foreach ($screen in $(if ($Quick) { @(3, 5) } else { @(0, 1, 2, 3, 4, 5, 6) })) {
    $scenarios += @{
        Name = "level5-screen$screen"
        Arguments = $level5Arguments + @("--evidence-screen", "$screen")
    }
}
foreach ($effect in @("mirror-swap", "witch-swap", "turret-volley", "portals", "special-blocks", "mixed-stress")) {
    $scenarios += @{
        Name = "effects-$effect"
        Arguments = $level5Arguments + @("--evidence-screen", "3", "--evidence-effects", $effect)
    }
}
if ($Configuration -in @("Debug", "RelWithDebInfo")) {
    $scenarios += @(
        @{
            Name = "level5-screen3-menu-hidden"
            Arguments = @("--evidence-level", "5", "--evidence-screen", "3", "--evidence-animate", "--evidence-disable-vsync")
        },
        @{
            Name = "level5-screen3-profiler-disabled"
            Arguments = $level5Arguments + @("--evidence-screen", "3", "--evidence-disable-profiler")
        }
    )
}
if (-not $Quick) {
    $scenarios += @(
        @{ Name = "render-scale-75"; Arguments = @("--evidence-render-scale", "75") },
        @{ Name = "ao-disabled"; Arguments = @("--evidence-disable-ao") },
        @{ Name = "translucent-water"; Arguments = @("--evidence-water") },
        @{ Name = "frustum-culling-disabled"; Arguments = @("--evidence-disable-frustum-culling") },
        @{
            Name = "point-shadow-optimizations-disabled"
            Arguments = @(
                "--evidence-point-light-stress",
                "--disable-point-shadow-optimizations"
            )
        },
        @{ Name = "recorder-scratch-reuse-disabled"; Arguments = @("--disable-recorder-scratch-reuse") },
        @{ Name = "level5-screen5-no-water"; Arguments = $level5Arguments + @("--evidence-screen", "5", "--evidence-disable-water") },
        @{ Name = "level5-screen5-no-water-reflections"; Arguments = $level5Arguments + @("--evidence-screen", "5", "--evidence-disable-water-reflections") },
        @{ Name = "level5-screen5-scale75"; Arguments = $level5Arguments + @("--evidence-screen", "5", "--evidence-render-scale", "75") },
        @{ Name = "level5-screen5-msaa1"; Arguments = $level5Arguments + @("--evidence-screen", "5", "--evidence-msaa", "1") },
        @{ Name = "level5-screen5-msaa8"; Arguments = $level5Arguments + @("--evidence-screen", "5", "--evidence-msaa", "8") },
        @{ Name = "level5-screen5-repeat"; Arguments = $level5Arguments + @("--evidence-screen", "5") },
        @{ Name = "baseline-repeat"; Arguments = @() }
    )
}

$originalLocation = Get-Location
$runId = Get-Date -Format "yyyyMMdd-HHmmss-fff"
$startupOutput = Join-Path $outputRoot "startup"
$startupState = Join-Path $startupOutput "state-$runId"
$startupMeasurements = @()
try {
    Set-Location (Split-Path -Parent $gameExecutable)
    New-Item -ItemType Directory -Path $startupState -Force | Out-Null
    foreach ($startupCase in @("cold-pipeline-cache", "warm-pipeline-cache")) {
        $startupLog = Join-Path $startupOutput "$startupCase.log"
        $stopwatch = [Diagnostics.Stopwatch]::StartNew()
        Invoke-Checked $gameExecutable @(
            "--smoke-frames", "1",
            "--save-directory", $startupState
        ) $startupLog
        $stopwatch.Stop()
        $startupText = Get-Content -LiteralPath $startupLog -Raw
        $firstFrame = [regex]::Match(
            $startupText,
            'Application startup phases \(us\): construction=([0-9]+) first-frame=([0-9]+)')
        $startupMeasurements += [pscustomobject]@{
            Name = $startupCase
            ConstructionMicroseconds = if ($firstFrame.Success) {
                [uint64]$firstFrame.Groups[1].Value
            } else { $null }
            FirstFrameMicroseconds = if ($firstFrame.Success) {
                [uint64]$firstFrame.Groups[2].Value
            } else { $null }
            ElapsedMicroseconds = [math]::Round(
                $stopwatch.Elapsed.TotalMilliseconds * 1000.0)
            Log = "$startupCase.log"
        }
    }
    $startupMeasurements |
        ConvertTo-Json -Depth 3 |
        Set-Content -LiteralPath (Join-Path $startupOutput "startup-results.json") `
            -Encoding utf8
    $startupSummary = @(
        "# Application startup",
        "",
        "Both runs use the same isolated save directory. The first has no game pipeline cache; the second reuses the cache persisted by the first run.",
        "",
        "| Case | Construction | First frame | Process wall time | Detailed phase log |",
        "|---|---:|---:|---:|---|"
    )
    foreach ($measurement in $startupMeasurements) {
        $constructionMilliseconds = if ($null -ne $measurement.ConstructionMicroseconds) {
            "$([math]::Round($measurement.ConstructionMicroseconds / 1000.0, 1)) ms"
        } else { "unavailable" }
        $firstFrameMilliseconds = if ($null -ne $measurement.FirstFrameMicroseconds) {
            "$([math]::Round($measurement.FirstFrameMicroseconds / 1000.0, 1)) ms"
        } else { "unavailable" }
        $milliseconds = [math]::Round(
            $measurement.ElapsedMicroseconds / 1000.0, 1)
        $startupSummary += "| $($measurement.Name) | $constructionMilliseconds | $firstFrameMilliseconds | $milliseconds ms | [$($measurement.Log)]($($measurement.Log)) |"
    }
    $startupSummary |
        Set-Content -LiteralPath (Join-Path $startupOutput "startup-report.md") `
            -Encoding utf8

    foreach ($scenario in $scenarios) {
        $scenarioOutput = Join-Path $outputRoot "gpu/$($scenario.Name)"
        # A fresh preference/profile root makes scenarios comparable even when
        # the same report directory is reused for another run.
        $stateOutput = Join-Path $scenarioOutput "state-$runId"
        New-Item -ItemType Directory -Path $scenarioOutput -Force | Out-Null
        New-Item -ItemType Directory -Path $stateOutput -Force | Out-Null
        $arguments = @(
            "--smoke-frames", "$frameCount",
            "--save-directory", $stateOutput,
            "--evidence-output", $scenarioOutput
        ) + $scenario.Arguments
        Write-HardwareSnapshot (Join-Path $scenarioOutput "hardware-start.txt")
        Invoke-Checked $gameExecutable $arguments (Join-Path $scenarioOutput "run.log")
        Write-HardwareSnapshot (Join-Path $scenarioOutput "hardware-end.txt")
    }
} finally {
    Set-Location $originalLocation
}

$indexLines = @(
    "# Comprehensive performance run",
    "",
    "- Configuration: $Configuration",
    "- Mode: $(if ($Quick) { 'quick' } else { 'full' })",
    "- Evidence frames per GPU scenario: $frameCount",
    "- When available, each GPU case includes NVIDIA hardware-start/end snapshots with clocks, temperature, power, and throttling reasons. Compare those and the repeated baselines before attributing timing drift to code.",
    "",
    "## CPU and engine workloads",
    "",
    "- [Ranked CPU report](cpu/performance-report.md)",
    "- [Machine-readable results](cpu/performance-results.json)",
    "- [CPU trace](cpu/cpu-trace.json)",
    "",
    "## Application startup",
    "",
    "- [Cold/warm startup report](startup/startup-report.md)",
    "- [Machine-readable startup results](startup/startup-results.json)",
    "",
    "## GPU evidence matrix",
    "",
    "| Scenario | Application avg / p95 ms | Renderer avg / p95 ms | GPU avg / p95 ms | Scene prep avg / p95 ms | Particles / draws | Top finding |",
    "|---|---:|---:|---:|---:|---:|---|"
)
foreach ($scenario in $scenarios) {
    $metrics = Get-ChildItem -LiteralPath (Join-Path $outputRoot "gpu/$($scenario.Name)") `
        -Filter "metrics-*.md" | Sort-Object LastWriteTime -Descending | Select-Object -First 1
    if ($null -ne $metrics) {
        $relative = "gpu/$($scenario.Name)/$($metrics.Name)" -replace '\\', '/'
        $text = Get-Content -LiteralPath $metrics.FullName -Raw
        $cpu = [regex]::Match(
            $text,
            '(?m)^- CPU frame: average ([0-9.]+) ms, p95 ([0-9.]+) ms')
        $application = [regex]::Match($text,
            '(?m)^- Application frame \(including profiler, excluding frame cap\): average ([0-9.]+) ms, p95 ([0-9.]+) ms')
        $particles = [regex]::Match($text, '(?m)^- Prepared particles: ([0-9]+)')
        $particleDraws = [regex]::Match($text, '(?m)^- Particle draw calls: ([0-9]+)')
        $gpu = [regex]::Match(
            $text,
            '(?m)^- GPU frame: average ([0-9.]+) ms, p95 ([0-9.]+) ms')
        $scene = [regex]::Match(
            $text,
            '(?m)^- Scene preparation: average ([0-9.]+) ms, p95 ([0-9.]+) ms')
        $topFinding = [regex]::Match(
            $text,
            '(?m)^1\. \*\*(.+?)\*\*')
        $cpuText = if ($cpu.Success) {
            "$($cpu.Groups[1].Value) / $($cpu.Groups[2].Value)"
        } else { "unavailable" }
        $gpuText = if ($gpu.Success) {
            "$($gpu.Groups[1].Value) / $($gpu.Groups[2].Value)"
        } else { "unavailable" }
        $sceneText = if ($scene.Success) {
            "$($scene.Groups[1].Value) / $($scene.Groups[2].Value)"
        } else { "unavailable" }
        $findingText = if ($topFinding.Success) {
            $topFinding.Groups[1].Value
        } else { "No rule-based bottleneck" }
        $applicationText = if ($application.Success) {
            "$($application.Groups[1].Value) / $($application.Groups[2].Value)"
        } else { "unavailable" }
        $particleText = if ($particles.Success -and $particleDraws.Success) {
            "$($particles.Groups[1].Value) / $($particleDraws.Groups[1].Value)"
        } else { "unavailable" }
        $indexLines += "| [$($scenario.Name)]($relative) | $applicationText | $cpuText | $gpuText | $sceneText | $particleText | $findingText |"
    }
}
if ($null -ne $vulkanRunner) {
    $indexLines += @(
        "",
        "## Vulkan startup and streaming",
        "",
        "- [Startup phases](vulkan/startup.txt)",
        "- [Prepared-asset pressure](vulkan/prepared-assets.txt)"
    )
    $startupText = Get-Content -LiteralPath `
        (Join-Path $outputRoot "vulkan/startup.txt") -Raw
    $startupPhases = [regex]::Matches(
        $startupText,
        '(?<name>[a-z_]+)_us=(?<value>[0-9]+)') | ForEach-Object {
            [pscustomobject]@{
                Name = $_.Groups['name'].Value -replace '_', ' '
                Microseconds = [uint64]$_.Groups['value'].Value
            }
        } | Sort-Object Microseconds -Descending | Select-Object -First 3
    if ($startupPhases.Count -gt 0) {
        $phaseSummary = ($startupPhases | ForEach-Object {
            "$($_.Name) $([math]::Round($_.Microseconds / 1000.0, 1)) ms"
        }) -join ", "
        $indexLines += "- Slowest measured startup phases: $phaseSummary."
    }
    $pressureText = Get-Content -LiteralPath `
        (Join-Path $outputRoot "vulkan/prepared-assets.txt") -Raw
    $pressure = [regex]::Match(
        $pressureText,
        'deferral_attempts=([0-9]+).*?deferred_us=([0-9]+).*?prepared_budget_deferrals=([0-9]+).*?elapsed_us=([0-9]+)')
    if ($pressure.Success) {
        $indexLines += "- Prepared-asset pressure: $($pressure.Groups[1].Value) residency deferral attempt(s), $($pressure.Groups[3].Value) prepared-budget deferral(s), $([math]::Round([uint64]$pressure.Groups[2].Value / 1000.0, 1)) ms deferred, $([math]::Round([uint64]$pressure.Groups[4].Value / 1000.0, 1)) ms total."
    }
}
$indexPath = Join-Path $outputRoot "index.md"
$indexLines | Set-Content -LiteralPath $indexPath -Encoding utf8
Write-Host "Comprehensive performance index: $indexPath"
