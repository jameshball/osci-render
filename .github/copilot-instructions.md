# osci-render Development Guide

## Communication

**NEVER ask clarifying questions inline in chat.** Always use the `vscode_askQuestions` tool to present questions to the user. This provides a structured UI with selectable options and keeps the conversation focused on code.

## Subagents

**Use subagents aggressively to protect the main context window.** Every file read, grep, or codebase exploration that isn't immediately needed in the main conversation should be delegated to a subagent. The main context window is expensive; treat it as a scarce resource.

**Rules:**
- **Any exploration task** — searching for where something is defined, understanding how a subsystem works, finding all callers of a function, reading a set of files to answer a question — should use the `Explore` subagent rather than running `grep_search`/`read_file`/`semantic_search` directly.
- **Only bring results back** that are directly needed to implement the next step. Don't paste large file contents into the main conversation.
- **Multi-file investigations** (e.g. tracing a data flow, auditing all usages of a class) must always go through a subagent.
- Single-line lookups or a single targeted `grep_search` are fine inline when the answer is trivially small.

Example invocations:
```
runSubagent("Explore", "Find all places where permanentEffects is populated. Return the file paths and line numbers only. Thoroughness: quick")
runSubagent("Explore", "Explain the full destruction order of OscirenderAudioProcessor members and which base class destructors run when. Thoroughness: thorough")
```

## Code Reviews

When asked to do a code review, follow this workflow:

1. **Review all unstaged changes.** Focus on: code quality, duplication, code structure, reusability, folder structure, performance, and potential for bugs.
2. **List every issue** you find, numbered, with a clear description and the relevant file/line.
3. **For each issue**, use `vscode_askQuestions` to ask whether to:
   - **Implement the fix** — you will apply the suggested change immediately
   - **Skip** — do not implement the change
4. **Implement all accepted fixes** within the same session, building and verifying after all changes are applied.

## Important Guidelines

- **Backwards compatibility**: Always ask the user before adding backwards compatibility when changing or refactoring something. Do not assume it is needed.
- **Recommendations and proposals**: When presenting optimization recommendations, refactoring suggestions, architectural changes, or any list of proposed improvements, **always present each one as a numbered question** using `vscode_askQuestions`. Each question should include a clear description of the proposed change, its rationale, and expected impact. This allows the user to approve, reject, or request more context on each item individually. Never present recommendations as a flat list in chat — always use the structured question UI so the user can comment against each one.

## Project Overview

osci-render is a JUCE-based audio plugin (VST3/AU/Standalone) that renders 2D/3D graphics to audio for oscilloscope displays. It supports `.obj`, `.svg`, `.txt`, `.lua`, and Blender scene rendering with real-time audio effects and DAW automation.

**Two products share this codebase:**
- `osci-render` - Full synthesizer with file parsing, Lua scripting, and effects
- `sosci` - Lightweight visualizer-only version

## Architecture

### Core Components

```
Source/
├── PluginProcessor.cpp/h      # Main audio processor (osci-render)
├── SosciPluginProcessor.cpp/h # Sosci variant processor  
├── CommonPluginProcessor.cpp/h # Shared base class
├── PluginEditor.cpp/h         # Main UI (osci-render)
├── CommonPluginEditor.cpp/h   # Shared UI base
├── audio/                     # Audio effects (BitCrush, Delay, etc.)
├── visualiser/                # OpenGL oscilloscope renderer
├── parser/                    # File parsing (FileParser.h routes to specific parsers)
├── lua/                       # LuaJIT integration for scripting
└── components/                # UI components
```

### Key Modules (`modules/`)

- **osci_render_core** - Core types in `osci` namespace: `Point`, `Shape`, `Effect`, `EffectParameter`
- **chowdsp_utils** - DSP utilities from chowdsp
- **LuaJIT** - Embedded Lua scripting engine
- **juce_sharedtexture** - Syphon/Spout texture sharing
- **pluginval** - Plugin validation tool (builds via CMake, used in CI and locally)

