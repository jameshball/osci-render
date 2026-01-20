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

### Configuration
- `.jucer` files define project structure and build settings
- `OSCI_PREMIUM=1/0` preprocessor flag toggles premium features
- Version is in `.jucer` file's `version` attribute

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

Run via CI script: `./ci/test.sh`

Test configuration defined in `osci-render-test.jucer`.

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
