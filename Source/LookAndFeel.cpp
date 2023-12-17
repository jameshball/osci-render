#include "LookAndFeel.h"

OscirenderLookAndFeel::OscirenderLookAndFeel() {
    setColour(juce::Slider::thumbColourId, Colours::veryDark);
    setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::white);
    setColour(juce::Slider::textBoxBackgroundColourId, Colours::veryDark);
    setColour(juce::Slider::textBoxHighlightColourId, Colours::accentColor.withMultipliedAlpha(0.5));
    setColour(juce::Slider::trackColourId, juce::Colours::grey);
    setColour(juce::Slider::backgroundColourId, Colours::dark);
    setColour(sliderThumbOutlineColourId, juce::Colours::white);
    setColour(juce::ToggleButton::tickDisabledColourId, juce::Colours::white);
    setColour(juce::ResizableWindow::backgroundColourId, Colours::dark);
    setColour(groupComponentBackgroundColourId, Colours::darker);
    setColour(groupComponentHeaderColourId, Colours::veryDark);
    setColour(juce::PopupMenu::backgroundColourId, Colours::veryDark);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, Colours::darker);
    setColour(juce::ComboBox::backgroundColourId, Colours::veryDark);
    setColour(juce::ComboBox::outlineColourId, juce::Colours::white);
    setColour(juce::ComboBox::arrowColourId, juce::Colours::white);
    setColour(juce::TextButton::buttonColourId, Colours::veryDark);
    setColour(juce::TextEditor::backgroundColourId, Colours::veryDark);
    setColour(juce::TextEditor::outlineColourId, juce::Colours::white);
    setColour(juce::ListBox::backgroundColourId, Colours::darker);
    setColour(juce::ScrollBar::thumbColourId, juce::Colours::white);
    setColour(juce::ScrollBar::trackColourId, Colours::veryDark);
    setColour(juce::ScrollBar::backgroundColourId, Colours::veryDark);
    setColour(effectComponentBackgroundColourId, juce::Colours::transparentBlack);
    setColour(effectComponentHandleColourId, Colours::veryDark);
    setColour(juce::CodeEditorComponent::backgroundColourId, Colours::darker);
    setColour(juce::CodeEditorComponent::defaultTextColourId, Dracula::foreground);
    setColour(juce::CodeEditorComponent::lineNumberBackgroundId, Colours::veryDark);
    setColour(juce::CodeEditorComponent::lineNumberTextId, Dracula::foreground);
    setColour(juce::CodeEditorComponent::highlightColourId, Colours::grey);
    setColour(juce::CaretComponent::caretColourId, Dracula::foreground);
    setColour(juce::TextEditor::highlightColourId, Colours::grey);
    setColour(juce::TabbedButtonBar::tabOutlineColourId, Colours::veryDark);
    setColour(juce::TabbedButtonBar::frontOutlineColourId, Colours::veryDark);
    setColour(juce::TabbedButtonBar::tabTextColourId, juce::Colours::white);
    setColour(juce::TabbedButtonBar::frontTextColourId, juce::Colours::white);
    setColour(juce::TabbedComponent::outlineColourId, Colours::veryDark);
    setColour(tabbedComponentBackgroundColourId, Colours::veryDark);
    setColour(juce::TooltipWindow::backgroundColourId, Colours::veryDark);
    setColour(juce::TooltipWindow::outlineColourId, juce::Colours::white);

    getCurrentColourScheme().setUIColour(ColourScheme::widgetBackground, Colours::veryDark);
    getCurrentColourScheme().setUIColour(ColourScheme::UIColour::defaultFill, Colours::accentColor);
}

