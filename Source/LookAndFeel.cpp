#include "LookAndFeel.h"
#include "components/SwitchButton.h"

OscirenderLookAndFeel::OscirenderLookAndFeel() {
    // slider
    setColour(juce::Slider::thumbColourId, Colours::veryDark);
    setColour(juce::Slider::textBoxOutlineColourId, Colours::veryDark);
    setColour(juce::Slider::textBoxBackgroundColourId, Colours::veryDark);
    setColour(juce::Slider::textBoxHighlightColourId, Colours::accentColor.withMultipliedAlpha(0.5));
    setColour(juce::Slider::trackColourId, juce::Colours::grey);
    setColour(juce::Slider::backgroundColourId, Colours::dark);
    setColour(sliderThumbOutlineColourId, juce::Colours::white);

    // buttons
    setColour(juce::ToggleButton::tickDisabledColourId, juce::Colours::white);
    setColour(juce::TextButton::buttonColourId, Colours::veryDark);
    setColour(jux::SwitchButton::switchColour, juce::Colours::white);
    setColour(jux::SwitchButton::switchOnBackgroundColour, Colours::accentColor);
    setColour(jux::SwitchButton::switchOffBackgroundColour, Colours::dark);

    // windows & menus
    setColour(juce::ResizableWindow::backgroundColourId, Colours::grey);
    setColour(groupComponentBackgroundColourId, Colours::darker);
    setColour(groupComponentHeaderColourId, Colours::veryDark);
    setColour(juce::PopupMenu::backgroundColourId, Colours::darker);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, Colours::grey);
    setColour(juce::TooltipWindow::backgroundColourId, Colours::darker);
    setColour(juce::TooltipWindow::outlineColourId, juce::Colours::white);
    setColour(juce::TextButton::buttonOnColourId, Colours::darker);

    // combo box
    setColour(juce::ComboBox::backgroundColourId, Colours::veryDark);
    setColour(juce::ComboBox::outlineColourId, Colours::veryDark);
    setColour(juce::ComboBox::arrowColourId, juce::Colours::white);
    
    // text box
    setColour(juce::TextEditor::backgroundColourId, Colours::veryDark);
    setColour(juce::TextEditor::outlineColourId, Colours::veryDark);
    setColour(juce::TextEditor::focusedOutlineColourId, Colours::accentColor);
    setColour(juce::CaretComponent::caretColourId, Dracula::foreground);
    setColour(juce::TextEditor::highlightColourId, Colours::grey);

    // list box
    setColour(juce::ListBox::backgroundColourId, Colours::darker);

    // scroll bar
    setColour(juce::ScrollBar::thumbColourId, juce::Colours::white);
    setColour(juce::ScrollBar::trackColourId, Colours::veryDark);
    setColour(juce::ScrollBar::backgroundColourId, Colours::veryDark);

    // custom components
    setColour(effectComponentBackgroundColourId, juce::Colours::transparentBlack);
    setColour(effectComponentHandleColourId, Colours::veryDark);

    // code editor
    setColour(juce::CodeEditorComponent::backgroundColourId, Colours::darker);
    setColour(juce::CodeEditorComponent::defaultTextColourId, Dracula::foreground);
    setColour(juce::CodeEditorComponent::lineNumberBackgroundId, Colours::veryDark);
    setColour(juce::CodeEditorComponent::lineNumberTextId, Dracula::foreground);
    setColour(juce::CodeEditorComponent::highlightColourId, Colours::grey);

    // envelope
    setColour(EnvelopeComponent::Node, Colours::veryDark);
    setColour(EnvelopeComponent::ReleaseNode, Colours::veryDark);
    setColour(EnvelopeComponent::LoopNode, Colours::veryDark);
    setColour(EnvelopeComponent::NodeOutline, juce::Colours::white);
    setColour(EnvelopeComponent::Line, juce::Colours::white);
    setColour(EnvelopeComponent::LoopLine, juce::Colours::white);
    setColour(EnvelopeComponent::Background, Colours::veryDark);
    setColour(EnvelopeComponent::GridLine, Colours::dark);
    setColour(EnvelopeComponent::LegendText, juce::Colours::white);
    setColour(EnvelopeComponent::LegendBackground, Colours::veryDark);
    setColour(EnvelopeComponent::LineBackground, juce::Colours::white);

    // midi keyboard
    setColour(juce::MidiKeyboardComponent::blackNoteColourId, Colours::veryDark);
    setColour(juce::MidiKeyboardComponent::whiteNoteColourId, juce::Colours::white);
    setColour(juce::MidiKeyboardComponent::mouseOverKeyOverlayColourId, Colours::accentColor.withAlpha(0.3f));
    setColour(juce::MidiKeyboardComponent::keyDownOverlayColourId, Colours::accentColor.withAlpha(0.7f));
    setColour(juce::MidiKeyboardComponent::shadowColourId, juce::Colours::transparentBlack);
    setColour(juce::MidiKeyboardComponent::upDownButtonBackgroundColourId, Colours::veryDark);
    setColour(juce::MidiKeyboardComponent::upDownButtonArrowColourId, juce::Colours::white);

    // UI colours
    getCurrentColourScheme().setUIColour(ColourScheme::widgetBackground, Colours::veryDark);
    getCurrentColourScheme().setUIColour(ColourScheme::UIColour::defaultFill, Colours::accentColor);

    // setDefaultSansSerifTypeface(juce::Typeface::createSystemTypefaceFor(BinaryData::font_ttf, BinaryData::font_ttfSize));

    // I have to do this, otherwise components are initialised before the look and feel is set
    juce::LookAndFeel::setDefaultLookAndFeel(this);
}

