#pragma once

#include "../../parser/FileFormatRegistry.h"

namespace motion {
// Motion imports every source osci-render opens, and MIDI files.
inline bool isMidiSource(const juce::String& extension) {
    const auto normalised = osci::files::normaliseExtension(extension);
    return normalised == "mid" || normalised == "midi";
}
inline bool isImportableSource(const juce::String& extension) { return osci::files::isSupportedSource(extension) || isMidiSource(extension); }
inline juce::String importWildcard() { return osci::files::sourceWildcard() + ";*.mid;*.midi"; }

// What one source may hold.
inline constexpr std::size_t maximumSourceBytes = 64 * 1024 * 1024;
inline constexpr std::size_t maximumSourceFrames = 3600;
inline constexpr std::size_t maximumShapesPerFrame = 100000;
inline constexpr std::size_t maximumSourceShapes = 1000000;
}
