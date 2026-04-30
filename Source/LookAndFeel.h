#pragma once

#include <JuceHeader.h>

enum ColourIds {
    groupComponentBackgroundColourId,
    groupComponentHeaderColourId,
    effectComponentBackgroundColourId,
    effectComponentHandleColourId,
    sliderThumbOutlineColourId,
    scrollFadeOverlayBackgroundColourId,
};

// ============================================================================
// Theme system — define colour palettes and switch between them at runtime.
// ============================================================================

struct Theme {
    juce::Colour dark;
    juce::Colour darker;
    juce::Colour darkerer;
    juce::Colour evenDarker;
    juce::Colour veryDark;
    juce::Colour grey;
    juce::Colour accentColor;

    static constexpr int kLabelHeight = 14;

    // --- Built-in themes ---

    static const Theme& classic() {
        static const Theme t {
            juce::Colour(0xff585858),   // dark
            juce::Colour(0xff454545),   // darker
            juce::Colour(0xff333333),   // darkerer
            juce::Colour(0xff222222),   // evenDarker
            juce::Colour(0xff111111),   // veryDark
            juce::Colour(0xff6c6c6c),   // grey
            juce::Colour(0xff00cc00),   // accentColor
        };
        return t;
    }

    static const Theme& blue() {
        static const Theme t {
            juce::Colour(0xff575a60),   // dark  (slightly more blue tinge)
            juce::Colour(0xff44474d),   // darker
            juce::Colour(0xff32353a),   // darkerer
            juce::Colour(0xff222428),   // evenDarker
            juce::Colour(0xff111215),   // veryDark
            juce::Colour(0xff6b6e75),   // grey
            juce::Colour(0xff00cc00),   // accentColor (keep green)
        };
        return t;
    }

    static const Theme& green() {
        static const Theme t {
            juce::Colour(0xff585a58),   // dark  (very slight green tinge)
            juce::Colour(0xff454745),   // darker
            juce::Colour(0xff333533),   // darkerer
            juce::Colour(0xff222422),   // evenDarker
            juce::Colour(0xff111211),   // veryDark
            juce::Colour(0xff6c6e6c),   // grey
            juce::Colour(0xff00cc00),   // accentColor (keep green)
        };
        return t;
    }

    // --- Active theme ---

    static const Theme& current() { return *activePalette(); }

    static void setCurrent(const Theme& theme) { activePalette() = &theme; }

private:
    static const Theme*& activePalette() {
        static const Theme* p = &classic();
        return p;
    }
};

// Backwards-compatible alias so existing code compiles unchanged.
namespace Colours {
    // These are thin accessors into the active theme.
    inline juce::Colour dark()            { return Theme::current().dark; }
    inline juce::Colour darker()          { return Theme::current().darker; }
    inline juce::Colour darkerer()        { return Theme::current().darkerer; }
    inline juce::Colour evenDarker()      { return Theme::current().evenDarker; }
    inline juce::Colour veryDark()        { return Theme::current().veryDark; }
    inline juce::Colour grey()            { return Theme::current().grey; }
    inline juce::Colour accentColor()     { return Theme::current().accentColor; }

    static constexpr int kLabelHeight = Theme::kLabelHeight;
    static constexpr float kPillRadius = kLabelHeight * 0.5f;

    inline juce::Colour midiLearnBackground() { return juce::Colours::red.withAlpha(0.6f); }
    inline juce::Colour midiLearnText()       { return juce::Colours::red; }
    inline const juce::String& midiLearnLabel() { static const juce::String s("MIDI Learn..."); return s; }
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
    [[maybe_unused]] static juce::Colour createBaseColour(juce::Colour buttonColour, bool hasKeyboardFocus, bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown, bool isEnabled) noexcept {
        const juce::Colour baseColour(buttonColour.withMultipliedAlpha(isEnabled ? 1.0f : 0.5f));

        if (shouldDrawButtonAsDown) return baseColour.contrasting (0.15f);
        if (shouldDrawButtonAsHighlighted) return baseColour.contrasting (0.05f);

        return baseColour;
    }


    [[maybe_unused]] static juce::TextLayout layoutTooltipText(const juce::String& text, juce::Colour colour) noexcept {
        const float tooltipFontSize = 13.0f;
        const int maxToolTipWidth = 400;

        juce::AttributedString s;
        s.setJustification (juce::Justification::centred);
        s.append (text, juce::Font(juce::FontOptions(tooltipFontSize, juce::Font::bold)), colour);

        juce::TextLayout tl;
        tl.createLayoutWithBalancedLineLengths (s, (float) maxToolTipWidth);
        return tl;
    }
}