void OscirenderLookAndFeel::drawLabel(juce::Graphics& g, juce::Label& label) {
    label.setRepaintsOnMouseActivity(true);
    auto baseColour = label.findColour(juce::Label::backgroundColourId);
    if (label.isEditable()) {
        label.setMouseCursor(juce::MouseCursor::IBeamCursor);
        baseColour = LookAndFeelHelpers::createBaseColour(baseColour, false, label.isMouseOver(true), false, label.isEnabled());
    }
    g.setColour(baseColour);
    g.fillRoundedRectangle(label.getLocalBounds().toFloat(), RECT_RADIUS);

    if (! label.isBeingEdited())
    {
        auto alpha = label.isEnabled() ? 1.0f : 0.5f;
        const juce::Font font (getLabelFont (label));

        g.setColour (label.findColour (juce::Label::textColourId).withMultipliedAlpha (alpha));
        g.setFont (font);

        auto textArea = getLabelBorderSize (label).subtractedFrom (label.getLocalBounds());

        g.drawFittedText (label.getText(), textArea, label.getJustificationType(),
            juce::jmax (1, (int) ((float) textArea.getHeight() / font.getHeight())),
            label.getMinimumHorizontalScale());

        g.setColour (label.findColour (juce::Label::outlineColourId).withMultipliedAlpha (alpha));
    }
    else if (label.isEnabled())
    {
        auto outlineColour = label.findColour(juce::Label::outlineColourId);
        if (label.isEditable()) {
            outlineColour = LookAndFeelHelpers::createBaseColour(outlineColour, false, label.isMouseOver(true), false, label.isEnabled());
        }
        g.setColour(outlineColour);
        g.drawRoundedRectangle(label.getLocalBounds().toFloat(), RECT_RADIUS, 1);
    }
}

void OscirenderLookAndFeel::fillTextEditorBackground(juce::Graphics& g, int width, int height, juce::TextEditor& textEditor) {
    textEditor.setRepaintsOnMouseActivity(true);
    if (dynamic_cast<juce::AlertWindow*> (textEditor.getParentComponent()) != nullptr)
    {
        g.setColour (textEditor.findColour (juce::TextEditor::backgroundColourId));
        g.fillRect (0, 0, width, height);

        g.setColour (textEditor.findColour (juce::TextEditor::outlineColourId));
        g.drawHorizontalLine (height - 1, 0.0f, static_cast<float> (width));
    }
    else
    {
        auto backgroundColour = textEditor.findColour (juce::TextEditor::backgroundColourId);
        auto baseColour = LookAndFeelHelpers::createBaseColour(backgroundColour, false, textEditor.isMouseOver(true), false, textEditor.isEnabled());
        g.setColour(baseColour);
        g.fillRoundedRectangle(textEditor.getLocalBounds().toFloat(), RECT_RADIUS);
    }
}

