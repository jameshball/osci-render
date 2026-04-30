#pragma once

#include <JuceHeader.h>

#include <vector>

#include "VisualiserParameters.h"

class VisualiserSvgExporter {
public:
    enum class RenderMode : int {
        XY = 1,
        XYZ = 2,
        XYRGB = 3,
    };

    struct FrameData {
        const std::vector<float>* xPoints = nullptr;
        const std::vector<float>* yPoints = nullptr;
        const std::vector<float>* zPoints = nullptr;
        const std::vector<float>* rPoints = nullptr;
        const std::vector<float>* gPoints = nullptr;
        const std::vector<float>* bPoints = nullptr;
        RenderMode mode = RenderMode::XY;
        bool usesUpsampledSamples = false;
        double frameRate = 60.0;
        double sampleRate = 44100.0;
        double resampleRatio = 1.0;
    };

    static juce::String createSvg(const FrameData& frameData, VisualiserParameters& parameters);
};
