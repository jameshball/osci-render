#pragma once

#include "PreparedPointFrames.h"

namespace motion {
struct RasterSettings {
    // Outlines round what is lit, horizontal passes over it, or a Hilbert
    // curve over it, as dense as the picture is bright.
    enum class Mode { contours, scanlines, hilbert };
    Mode mode = Mode::contours;
    // How a Hilbert curve shows how bright the picture is: by how dense it
    // is, for a scope with X and Y alone; or by its colour, the curve even
    // over all that is lit, for one with brightness (XYRGB).
    enum class Shading { density, colour };
    Shading shading = Shading::density;
    double threshold = 0.02;
    bool invert = false;
    int resolution = 256;
    double videoFrameRate = 30;
    std::size_t pointsPerFrame = 4096;

    std::string validate() const {
        if (!std::isfinite(videoFrameRate) || videoFrameRate < 1 || videoFrameRate > 120) { return "Video preparation rate must be between 1 and 120 FPS."; }
        if (mode != Mode::contours && mode != Mode::scanlines && mode != Mode::hilbert) { return "Choose outlines, scanlines or Hilbert."; }
        if (shading != Shading::density && shading != Shading::colour) { return "Choose density or colour shading."; }
        if (!std::isfinite(threshold) || threshold < 0 || threshold > 1) { return "Image threshold must be between 0 and 1."; }
        if (resolution < 16 || resolution > 512) { return "Image tracing resolution must be between 16 and 512 pixels."; }
        if (pointsPerFrame < PreparedPointFrames::minimumPointsPerFrame || pointsPerFrame > PreparedPointFrames::maximumPointsPerFrame) {
            return "Each image frame requires 16-16384 beam samples.";
        }
        return {};
    }
};
}
