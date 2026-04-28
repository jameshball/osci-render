# Build Details

Supplementary build documentation. See [copilot-instructions.md](../copilot-instructions.md) for primary guide.

## ccache + PCH

Both Debug and Release builds use:

- **PCH** — `Source/pch.h` pre-parses `<JuceHeader.h>` once. Halves clean build time (~2m49s → ~1m12s).
- **ccache/sccache** — compiler output cached by preprocessed-source hash. Wired via `ci/ccache-clang` / `ci/ccache-clang++` on macOS, via `CXX="ccache g++"` env in `ci/build.sh` / `ci/test.sh` on Linux, and via `sccache` as a fake `cl.exe` for Windows MSVC builds.

**One-time setup (macOS, required on a new machine):**
```bash
brew install ccache
ccache --set-config sloppiness=pch_defines,time_macros,include_file_mtime,include_file_ctime
```

Without the sloppiness config, every build reports "uncacheable" and the cache is never used.

**CI** uses `hendrikmuhs/ccache-action` for macOS/Linux and `mozilla-actions/sccache-action` for Windows to persist compiler cache state across runs via the GitHub Actions cache.

## CI architecture

`.github/workflows/build.yaml` runs a 9-job matrix (3 OS × {osci-free, osci-premium, sosci}) with:

- **Compile cache** — `hendrikmuhs/ccache-action` for macOS/Linux and `mozilla-actions/sccache-action` for Windows MSVC builds
- **LuaJIT cache** — keyed on runner image, `modules/LuaJIT` submodule SHA, and the LuaJIT build recipe
- **pluginval cache** — keyed on runner image, `modules/pluginval` submodule SHA, and the pluginval build recipe; pluginval.sh skips rebuild when `Builds/.osci_built` exists
- **apt cache** — `awalsh128/cache-apt-pkgs-action` on Linux runners
- **NuGet cache** — `~/.nuget/packages` keyed on project files
- **Packages installer cache** — caches `Packages_1211_dev.dmg` between macOS runs
- **Concurrency** — older in-progress runs are auto-cancelled when a new one starts on the same ref
- **Parallel pluginval** — effect + instrument validation run concurrently in the same job
- **Ninja** — pluginval CMake build uses Ninja when available (significantly faster than Xcode/MSBuild generators)

## Building (Windows)

MSBuild must run from a Visual Studio Developer environment so `luajit_win.bat` finds the MSVC toolchain.

```powershell
# 1. Import VS 2022 environment
$vsPath = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -property installationPath
cmd /c "`"$vsPath\VC\Auxiliary\Build\vcvars64.bat`" >nul 2>&1 && set" |
    ForEach-Object { if ($_ -match '^([^=]+)=(.*)$') {
        [System.Environment]::SetEnvironmentVariable($Matches[1], $Matches[2], 'Process') } }

# 2. Resave and build
C:\JUCE\Projucer.exe --resave osci-render.jucer
MSBuild Builds\osci-render\VisualStudio2022\osci-render.sln /p:Configuration=Debug /p:Platform=x64 /m
```

### Fast iteration (Windows)

```powershell
Stop-Process -Name "osci-render" -ErrorAction SilentlyContinue
MSBuild Builds\osci-render\VisualStudio2022\osci-render.sln /p:Configuration=Debug /p:Platform=x64 /m
Start-Process "Builds\osci-render\VisualStudio2022\x64\Debug\Standalone Plugin\osci-render.exe"
```