### Data Flow

1. **File Parsing**: `FileParser` → specific parser (`LuaParser`, `SvgParser`, `WorldObject`) → `osci::Shape` vector
2. **Audio Generation**: `ShapeVoice` renders shapes to audio samples via `ShapeSound`
3. **Effects Chain**: `osci::Effect` instances process `osci::Point` samples sequentially
4. **Visualization**: `VisualiserRenderer` renders audio buffer via OpenGL shaders

## Build System

### Prerequisites
- JUCE framework with Projucer
- Xcode (macOS), Visual Studio 2022 (Windows), or GCC (Linux)
- LuaJIT (built automatically via `luajit_linux_macos.sh` or `luajit_win.bat`)
- ccache (macOS): `brew install ccache` — required for fast incremental builds (see below)

### Build speed: ccache + PCH

The Debug configuration uses two compile-time optimisations that are already wired into the `.jucer` files:

- **Precompiled header (PCH)** — `Source/pch.h` pre-parses `<JuceHeader.h>` once. All Debug TUs load the binary snapshot instead of re-parsing JUCE every time. Halves clean build time (~2m49s → ~1m12s). Active automatically; no per-build action needed.
- **ccache** — compiler output is cached by preprocessed-source hash. Cache hits return the stored `.o` in milliseconds. Wired via `ci/ccache-clang` / `ci/ccache-clang++` wrapper scripts in `customXcodeFlags`.

**One-time machine setup (macOS) — required on a new machine:**
```bash
brew install ccache
# Allow ccache to cache PCH-using translation units:
ccache --set-config sloppiness=pch_defines,time_macros,include_file_mtime,include_file_ctime
```

Without the sloppiness config, every build will report "uncacheable" for all TUs and the cache will never be used.

### Building (macOS example)
```bash
# Resave project and build
~/JUCE/Projucer.app/Contents/MacOS/Projucer --resave osci-render.jucer
cd Builds/osci-render/MacOSX && xcodebuild -configuration Debug
```

### Fast iteration (macOS Standalone Debug)
For quickest local iteration, build just the Standalone app (arm64) in Debug:

```bash
cd /Users/james/osci-render \
    && ~/JUCE/Projucer.app/Contents/MacOS/Projucer --resave osci-render.jucer \
    && cd Builds/osci-render/MacOSX \
    && xcodebuild -project osci-render.xcodeproj \
             -scheme "osci-render - Standalone Plugin" \
             -configuration Debug \
             -arch arm64 \
             build
```

Resulting app:
- `Builds/osci-render/MacOSX/build/Debug/osci-render.app`

### Post-build app relaunch
After a successful macOS standalone build that changes runtime behavior or UI, always relaunch the standalone app as part of task completion.

Use this workflow:
```bash
pkill -x "osci-render" || true
open -n /Users/james/osci-render/Builds/osci-render/MacOSX/build/Debug/osci-render.app
```

Requirements:
- Do this automatically after the build succeeds; do not wait for the user to ask.
- Run it in the background so the editor session does not block.
- Ignore `pkill` failure when no app instance is running.

### Building (Windows)

MSBuild must run from a Visual Studio Developer environment so that `luajit_win.bat` can find the MSVC toolchain. In a plain PowerShell session, import the environment first:

```powershell
# 1. Import Visual Studio 2022 environment variables into PowerShell
$vsPath = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -property installationPath
cmd /c "`"$vsPath\VC\Auxiliary\Build\vcvars64.bat`" >nul 2>&1 && set" |
    ForEach-Object { if ($_ -match '^([^=]+)=(.*)$') {
        [System.Environment]::SetEnvironmentVariable($Matches[1], $Matches[2], 'Process') } }

# 2. Resave the .jucer project (regenerates VS solution/project files)
C:\JUCE\Projucer.exe --resave osci-render.jucer