void OscirenderLookAndFeel::drawComboBox(juce::Graphics& g, int width, int height, bool, int, int, int, int, juce::ComboBox& box) {
    juce::Rectangle<int> boxBounds{0, 0, width, height};

    g.setColour(box.findColour(juce::ComboBox::backgroundColourId));
    g.fillRect(boxBounds.toFloat());

    g.setColour(box.findColour(juce::ComboBox::outlineColourId).withAlpha(box.isEnabled() ? 1.0f : 0.5f));
    g.drawRect(boxBounds.toFloat(), 1.0f);

    juce::Rectangle<int> arrowZone{width - 15, 0, 10, height};
    juce::Path path;
    path.startNewSubPath((float)arrowZone.getX(), (float)arrowZone.getCentreY() - 3.0f);
    path.lineTo((float)arrowZone.getCentreX(), (float)arrowZone.getCentreY() + 4.0f);
    path.lineTo((float)arrowZone.getRight(), (float)arrowZone.getCentreY() - 3.0f);
    path.closeSubPath();

    g.setColour(box.findColour(juce::ComboBox::arrowColourId).withAlpha(box.isEnabled() ? 1.0f : 0.5f));
    g.fillPath(path);
}

void OscirenderLookAndFeel::positionComboBoxText(juce::ComboBox& box, juce::Label& label) {
    label.setBounds(1, 1, box.getWidth() - 15, box.getHeight() - 2);
    label.setFont(getComboBoxFont(box));
}

void OscirenderLookAndFeel::drawTickBox(juce::Graphics& g, juce::Component& component,
    float x, float y, float w, float h,
    const bool ticked,
    const bool isEnabled,
    const bool shouldDrawButtonAsHighlighted,
    const bool shouldDrawButtonAsDown) {
    juce::Rectangle<float> tickBounds(x, y, w, h);

    g.setColour(component.findColour(juce::TextButton::buttonColourId));
    g.fillRect(tickBounds);
    g.setColour(component.findColour(juce::ToggleButton::tickDisabledColourId));
    g.drawRect(tickBounds, 1.0f);

    if (ticked) {
        g.setColour(component.findColour(juce::ToggleButton::tickColourId));
        auto tick = getTickShape(0.75f);
        g.fillPath(tick, tick.getTransformToScaleToFit(tickBounds.reduced(4, 5).toFloat(), false));
    }
}

void OscirenderLookAndFeel::drawGroupComponentOutline(juce::Graphics& g, int width, int height, const juce::String& text, const juce::Justification& position, juce::GroupComponent& group) {
    auto bounds = group.getLocalBounds();

    const float textH = 15.0f;
    const float indent = 3.0f;
    const float textEdgeGap = 4.0f;
    auto cs = 5.0f;

    juce::Font f(textH);

    juce::Path p;
    auto x = indent;
    auto y = f.getAscent() - 3.0f;
    auto w = juce::jmax(0.0f, (float)width - x * 2.0f);
    auto h = juce::jmax(0.0f, (float)height - y - indent);
    cs = juce::jmin(cs, w * 0.5f, h * 0.5f);
    auto cs2 = 2.0f * cs;

    auto textW = text.isEmpty() ? 0
        : juce::jlimit(0.0f,
            juce::jmax(0.0f, w - cs2 - textEdgeGap * 2),
            (float)f.getStringWidth(text) + textEdgeGap * 2.0f);
    auto textX = cs + textEdgeGap;

    if (position.testFlags(juce::Justification::horizontallyCentred))
        textX = cs + (w - cs2 - textW) * 0.5f;
    else if (position.testFlags(juce::Justification::right))
        textX = w - cs - textW - textEdgeGap;

    auto alpha = group.isEnabled() ? 1.0f : 0.5f;

    g.setColour(group.findColour(groupComponentBackgroundColourId).withMultipliedAlpha(alpha));
    g.fillRect(bounds);

    auto header = bounds.removeFromTop(2 * textH);

    g.setColour(group.findColour(groupComponentHeaderColourId).withMultipliedAlpha(alpha));
    g.fillRect(header);

    g.setColour(group.findColour(juce::GroupComponent::textColourId).withMultipliedAlpha(alpha));
    g.setFont(f);
    g.drawText(text,
        juce::roundToInt(header.getX() + x + textX), header.getY() + 7,
        juce::roundToInt(textW),
        juce::roundToInt(textH),
        juce::Justification::centred, true
    );
}

