@echo off
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b %errorlevel%
if not exist "out\code-quality-review\probes" mkdir "out\code-quality-review\probes"
cl /nologo /std:c++20 /EHsc /MTd /D_DEBUG /DSOKOBAN_ENABLE_DEBUG_UI=0 /I"src" /I"third_party\SDL\include" /I"out\code-quality-review\third_party\SDL\include-revision" /Fo"out\code-quality-review\probes\content-editor-probe.obj" /Fe"out\code-quality-review\probes\content-editor-probe.exe" "docs\reviews\2026-09-11-code-quality\content-editor-probe.cpp" /Fd"out\code-quality-review\probes\content-editor-probe.pdb" /link "out\code-quality-review\Debug\sokoban_core.lib" "out\code-quality-review\Debug\sokoban_bc7enc16.lib" "out\code-quality-review\third_party\SDL\Debug\SDL3-static.lib" kernel32.lib user32.lib gdi32.lib winmm.lib imm32.lib ole32.lib oleaut32.lib version.lib uuid.lib advapi32.lib setupapi.lib shell32.lib dinput8.lib dbghelp.lib winspool.lib comdlg32.lib
if errorlevel 1 exit /b %errorlevel%
"out\code-quality-review\probes\content-editor-probe.exe" "out\code-quality-review\probes\content-editor-fixtures-%RANDOM%-%RANDOM%" "."

