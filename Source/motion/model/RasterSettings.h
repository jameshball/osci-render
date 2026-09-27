#pragma once

#include "PreparedPointFrames.h"

namespace motion {
struct RasterSettings {
    enum class Mode { contours, scanlines };
    Mode mode = Mode::contours;
    double threshold = 0.02;
    bool invert = false;
    int resolution = 256;
    double videoFrameRate = 30;
    std::size_t pointsPerFrame = 4096;

    std::string validate() const {
        if (!std::isfinite(videoFrameRate) || videoFrameRate < 1 || videoFrameRate > 120) { return "Video preparation rate must be between 1 and 120 FPS."; }
        if (mode != Mode::contours && mode != Mode::scanlines) { return "Choose outlines or scanlines."; }
        if (!std::isfinite(threshold) || threshold < 0 || threshold > 1) { return "Image threshold must be between 0 and 1."; }
        if (resolution < 16 || resolution > 512) { return "Image tracing resolution must be between 16 and 512 pixels."; }
        if (pointsPerFrame < PreparedPointFrames::minimumPointsPerFrame || pointsPerFrame > PreparedPointFrames::maximumPointsPerFrame) {
            return "Each image frame requires 16-16384 beam samples.";
        }
        return {};
    }
};
}
