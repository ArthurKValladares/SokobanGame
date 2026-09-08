[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [ValidateScript({ Test-Path -LiteralPath $_ })]
    [string]$Package,

    [Alias('LaunchSeconds')]
    [ValidateRange(1, 600)]
    [int]$SmokeTimeoutSeconds = 60,

    [ValidateRange(1, 10000)]
    [int]$SmokeFrames = 240,

    [string]$DiagnosticOutputDirectory,

    [switch]$SkipLaunch,
    [switch]$KeepExtracted
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Assert-Condition {
    param(
        [Parameter(Mandatory)]
        [bool]$Condition,
        [Parameter(Mandatory)]
        [string]$Message
    )

    if (-not $Condition) {
        throw $Message
    }
}

function Get-PackageRoot {
    param(
        [Parameter(Mandatory)]
        [string]$Path,
        [Parameter(Mandatory)]
        [string]$ExtractionDirectory
    )

    $resolvedPath = (Resolve-Path -LiteralPath $Path).Path
    if ((Get-Item -LiteralPath $resolvedPath).PSIsContainer) {
        return $resolvedPath
    }

    Assert-Condition ([IO.Path]::GetExtension($resolvedPath) -ieq '.zip') `
        "Package must be a runtime directory or ZIP: $resolvedPath"
    Expand-Archive -LiteralPath $resolvedPath -DestinationPath $ExtractionDirectory

    $roots = @(Get-ChildItem -LiteralPath $ExtractionDirectory -Directory)
    if ($roots.Count -eq 1 -and
        (Test-Path -LiteralPath (Join-Path $roots[0].FullName 'sokoban.exe'))) {
        return $roots[0].FullName
    }
    return $ExtractionDirectory
}

function Test-ContentIndex {
    param(
        [Parameter(Mandatory)]
        [string]$AssetRoot
    )

    $indexPath = Join-Path $AssetRoot 'content.index'
    Assert-Condition (Test-Path -LiteralPath $indexPath -PathType Leaf) `
        "Missing content index: $indexPath"
    $lines = @(Get-Content -LiteralPath $indexPath)
    Assert-Condition ($lines.Count -ge 4) 'Content index is truncated.'
    Assert-Condition ($lines[0] -match '^format 1$') `
        "Unsupported content index header: $($lines[0])"
    Assert-Condition ($lines[1] -match '^game-version \S+$') `
        "Invalid content index game version: $($lines[1])"
    Assert-Condition ($lines[2] -match '^file-count (\d+)$') `
        "Invalid content index file count: $($lines[2])"
    $expectedCount = [int]$Matches[1]
    Assert-Condition ($lines[3] -match '^total-bytes (\d+)$') `
        "Invalid content index total size: $($lines[3])"
    $expectedBytes = [Int64]$Matches[1]

    $assetRootFullPath = [IO.Path]::GetFullPath($AssetRoot)
    $assetRootPrefix = $assetRootFullPath.TrimEnd([IO.Path]::DirectorySeparatorChar) +
        [IO.Path]::DirectorySeparatorChar
    [Int64]$actualBytes = 0
    $actualCount = 0

    foreach ($line in $lines | Select-Object -Skip 4) {
        Assert-Condition ($line -match '^file (\d+) (.+)$') `
            "Invalid content index entry: $line"
        [Int64]$expectedFileBytes = $Matches[1]
        $relativePath = $Matches[2]
        Assert-Condition (-not [IO.Path]::IsPathRooted($relativePath)) `
            "Content index contains an absolute path: $relativePath"

        $fullPath = [IO.Path]::GetFullPath((Join-Path $AssetRoot $relativePath))
        Assert-Condition ($fullPath.StartsWith($assetRootPrefix,
                [StringComparison]::OrdinalIgnoreCase)) `
            "Content index escapes the asset root: $relativePath"
        Assert-Condition (Test-Path -LiteralPath $fullPath -PathType Leaf) `
            "Indexed asset is missing: $relativePath"

        $actualFileBytes = (Get-Item -LiteralPath $fullPath).Length
        Assert-Condition ($actualFileBytes -eq $expectedFileBytes) `
            "Indexed asset size differs: $relativePath (expected $expectedFileBytes, got $actualFileBytes)"
        $actualBytes += $actualFileBytes
        ++$actualCount
    }

    Assert-Condition ($actualCount -eq $expectedCount) `
        "Content index count differs: expected $expectedCount, got $actualCount"
    Assert-Condition ($actualBytes -eq $expectedBytes) `
        "Content index total differs: expected $expectedBytes, got $actualBytes"
    Write-Host "Verified $actualCount indexed assets ($actualBytes bytes)."
}

function Get-SmokeDiagnosticText {
    param(
        [Parameter(Mandatory)]
        [string]$SmokeRoot,
        [Parameter(Mandatory)]
        [string]$SaveDirectory
    )

    $diagnosticFiles = @(
        Get-Item -LiteralPath (Join-Path $SmokeRoot 'stdout.txt') -ErrorAction SilentlyContinue
        Get-Item -LiteralPath (Join-Path $SmokeRoot 'stderr.txt') -ErrorAction SilentlyContinue
        Get-ChildItem -LiteralPath $SaveDirectory -Filter 'log*.txt' -File -ErrorAction SilentlyContinue
    )
    if ($diagnosticFiles.Count -eq 0) {
        return 'No smoke-run stdout, stderr, or game log was produced.'
    }

    $sections = foreach ($file in $diagnosticFiles) {
        $content = @(Get-Content -LiteralPath $file.FullName -Tail 200 -ErrorAction Continue)
        "===== $($file.Name) (last 200 lines) =====`n$($content -join "`n")"
    }
    return $sections -join "`n"
}

function Copy-SmokeDiagnostics {
    param(
        [Parameter(Mandatory)]
        [string]$Source,
        [Parameter(Mandatory)]
        [string]$Destination
    )

    New-Item -ItemType Directory -Path $Destination -Force | Out-Null
    foreach ($name in @('stdout.txt', 'stderr.txt')) {
        $path = Join-Path $Source $name
        if (Test-Path -LiteralPath $path -PathType Leaf) {
            Copy-Item -LiteralPath $path -Destination $Destination -Force
        }
    }

    $profileSource = Join-Path $Source 'profile'
    $profileDestination = Join-Path $Destination 'profile'
    $logs = @(Get-ChildItem -LiteralPath $profileSource `
        -Filter 'log*.txt' -File -ErrorAction SilentlyContinue)
    if ($logs.Count -gt 0) {
        New-Item -ItemType Directory -Path $profileDestination -Force | Out-Null
        foreach ($log in $logs) {
            Copy-Item -LiteralPath $log.FullName -Destination $profileDestination -Force
        }
    }
    $crashSource = Join-Path $profileSource 'crashes'
    if (Test-Path -LiteralPath $crashSource -PathType Container) {
        New-Item -ItemType Directory -Path $profileDestination -Force | Out-Null
        Copy-Item -LiteralPath $crashSource -Destination $profileDestination -Recurse -Force
    }
}

function Stop-SmokeProcess {
    param(
        [Parameter(Mandatory)]
        [Diagnostics.Process]$Process
    )

    if ($Process.HasExited) {
        return
    }
    try {
        $Process.Kill()
    } catch {
        $Process.Refresh()
        if (-not $Process.HasExited) {
            throw
        }
    }
    $Process.WaitForExit()
}

$temporaryRoot = Join-Path ([IO.Path]::GetTempPath()) (
    'Sokoban3D-PackageValidation-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $temporaryRoot | Out-Null
$smokeRoot = Join-Path $temporaryRoot 'smoke'

try {
    $runtimeRoot = Get-PackageRoot -Path $Package -ExtractionDirectory $temporaryRoot
    $gameExecutable = Join-Path $runtimeRoot 'sokoban.exe'
    $assetRoot = Join-Path $runtimeRoot 'assets'

    Assert-Condition (Test-Path -LiteralPath $gameExecutable -PathType Leaf) `
        "Runtime package is missing sokoban.exe."
    Assert-Condition (Test-Path -LiteralPath $assetRoot -PathType Container) `
        "Runtime package is missing assets/."
    foreach ($license in @(
            'SDL-LICENSE.txt',
            'miniaudio-LICENSE.txt',
            'nlohmann-json-LICENSE.txt',
            'stb-LICENSE.txt',
            'cgltf-LICENSE.txt',
            'imgui-LICENSE.txt')) {
        Assert-Condition (Test-Path -LiteralPath (Join-Path $runtimeRoot "licenses/$license") -PathType Leaf) `
            "Runtime package is missing license: $license"
    }
    Assert-Condition (@(Get-ChildItem -LiteralPath $runtimeRoot -Filter '*.pdb' -Recurse -File).Count -eq 0) `
        'Runtime package contains PDB files; symbols must remain in the Symbols component.'

    Test-ContentIndex -AssetRoot $assetRoot

    if (-not $SkipLaunch) {
        New-Item -ItemType Directory -Path $smokeRoot | Out-Null
        $saveDirectory = Join-Path $smokeRoot 'profile'
        New-Item -ItemType Directory -Path $saveDirectory | Out-Null
        $stdoutPath = Join-Path $smokeRoot 'stdout.txt'
        $stderrPath = Join-Path $smokeRoot 'stderr.txt'
        $game = $null
        $gameStarted = $false
        $stdoutTask = $null
        $stderrTask = $null
        try {
            try {
                $startInfo = [Diagnostics.ProcessStartInfo]::new()
                $startInfo.FileName = $gameExecutable
                $startInfo.Arguments = "--smoke-frames $SmokeFrames --save-directory `"$saveDirectory`""
                $startInfo.WorkingDirectory = $runtimeRoot
                $startInfo.UseShellExecute = $false
                $startInfo.CreateNoWindow = $true
                $startInfo.WindowStyle = [Diagnostics.ProcessWindowStyle]::Hidden
                $startInfo.RedirectStandardOutput = $true
                $startInfo.RedirectStandardError = $true
                $game = [Diagnostics.Process]::new()
                $game.StartInfo = $startInfo
                $gameStarted = $game.Start()
                Assert-Condition $gameStarted `
                    'The operating system refused to start the packaged game.'
                # Drain while the process runs. Reading only after WaitForExit
                # can deadlock once either pipe fills its fixed-size buffer.
                $stdoutTask = $game.StandardOutput.ReadToEndAsync()
                $stderrTask = $game.StandardError.ReadToEndAsync()
            } catch {
                throw "Could not start packaged game: $($_.Exception.Message)"
            }

            $completed = $game.WaitForExit($SmokeTimeoutSeconds * 1000)
            if (-not $completed) {
                Stop-SmokeProcess -Process $game
                [IO.File]::WriteAllText($stdoutPath, $stdoutTask.Result)
                [IO.File]::WriteAllText($stderrPath, $stderrTask.Result)
                $diagnostics = Get-SmokeDiagnosticText `
                    -SmokeRoot $smokeRoot -SaveDirectory $saveDirectory
                throw "Packaged game did not complete its $SmokeFrames-frame smoke run within $SmokeTimeoutSeconds seconds.`n$diagnostics"
            }

            # WaitForExit(timeout) establishes process completion. The
            # parameterless call also drains both redirected output streams.
            $game.WaitForExit()
            [IO.File]::WriteAllText($stdoutPath, $stdoutTask.Result)
            [IO.File]::WriteAllText($stderrPath, $stderrTask.Result)
            $exitCode = $game.ExitCode
            if ($exitCode -ne 0) {
                $diagnostics = Get-SmokeDiagnosticText `
                    -SmokeRoot $smokeRoot -SaveDirectory $saveDirectory
                throw "Packaged game smoke run exited with code $exitCode.`n$diagnostics"
            }
        } finally {
            if ($gameStarted) {
                Stop-SmokeProcess -Process $game
            }
            if ($game) {
                $game.Dispose()
            }
        }
        Write-Host "Packaged game completed $SmokeFrames smoke frames successfully."
    }

    Write-Host "Package validation passed: $runtimeRoot"
} finally {
    if ($DiagnosticOutputDirectory -and (Test-Path -LiteralPath $smokeRoot)) {
        Copy-SmokeDiagnostics `
            -Source $smokeRoot `
            -Destination ([IO.Path]::GetFullPath($DiagnosticOutputDirectory))
        Write-Host "Smoke diagnostics copied to: $([IO.Path]::GetFullPath($DiagnosticOutputDirectory))"
    }
    if ($KeepExtracted) {
        Write-Host "Extracted package retained at: $temporaryRoot"
    } else {
        Remove-Item -LiteralPath $temporaryRoot -Recurse -Force -ErrorAction SilentlyContinue
    }
}
