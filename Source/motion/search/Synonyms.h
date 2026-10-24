#pragma once

#include <JuceHeader.h>
#include <initializer_list>
#include <vector>

namespace motion::search {
// Words people use for the same thing, so a search for one finds the others:
// spellings (colour, color), other apps' names (precompose, bounce) and plain
// words for the app's terms (trail for persistence).
inline const std::vector<juce::StringArray>& synonymGroups() {
    static const std::vector<juce::StringArray> groups = [] {
        std::vector<juce::StringArray> made;
        for (const auto group : std::initializer_list<std::initializer_list<const char*>> {
                 // Spellings
                 {"colour", "color", "hue", "tint", "rgb"},
                 {"grey", "gray"},
                 {"centre", "center", "middle"},
                 {"normalise", "normalize"},
                 {"visualise", "visualize", "visualiser", "visualizer"},
                 {"organise", "organize", "arrange"},
                 {"behaviour", "behavior"},
                 {"favourite", "favorite"},
                 {"licence", "license", "activate"},
                 {"maximise", "maximize"},
                 {"synchronise", "synchronize", "sync"},
                 // Doing things
                 {"delete", "remove", "erase", "trash"},
                 {"duplicate", "clone"},
                 {"export", "render", "bounce", "encode", "mixdown"},
                 {"import", "load", "insert"},
                 {"new", "create", "blank", "fresh"},
                 {"open", "browse"},
                 {"save", "store", "keep"},
                 {"undo", "revert", "go back", "history"},
                 {"rename", "label"},
                 {"split", "cut", "razor", "blade"},
                 {"trim", "crop", "shorten"},
                 {"stretch", "time stretch", "retime"},
                 {"slip", "offset", "nudge"},
                 {"snap", "magnet", "align"},
                 {"select", "pick", "choose", "highlight"},
                 {"group", "folder", "collect"},
                 {"ungroup", "flatten"},
                 {"make composition", "precompose", "precomp", "nest"},
                 {"record", "capture", "arm"},
                 {"play", "playback"},
                 {"loop", "repeat"},
                 {"speed", "faster", "slower", "fast forward"},
                 // Transforms
                 {"move", "translate", "position", "location"},
                 {"rotate", "rotation", "spin", "turn", "angle"},
                 {"scale", "resize", "grow", "shrink", "bigger", "smaller"},
                 {"anchor", "pivot", "origin"},
                 {"mirror", "reflect", "reflection"},
                 // Looks
                 {"opacity", "transparency", "alpha", "fade"},
                 {"brightness", "intensity", "bright", "luminance"},
                 {"glow", "bloom", "halo"},
                 {"blur", "soften", "sharpness", "defocus"},
                 {"persistence", "trail", "phosphor", "decay"},
                 {"thickness", "weight", "stroke"},
                 {"surface", "solid", "wireframe", "shading"},
                 {"hatching", "hatch", "crosshatch"},
                 {"hidden lines", "occlusion", "backface", "culling"},
                 {"noise", "random", "jitter", "grain"},
                 {"wobble", "wiggle", "shake"},
                 // Things
                 {"keyframe", "key", "keys", "animate", "animation", "automation"},
                 {"modulator", "modulation", "lfo", "oscillator", "envelope", "automate"},
                 {"effect", "effects", "filter", "fx", "plugin"},
                 {"text", "font", "words", "letters", "typography", "title"},
                 {"shape", "primitive", "polygon", "geometry"},
                 {"circle", "ellipse", "oval", "ring"},
                 {"rectangle", "box"},
                 {"camera", "viewpoint", "eye", "perspective"},
                 {"light", "lamp", "lighting", "illumination"},
                 {"audio", "sound", "music", "soundtrack", "song"},
                 {"output", "device", "routing", "channels", "speakers", "sound card"},
                 {"sample rate", "samples", "khz"},
                 {"beam", "trace", "electron", "spot"},
                 {"frequency", "hz", "refresh"},
                 {"tempo", "bpm", "beats", "metronome"},
                 {"meter", "time signature", "bars"},
                 {"marker", "bookmark", "cue", "flag", "locator"},
                 {"track", "layer", "lane"},
                 {"clip", "region", "segment"},
                 {"timeline", "sequence", "arrangement"},
                 {"graph", "curve", "curves", "easing", "interpolation", "bezier"},
                 {"ease", "easing", "smooth", "smoothing"},
                 {"canvas", "aspect", "frame size", "dimensions", "aspect ratio"},
                 {"scope", "oscilloscope", "monitor"},
                 {"scene", "viewport", "3d view", "stage"},
                 {"lua", "script", "code", "program", "expression"},
                 {"svg", "vector", "illustrator", "logo"},
                 {"image", "picture", "photo", "png", "jpg", "bitmap", "raster"},
                 {"video", "movie", "footage", "mp4", "mov"},
                 {"blender", "3d", "mesh", "obj", "model"},
                 {"drawing", "draw", "sketch", "pen", "pencil", "freehand"},
                 {"live", "syphon", "spout", "stream", "share"},
                 {"fractal", "l-system", "lsystem", "recursive"},
                 {"gain", "volume", "level", "loudness"},
                 {"distort", "distortion", "warp", "deform"},
                 {"pan", "balance"},
                 {"background", "backdrop", "backplate"},
                 {"reverse", "backwards", "rewind", "play backwards"},
                 {"mute", "silence"},
                 {"solo", "isolate"},
                 {"lock", "protect"},
                 {"laser", "ilda", "galvo"},
                 {"field of view", "fov", "lens", "focal length", "zoom lens"},
                 // The app
                 {"settings", "preferences", "options", "config", "configure"},
                 {"shortcut", "shortcuts", "hotkey", "keybinding", "keyboard"},
                 {"help", "docs", "manual", "tutorial", "guide"},
                 {"full screen", "fullscreen", "presentation", "maximise"},
                 {"zoom", "magnify", "enlarge"},
                 {"example", "examples", "template", "demo", "sample project"},
                 {"project", "file", "document", "session"},
                 {"panel", "pane", "window", "tab", "sidebar"},
             }) {
            made.emplace_back(group);
        }
        return made;
    }();
    return groups;
}

// The other words to try for one typed `word`: the rest of every group with
// a member that is the word, or starts with it once three letters are typed.
inline juce::StringArray synonymsOf(const juce::String& word) {
    const auto lower = word.toLowerCase();
    juce::StringArray found;
    if (lower.length() < 2) {
        return found;
    }
    for (const auto& group : synonymGroups()) {
        const auto member = std::any_of(group.begin(), group.end(), [&](const juce::String& candidate) {
            return candidate == lower || (lower.length() >= 3 && candidate.startsWith(lower));
        });
        if (!member) {
            continue;
        }
        for (const auto& candidate : group) {
            if (candidate != lower && !candidate.startsWith(lower)) {
                found.addIfNotAlreadyThere(candidate);
            }
        }
    }
    return found;
}
}
