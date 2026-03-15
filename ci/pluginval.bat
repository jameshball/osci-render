@echo off
setlocal enabledelayedexpansion

REM ── pluginval local runner for Windows ──────────────────────────
REM Usage:  ci\pluginval.bat [plugin-name] [strictness]
REM Example: ci\pluginval.bat osci-render 5
REM
REM Requires: cmake on PATH, Visual Studio (for MSBuild), and a built plugin.
REM Both Debug and Release VST3 builds are checked (Debug first for local dev).

set "PLUGIN=%~1"
if "%PLUGIN%"=="" set "PLUGIN=osci-render"

set "STRICTNESS=%~2"
if "%STRICTNESS%"=="" set "STRICTNESS=5"

set "ROOT=%~dp0.."
set "PLUGINVAL_SRC=%ROOT%\modules\pluginval"
set "PLUGINVAL_BUILD=%PLUGINVAL_SRC%\Builds"

REM ── Find CMake >= 3.22 (JUCE 8 requirement) ────────────────────
REM Prefer VS2022's bundled CMake over whatever is on PATH
set "CMAKE_EXE=cmake"
for /f "tokens=*" %%i in ('"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -property installationPath 2^>nul') do (
    if exist "%%i\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" (
        set "CMAKE_EXE=%%i\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
    )
)
echo Using CMake: %CMAKE_EXE%

REM ── Build pluginval ─────────────────────────────────────────────
echo =============================================
echo  Building pluginval from source...
echo =============================================

pushd "%PLUGINVAL_SRC%"
"%CMAKE_EXE%" -B Builds -DCMAKE_BUILD_TYPE=Release -DPLUGINVAL_VST3_VALIDATOR=OFF -Dpluginval_IS_TOP_LEVEL=ON .
"%CMAKE_EXE%" --build Builds --config Release --parallel
popd

set "PLUGINVAL=%PLUGINVAL_BUILD%\pluginval_artefacts\Release\pluginval.exe"
if not exist "%PLUGINVAL%" (
    echo ERROR: pluginval binary not found at %PLUGINVAL%
    exit /b 1
)

REM ── Find the VST3 plugin ────────────────────────────────────────
set "VST3_PATH=%ROOT%\Builds\%PLUGIN%\VisualStudio2022\x64\Debug\VST3\%PLUGIN%.vst3"
if not exist "%VST3_PATH%" (
    set "VST3_PATH=%ROOT%\Builds\%PLUGIN%\VisualStudio2022\x64\Release\VST3\%PLUGIN%.vst3"
)
if not exist "%VST3_PATH%" (
    echo ERROR: VST3 plugin not found. Build the plugin first.
    echo Looked in: Builds\%PLUGIN%\VisualStudio2022\x64\{Debug,Release}\VST3\
    exit /b 1
)

echo VST3 plugin: %VST3_PATH%

REM ── Run pluginval ───────────────────────────────────────────────
echo =============================================
echo  Running pluginval (strictness %STRICTNESS%)
echo =============================================

"%PLUGINVAL%" --strictness-level %STRICTNESS% --validate "%VST3_PATH%"
if %ERRORLEVEL% neq 0 (
    echo =============================================
    echo  pluginval: VALIDATION FAILED
    echo =============================================
    exit /b 1
)

echo =============================================
echo  pluginval: ALL TESTS PASSED
echo =============================================
