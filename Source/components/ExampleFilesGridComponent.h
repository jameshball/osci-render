#pragma once

#include <JuceHeader.h>
#include "../PluginProcessor.h"
#include "GridComponent.h"

// A grid-based browser for example files grouped by category
class ExampleFilesGridComponent : public juce::Component
{
public:
    ExampleFilesGridComponent(OscirenderAudioProcessor& processor);
    ~ExampleFilesGridComponent() override = default;

    void paint(juce::Graphics& g) override;
    void resized() override;

    // Called when the user closes the view
    std::function<void()> onClosed;
    // Called after the file has been added to the processor; consumer may open editors, etc.
    std::function<void(const juce::String& fileName, bool shouldOpenEditor)> onExampleOpened;

private:
    OscirenderAudioProcessor& audioProcessor;

    // Top bar
    juce::Label title { {}, "Examples" };
    juce::TextButton closeButton { "Close" };

    // Categories
    struct CategoryViews {
        juce::Label heading;
        GridComponent grid;
    };

    CategoryViews audioCat { juce::Label({}, "Audio"), GridComponent{} };
    CategoryViews textCat { juce::Label({}, "Text"), GridComponent{} };
    CategoryViews imagesCat { juce::Label({}, "Images"), GridComponent{} };
    CategoryViews luaCat { juce::Label({}, "Lua"), GridComponent{} };
    CategoryViews modelsCat { juce::Label({}, "3D models"), GridComponent{} };
    CategoryViews svgsCat { juce::Label({}, "SVGs"), GridComponent{} };

    // Helpers
    void addExample(CategoryViews& cat, const juce::String& fileName, const char* data, int size);
    void populate();
    void styleHeading(juce::Label& l);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ExampleFilesGridComponent)
};
