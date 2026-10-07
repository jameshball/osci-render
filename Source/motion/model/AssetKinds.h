#pragma once

#include "../../parser/FileFormatRegistry.h"

namespace motion {
// What one source may hold.
inline constexpr std::size_t maximumSourceBytes = 64 * 1024 * 1024;
inline constexpr std::size_t maximumSourceFrames = 3600;
inline constexpr std::size_t maximumShapesPerFrame = 100000;
inline constexpr std::size_t maximumSourceShapes = 1000000;
}