# 3. Build
MSBuild Builds\osci-render\VisualStudio2022\osci-render.sln /p:Configuration=Debug /p:Platform=x64 /m
```

### Fast iteration (Windows Standalone Debug)

To close any running instance, rebuild, and launch the new build:

```powershell
# Kill any running osci-render instance (ignore errors if not running)
Stop-Process -Name "osci-render" -ErrorAction SilentlyContinue

# Build (assumes vcvars64 already imported in this session)
MSBuild Builds\osci-render\VisualStudio2022\osci-render.sln /p:Configuration=Debug /p:Platform=x64 /m

# Launch the standalone app
Start-Process "Builds\osci-render\VisualStudio2022\x64\Debug\Standalone Plugin\osci-render.exe"
```

Resulting exe:
- `Builds/osci-render/VisualStudio2022/x64/Debug/Standalone Plugin/osci-render.exe`

### VS Code Debugging (Windows)

**Launch configurations** (`.vscode/launch.json`):
- **Debug Standalone** — Runs the "Build Debug" task then launches the Standalone exe under the VS debugger (`cppvsdbg`).

**Build tasks** (`.vscode/tasks.json`):
- **Build Debug** — Default build task (`Ctrl+Shift+B`). Runs MSBuild Debug x64. Requires that the terminal has vcvars64 environment loaded.
- **Build Release** — Same for Release.

> **Note:** The VS Code tasks call `MSBuild` directly without importing vcvars. If MSBuild is not on `PATH`, either run VS Code from a Developer Command Prompt, or add the vcvars import as a pre-task.

### VS Code Debugging (macOS)

The macOS workspace includes VS Code tasks and launch configurations for building and debugging with LLDB. These require the [CodeLLDB](https://marketplace.visualstudio.com/items?itemName=vadimcn.vscode-lldb) extension.

**Launch configurations** (`.vscode/launch.json`):
- **Build & Debug osci-render** — Runs Projucer resave + xcodebuild, then launches the Standalone app under LLDB. Use this for the standard build-and-debug cycle.
- **Debug osci-render (no build)** — Launches the last-built app under LLDB without rebuilding. Useful when you've already built from the terminal.
- **Attach to osci-render** — Attaches the debugger to an already-running osci-render process (waits for launch).
- **Build & Debug sosci** — Same as above but for the sosci product.

**Build tasks** (`.vscode/tasks.json`):
- **Build osci-render Standalone (Debug)** — Default build task (`Cmd+Shift+B`). Runs Projucer resave then xcodebuild.
- **Build sosci Standalone (Debug)** — Same for sosci.

When asked to "build and open" or "build and run" osci-render, prefer using the VS Code task runner (`create_and_run_task` or the terminal) so that any crashes are visible in the Debug Console.

### Configuration
- `.jucer` files define project structure and build settings
- `OSCI_PREMIUM=1/0` preprocessor flag toggles premium features
- Version is in `.jucer` file's `version` attribute

### Version bumping (`bump_version`)

This repo uses a 4-part version string: `A.B.C.D`.

- `A` is treated as a fixed epoch/compat digit (the script never increments it).
- `--major` increments `B` and resets `C.D` to `0.0`.
- `--minor` increments `C` and resets `D` to `0`.
- `--patch` increments `D`.

The script updates and commits version changes for both products across:
- `.jucer` versions: [osci-render.jucer](../osci-render.jucer) and [sosci.jucer](../sosci.jucer)
- Windows installers: `packaging/osci-render.iss` and `packaging/sosci.iss`
- macOS packages: `packaging/osci-render.pkgproj` and `packaging/sosci.pkgproj`

Important behavior:
- Requires a clean working tree (it will refuse to run otherwise)
- Creates a git commit for the bump

Usage examples:
```bash
./bump_version --osci minor --sosci patch
./bump_version --osci patch --sosci none
./bump_version --osci minor --sosci major --message "chore: bump versions"
```

## Code Patterns

### Adding Audio Effects

Effects inherit from `osci::EffectApplication` and implement `build()`:

```cpp
// Source/audio/MyEffect.h
class MyEffect : public osci::EffectApplication {
public:
    osci::Point apply(int index, osci::Point input, osci::Point externalInput, 
                      const std::vector<std::atomic<float>>& values, float sampleRate, float frequency) override {
        float strength = values[0].load();
        // Transform input point
        return transformedPoint;
    }

