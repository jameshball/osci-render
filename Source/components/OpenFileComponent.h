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

    void resized() override;

    // Called when the user closes the view
    std::function<void()> onClosed;
    // Called after a file has been added to the processor; consumer may open editors, etc.
    // This is called for each file opened (e.g., multiple files when importing).
    std::function<void(const juce::String& fileName, bool shouldOpenEditor, int fileIndex)> onFileOpened;

private:
    OscirenderAudioProcessor& audioProcessor;

    // Start screen buttons
    juce::TextButton startImportButton { "Import a File" };
    juce::Label chooseExampleLabel { "chooseExampleLabel", "or choose an example below" };

    // Outer chrome and scrolling (examples mode)
    juce::GroupComponent group { {}, "Open Files" };
    ScrollFadeViewport viewport;        // Outer scroll container for entire examples panel
    juce::Component content;        // Holds all headings + category grids

    // Close icon overlayed in the group header
    SvgButton closeButton { "closeExamples", juce::String::createStringFromData(BinaryData::close_svg, BinaryData::close_svgSize), juce::Colours::white, juce::Colours::white };

    std::unique_ptr<juce::FileChooser> chooser;

    // Categories
    struct CategoryViews {
        juce::GroupComponent group;
        GridComponent grid;
    };

    CategoryViews textCat { juce::GroupComponent({}, "Text"), GridComponent{} };
    CategoryViews luaCat { juce::GroupComponent({}, "Lua"), GridComponent{} };
    CategoryViews modelsCat { juce::GroupComponent({}, "3D models"), GridComponent{} };
    CategoryViews svgsCat { juce::GroupComponent({}, "SVGs"), GridComponent{} };

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
    void openFileChooser();
    static bool shouldOpenEditorFor(const juce::String& fileName) { return fileName.endsWithIgnoreCase(".lua") || fileName.endsWithIgnoreCase(".txt"); }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OpenFileComponent)
};