class OscirenderLookAndFeel : public juce::LookAndFeel_V4 {
public:
    OscirenderLookAndFeel();

    // Shared instance that can be used before the editor is created
    static OscirenderLookAndFeel& getSharedInstance();

    static void applyOscirenderColours(juce::LookAndFeel& lookAndFeel);
    static void drawOscirenderComboBox(juce::Graphics& g, int width, int height, juce::ComboBox& box);
    static void positionOscirenderComboBoxText(juce::LookAndFeel& lookAndFeel, juce::ComboBox& box, juce::Label& label);

    static const int RECT_RADIUS = 5;
    juce::Typeface::Ptr regularTypeface = juce::Typeface::createSystemTypefaceFor(BinaryData::FiraSansRegular_ttf, BinaryData::FiraSansRegular_ttfSize);
    juce::Typeface::Ptr boldTypeface = juce::Typeface::createSystemTypefaceFor(BinaryData::FiraSansSemiBold_ttf, BinaryData::FiraSansSemiBold_ttfSize);
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
    void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
        float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
        juce::Slider& slider) override;
    void drawButtonBackground(juce::Graphics& g,
        juce::Button& button,
        const juce::Colour& backgroundColour,
        bool shouldDrawButtonAsHighlighted,
        bool shouldDrawButtonAsDown) override;
    void drawMenuBarBackground(juce::Graphics& g, int width, int height, bool, juce::MenuBarComponent& menuBar) override;
    juce::TextLayout layoutTooltipText(const juce::String& text, juce::Colour colour);
    juce::Rectangle<int> getTooltipBounds(const juce::String& tipText, juce::Point<int> screenPos, juce::Rectangle<int> parentArea) override;
    static juce::CodeEditorComponent::ColourScheme getDefaultColourScheme();
    void drawTooltip(juce::Graphics& g, const juce::String& text, int width, int height) override;
    void drawCornerResizer(juce::Graphics&, int w, int h, bool isMouseOver, bool isMouseDragging) override;
    void drawToggleButton(juce::Graphics&, juce::ToggleButton&, bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;
    juce::MouseCursor getMouseCursorFor(juce::Component& component) override;
    void drawCallOutBoxBackground(juce::CallOutBox& box, juce::Graphics& g, const juce::Path& path, juce::Image& cachedImage) override;
    void drawProgressBar(juce::Graphics& g, juce::ProgressBar& progressBar, int width, int height, double progress, const juce::String& textToShow) override;
    void customDrawLinearProgressBar(juce::Graphics& g, const juce::ProgressBar& progressBar, int width, int height, double progress, const juce::String& textToShow);
    juce::Typeface::Ptr getTypefaceForFont(const juce::Font& font) override;

    // AlertWindow overrides
    void drawAlertBox(juce::Graphics& g, juce::AlertWindow& alert, const juce::Rectangle<int>& textArea, juce::TextLayout& textLayout) override;
    int getAlertWindowButtonHeight() override;
    juce::Font getAlertWindowTitleFont() override;
    juce::Font getAlertWindowMessageFont() override;
    juce::Font getAlertWindowFont() override;
    void drawStretchableLayoutResizerBar(juce::Graphics& g, int w, int h, bool isVerticalBar, bool isMouseOver, bool isMouseDragging) override;
    void drawBubble(juce::Graphics& g, juce::BubbleComponent& bubble,
                    const juce::Point<float>& tip, const juce::Rectangle<float>& body) override;

    // PopupMenu
    void drawPopupMenuBackground(juce::Graphics& g, int width, int height) override;
    void drawPopupMenuItem(juce::Graphics& g, const juce::Rectangle<int>& area,
                           bool isSeparator, bool isActive, bool isHighlighted,
                           bool isTicked, bool hasSubMenu,
                           const juce::String& text, const juce::String& shortcutKeyText,
                           const juce::Drawable* icon, const juce::Colour* textColour) override;
    void getIdealPopupMenuItemSize(const juce::String& text, bool isSeparator,
                                   int standardMenuItemHeight, int& idealWidth, int& idealHeight) override;
    int getPopupMenuBorderSize() override;
};
