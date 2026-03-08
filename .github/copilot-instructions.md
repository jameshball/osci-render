# osci-render Development Guide

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
