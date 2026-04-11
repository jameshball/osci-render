#include "ModulationModeOverlay.h"

ModulationModeOverlay::ModulationModeOverlay(ModulationMode currentMode)
    : simpleCard(
          "Simple",
          juce::String::createStringFromData(BinaryData::cog_svg, BinaryData::cog_svgSize),
          "simple",
          juce::Colours::white.withAlpha(0.7f)),
      standardCard(
          "Standard",
          juce::String::createStringFromData(BinaryData::waves_svg, BinaryData::waves_svgSize),
          "standard",
          Colours::accentColor()),
      selectedMode(currentMode)
{
    const auto headingFont = juce::Font(24.0f, juce::Font::bold);
    const auto subtitleFont = juce::Font(15.0f, juce::Font::plain);

    configureLabel(titleLabel, headingFont, juce::Justification::centred);
    titleLabel.setText("Choose your default modulation style", juce::NotificationType::dontSendNotification);
    titleLabel.setMinimumHorizontalScale(1.0f);

    configureLabel(subtitleLabel, subtitleFont, juce::Justification::centred);
    subtitleLabel.setText(
        "This determines how effects are modulated in your projects. "
        "Standard is recommended for most users, similar to synths like Vital and Serum. "
        "You can change this at any time in the Interface menu.",
        juce::NotificationType::dontSendNotification);
    subtitleLabel.setMinimumHorizontalScale(1.0f);

    // Set up cards
    simpleCard.setDescription(
        "Per-slider inline LFO controls and sidechain. "
        "Same behaviour as the free version of osci-render.");
    standardCard.setDescription(
        "Drag-and-drop LFOs, envelopes, random, and sidechain modulation. "
        "Full control over every parameter.");
    standardCard.setRecommended(true);

    simpleCard.onItemSelected = [this](const juce::String&) { selectMode(ModulationMode::Simple); };
    standardCard.onItemSelected = [this](const juce::String&) { selectMode(ModulationMode::Standard); };

    selectMode(selectedMode);

    confirmButton.onClick = [this] {
        if (onModeSelected)
            onModeSelected(selectedMode);
        dismiss();
    };
    confirmButton.setColour(juce::TextButton::buttonColourId, Colours::accentColor());
    confirmButton.setColour(juce::TextButton::textColourOffId, Colours::veryDark());
    confirmButton.setColour(juce::TextButton::textColourOnId, Colours::veryDark());

    addAndMakeVisible(titleLabel);
    addAndMakeVisible(subtitleLabel);
    addAndMakeVisible(simpleCard);
    addAndMakeVisible(standardCard);
    addAndMakeVisible(confirmButton);
}

void ModulationModeOverlay::selectMode(ModulationMode mode)
{
    selectedMode = mode;
    simpleCard.setSelected(mode == ModulationMode::Simple);
    standardCard.setSelected(mode == ModulationMode::Standard);
    repaint();
}

void ModulationModeOverlay::mouseDown(const juce::MouseEvent& e)
{
    // Do NOT dismiss on background click — force the user to choose.
    juce::ignoreUnused(e);
}

void ModulationModeOverlay::resized()
{
    // Compute available panel width to decide if cards wrap
    auto bounds = getLocalBounds();
    auto horizontalMargin = juce::jmax(6, bounds.getWidth() / 10);
    auto estimatedPanelWidth = bounds.getWidth() - 2 * horizontalMargin - 2 * 20; // panel padding

    bool willWrap = estimatedPanelWidth < 2 * MIN_CARD_WIDTH + 24; // 24 = gap + margins
    maxPanelHeight = willWrap ? PANEL_HEIGHT_TWO_ROWS : PANEL_HEIGHT_SINGLE_ROW;

    OverlayComponent::resized();
}

void ModulationModeOverlay::resizeContent(juce::Rectangle<int> contentArea)
{
    auto titleArea = contentArea.removeFromTop(36);
    titleLabel.setBounds(titleArea);

    contentArea.removeFromTop(8);

    // Use a TextLayout to measure the actual height needed for the subtitle
    juce::AttributedString attrStr;
    attrStr.setJustification(juce::Justification::centred);
    attrStr.append(subtitleLabel.getText(), subtitleLabel.getFont(), juce::Colours::white);
    juce::TextLayout subtitleLayout;
    subtitleLayout.createLayout(attrStr, (float)contentArea.getWidth());
    int subtitleHeight = juce::jmax(40, (int)std::ceil(subtitleLayout.getHeight()) + 8);
    auto subtitleArea = contentArea.removeFromTop(subtitleHeight);
    subtitleLabel.setBounds(subtitleArea);

    contentArea.removeFromTop(8);

    auto buttonArea = contentArea.removeFromBottom(56);

    // Cards area — use FlexBox to wrap onto two rows when narrow
    auto cardsArea = contentArea.reduced(0, 4);

    const int gap = 12;

    juce::FlexBox fb;
    fb.flexWrap = juce::FlexBox::Wrap::wrap;
    fb.justifyContent = juce::FlexBox::JustifyContent::center;
    fb.alignContent = juce::FlexBox::AlignContent::center;

    fb.items.add(juce::FlexItem(simpleCard)
                     .withMinWidth((float)MIN_CARD_WIDTH)
                     .withFlex(1.0f)
                     .withHeight((float)CARD_HEIGHT)
                     .withMargin(juce::FlexItem::Margin((float)gap / 2)));

    fb.items.add(juce::FlexItem(standardCard)
                     .withMinWidth((float)MIN_CARD_WIDTH)
                     .withFlex(1.0f)
                     .withHeight((float)CARD_HEIGHT)
                     .withMargin(juce::FlexItem::Margin((float)gap / 2)));

    fb.performLayout(cardsArea);

    // Center the confirm button
    buttonArea = buttonArea.reduced(0, 6);
    auto buttonWidth = juce::jmin(200, buttonArea.getWidth());
    confirmButton.setBounds(buttonArea.withSizeKeepingCentre(buttonWidth, 40));
}
