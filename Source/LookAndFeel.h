#pragma once

#include <JuceHeader.h>
#include "envelope/ugen_JuceEnvelopeComponent.h"

enum ColourIds {
    groupComponentBackgroundColourId,
    groupComponentHeaderColourId,
    effectComponentBackgroundColourId,
    effectComponentHandleColourId,
    sliderThumbOutlineColourId,
    scrollFadeOverlayBackgroundColourId,
};

namespace Colours {
    const juce::Colour dark{0xff585858};
    const juce::Colour darker{0xff454545};
    const juce::Colour veryDark{0xff111111};
    const juce::Colour grey{0xff6c6c6c};
    const juce::Colour accentColor{0xff00cc00};
}

namespace Dracula {
    const juce::Colour background{0xff282a36};
    const juce::Colour currentLine{0xff44475a};
    const juce::Colour selection{0xff44475a};
    const juce::Colour foreground{0xfff8f8f2};
    const juce::Colour comment{0xff6272a4};
    const juce::Colour cyan{0xff8be9fd};
    const juce::Colour green{0xff50fa7b};
    const juce::Colour orange{0xffffb86c};
    const juce::Colour pink{0xffff79c6};
    const juce::Colour purple{0xffbd93f9};
    const juce::Colour red{0xffff5555};
    const juce::Colour yellow{0xfff1fa8c};
}

namespace LookAndFeelHelpers
{
    static juce::Colour createBaseColour(juce::Colour buttonColour, bool hasKeyboardFocus, bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown, bool isEnabled) noexcept {
        const float sat = hasKeyboardFocus ? 1.3f : 0.9f;
        const juce::Colour baseColour(buttonColour.withMultipliedSaturation(sat).withMultipliedAlpha(isEnabled ? 1.0f : 0.5f));

        if (shouldDrawButtonAsDown) return baseColour.contrasting (0.3f);
        if (shouldDrawButtonAsHighlighted) return baseColour.contrasting (0.15f);

        return baseColour;
    }


    static juce::TextLayout layoutTooltipText(const juce::String& text, juce::Colour colour) noexcept {
        const float tooltipFontSize = 13.0f;
        const int maxToolTipWidth = 400;

        juce::AttributedString s;
        s.setJustification (juce::Justification::centred);
        s.append (text, juce::Font (tooltipFontSize, juce::Font::bold), colour);

        juce::TextLayout tl;
        tl.createLayoutWithBalancedLineLengths (s, (float) maxToolTipWidth);
        return tl;
    }
}

class OscirenderLookAndFeel : public juce::LookAndFeel_V4 {
public:
    OscirenderLookAndFeel();

    static void applyOscirenderColours(juce::LookAndFeel& lookAndFeel);
    static void drawOscirenderComboBox(juce::Graphics& g, int width, int height, juce::ComboBox& box);
    static void positionOscirenderComboBoxText(juce::LookAndFeel& lookAndFeel, juce::ComboBox& box, juce::Label& label);

    static const int RECT_RADIUS = 5;
    juce::Typeface::Ptr regularTypeface = juce::Typeface::createSystemTypefaceFor(BinaryData::FiraSansRegular_ttf, BinaryData::FiraSansRegular_ttfSize);
    juce::Typeface::Ptr boldTypeface = juce::Typeface::createSystemTypefaceFor(BinaryData::FiraSansBold_ttf, BinaryData::FiraSansBold_ttfSize);
    juce::Typeface::Ptr italicTypeface = juce::Typeface::createSystemTypefaceFor(BinaryData::FiraSansItalic_ttf, BinaryData::FiraSansItalic_ttfSize);

    void drawLabel(juce::Graphics& g, juce::Label& label) override;
    void fillTextEditorBackground(juce::Graphics& g, int width, int height, juce::TextEditor& textEditor) override;
    void drawTextEditorOutline(juce::Graphics& g, int width, int height, juce::TextEditor& textEditor) override;
    void drawComboBox(juce::Graphics& g, int width, int height, bool, int, int, int, int, juce::ComboBox& box) override;
    void positionComboBoxText(juce::ComboBox& box, juce::Label& label) override;
    void drawTickBox(juce::Graphics& g, juce::Component& component,
        float x, float y, float w, float h,
        const bool ticked,
        const bool isEnabled,
        const bool shouldDrawButtonAsHighlighted,
        const bool shouldDrawButtonAsDown) override;
    static void drawGroupComponentDropShadow(juce::Graphics& g, juce::GroupComponent& group);
    void drawGroupComponentOutline(juce::Graphics&, int w, int h, const juce::String &text, const juce::Justification&, juce::GroupComponent&) override;
    void drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height,
        float sliderPos,
        float minSliderPos,
        float maxSliderPos,
        const juce::Slider::SliderStyle style, juce::Slider& slider) override;
    void drawButtonBackground(juce::Graphics& g,
        juce::Button& button,
        const juce::Colour& backgroundColour,
        bool shouldDrawButtonAsHighlighted,
        bool shouldDrawButtonAsDown) override;
    void drawMenuBarBackground(juce::Graphics& g, int width, int height, bool, juce::MenuBarComponent& menuBar) override;
    juce::TextLayout layoutTooltipText(const juce::String& text, juce::Colour colour);
    juce::Rectangle<int> getTooltipBounds(const juce::String& tipText, juce::Point<int> screenPos, juce::Rectangle<int> parentArea) override;
    juce::CodeEditorComponent::ColourScheme getDefaultColourScheme();
    void drawTooltip(juce::Graphics& g, const juce::String& text, int width, int height) override;
    void drawCornerResizer(juce::Graphics&, int w, int h, bool isMouseOver, bool isMouseDragging) override;
    void drawToggleButton(juce::Graphics&, juce::ToggleButton&, bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;
    juce::MouseCursor getMouseCursorFor(juce::Component& component) override;
    void drawCallOutBoxBackground(juce::CallOutBox& box, juce::Graphics& g, const juce::Path& path, juce::Image& cachedImage) override;
    void drawProgressBar(juce::Graphics& g, juce::ProgressBar& progressBar, int width, int height, double progress, const juce::String& textToShow) override;
    void customDrawLinearProgressBar(juce::Graphics& g, const juce::ProgressBar& progressBar, int width, int height, double progress, const juce::String& textToShow);
    juce::Typeface::Ptr getTypefaceForFont(const juce::Font& font) override;
    void drawStretchableLayoutResizerBar(juce::Graphics& g, int w, int h, bool isVerticalBar, bool isMouseOver, bool isMouseDragging) override;
};
