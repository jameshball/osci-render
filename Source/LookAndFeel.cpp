#include "LookAndFeel.h"
#include "components/CustomMidiKeyboardComponent.h"
#include "components/SwitchButton.h"
#include "components/modulation/EnvelopeComponent.h"

OscirenderLookAndFeel::OscirenderLookAndFeel() {
    applyOscirenderColours(*this);

    // UI colours
    getCurrentColourScheme().setUIColour(ColourScheme::widgetBackground, Colours::veryDark);

    // I have to do this, otherwise components are initialised before the look and feel is set
    juce::LookAndFeel::setDefaultLookAndFeel(this);
}

void OscirenderLookAndFeel::applyOscirenderColours(juce::LookAndFeel& lookAndFeel) {
    // slider
    lookAndFeel.setColour(juce::Slider::thumbColourId, Colours::veryDark);
    lookAndFeel.setColour(juce::Slider::textBoxOutlineColourId, Colours::veryDark);
    lookAndFeel.setColour(juce::Slider::textBoxBackgroundColourId, Colours::veryDark);
    lookAndFeel.setColour(juce::Slider::textBoxHighlightColourId, Colours::accentColor.withMultipliedAlpha(0.5f));
    lookAndFeel.setColour(juce::Slider::trackColourId, juce::Colours::grey);
    lookAndFeel.setColour(juce::Slider::backgroundColourId, Colours::dark);
    lookAndFeel.setColour(sliderThumbOutlineColourId, juce::Colours::white);

    // buttons
    lookAndFeel.setColour(juce::TextButton::buttonColourId, Colours::veryDark);
    lookAndFeel.setColour(jux::SwitchButton::switchColour, juce::Colours::white);
    lookAndFeel.setColour(jux::SwitchButton::switchOnBackgroundColour, Colours::accentColor);
    lookAndFeel.setColour(jux::SwitchButton::switchOffBackgroundColour, Colours::grey);

    // windows & menus
    lookAndFeel.setColour(juce::ResizableWindow::backgroundColourId, Colours::veryDark.brighter(0.1f));
    lookAndFeel.setColour(groupComponentBackgroundColourId, Colours::darker);
    lookAndFeel.setColour(scrollFadeOverlayBackgroundColourId, Colours::darker);
    lookAndFeel.setColour(groupComponentHeaderColourId, Colours::veryDark);
    lookAndFeel.setColour(juce::PopupMenu::backgroundColourId, Colours::popupBackground);
    lookAndFeel.setColour(juce::PopupMenu::highlightedBackgroundColourId, Colours::accentColor);
    lookAndFeel.setColour(juce::TooltipWindow::backgroundColourId, Colours::darker.darker(0.5f));
    lookAndFeel.setColour(juce::TooltipWindow::outlineColourId, Colours::darker);
    lookAndFeel.setColour(juce::TextButton::buttonOnColourId, Colours::darker);
    lookAndFeel.setColour(juce::AlertWindow::outlineColourId, Colours::darker);
    lookAndFeel.setColour(juce::AlertWindow::backgroundColourId, Colours::darker);
    lookAndFeel.setColour(juce::ColourSelector::backgroundColourId, Colours::darker);

    // combo box
    lookAndFeel.setColour(juce::ComboBox::backgroundColourId, Colours::veryDark);
    lookAndFeel.setColour(juce::ComboBox::outlineColourId, Colours::veryDark);
    lookAndFeel.setColour(juce::ComboBox::arrowColourId, juce::Colours::white);

    // text box
    lookAndFeel.setColour(juce::TextEditor::backgroundColourId, Colours::veryDark);
    lookAndFeel.setColour(juce::TextEditor::outlineColourId, Colours::veryDark);
    lookAndFeel.setColour(juce::TextEditor::focusedOutlineColourId, Colours::accentColor);
    lookAndFeel.setColour(juce::CaretComponent::caretColourId, Dracula::foreground);
    lookAndFeel.setColour(juce::TextEditor::highlightColourId, Colours::grey);

    // list box
    lookAndFeel.setColour(juce::ListBox::backgroundColourId, Colours::darker);

    // scroll bar
    lookAndFeel.setColour(juce::ScrollBar::thumbColourId, juce::Colours::white);
    lookAndFeel.setColour(juce::ScrollBar::trackColourId, Colours::veryDark);
    lookAndFeel.setColour(juce::ScrollBar::backgroundColourId, Colours::veryDark);

    // custom components
    lookAndFeel.setColour(effectComponentBackgroundColourId, juce::Colours::transparentBlack);
    lookAndFeel.setColour(effectComponentHandleColourId, Colours::veryDark);

    // code editor
    lookAndFeel.setColour(juce::CodeEditorComponent::backgroundColourId, Colours::darker);
    lookAndFeel.setColour(juce::CodeEditorComponent::defaultTextColourId, Dracula::foreground);
    lookAndFeel.setColour(juce::CodeEditorComponent::lineNumberBackgroundId, Colours::veryDark);
    lookAndFeel.setColour(juce::CodeEditorComponent::lineNumberTextId, Dracula::foreground);
    lookAndFeel.setColour(juce::CodeEditorComponent::highlightColourId, Colours::grey);

    // envelope
    lookAndFeel.setColour(EnvelopeComponent::Node, Colours::veryDark);
    lookAndFeel.setColour(EnvelopeComponent::ReleaseNode, Colours::veryDark);
    lookAndFeel.setColour(EnvelopeComponent::LoopNode, Colours::veryDark);
    lookAndFeel.setColour(EnvelopeComponent::NodeOutline, juce::Colours::white);
    lookAndFeel.setColour(EnvelopeComponent::Line, juce::Colours::white);
    lookAndFeel.setColour(EnvelopeComponent::LoopLine, juce::Colours::white);
    lookAndFeel.setColour(EnvelopeComponent::Background, Colours::veryDark);
    lookAndFeel.setColour(EnvelopeComponent::GridLine, Colours::dark);
    lookAndFeel.setColour(EnvelopeComponent::LegendText, juce::Colours::white);
    lookAndFeel.setColour(EnvelopeComponent::LegendBackground, Colours::veryDark);
    lookAndFeel.setColour(EnvelopeComponent::LineBackground, juce::Colours::white);

    // midi keyboard
    lookAndFeel.setColour(juce::CustomMidiKeyboardComponent::whiteNoteColourId, Colours::dark.brighter(0.12f));
    lookAndFeel.setColour(juce::CustomMidiKeyboardComponent::blackNoteColourId, Colours::veryDark.brighter(0.03f));
    lookAndFeel.setColour(juce::CustomMidiKeyboardComponent::keySeparatorLineColourId, Colours::veryDark.brighter(0.16f));
    lookAndFeel.setColour(juce::CustomMidiKeyboardComponent::mouseOverKeyOverlayColourId, Colours::accentColor.withAlpha(0.28f));
    lookAndFeel.setColour(juce::CustomMidiKeyboardComponent::keyDownOverlayColourId, Colours::accentColor.withAlpha(0.62f));
    lookAndFeel.setColour(juce::CustomMidiKeyboardComponent::shadowColourId, juce::Colours::transparentBlack);
    lookAndFeel.setColour(juce::CustomMidiKeyboardComponent::upDownButtonBackgroundColourId, Colours::veryDark);
    lookAndFeel.setColour(juce::CustomMidiKeyboardComponent::upDownButtonArrowColourId, juce::Colours::white);

    // progress bar
    lookAndFeel.setColour(juce::ProgressBar::backgroundColourId, juce::Colours::transparentBlack);
    lookAndFeel.setColour(juce::ProgressBar::foregroundColourId, Colours::accentColor);
}

