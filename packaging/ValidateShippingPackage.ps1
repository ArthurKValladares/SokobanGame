[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [ValidateScript({ Test-Path -LiteralPath $_ })]
    [string]$Package,

    [ValidateRange(1, 120)]
    [int]$LaunchSeconds = 10,

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

$temporaryRoot = Join-Path ([IO.Path]::GetTempPath()) (
    'Sokoban3D-PackageValidation-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $temporaryRoot | Out-Null

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
            'imgui-LICENSE.txt')) {
        Assert-Condition (Test-Path -LiteralPath (Join-Path $runtimeRoot "licenses/$license") -PathType Leaf) `
            "Runtime package is missing license: $license"
    }
    Assert-Condition (@(Get-ChildItem -LiteralPath $runtimeRoot -Filter '*.pdb' -Recurse -File).Count -eq 0) `
        'Runtime package contains PDB files; symbols must remain in the Symbols component.'

    Test-ContentIndex -AssetRoot $assetRoot

    if (-not $SkipLaunch) {
        $game = Start-Process -FilePath $gameExecutable -WorkingDirectory $runtimeRoot -PassThru
        Start-Sleep -Seconds $LaunchSeconds
        $game.Refresh()
        Assert-Condition (-not $game.HasExited) `
            "Packaged game exited during its $LaunchSeconds-second launch check (exit code $($game.ExitCode))."

        # The launch check proves the package reaches a live Vulkan window.
        # It intentionally closes the process afterwards; manual gameplay
        # acceptance remains part of the hardware matrix in ReleaseValidation.md.
        if (-not $game.CloseMainWindow()) {
            Stop-Process -Id $game.Id -ErrorAction Stop
        } else {
            $game.WaitForExit(5000) | Out-Null
            if (-not $game.HasExited) {
                Stop-Process -Id $game.Id -ErrorAction Stop
            }
        }
        Write-Host "Packaged game remained running for $LaunchSeconds seconds."
    }

    Write-Host "Package validation passed: $runtimeRoot"
} finally {
    if ($KeepExtracted) {
        Write-Host "Extracted package retained at: $temporaryRoot"
    } else {
        Remove-Item -LiteralPath $temporaryRoot -Recurse -Force -ErrorAction SilentlyContinue
    }
}
