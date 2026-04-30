#include "SplashScreenComponent.h"

#include <array>

SplashScreenComponent::SplashScreenComponent() {
    const auto headingFont = juce::Font(juce::FontOptions(26.0f, juce::Font::bold));
    const auto subtitleFont = juce::Font(juce::FontOptions(17.0f, juce::Font::plain));
    configureLabel(titleLabel, headingFont, juce::Justification::centred);
    titleLabel.setText("Upgrade to osci-render Premium", juce::NotificationType::dontSendNotification);

    configureLabel(subtitleLabel, subtitleFont, juce::Justification::centred);
    subtitleLabel.setText("Unlock new features, and more creative firepower.", juce::NotificationType::dontSendNotification);

    benefitsGrid.setUseViewport(true);
    benefitsGrid.setUseCenteringPlaceholders(true);
    benefitsGrid.setItemsInteractive(false);
    benefitsGrid.setItemHeight(120);
    benefitsGrid.setColour(ColourIds::scrollFadeOverlayBackgroundColourId, Colours::veryDark());
    addAndMakeVisible(benefitsGrid);

    configureLabel(supportLabel, juce::Font(juce::FontOptions(14.5f, juce::Font::italic)), juce::Justification::centred);
    supportLabel.setColour(juce::Label::textColourId, Colours::accentColor().brighter(0.2f));
    supportLabel.setText("Purchasing premium directly supports ongoing development. Thank you!", juce::NotificationType::dontSendNotification);
    addAndMakeVisible(supportLabel);

    buildBenefitTiles();

    upgradeButton.onClick = [this] {
        if (onUpgradeClicked) {
            onUpgradeClicked();
        }
        dismiss();
    };

    continueButton.onClick = [this] {
        dismiss();
    };

    upgradeButton.setColour(juce::TextButton::buttonColourId, Colours::accentColor());
    upgradeButton.setColour(juce::TextButton::textColourOffId, Colours::veryDark());
    upgradeButton.setColour(juce::TextButton::textColourOnId, Colours::veryDark());

    continueButton.setColour(juce::TextButton::buttonColourId, Colours::darker().withAlpha(0.8f));
    continueButton.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    continueButton.setColour(juce::TextButton::textColourOnId, juce::Colours::white);

    addAndMakeVisible(titleLabel);
    addAndMakeVisible(subtitleLabel);
    addAndMakeVisible(upgradeButton);
    addAndMakeVisible(continueButton);
}

void SplashScreenComponent::resizeContent(juce::Rectangle<int> contentArea) {

    auto titleArea = contentArea.removeFromTop(40);
    titleLabel.setBounds(titleArea);

    contentArea.removeFromTop(12);
    auto subtitleArea = contentArea.removeFromTop(32);
    subtitleLabel.setBounds(subtitleArea);

    contentArea.removeFromTop(16);
    auto buttonsArea = contentArea.removeFromBottom(68);
    auto supportArea = contentArea.removeFromBottom(30);
    supportArea = supportArea.reduced(0, 2);
    supportLabel.setBounds(supportArea);

    auto benefitsArea = contentArea.reduced(0, 8);
    benefitsGrid.setBounds(benefitsArea);

    const int itemWidth = benefitsGrid.getItemWidthFor(benefitsArea.getWidth());
    if (itemWidth > 0)
    {
        int maximumHeight = 0;
        for (auto* item : benefitsGrid.getItems())
        {
            if (item != nullptr)
                maximumHeight = juce::jmax(maximumHeight, item->getPreferredHeight(itemWidth));
        }

        if (maximumHeight > 0 && benefitsGrid.getItemHeight() != maximumHeight)
            benefitsGrid.setItemHeight(maximumHeight);
    }

    buttonsArea = buttonsArea.reduced(0, 6);
    juce::FlexBox buttonsFlex;
    buttonsFlex.flexDirection = juce::FlexBox::Direction::row;
    buttonsFlex.justifyContent = juce::FlexBox::JustifyContent::center;
    buttonsFlex.alignItems = juce::FlexBox::AlignItems::stretch;
    buttonsFlex.items.add(juce::FlexItem(upgradeButton).withMinWidth(160.0f).withFlex(1.0f).withHeight(40.0f));
    buttonsFlex.items.add(juce::FlexItem().withWidth(12.0f));
    buttonsFlex.items.add(juce::FlexItem(continueButton).withMinWidth(140.0f).withFlex(1.0f).withHeight(40.0f));
    buttonsFlex.performLayout(buttonsArea.toFloat());
}

void SplashScreenComponent::buildBenefitTiles()
{
    benefitsGrid.clearItems();

    struct BenefitEntry
    {
        const char* title;
        const char* description;
        const char* iconData;
        int iconSize;
    };

    static const std::array<BenefitEntry, 11> benefits{{
        { "New Effects", "Twist, Kaleidoscope, Vortex, Multiplex, and more.", BinaryData::distort_svg, BinaryData::distort_svgSize },
        { "Video Support", "Turn MP4 or MOV footage into oscilloscope audio.", BinaryData::desktop_svg, BinaryData::desktop_svgSize },
        { "Recording", "Export your visuals directly to MP4.", BinaryData::record_svg, BinaryData::record_svgSize },
        { "System Audio Capture", "Capture system audio on macOS and Windows.", BinaryData::microphone_svg, BinaryData::microphone_svgSize },
        { "Fractals", "Koch Snowflake, Sierpinski Triangle, and custom fractals.", BinaryData::snowflake_svg, BinaryData::snowflake_svgSize },
        { "Live Video Input", "Perform live via Syphon or Spout feeds.", BinaryData::spout_svg, BinaryData::spout_svgSize },
        { "Synth Engine", "Rewritten synth with polyphony, glide, legato, and microtonal tuning.", BinaryData::waves_svg, BinaryData::waves_svgSize },
        { "Modulation", "8 LFOs, 4 envelopes, random sources, and sidechain modulation.", BinaryData::graph_svg, BinaryData::graph_svgSize },
        { "MIDI CC Learning", "Map hardware MIDI controllers to any parameter.", BinaryData::link_svg, BinaryData::link_svgSize },
        { "And Many More", "Enhanced simulation, advanced controls, popout visualiser, and more.", BinaryData::plus_svg, BinaryData::plus_svgSize },
        { "Support the Project", "Your upgrade funds ongoing releases and support.", BinaryData::cash_svg, BinaryData::cash_svgSize }
    }};

    for (const auto& benefit : benefits)
    {
        auto iconSvg = juce::String::createStringFromData(benefit.iconData, benefit.iconSize);
        auto* tile = new GridItemComponent(benefit.title, iconSvg, benefit.title, Colours::accentColor());
        tile->setDescription(benefit.description);
        tile->setInteractive(false);
        benefitsGrid.addItem(tile);
    }

    benefitsGrid.resized();
}
