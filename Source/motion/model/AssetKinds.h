#pragma once

#include "../../parser/FileFormatRegistry.h"

namespace motion {
// What one source may hold.
inline constexpr std::size_t maximumSourceBytes = 64 * 1024 * 1024;
inline constexpr std::size_t maximumSourceFrames = 3600;
inline constexpr std::size_t maximumShapesPerFrame = 100000;
inline constexpr std::size_t maximumSourceShapes = 1000000;
// Sound and video larger than this are linked where they are on disk rather
// than held in the project, so a long recording costs the project file (and
// every save of it) nothing; smaller ones travel inside it. Linked, a file
// may be as large as decoding it allows.
inline constexpr std::size_t linkSourcesAbove = 16 * 1024 * 1024;
inline bool linksFromDisk(const juce::File& file) {
    const auto extension = file.getFileExtension();
    return (osci::files::isAudio(extension) || osci::files::isVideo(extension)) && file.getSize() > static_cast<juce::int64>(linkSourcesAbove);
}
}