void OscirenderLookAndFeel::drawTextEditorOutline(juce::Graphics& g, int width, int height, juce::TextEditor& textEditor) {
    if (textEditor.isEnabled())
    {
        if (textEditor.hasKeyboardFocus (true) && ! textEditor.isReadOnly())
        {
            const int border = 2;

            g.setColour (textEditor.findColour (juce::TextEditor::focusedOutlineColourId));
            g.drawRoundedRectangle(0, 0, width, height, RECT_RADIUS, border);
        }
        else
        {
            auto outlineColour = textEditor.findColour(juce::TextEditor::outlineColourId);
            outlineColour = LookAndFeelHelpers::createBaseColour(outlineColour, false, textEditor.isMouseOver(true), false, textEditor.isEnabled());
            g.setColour(outlineColour);
            g.drawRoundedRectangle(0, 0, width, height, RECT_RADIUS, 1.0f);
        }
    }
}

void OscirenderLookAndFeel::drawComboBox(juce::Graphics& g, int width, int height, bool, int, int, int, int, juce::ComboBox& box) {
    juce::Rectangle<int> boxBounds{0, 0, width, height};
    box.setMouseCursor(juce::MouseCursor::PointingHandCursor);

    g.setColour(box.findColour(juce::ComboBox::backgroundColourId));
    g.fillRoundedRectangle(boxBounds.toFloat(), RECT_RADIUS);

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
    label.setMouseCursor(juce::MouseCursor::PointingHandCursor);
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

    juce::Path background;
	background.addRoundedRectangle(bounds.getX(), bounds.getY(), bounds.getWidth(), bounds.getHeight(), RECT_RADIUS, RECT_RADIUS);
    g.setColour(group.findColour(groupComponentBackgroundColourId).withMultipliedAlpha(alpha));
    g.fillPath(background);

    auto header = bounds.removeFromTop(2 * textH);

    juce::Path headerPath;
    headerPath.addRoundedRectangle(header.getX(), header.getY(), header.getWidth(), header.getHeight(), RECT_RADIUS, RECT_RADIUS);

    g.setColour(group.findColour(groupComponentHeaderColourId).withMultipliedAlpha(alpha));
    g.fillPath(headerPath);

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

	juce::Path thumb;
	thumb.addEllipse(juce::Rectangle<float>(static_cast<float>(thumbWidth), static_cast<float>(thumbWidth)).withCentre(point));
   
    g.setColour(slider.findColour(sliderThumbOutlineColourId).withAlpha(slider.isEnabled() ? 1.0f : 0.5f));
	g.strokePath(thumb, juce::PathStrokeType(1.0f));
}

void OscirenderLookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& button, const juce::Colour& backgroundColour, bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) {
    button.setMouseCursor(juce::MouseCursor::PointingHandCursor);
    
    auto bounds = button.getLocalBounds().toFloat().reduced(0.5f, 0.5f);

    auto baseColour = LookAndFeelHelpers::createBaseColour(backgroundColour, button.hasKeyboardFocus(true), shouldDrawButtonAsHighlighted, shouldDrawButtonAsDown, button.isEnabled());

    g.setColour(baseColour);
    g.fillRoundedRectangle(bounds, RECT_RADIUS);
}

void OscirenderLookAndFeel::drawMenuBarBackground(juce::Graphics& g, int width, int height, bool, juce::MenuBarComponent& menuBar) {
    juce::Rectangle<int> r(width, height);

    g.setColour(menuBar.findColour(juce::TextButton::buttonColourId));
    g.fillRect(r);
}

void OscirenderLookAndFeel::drawTooltip(juce::Graphics& g, const juce::String& text, int width, int height) {
    juce::Rectangle<int> bounds (width, height);

    g.setColour(findColour(juce::TooltipWindow::backgroundColourId));
    g.fillRect(bounds);

    LookAndFeelHelpers::layoutTooltipText (text, findColour (juce::TooltipWindow::textColourId))
        .draw (g, { static_cast<float> (width), static_cast<float> (height) });
}

void OscirenderLookAndFeel::drawCornerResizer(juce::Graphics&, int w, int h, bool isMouseOver, bool isMouseDragging) {
    
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
