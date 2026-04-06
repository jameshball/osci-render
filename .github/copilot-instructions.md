# osci-render Development Guide

## Communication

- **NEVER ask clarifying questions inline in chat.** Always use the `vscode_askQuestions` tool for structured UI with selectable options.
- After each completed task, **YOU MUST** ask the user if the changes are satisfactory or if further modifications are needed using the `vscode_askQuestions` tool. Do not assume completion until the user confirms.

## Subagents

**Multiple independent subagents MUST be invoked in the same `<function_calls>` block to execute in parallel.** Separate blocks run sequentially.

## Code Reviews

1. Review all unstaged changes (quality, duplication, structure, performance, bugs).
2. List every issue numbered with file/line.
3. For each issue, use `vscode_askQuestions` to ask: implement the fix or skip.
4. Implement all accepted fixes, then build and verify.

## Guidelines

- **Backwards compatibility**: Always ask before adding it. Do not assume it is needed.
- **Recommendations/proposals**: Always first write your findings, and then for each finding, present it as a numbered question using `vscode_askQuestions` so the user can approve/reject individually.

## Project Overview

JUCE-based audio plugin (VST3/AU/Standalone) that renders 2D/3D graphics to audio for oscilloscope displays. Supports `.obj`, `.svg`, `.txt`, `.lua`, `.gpla`, `.gif`, and Blender scenes with real-time effects and DAW automation.

**Two products:** `osci-render` (full synthesizer) and `sosci` (visualizer-only).

## Architecture

### Source Layout

- `CommonPluginProcessor` → `OscirenderAudioProcessor` / `SosciAudioProcessor`
- `CommonPluginEditor` → `OscirenderAudioProcessorEditor`
- `Source/audio/effects/` — 30+ effect implementations
- `Source/audio/modulation/` — LFO, Envelope, Random, Sidechain parameters
- `Source/audio/synth/` — ShapeVoice, ShapeSound (polyphonic synth)
- `Source/visualiser/` — OpenGL oscilloscope renderer
- `Source/parser/` — File parsing (svg/, txt/, fractal/, gpla/, img/)
- `Source/lua/` — LuaJIT scripting integration
- `Source/obj/` — OBJ/Blender 3D object loading
- `Source/components/` — UI components
- `Source/video/` — FFmpeg encoding, Syphon/Spout

### Key Modules (`modules/`)

- **osci_render_core** — `osci::Point`, `osci::Shape`, `osci::Effect`, `osci::EffectParameter`, `osci::FloatParameter`, `osci::BooleanParameter`, `osci::IntParameter`
- **LuaJIT**, **chowdsp_utils**, **juce_sharedtexture**, **pluginval**, **Mathter**, **melatonin_blur**, **tinyobjloader**

### Data Flow

1. `FileParser` → parser → `osci::Shape` vector
2. `ShapeVoice` renders shapes to audio via `ShapeSound`
3. `osci::Effect` chain processes `osci::Point` samples
4. `VisualiserRenderer` renders audio buffer via OpenGL

### Parameter System (no APVTS)

All parameters inherit `juce::AudioProcessorParameterWithID` directly (defined in `osci_EffectParameter.h`). No `AudioProcessorValueTreeState` is used. Parameters are registered via `addAllParameters()` which iterates effects and standalone parameter vectors.

**All audio/modulation state MUST be DAW-automatable parameters** (`osci::FloatParameter`, `osci::IntParameter`, or `osci::BooleanParameter`). Never use raw `std::atomic` or plain `int` for state that affects audio output.

When adding parameters: declare as pointer in processor header, create with `new` in constructor, push to `floatParameters`/`intParameters`/`booleanParameters`. Use `VERSION_HINT` (currently `2`) for DAW compatibility. Access via `getValueUnnormalised()` on audio thread, `setUnnormalisedValueNotifyingHost()` from UI.

### State Serialization

State is saved as binary-encoded XML (magic `0x21324356`). Each parameter/effect implements `save(XmlElement*)` / `load(XmlElement*)`. Files are stored as base64 blobs. Modulation assignments serialized separately per source type.

## Build System

### Prerequisites
- JUCE framework with Projucer
- Xcode (macOS), Visual Studio 2022 (Windows), or GCC (Linux)
- LuaJIT (built automatically via `luajit_linux_macos.sh` or `luajit_win.bat`)
- ccache (macOS) — see `.github/docs/build-details.md`

### Building & Fast Iteration (macOS)

```bash
cd /Users/james/osci-render \
    && ~/JUCE/Projucer.app/Contents/MacOS/Projucer --resave osci-render.jucer \
    && cd Builds/osci-render/MacOSX \
    && xcodebuild -project osci-render.xcodeproj \
             -scheme "osci-render - Standalone Plugin" \
             -configuration Debug -arch arm64 build
```

### Post-build Relaunch
After a successful macOS standalone build that changes runtime behavior or UI, automatically relaunch:
```bash
pkill -x "osci-render" || true
open -n /Users/james/osci-render/Builds/osci-render/MacOSX/build/Debug/osci-render.app
```

### Windows / ccache / PCH
See `.github/docs/build-details.md`.

### Configuration
- `.jucer` files define project structure and build settings
- `OSCI_PREMIUM=1/0` preprocessor flag toggles premium features

### Version Bumping
4-part version `A.B.C.D`. `--major` bumps B, `--minor` bumps C, `--patch` bumps D. Updates both `.jucer` files, `packaging/*.iss` and `packaging/*.pkgproj`. Requires clean working tree.
```bash
./bump_version --osci minor --sosci patch
```

### Thread Safety & Realtime Safety
**All code on the audio thread MUST be realtime-safe.** This means:
- **No mutex/CriticalSection locking** — use `juce::SpinLock` only
- **No heap allocation or deallocation** (no `new`, `delete`, `std::vector::push_back` that grows, `std::string` construction)
- **No blocking I/O** (no file access, no logging, no system calls)
- `std::atomic` for simple cross-thread values
- Pre-allocate containers in constructors with `reserve()` to avoid audio-thread allocations

## Testing

```bash
./run_tests.sh                    # all tests
./run_tests.sh --category LFO    # specific category
./run_tests.sh --release          # Release mode
```

Tests live in `tests/`, configured by `osci-render-test.jucer`.

### Validation

**pluginval** — run locally only after major changes (effects, parameters, audio processing). Runs in CI automatically.
```bash
./run_tests.sh --pluginval
./run_tests.sh --pluginval --plugin sosci
```

**Sanitizers (preferred)** — TSan/ASan via pluginval or unit tests. Catches races and memory errors invisible in Debug builds.
```bash
./run_tests.sh --tsan --pluginval                # TSan + pluginval
./run_tests.sh --asan --pluginval                # ASan + pluginval
./run_tests.sh --tsan                            # TSan + unit tests
./run_tests.sh --tsan --pluginval --tests        # TSan + both
```
