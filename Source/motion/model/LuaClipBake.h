#pragma once

#include "PreparedSource.h"
#include <JuceHeader.h>
#include <string>

namespace motion {
// One Lua clip's frames with its animated sliders baked in. Immutable; shared
// by undo snapshots. The archive is saved with the project so loading never
// executes scripts; `key` identifies the script, bake settings, slider curves
// and bake length that produced it.
struct LuaClipBake {
    std::string key;
    std::shared_ptr<const PreparedSource> source;
    juce::MemoryBlock archive;
};
}