void OscirenderLookAndFeel::drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height, float sliderPos, float minSliderPos, float maxSliderPos, const juce::Slider::SliderStyle style, juce::Slider& slider) {
    juce::LookAndFeel_V4::drawLinearSlider(g, x, y, width, height, sliderPos, minSliderPos, maxSliderPos, style, slider);
    
    auto kx = slider.isHorizontal() ? sliderPos : ((float)x + (float)width * 0.5f);
    auto ky = slider.isHorizontal() ? ((float)y + (float)height * 0.5f) : sliderPos;

    juce::Point<float> point = { kx, ky };

    auto thumbWidth = getSliderThumbRadius(slider);

    g.setColour(slider.findColour(sliderThumbOutlineColourId).withAlpha(slider.isEnabled() ? 1.0f : 0.5f));
    g.drawEllipse(juce::Rectangle<float>(static_cast<float>(thumbWidth), static_cast<float>(thumbWidth)).withCentre(point), 1.0f);
}

void OscirenderLookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& button, const juce::Colour& backgroundColour, bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) {
    auto bounds = button.getLocalBounds().toFloat().reduced(0.5f, 0.5f);

    auto baseColour = backgroundColour.withMultipliedSaturation(button.hasKeyboardFocus(true) ? 1.3f : 0.9f)
        .withMultipliedAlpha(button.isEnabled() ? 1.0f : 0.5f);

    if (shouldDrawButtonAsDown || shouldDrawButtonAsHighlighted)
        baseColour = baseColour.contrasting(shouldDrawButtonAsDown ? 0.2f : 0.05f);

    g.setColour(baseColour);
    g.fillRect(bounds);

    g.setColour(button.findColour(juce::ComboBox::outlineColourId));
    g.drawRect(bounds, 1.0f);
}

void OscirenderLookAndFeel::drawMenuBarBackground(juce::Graphics& g, int width, int height, bool, juce::MenuBarComponent& menuBar) {
    juce::Rectangle<int> r(width, height);

    g.setColour(menuBar.findColour(juce::TextButton::buttonColourId));
    g.fillRect(r);
}

void OscirenderLookAndFeel::drawTooltip(juce::Graphics& g, const juce::String& text, int width, int height) {
    juce::Rectangle<int> bounds (width, height);
    auto cornerSize = 5.0f;

    g.setColour(findColour(juce::TooltipWindow::backgroundColourId));
    g.fillRect(bounds.toFloat());

    g.setColour(findColour(juce::TooltipWindow::outlineColourId));
    g.drawRect(bounds.toFloat().reduced(0.5f, 0.5f), 1.0f);

    LookAndFeelHelpers::layoutTooltipText (text, findColour (juce::TooltipWindow::textColourId))
        .draw (g, { static_cast<float> (width), static_cast<float> (height) });
}

juce::CodeEditorComponent::ColourScheme OscirenderLookAndFeel::getDefaultColourScheme() {
    juce::CodeEditorComponent::ColourScheme cs;

    static const juce::CodeEditorComponent::ColourScheme::TokenType types[] = {
        {"Error", juce::Colour(Dracula::red)},
        {"Comment", juce::Colour(Dracula::comment)},
        {"Keyword", juce::Colour(Dracula::pink)},
        {"Operator", juce::Colour(Dracula::foreground)},
        {"Identifier", juce::Colour(Dracula::foreground)},
        {"Integer", juce::Colour(Dracula::purple)},
        {"Float", juce::Colour(Dracula::purple)},
        {"String", juce::Colour(Dracula::yellow)},
        {"Bracket", juce::Colour(Dracula::foreground)},
        {"Punctuation", juce::Colour(Dracula::pink)},
        {"Preprocessor Text", juce::Colour(Dracula::comment)}
    };

    for (auto& t : types) {
        cs.set(t.name, juce::Colour(t.colour));
    }

    return cs;
}