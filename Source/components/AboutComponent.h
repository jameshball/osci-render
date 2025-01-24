#pragma once

#include <JuceHeader.h>

class AboutComponent : public juce::Component {
public:
    AboutComponent(const void *image, size_t imageSize, juce::String sectionText, int* port);

    void resized() override;
    void updatePortText(int* port);

private:
    juce::Image logo;
    juce::ImageComponent logoComponent;

    juce::TextEditor text;
    juce::TextEditor portText;
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AboutComponent)
};