void OscirenderLookAndFeel::drawOscirenderComboBox(juce::Graphics& g, int width, int height, juce::ComboBox& box) {
    juce::Rectangle<int> boxBounds{0, 0, width, height};

    g.setColour(box.findColour(juce::ComboBox::backgroundColourId));
    g.fillRoundedRectangle(boxBounds.toFloat(), (float)RECT_RADIUS);

    juce::Rectangle<int> arrowZone{width - 15, 0, 10, height};
    juce::Path path;
    path.startNewSubPath((float)arrowZone.getX(), (float)arrowZone.getCentreY() - 3.0f);
    path.lineTo((float)arrowZone.getCentreX(), (float)arrowZone.getCentreY() + 4.0f);
    path.lineTo((float)arrowZone.getRight(), (float)arrowZone.getCentreY() - 3.0f);
    path.closeSubPath();

    g.setColour(box.findColour(juce::ComboBox::arrowColourId).withAlpha(box.isEnabled() ? 1.0f : 0.5f));
    g.fillPath(path);
}

void OscirenderLookAndFeel::positionOscirenderComboBoxText(juce::LookAndFeel& lookAndFeel, juce::ComboBox& box, juce::Label& label) {
    label.setMouseCursor(juce::MouseCursor::PointingHandCursor);
    label.setBounds(1, 1, box.getWidth() - 15, box.getHeight() - 2);
    label.setFont(lookAndFeel.getComboBoxFont(box));
}

