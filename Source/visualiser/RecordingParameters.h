#pragma once

#include <osci_render_core/osci_render_core.h>
#include <osci_gui/visualiser/osci_VisualiserGeometry.h>
#include "../video/VideoEncodingConstants.h"

class RecordingParameters {
public:
    RecordingParameters();

private:

#if OSCI_PREMIUM
    const bool sosciFeatures = true;
#else
    const bool sosciFeatures = false;
#endif

public:

    osci::EffectParameter qualityParameter = osci::EffectParameter(
        "Video Quality",
        "Controls the quality of the recording video. 0 is the worst possible quality, and 1 is almost lossless.",
        "brightness",
        VERSION_HINT, 0.7, 0.0, 1.0
    );
    osci::BooleanParameter losslessAudio = osci::BooleanParameter("Lossless Audio", "losslessAudio", VERSION_HINT, false, "Record audio in a lossless format.");
    osci::BooleanParameter losslessVideo = osci::BooleanParameter("Lossless Video", "losslessVideo", VERSION_HINT, false, "Record video in a lossless format. WARNING: This is not supported by all media players.");
    std::shared_ptr<osci::Effect> qualityEffect = std::make_shared<osci::SimpleEffect>(&qualityParameter);

    osci::BooleanParameter recordAudio = osci::BooleanParameter("Record Audio", "recordAudio", VERSION_HINT, true, "Record audio along with the video.");
    osci::BooleanParameter recordVideo = osci::BooleanParameter("Record Video", "recordVideo", VERSION_HINT, sosciFeatures, "Record video output of the visualiser.");

    VisualiserCanvasPreset canvasPreset = VisualiserCanvasPreset::Square;

    osci::EffectParameter canvasWidth = osci::EffectParameter(
        "Canvas Width",
        "The width of the visualiser canvas and recorded video. This only changes when not recording.",
        "canvasWidth",
        VERSION_HINT, 1024, VisualiserGeometry::minCanvasDimension, VisualiserGeometry::maxCanvasDimension, 2.0
    );
    std::shared_ptr<osci::Effect> canvasWidthEffect = std::make_shared<osci::SimpleEffect>(&canvasWidth);

    osci::EffectParameter canvasHeight = osci::EffectParameter(
        "Canvas Height",
        "The height of the visualiser canvas and recorded video. This only changes when not recording.",
        "canvasHeight",
        VERSION_HINT, 1024, VisualiserGeometry::minCanvasDimension, VisualiserGeometry::maxCanvasDimension, 2.0
    );
    std::shared_ptr<osci::Effect> canvasHeightEffect = std::make_shared<osci::SimpleEffect>(&canvasHeight);

    osci::EffectParameter frameRate = osci::EffectParameter(
        "Frame Rate",
        "The frame rate of the recorded video. This only changes when not recording.",
        "frameRate",
        VERSION_HINT, 60.0, 10, 240, 0.01
    );
    std::shared_ptr<osci::Effect> frameRateEffect = std::make_shared<osci::SimpleEffect>(&frameRate);

    juce::String compressionPreset = "fast";
    VideoCodec videoCodec = VideoCodec::H264;

    void save(juce::XmlElement* xml);

    // opt to not change any values if not found
    void load(juce::XmlElement* xml);

    juce::StringArray compressionPresets = { "ultrafast", "superfast", "veryfast", "faster", "fast", "medium", "slow", "slower", "veryslow" };
    juce::String customTextureOutputName = "";

    VisualiserRenderSize getCanvasSize();
    void setCanvasSize(VisualiserRenderSize size);
    void sanitiseCanvasParameters();
};

