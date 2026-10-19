#pragma once

#include <string>

namespace motion {
// Why a capture of a live source (Blender's, or a picture another app shares)
// stopped by itself: its sender went away, or it holds all it can. It keeps
// what came before and is saved as if stopped there.
enum class CaptureEnding { none, disconnected, duration, updates, size };

// What to say once it's saved; `source` is the live source's name.
inline std::string captureEndedBecause(CaptureEnding ending, const std::string& source) {
    switch (ending) {
        case CaptureEnding::none: return {};
        case CaptureEnding::disconnected: return source + " stopped sending, so its capture ended there.";
        case CaptureEnding::duration: return "The capture reached its ten-minute limit, so it ended there.";
        case CaptureEnding::updates: return "The capture reached the most updates it can hold, so it ended there.";
        case CaptureEnding::size: return "The capture reached the most drawing it can hold, so it ended there.";
    }
    return {};
}
}
