#pragma once

#include <JuceHeader.h>

class CommonAudioProcessor;

class FeedbackReportBuilder final {
public:
    static osci::FeedbackOverlayConfig create(CommonAudioProcessor& processor,
                                               juce::Component& screenshotSource,
                                               juce::String projectFileType);

private:
    FeedbackReportBuilder() = delete;
};
