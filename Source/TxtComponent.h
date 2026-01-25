#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "LookAndFeel.h"

class OscirenderAudioProcessorEditor;
class TxtComponent : public juce::ComboBox {
public:
    TxtComponent(OscirenderAudioProcessor&, OscirenderAudioProcessorEditor&);
    ~TxtComponent() override;

    void update();
    
private:
    class FontPreviewLookAndFeel : public juce::LookAndFeel_V4 {
    public:
        FontPreviewLookAndFeel();
        void drawPopupMenuItem(juce::Graphics& g, const juce::Rectangle<int>& area, bool isSeparator, bool isActive,
                               bool isHighlighted, bool isTicked, bool hasSubMenu, const juce::String& text,
                               const juce::String& shortcutKeyText, const juce::Drawable* icon,
                               const juce::Colour* textColour) override;
        void drawComboBox(juce::Graphics& g, int width, int height, bool, int, int, int, int, juce::ComboBox& box) override;
        void positionComboBoxText(juce::ComboBox& box, juce::Label& label) override;
    };

    OscirenderAudioProcessor& audioProcessor;
    OscirenderAudioProcessorEditor& pluginEditor;

    juce::StringArray installedFonts = juce::Font::findAllTypefaceNames();
    FontPreviewLookAndFeel fontPreviewLookAndFeel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TxtComponent)
};