void OscirenderLookAndFeel::drawLabel(juce::Graphics& g, juce::Label& label) {
    label.setRepaintsOnMouseActivity(true);
    auto baseColour = label.findColour(juce::Label::backgroundColourId);
    if (label.isEditable()) {
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
    drawOscirenderComboBox(g, width, height, box);
}

void OscirenderLookAndFeel::positionComboBoxText(juce::ComboBox& box, juce::Label& label) {
    positionOscirenderComboBoxText(*this, box, label);
}

void OscirenderLookAndFeel::drawTickBox(juce::Graphics& g, juce::Component& component,
    float x, float y, float w, float h,
    const bool ticked,
    const bool isEnabled,
    const bool shouldDrawButtonAsHighlighted,
    const bool shouldDrawButtonAsDown) {
    juce::Rectangle<float> tickBounds(x, y, w, h);

    g.setColour(component.findColour(juce::TextButton::buttonColourId));
    g.fillRoundedRectangle(tickBounds, RECT_RADIUS);

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
    // --- LFO modulated value indicator (drawn BEFORE the base slider so it sits under the thumb) ---
    auto& props = slider.getProperties();
    bool lfoActive = (bool)props.getWithDefault("lfo_active", false);

    if (lfoActive && slider.isHorizontal()) {
        float modPos = (float)(double)props.getWithDefault("lfo_mod_pos", 0.0);
        juce::uint32 colourArgb = (juce::uint32)(juce::int64)props.getWithDefault("lfo_colour", (juce::int64)0xFF00E5FF);
        auto lfoColour = juce::Colour(colourArgb);

        // Match the JUCE V4 track geometry: 6px tall, centred vertically
        float trackHeight = 6.0f;
        float trackY = (float)y + (float)height * 0.5f - trackHeight * 0.5f;
        float trackRadius = trackHeight * 0.5f;

        // Draw a coloured bar from the unmodulated thumb position to the modulated position
        float barLeft = juce::jmin(sliderPos, modPos);
        float barRight = juce::jmax(sliderPos, modPos);
        float barWidth = barRight - barLeft;

        if (barWidth > 0.5f) {
            auto modRect = juce::Rectangle<float>(barLeft, trackY, barWidth, trackHeight);

            // Outer soft glow
            g.setColour(lfoColour.withAlpha(0.2f));
            g.fillRoundedRectangle(modRect.expanded(1.5f, 2.0f), trackRadius + 1.5f);
        }
    }

    // --- Envelope modulated value indicator ---
    bool envActive = (bool)props.getWithDefault("env_active", false);

    if (envActive && slider.isHorizontal()) {
        float modPos = (float)(double)props.getWithDefault("env_mod_pos", 0.0);
        juce::uint32 colourArgb = (juce::uint32)(juce::int64)props.getWithDefault("env_colour", (juce::int64)0xFFFF6E4A);
        auto envColour = juce::Colour(colourArgb);

        float trackHeight = 6.0f;
        float trackY = (float)y + (float)height * 0.5f - trackHeight * 0.5f;
        float trackRadius = trackHeight * 0.5f;

        float barLeft = juce::jmin(sliderPos, modPos);
        float barRight = juce::jmax(sliderPos, modPos);
        float barWidth = barRight - barLeft;

        if (barWidth > 0.5f) {
            auto modRect = juce::Rectangle<float>(barLeft, trackY, barWidth, trackHeight);
            g.setColour(envColour.withAlpha(0.2f));
            g.fillRoundedRectangle(modRect.expanded(1.5f, 2.0f), trackRadius + 1.5f);
        }
    }

    // --- Base slider (track + value fill + thumb) — drawn on top of LFO bar ---
    juce::LookAndFeel_V4::drawLinearSlider(g, x, y, width, height, sliderPos, minSliderPos, maxSliderPos, style, slider);
    
    // --- Thumb outline ring ---
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

juce::TextLayout OscirenderLookAndFeel::layoutTooltipText(const juce::String& text, juce::Colour colour) {
    const float tooltipFontSize = 17.0f;
    const int maxToolTipWidth = 600;

    juce::AttributedString s;
    s.setJustification (juce::Justification::centred);
    s.append (text, juce::Font (tooltipFontSize, juce::Font::bold), colour);

    juce::TextLayout tl;
    tl.createLayoutWithBalancedLineLengths (s, (float) maxToolTipWidth);
    return tl;
}

juce::Rectangle<int> OscirenderLookAndFeel::getTooltipBounds (const juce::String& tipText, juce::Point<int> screenPos, juce::Rectangle<int> parentArea) {
    const juce::TextLayout tl (layoutTooltipText(tipText, juce::Colours::black));

    auto w = (int) (tl.getWidth() + 14.0f);
    auto h = (int) (tl.getHeight() + 6.0f);

    return juce::Rectangle<int> (screenPos.x > parentArea.getCentreX() ? screenPos.x - (w + 12) : screenPos.x + 24,
        screenPos.y > parentArea.getCentreY() ? screenPos.y - (h + 6)  : screenPos.y + 6,
        w, h)
        .constrainedWithin (parentArea);
}

void OscirenderLookAndFeel::drawTooltip(juce::Graphics& g, const juce::String& text, int width, int height) {
    juce::Rectangle<int> bounds (width, height);

    g.setColour(findColour(juce::TooltipWindow::backgroundColourId));
    g.fillRect(bounds);

    layoutTooltipText(text, findColour(juce::TooltipWindow::textColourId))
        .draw(g, {static_cast<float> (width), static_cast<float> (height)});
}

void OscirenderLookAndFeel::drawCornerResizer(juce::Graphics&, int w, int h, bool isMouseOver, bool isMouseDragging) {
    
}

void OscirenderLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& button, bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) {
    LookAndFeel_V4::drawToggleButton(g, button, shouldDrawButtonAsHighlighted, shouldDrawButtonAsDown);
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

juce::MouseCursor OscirenderLookAndFeel::getMouseCursorFor(juce::Component& component) {
    juce::Label* label = dynamic_cast<juce::Label*>(&component);
    if (label != nullptr && label->isEditable()) {
        return juce::MouseCursor::IBeamCursor;
    }
    juce::TextEditor* textEditor = dynamic_cast<juce::TextEditor*>(&component);
    if (textEditor != nullptr) {
        return juce::MouseCursor::IBeamCursor;
    }
    juce::Button* button = dynamic_cast<juce::Button*>(&component);
    if (button != nullptr) {
        return juce::MouseCursor::PointingHandCursor;
    }
    juce::ComboBox* comboBox = dynamic_cast<juce::ComboBox*>(&component);
    if (comboBox != nullptr) {
        return juce::MouseCursor::PointingHandCursor;
    }
    return juce::LookAndFeel_V4::getMouseCursorFor(component);
}

void OscirenderLookAndFeel::drawCallOutBoxBackground(juce::CallOutBox& box, juce::Graphics& g, const juce::Path& path, juce::Image& cachedImage) {
    if (cachedImage.isNull()) {
        cachedImage = juce::Image(juce::Image::ARGB, box.getWidth(), box.getHeight(), true);
        juce::Graphics g2(cachedImage);

        juce::DropShadow(juce::Colours::black.withAlpha(0.7f), 8, juce::Point<int>(0, 2)).drawForPath(g2, path);
    }

    g.setColour(juce::Colours::black);
    g.drawImageAt(cachedImage, 0, 0);

    g.setColour(Colours::darker);
    g.fillPath(path);

    g.setColour(juce::Colours::black);
    g.strokePath(path, juce::PathStrokeType(1.0f));
}

void OscirenderLookAndFeel::drawProgressBar(juce::Graphics& g, juce::ProgressBar& progressBar, int width, int height, double progress, const juce::String& textToShow) {
    switch (progressBar.getResolvedStyle()) {
        case juce::ProgressBar::Style::linear:
            customDrawLinearProgressBar(g, progressBar, width, height, progress, textToShow);
            break;
        case juce::ProgressBar::Style::circular:
            juce::LookAndFeel_V4::drawProgressBar(g, progressBar, width, height, progress, textToShow);
            break;
    }
}

void OscirenderLookAndFeel::customDrawLinearProgressBar(juce::Graphics& g, const juce::ProgressBar& progressBar, int width, int height, double progress, const juce::String& textToShow) {
    auto background = progressBar.findColour(juce::ProgressBar::backgroundColourId);
    auto foreground = progressBar.findColour(juce::ProgressBar::foregroundColourId).withAlpha(0.5f);
    int rectRadius = 2;

    auto barBounds = progressBar.getLocalBounds().toFloat();

    g.setColour(background);
    g.fillRoundedRectangle(barBounds, rectRadius);
    
    juce::String text = textToShow.isEmpty() ? "waiting..." : textToShow;

    if (progress >= 0.0f && progress <= 1.0f) {
        juce::Path p;
        p.addRoundedRectangle(barBounds, rectRadius);
        g.reduceClipRegion(p);

        barBounds.setWidth(barBounds.getWidth() * (float) progress);
        g.setColour(foreground);
        g.fillRoundedRectangle(barBounds, rectRadius);
    } else {
        if (progress == -2) {
            background = juce::Colours::red;
            text = "Error";
        }
        
        // spinning bar..
        g.setColour(background);

        auto stripeWidth = height * 2;
        auto position = static_cast<int>(juce::Time::getMillisecondCounter() / 15) % stripeWidth;

        juce::Path p;

        for (auto x = static_cast<float> (-position); x < (float) (width + stripeWidth); x += (float) stripeWidth) {
            p.addQuadrilateral (x, 0.0f,
                                x + (float) stripeWidth * 0.5f, 0.0f,
                                x, static_cast<float> (height),
                                x - (float) stripeWidth * 0.5f, static_cast<float> (height));
        }

        juce::Image im(juce::Image::ARGB, width, height, true);

        {
            juce::Graphics g2(im);
            g2.setColour(foreground);
            g2.fillRoundedRectangle(barBounds, rectRadius);
        }

        g.setTiledImageFill(im, 0, 0, 0.85f);
        g.fillPath(p);
    }
    
    g.setColour(juce::Colours::white);
    juce::Font font = juce::Font(juce::FontOptions((float) height * 0.9f, juce::Font::bold));
    g.setFont(font);

    g.drawText(text, 0, 0, width, height, juce::Justification::centred, false);
}

juce::Typeface::Ptr OscirenderLookAndFeel::getTypefaceForFont(const juce::Font& font) {
    if (font.getTypefaceName() == juce::Font::getDefaultSansSerifFontName()) {
        if (font.getTypefaceStyle() == "Regular") {
            return regularTypeface;
        } else if (font.getTypefaceStyle() == "Bold") {
            return boldTypeface;
        } else if (font.getTypefaceStyle() == "Italic") {
            return italicTypeface;
        }
    }

    return juce::Font::getDefaultTypefaceForFont(font);
}

void OscirenderLookAndFeel::drawStretchableLayoutResizerBar(juce::Graphics& g, int w, int h, bool isVerticalBar, bool isMouseOver, bool isMouseDragging) {
    if (isMouseOver || isMouseDragging) {
        g.setColour(Colours::accentColor.withAlpha(0.5f));
        g.fillRoundedRectangle(0, 0, w, h, 4.0f);
    }
}

// ============================================================================
// PopupMenu — modern rounded design
// ============================================================================

void OscirenderLookAndFeel::drawPopupMenuBackground(juce::Graphics& g, int width, int height) {
    auto bounds = juce::Rectangle<int>(0, 0, width, height).toFloat();
    constexpr float radius = 8.0f;

    // Background fill — draw edge-to-edge to avoid double border from window inset gap
    g.setColour(Colours::popupBackground);
    g.fillRoundedRectangle(bounds, radius);

    // Subtle border
    g.setColour(juce::Colours::white.withAlpha(0.10f));
    g.drawRoundedRectangle(bounds.reduced(0.5f), radius, 1.0f);
}

void OscirenderLookAndFeel::drawPopupMenuItem(juce::Graphics& g, const juce::Rectangle<int>& area,
                                               bool isSeparator, bool isActive, bool isHighlighted,
                                               bool isTicked, bool hasSubMenu,
                                               const juce::String& text, const juce::String& shortcutKeyText,
                                               const juce::Drawable* icon, const juce::Colour* textColour) {
    if (isSeparator) {
        auto sepArea = area.reduced(8, 0);
        g.setColour(juce::Colours::white.withAlpha(0.08f));
        g.fillRect(sepArea.getX(), area.getCentreY(), sepArea.getWidth(), 1);
        return;
    }

    auto r = area.reduced(5, 1);
    constexpr float itemRadius = 4.0f;

    if (isHighlighted && isActive) {
        // Vital-style: accent colour highlight
        g.setColour(Colours::accentColor.withAlpha(0.6f));
        g.fillRoundedRectangle(r.toFloat(), itemRadius);
    }

    auto textColourToUse = isHighlighted && isActive
                         ? juce::Colours::white
                         : (isActive ? juce::Colours::white.withAlpha(0.88f)
                                     : juce::Colours::white.withAlpha(0.35f));
    if (textColour != nullptr)
        textColourToUse = *textColour;

    auto font = juce::Font(13.0f);
    g.setFont(font);
    g.setColour(textColourToUse);

    auto textArea = r.reduced(10, 0);

    // Tick column — always reserve space so text is aligned across all items
    constexpr int tickW = 10;
    auto tickArea = textArea.removeFromLeft(tickW).toFloat();
    if (isTicked) {
        // Draw a simple checkmark
        auto cx = tickArea.getCentreX() - 1.0f;
        auto cy = tickArea.getCentreY();
        juce::Path tick;
        tick.startNewSubPath(cx - 4.0f, cy);
        tick.lineTo(cx - 1.0f, cy + 3.5f);
        tick.lineTo(cx + 5.0f, cy - 4.0f);
        g.setColour(textColourToUse);
        g.strokePath(tick, juce::PathStrokeType(1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    if (hasSubMenu) {
        auto arrowArea = textArea.removeFromRight(14).toFloat();
        juce::Path arrow;
        auto ay = arrowArea.getCentreY();
        auto ax = arrowArea.getCentreX();
        arrow.startNewSubPath(ax - 2.0f, ay - 4.0f);
        arrow.lineTo(ax + 2.0f, ay);
        arrow.lineTo(ax - 2.0f, ay + 4.0f);
        g.strokePath(arrow, juce::PathStrokeType(1.5f));
    }

    if (text.isNotEmpty())
        g.drawFittedText(text, textArea, juce::Justification::centredLeft, 1);

    if (shortcutKeyText.isNotEmpty()) {
        g.setColour(textColourToUse.withAlpha(0.5f));
        g.setFont(juce::Font(11.0f));
        g.drawText(shortcutKeyText, textArea, juce::Justification::centredRight, true);
    }
}

void OscirenderLookAndFeel::getIdealPopupMenuItemSize(const juce::String& text, bool isSeparator,
                                                       int standardMenuItemHeight, int& idealWidth, int& idealHeight) {
    if (isSeparator) {
        idealWidth = 50;
        idealHeight = 8;
        return;
    }

    auto font = juce::Font(13.0f);
    idealWidth = font.getStringWidth(text) + 40;
    idealHeight = 28;
}

int OscirenderLookAndFeel::getPopupMenuBorderSize() {
    return 4;
}
