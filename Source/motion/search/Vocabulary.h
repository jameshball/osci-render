#pragma once

#include <JuceHeader.h>
#include <utility>
#include <vector>

// The words search knows things by beyond their names, and why things can't
// run: one table per kind of thing, used wherever that thing is indexed.
namespace motion::search::vocabulary {
namespace detail {
using Table = std::vector<std::pair<const char*, juce::StringArray>>;

inline juce::StringArray lookUp(const Table& table, const juce::String& name) {
    for (const auto& [key, words] : table) {
        if (name == key) {
            return words;
        }
    }
    return {};
}
}

// Why a greyed menu item can't run, by its name: the menus only grey it.
// `parentReason`: why the submenu holding it is greyed, when that says
// more than the item would.
inline juce::String reasonFor(const juce::String& title, const juce::String& path, const juce::String& parentReason = {}) {
    if (parentReason.isNotEmpty()) {
        return parentReason;
    }
    static const std::vector<std::pair<juce::StringArray, const char*>> reasons {
        {{"Cut", "Copy"}, "Select clips or keys first"},
        {{"Delete"}, "Select clips, keys, tracks or a camera first"},
        {{"Paste"}, "Copy something first"},
        {{"Split at playhead"}, "Select a clip under the playhead"},
        {{"Duplicate"}, "Select clips first"},
        {{"Group tracks"}, "Select tracks or clips first"},
        {{"Move start to playhead", "Move end to playhead", "Trim start to playhead", "Trim end to playhead", "Show key lanes"}, "Select a clip first"},
        {{"Detect tempo from audio"}, "Add a soundtrack first"},
        {{"Centre anchor", "Show in timeline"}, "Select something first"},
        {{"Clear recent projects"}, "No recent projects to clear"},
    };
    if (title.startsWith("Undo")) {
        return "Nothing to undo";
    }
    if (title.startsWith("Redo")) {
        return "Nothing to redo";
    }
    if (title.startsWith("Keyframe ")) {
        return "Select something that has it";
    }
    if (title.startsWith("Easy ease")) {
        return "Select keys first";
    }
    if (title.startsWith("Export")) {
        return title.containsIgnoreCase("sound") ? "Add a soundtrack first" : "Wait for the export under way";
    }
    if (path.contains("Beat grid") || title.startsWith("Beat grid")) {
        return "Show time in bars and beats first";
    }
    for (const auto& [names, reason] : reasons) {
        if (names.contains(title)) {
            return reason;
        }
    }
    return "Not available right now";
}

// Why a submenu is greyed, for what it holds, or empty when each item
// says why for itself.
inline juce::String reasonForSubmenu(const juce::String& name) {
    if (name == "Export audio") {
        return "Wait for the export under way";
    }
    if (name == "Easy ease") {
        return "Select keys first";
    }
    if (name == "Beat grid") {
        return "Show time in bars and beats first";
    }
    return {};
}

// More words for a menu command than its name holds.
inline juce::StringArray menuKeywords(const juce::String& title) {
    static const detail::Table words {
        {"Split at playhead", {"razor", "cut clip", "add edit"}},
        {"Make composition", {"precompose", "nest", "group clips"}},
        {"Show key lanes", {"keyframes", "animation lanes"}},
        {"Audio settings...", {"device", "sound card", "buffer", "latency", "midi", "preferences"}},
        {"Export video...", {"render", "mp4", "mov", "movie", "save video", "codec", "prores", "quality", "transparent background", "alpha", "1080p", "4k"}},
        {"Detect tempo from audio", {"bpm", "beat detection"}},
        {"Snap to grid", {"snapping", "magnet", "quantise", "quantize", "beat sync"}},
        {"Follow playhead", {"scroll", "auto scroll"}},
        {"Loop playback", {"repeat", "cycle"}},
        {"Play / pause", {"stop", "pause", "start playback", "space"}},
        {"Keyboard shortcuts...", {"hotkeys", "keys", "cheat sheet"}},
        {"Start screen...", {"welcome", "home", "recent projects"}},
        {"New project", {"blank", "empty"}},
        {"Import source...", {"open file", "load", "svg", "obj", "image", "video", "audio", "add file", "add music", "soundtrack"}},
        {"Draw a shape...", {"pen", "pencil", "freehand", "sketch"}},
        {"Write a script...", {"lua", "code", "program"}},
        {"Add camera from this view", {"new camera", "shot"}},
        {"Pick parts", {"edit mode", "faces", "components"}},
        {"Frame selection", {"zoom to fit", "fit view", "focus view"}},
        {"Reset view", {"home view", "default view"}},
        {"Add marker...", {"bookmark", "cue", "flag"}},
        {"Add tempo change...", {"bpm change", "tempo map"}},
        {"License and updates...", {"activate", "licence", "update", "version"}},
        {"Show log file", {"debug", "logs", "crash"}},
        {"Show Graph", {"graph editor", "curve editor", "curves", "f-curve"}},
        {"Show timeline", {"dope sheet", "sequencer"}},
        {"Select all", {"everything"}},
        {"Delete", {"delete clip", "delete track", "delete key", "remove"}},
        {"Duplicate", {"duplicate clip", "copy clip"}},
    };
    if (title.startsWith("Export beam")) {
        // The five channels (and their brightness) are what a laser's DAC plays.
        juce::StringArray words {"wav", "oscilloscope audio", "render audio"};
        if (title.contains("XYRGB")) {
            words.addArray({"laser", "ilda", "dac", "rgb laser"});
        }
        return words;
    }
    if (title.startsWith("Export sound")) {
        return {"wav", "render audio", "soundtrack", "mixdown"};
    }
    if (title.startsWith("Keyframe ")) {
        return {"add key", "set key", "keyframe", "key it"};
    }
    if (title.startsWith("Interface size")) {
        return {"zoom", "ui scale", "bigger text", "smaller text", "text size"};
    }
    if (title.startsWith("Zoom")) {
        return {"timeline", "magnify"};
    }
    return detail::lookUp(words, title);
}

// A little extra for what most people come for, so it leads its matches.
inline int relevanceFor(const juce::String& title) {
    static const juce::StringArray common {"Export video...", "Save project", "Open project...", "New project", "Import source...", "Undo", "Redo",
        "Split at playhead", "Play / pause", "Duplicate", "Delete", "Audio settings..."};
    return common.contains(title) || title.startsWith("Undo ") || title.startsWith("Redo ") ? 6 : 0;
}

// A generator's other names: a cube is a Box.
inline juce::StringArray generatorKeywords(const juce::String& name) {
    static const detail::Table words {
        {"Rectangle", {"square", "box", "frame", "border"}},
        {"Ellipse", {"circle", "oval", "round", "dot"}},
        {"Polygon", {"triangle", "pentagon", "hexagon", "octagon", "shape"}},
        {"Star", {"asterisk", "sparkle"}},
        {"Ring", {"donut", "circle", "annulus"}},
        {"Arc", {"curve", "semicircle", "bow"}},
        {"Lissajous", {"xy figure", "harmonograph", "sine figure"}},
        {"Spirograph", {"hypotrochoid", "epicycle", "flower"}},
        {"Rose", {"flower", "petals", "rhodonea"}},
        {"Wave", {"sine", "sine wave", "squiggle", "line"}},
        {"Grid", {"lines", "mesh", "graph paper"}},
        {"Polar grid", {"radar", "target", "circles"}},
        {"Hex grid", {"honeycomb", "hexagons"}},
        {"Box", {"cube", "block", "3d"}},
        {"Sphere", {"ball", "globe", "planet", "3d"}},
        {"Torus", {"donut", "ring", "3d"}},
        {"Cylinder", {"tube", "pipe", "can", "3d"}},
        {"Cone", {"pyramid", "3d"}},
        {"Point light", {"lamp", "bulb", "light"}},
        {"Spot light", {"spotlight", "torch", "light"}},
        {"Directional light", {"sun", "sunlight", "light"}},
        {"Ambient light", {"fill light", "light"}},
    };
    return detail::lookUp(words, name);
}

// A property group's other words, for every object that has it.
inline juce::StringArray propertyKeywords(const juce::String& group) {
    static const detail::Table words {
        {"Position", {"move", "location", "place", "translate", "x y z", "offset"}},
        {"Rotation", {"spin", "turn", "angle", "rotate", "tilt", "orientation"}},
        {"Scale", {"size", "resize", "bigger", "smaller", "flip", "stretch", "squash"}},
        {"Anchor", {"pivot", "origin", "centre"}},
        {"Colour", {"color", "hue", "tint", "rgb", "red", "green", "blue"}},
        {"Opacity", {"fade", "fade in", "fade out", "transparency", "transparent", "visible", "visibility", "alpha", "see through"}},
        {"Beam share", {"beam time", "brightness", "weight", "dwell", "flicker"}},
        {"Lit by lights", {"lighting", "shading", "shade"}},
        {"Mirror", {"reflect", "reflection", "reflective"}},
        {"Solid", {"hidden lines", "hidden line removal", "occlusion", "filled", "opaque"}},
        {"Noise", {"jitter", "random", "rough"}},
        {"Fill detail", {"density", "fill"}},
        {"Gain", {"volume", "loudness", "level", "louder", "quieter"}},
        {"Pan", {"balance", "left", "right", "stereo"}},
        {"Depth", {"iterations", "recursion", "levels"}},
        {"Field of view", {"fov", "zoom", "lens", "focal length", "wide angle", "telephoto"}},
        {"Hatching", {"cross hatch", "shading", "pen shading"}},
    };
    return detail::lookUp(words, group);
}

// The Scope's picture settings, by the words people use for how it looks.
inline juce::StringArray scopeKeywords(const juce::String& label) {
    static const detail::Table words {
        {"Intensity", {"brightness", "brighter", "bright", "dim", "exposure"}},
        {"Focus", {"blur", "blurry", "sharp", "sharpness", "thick", "thicker", "thickness", "thin", "line width", "soft"}},
        {"Persistence", {"trails", "trail", "phosphor", "decay", "afterimage", "longer trails", "motion blur"}},
        {"Afterglow", {"trails", "glow tail", "decay"}},
        {"Glow", {"bloom", "halo", "brighter"}},
        {"Hue", {"colour", "color", "tint", "beam colour"}},
        {"Saturation", {"colour", "vivid", "grey", "washed out"}},
        {"Ambient", {"background", "room light", "screen colour"}},
        {"Smoothing", {"smooth", "smoother lines", "filter"}},
    };
    auto found = detail::lookUp(words, label);
    found.add("scope");
    return found;
}

// An effect's other names, by its id: what osci-render's effects went by,
// and what other apps call it.
inline juce::StringArray effectAliases(const juce::String& id) {
    static const detail::Table words {
        {"polygon", {"polygonizer"}},
        {"bitCrush", {"bitcrush", "lo-fi", "pixelate"}},
        {"spiralCrush", {"spiral bit crush"}},
        {"colour", {"hue shift", "tint"}},
        {"ripple", {"distort", "water"}},
        {"swirl", {"distort", "twirl"}},
        {"bulge", {"distort", "fisheye", "pinch"}},
        {"vortex", {"distort", "whirl"}},
        {"twist", {"distort"}},
        {"skew", {"distort", "shear", "slant"}},
    };
    return detail::lookUp(words, id);
}

// Modulator kinds, in words people use for them.
inline juce::StringArray modulatorKeywords(const juce::String& kind) {
    static const detail::Table words {
        {"Audio loudness", {"audio reactive", "sidechain", "envelope follower", "react to music", "beat", "beat sync", "pulse to music"}},
        {"Smooth random", {"wiggle", "drift", "noise", "shake", "camera shake", "sway", "handheld"}},
        {"Random steps", {"sample and hold", "random", "jitter"}},
        {"Saw", {"ramp", "sawtooth", "spin forever", "constant rotation"}},
        {"Square", {"pulse", "gate", "flash", "blink"}},
        {"Sine", {"wave", "oscillate", "bounce", "breathe"}},
        {"Triangle", {"ping pong", "back and forth"}},
    };
    auto found = detail::lookUp(words, kind);
    found.addArray({"modulator", "lfo"});
    return found;
}
}
