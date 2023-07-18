# Osci-render 2.0

This is a complete rewrite of [osci-render](https://github.com/jameshball/osci-render) in C++ using the JUCE framework. This will enable a plethora of improvements, such as:

- Massively improved audio stability
- Better performance
- Support as a VST/AU audio plugin that can be controlled using a DAW
- Significantly lower latency
- In-app software oscilloscope
- More maintainable codebase allowing more features to be added

## Screenshots

Please note that the current GUI has not been changed from the default colours - it is currently very ugly and subject to change!

### Opening and editing Lua files

![image](https://github.com/jameshball/osci-render-juce/assets/38670946/2f373833-a4ad-4be3-8cef-8d0745a39a35)

### Changing the order of audio effects

![image](https://github.com/jameshball/osci-render-juce/assets/38670946/7860dee3-4793-4e4b-954c-4e6fbe277066)

### Osci-render 2.0 being used as a VST

![Screenshot_1](https://github.com/jameshball/osci-render-juce/assets/38670946/b98108c6-d78d-448e-9209-c15dba757a15)

## Current status

Osci-render 2.0 is currently in pre-alpha and has no formal support or public release to download. There is currently no planned release date.

You can track the most up-to-date progress [here](https://github.com/users/jameshball/projects/2), but in summary:

### Implemented features

- Support for .obj
- Support for .txt
- Support for .svg
- Support for .lua
- Most audio effects implemented
- Audio effects are reorderable
- Infinite Lua sliders can be added for more control
- Text editor for the current file
- Smooth changing of effect values (preventing harsh clicks!)
- Changing the range of sliders
- Basic in-app software oscilloscope
- Support for the existing [web-based oscilloscope](https://james.ball.sh/oscilloscope)
- Support for audio-plugin parameters being controlled from a DAW and vice versa
- Volume visualiser

### Major features still TODO

- Massive visual overhaul to make it look nice!!
- Saving to a .osci project file
- Project select screen
- Blender integration
- MIDI support
- 3D perspective effect
- Translation effect
- Improved algorithm for finding the best path to render 3D object
