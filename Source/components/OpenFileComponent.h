#pragma once

#include <JuceHeader.h>
#include "../PluginProcessor.h"
#include "GridComponent.h"
#include "SvgButton.h"
#include "ScrollFadeViewport.h"

// A grid-based browser for opening files: includes examples by category and a generic file chooser
class OpenFileComponent : public juce::Component
{
public:
    OpenFileComponent(OscirenderAudioProcessor& processor);
    ~OpenFileComponent() override = default;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void visibilityChanged() override; // reset to chooser prompt each time shown

    // Called when the user closes the view
    std::function<void()> onClosed;
    // Called after the file has been added to the processor; consumer may open editors, etc.
    std::function<void(const juce::String& fileName, bool shouldOpenEditor)> onExampleOpened;

private:
    OscirenderAudioProcessor& audioProcessor;

    enum class Mode { chooserPrompt, examples };
    Mode mode { Mode::chooserPrompt };

    // Start screen buttons
    juce::TextButton startImportButton { "Import a File" };
    juce::TextButton startExamplesButton { "Browse Examples" };

    void enterExamplesMode();
    void resetToChooserPrompt();

    // Outer chrome and scrolling (examples mode)
    juce::GroupComponent group { {}, "Open Files" };
    ScrollFadeViewport viewport;        // Outer scroll container for entire examples panel
    juce::Component content;        // Holds all headings + category grids

    // Close icon overlayed in the group header
    SvgButton closeButton { "closeExamples", juce::String::createStringFromData(BinaryData::close_svg, BinaryData::close_svgSize), juce::Colours::white, juce::Colours::white };

    std::unique_ptr<juce::FileChooser> chooser;

    // Categories
    struct CategoryViews {
        juce::Label heading;
        GridComponent grid;
    };

    CategoryViews textCat { juce::Label({}, "Text"), GridComponent{} };
    CategoryViews luaCat { juce::Label({}, "Lua"), GridComponent{} };
    CategoryViews modelsCat { juce::Label({}, "3D models"), GridComponent{} };
    CategoryViews svgsCat { juce::Label({}, "SVGs"), GridComponent{} };

    // Helpers
    // Adds an example resource to a category. Optionally you may supply a custom icon SVG string & size.
    // If iconData is nullptr, a default placeholder icon (random_svg) is used.
    void addExample(CategoryViews& cat,
                    const juce::String& fileName,
                    const juce::String& displayName,
                    const char* data,
                    int size,
                    const char* iconData = nullptr,
                    int iconSize = 0);
    void populate();
    void styleHeading(juce::Label& l);
    void openFileChooser();
    static bool shouldOpenEditorFor(const juce::String& fileName) { return fileName.endsWithIgnoreCase(".lua") || fileName.endsWithIgnoreCase(".txt"); }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OpenFileComponent)
};
