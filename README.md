# Osci-render 2.0

This is a complete rewrite of [osci-render](https://github.com/jameshball/osci-render) in C++ using the JUCE framework. This will enable a plethora of improvements, such as:

- Massively improved audio stability
- Better performance
- Support as a VST/AU audio plugin that can be controlled using a DAW
- Significantly lower latency
- In-app software oscilloscope
- More maintainable codebase allowing more features to be added

## Screenshots

### Opening and editing Lua files

![image](https://github.com/jameshball/osci-render-juce/assets/38670946/5b240357-5e23-4831-8556-63d10b512c9b)

### Changing the order of audio effects

![image](https://github.com/jameshball/osci-render-juce/assets/38670946/d8a56c41-6d7a-439a-86f4-b7e872ad9476)

### Osci-render 2.0 being used as a VST

![image](https://github.com/jameshball/osci-render-juce/assets/38670946/91350b94-4563-4ada-9aac-b40978b59fc6)

## Current status

Osci-render 2.0 is currently in pre-alpha and has no formal support or public release to download. There is currently no planned release date.

You can track the most up-to-date progress [here](https://github.com/users/jameshball/projects/2), but in summary:

### Implemented features

- Support for .obj
- Support for .txt
- Support for .svg
- Support for .lua
- All audio effects implemented
- Audio effects are reorderable
- Many more Lua sliders supported for more control
- Text editor for the current file
- Smooth changing of effect values (preventing harsh clicks!)
- Changing the range of sliders
- Basic in-app software oscilloscope
- Support for the existing [web-based oscilloscope](https://james.ball.sh/oscilloscope)
- Support for audio-plugin parameters being controlled from a DAW and vice versa
- Volume visualiser
- Saving to a .osci project file, including support for legacy osci-render projects

### Major features still TODO

- Project select screen
- Blender integration
- MIDI support
- Improved algorithm for finding the best path to render 3D object
