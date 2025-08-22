#pragma once

#include <JuceHeader.h>
#include "../PluginProcessor.h"
#include "GridComponent.h"
#include "SvgButton.h"
#include "ScrollFadeViewport.h"

// A grid-based browser for opening files: includes examples by category and a generic file chooser
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

    // Outer chrome and scrolling
    juce::GroupComponent group { {}, "Open Files" };
    ScrollFadeViewport viewport;        // Outer scroll container for entire examples panel
    juce::Component content;        // Holds all headings + category grids

    // Close icon overlayed in the group header
    SvgButton closeButton { "closeExamples",
        juce::String::createStringFromData(BinaryData::close_svg, BinaryData::close_svgSize),
        juce::Colours::white, juce::Colours::white };

    // Choose files button (moved from MainComponent)
    juce::TextButton chooseFilesButton { "Choose File(s)" };
    std::unique_ptr<juce::FileChooser> chooser;

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
    void openFileChooser();
    static bool shouldOpenEditorFor(const juce::String& fileName) { return fileName.endsWithIgnoreCase(".lua") || fileName.endsWithIgnoreCase(".txt"); }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ExampleFilesGridComponent)
};
