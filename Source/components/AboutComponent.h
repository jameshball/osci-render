#pragma once

#include <JuceHeader.h>

class AboutComponent : public juce::Component {
public:
    AboutComponent();

    void resized() override;

private:
    juce::Image logo = juce::ImageFileFormat::loadFrom(BinaryData::logo_png, BinaryData::logo_pngSize);
    juce::ImageComponent logoComponent;
    
    juce::TextEditor text;
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AboutComponent)
};