    std::shared_ptr<osci::Effect> build() const override {
        auto eff = std::make_shared<osci::SimpleEffect>(
            std::make_shared<MyEffect>(),
            new osci::EffectParameter("My Effect", "Description", "myEffectId", 
                                      VERSION_HINT, 1.0, 0.0, 1.0));
        eff->setIcon(BinaryData::my_icon_svg);
        return eff;
    }
};
```

Register in `PluginProcessor.cpp` constructor:
```cpp
toggleableEffects.push_back(MyEffect().build());
```

### Parameters

Use `VERSION_HINT` (currently `2`) for parameter versioning to maintain DAW compatibility:
```cpp
new osci::FloatParameter("Name", "id", VERSION_HINT, defaultVal, min, max);
new osci::BooleanParameter("Name", "id", VERSION_HINT, false, "tooltip");
```

**CRITICAL: ALL plugin state MUST be DAW-automatable parameters.**

Every piece of state that affects audio output or modulation behaviour — including modulation modes, rate modes, tempo divisions, styles, phase offsets, smooth amounts, delay amounts, attack/release times — **must** be stored as an `osci::FloatParameter`, `osci::IntParameter`, or `osci::BooleanParameter` and registered in the processor's `floatParameters`, `intParameters`, or `booleanParameters` vectors.

**Never** store modulation/audio state as raw `std::atomic<float>`, plain `int`, or enum arrays. These are invisible to DAW hosts and cannot be automated, recorded, or modulated externally.

When adding new modulation parameters:
1. Declare as `osci::FloatParameter*` / `osci::IntParameter*` / `osci::BooleanParameter*` in the processor header
2. Create with `new` in the constructor and push to the appropriate vector (`floatParameters`, `intParameters`, or `booleanParameters`)
3. Access on the audio thread via `getValueUnnormalised()` (atomic, lock-free)
4. Set from the UI via `setUnnormalisedValueNotifyingHost()` (notifies the DAW for automation recording)
5. Provide getter/setter methods on the processor for UI components to use

For enum-style parameters, use `osci::IntParameter` with min=0 and max=(numValues-1), and cast to/from the enum type in getter/setter methods.

### Points and Shapes

`osci::Point` represents a sample with position (x,y,z) and color (r,g,b):
```cpp
osci::Point p(x, y, z);           // z is legacy brightness
osci::Point p(x, y, z, r, g, b);  // Full color
```

### Lua Scripts

Lua scripts in `Resources/lua/` receive global variables:
- `slider_a` through `slider_z` - User-controlled sliders
- `sample_rate`, `frequency`, `step`, `phase` - Audio context
- Return `{x, y}` or `{x, y, z}` for output point

### Thread Safety

- Use `juce::SpinLock` for audio-thread-safe data (see `parsersLock` in PluginProcessor)
- `std::atomic` for simple values shared between threads
- Avoid allocations in `processBlock()`

## Testing

Run unit tests via CI script: `./ci/test.sh`

Test files live in `tests/`. Test configuration is defined in `osci-render-test.jucer`.

### Running tests locally

Use `run_tests.sh` for the quickest local iteration:

```bash
# Run all tests
./run_tests.sh

# Run only LFO stress tests
./run_tests.sh --category LFO

