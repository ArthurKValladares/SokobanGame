param([switch]$RepaintOnly)
$ErrorActionPreference = 'Stop'
Import-Module 'C:/Program Files/Microsoft Visual Studio/2022/Community/Common7/Tools/Microsoft.VisualStudio.DevShell.dll'
Enter-VsDevShell -VsInstallPath 'C:/Program Files/Microsoft Visual Studio/2022/Community' -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64'
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '../../../..')).Path
$probeOutput = Join-Path $projectRoot 'out/code-quality-review/probes/renderer'
New-Item -ItemType Directory -Force -Path $probeOutput | Out-Null
$sourcePath = Join-Path $PSScriptRoot 'RendererReviewProbe.cpp'
$objectPath = Join-Path $probeOutput 'RendererReviewProbe.obj'
$executablePath = Join-Path $probeOutput 'RendererReviewProbe.exe'
& cl /nologo /std:c++20 /EHsc /MTd "/I$projectRoot/src" "/I$projectRoot/third_party/SDL/include" '/IC:/VulkanSDK/1.4.309.0/Include' "/Fo$objectPath" "/Fe$executablePath" $sourcePath "/Fd$probeOutput/RendererReviewProbe.pdb" /link (Join-Path $projectRoot 'out/code-quality-review/Debug/sokoban_render_vulkan.lib') (Join-Path $projectRoot 'out/code-quality-review/Debug/sokoban_core.lib') (Join-Path $projectRoot 'out/code-quality-review/third_party/SDL/Debug/SDL3-static.lib') (Join-Path $projectRoot 'out/code-quality-review/Debug/sokoban_bc7enc16.lib') 'C:/VulkanSDK/1.4.309.0/Lib/vulkan-1.lib' kernel32.lib user32.lib gdi32.lib winmm.lib imm32.lib ole32.lib oleaut32.lib version.lib uuid.lib advapi32.lib setupapi.lib shell32.lib dinput8.lib dbghelp.lib
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
$assetRoot = Join-Path $projectRoot 'out/code-quality-review/Debug/assets'
if ($RepaintOnly) {
    & $executablePath $assetRoot 'repaint' 2>&1 | Tee-Object -FilePath (Join-Path $probeOutput 'repaint-results.txt')
    exit $LASTEXITCODE
}
& $executablePath $assetRoot 'finite' 2>&1 | Tee-Object -FilePath (Join-Path $probeOutput 'finite-results.txt')
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
$stdout = Join-Path $probeOutput 'wait-stdout.txt'
$stderr = Join-Path $probeOutput 'wait-stderr.txt'
$process = Start-Process -FilePath $executablePath -ArgumentList @(('"' + $assetRoot + '"'), 'wait') -WindowStyle Hidden -PassThru -RedirectStandardOutput $stdout -RedirectStandardError $stderr
if (!$process.WaitForExit(15000)) {
    Stop-Process -Id $process.Id
    'Blocking wait exceeded 15 seconds; terminated review probe process.' | Set-Content (Join-Path $probeOutput 'wait-result.txt')
} else {
    "Blocking wait returned: exit=$($process.ExitCode)" | Set-Content (Join-Path $probeOutput 'wait-result.txt')
}
Get-Content $stdout
Get-Content (Join-Path $probeOutput 'wait-result.txt')