# Build and run in Release mode
./run_tests.sh --release
```

### Running via CI

```bash
./ci/test.sh
```

### Plugin Validation (pluginval)

pluginval validates the built VST3 plugin for host compatibility and stability.

**When to run pluginval:**
- In CI: runs automatically after every build for **both osci-render and sosci**, on **all three platforms** (Windows, macOS, Linux)
- Locally: only run when explicitly requested, or after **major changes** — new effects, parameter additions/removals, audio processing changes, UI refactors, JUCE upgrades. Do **not** run automatically after small or incremental changes (typo fixes, minor UI tweaks, comment updates, refactors unlikely to affect plugin host compatibility) as it takes several minutes to build and run.

**Run locally (Windows):**
```bash
ci\pluginval.bat                    # validates osci-render (Debug or Release)
ci\pluginval.bat osci-render 5      # explicit plugin name and strictness
ci\pluginval.bat sosci 5            # validate sosci
```

**Run locally (macOS):**
```bash
ROOT=$(pwd) OS=mac ./ci/pluginval.sh osci-render
ROOT=$(pwd) OS=mac ./ci/pluginval.sh sosci
```

**Run locally (Linux):**
```bash
ROOT=$(pwd) OS=linux ./ci/pluginval.sh osci-render
ROOT=$(pwd) OS=linux ./ci/pluginval.sh sosci
```

**Strictness levels:** 1–10. Level 5 is the recommended minimum for host compatibility. Lower levels check for crashes; higher levels include parameter fuzzing and state restoration.

The pluginval source is at `modules/pluginval` (git submodule). It is built from source via CMake automatically.

### Sanitizer Validation (`run_sanitizers.sh`) — **preferred over plain pluginval**

`run_sanitizers.sh` is the primary local validation tool. It runs pluginval under **ThreadSanitizer (TSan)** and **AddressSanitizer (ASan)**, which catches data races, heap-use-after-free, use-after-scope, and undefined behaviour that plain pluginval never sees. Always prefer this over a bare `ci/pluginval.sh` run when doing any meaningful validation locally.

**When to run it:**
- After any threading, audio processing, or lifecycle changes — it will catch races and memory errors that are invisible in normal Debug builds.
- After adding new effects, parameters, or UI components.
- Whenever you would have reached for `ci/pluginval.sh` locally.
- It is **not** run in CI (CI uses plain pluginval); this is a local-only deep-check tool.

**Usage:**
```bash
# TSan — catches data races
bash run_sanitizers.sh --tsan --plugin osci-render --pluginval --strictness 5
bash run_sanitizers.sh --tsan --plugin sosci --pluginval --strictness 5

# ASan — catches memory errors
bash run_sanitizers.sh --asan --plugin osci-render --pluginval --strictness 5
bash run_sanitizers.sh --asan --plugin sosci --pluginval --strictness 5

# Skip rebuild (re-use last sanitizer build)
bash run_sanitizers.sh --tsan --plugin osci-render --pluginval --no-build
```

**Key flags:**
| Flag | Description |
|------|-------------|
| `--tsan` / `--asan` | Which sanitizer to use (required) |
| `--plugin osci-render\|sosci` | Which product to build and test |
| `--pluginval` | Run pluginval after building |
| `--strictness N` | pluginval strictness level (default 5) |
| `--no-build` | Skip rebuild, reuse last sanitizer build |
| `--tests` | Run unit tests instead of pluginval |

**Output:** Logs are written to `bin/sanitizer-logs/`. The script prints a summary of distinct issues found with file/line references.

## Key Files Reference

| Purpose | File |
|---------|------|
| Main processor | `Source/PluginProcessor.cpp` |
| Effect base | `modules/osci_render_core/effect/osci_Effect.h` |
| Point type | `modules/osci_render_core/shape/osci_Point.h` |
| Visualizer | `Source/visualiser/VisualiserRenderer.cpp` |
| Lua integration | `Source/lua/LuaParser.cpp` |
| UI styling | `Source/LookAndFeel.cpp` |
| Build config | `osci-render.jucer`, `sosci.jucer` |
