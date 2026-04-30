#include "FrameSettingsComponent.h"
#include "../../PluginEditor.h"

FrameSettingsComponent::FrameSettingsComponent(OscirenderAudioProcessor& p, OscirenderAudioProcessorEditor& editor)
    : audioProcessor(p), pluginEditor(editor)
{
    setText("Frame Settings");

    if (!juce::JUCEApplicationBase::isStandaloneApp()) {
        addAndMakeVisible(sync);
        addAndMakeVisible(offsetLabel);
        addAndMakeVisible(offsetBox);

        offsetLabel.setText("Start Frame", juce::dontSendNotification);
        offsetLabel.setTooltip("Offsets the animation's start point by a specified number of frames.");
        offsetBox.setJustification(juce::Justification::left);
    } else {
        audioProcessor.animationSyncBPM->setValueNotifyingHost(false);
    }

    addAndMakeVisible(invertImage);
    addAndMakeVisible(threshold);
    addAndMakeVisible(stride);

    update();

    auto updateAnimation = [this]() {
        audioProcessor.animationOffset->setUnnormalisedValueNotifyingHost(offsetBox.getValue());
    };

    offsetBox.onFocusLost = updateAnimation;
    offsetBox.onReturnKey = updateAnimation;

    audioProcessor.animationOffset->addListener(this);
}

FrameSettingsComponent::~FrameSettingsComponent()
{
    audioProcessor.animationOffset->removeListener(this);
}

void FrameSettingsComponent::resized()
{
    const int topMargin = 20;
    const int sideMargins = 40;
    const int rowHeight = 24;
    const int effectRowHeight = 30;
    const int rowGap = 6;
    const bool isPlugin = !juce::JUCEApplicationBase::isStandaloneApp();

    auto area = getLocalBounds().withTrimmedTop(topMargin).reduced(20);
    int heightUsed = topMargin + sideMargins;
    int firstColumnHeight = 0;
    int secondColumnHeight = 0;
    bool toggleRowUsed = false;

    auto firstColumn = area.removeFromLeft(220);
    auto toggleBounds = isPlugin && (animated || image) ? firstColumn.removeFromTop(rowHeight) : juce::Rectangle<int>();
    const int visibleToggles = (animated ? 1 : 0) + (image ? 1 : 0);
    const int toggleWidth = visibleToggles > 0 ? juce::jmin(toggleBounds.getWidth() / visibleToggles, 150) : 0;

    if (animated && isPlugin) {
        sync.setBounds(toggleBounds.removeFromLeft(toggleWidth));
        toggleRowUsed = true;

        firstColumn.removeFromTop(rowGap);
        firstColumnHeight += rowHeight + rowGap;

        auto offsetBounds = firstColumn.removeFromTop(rowHeight);
        offsetLabel.setBounds(offsetBounds.removeFromLeft(140));
        offsetBox.setBounds(offsetBounds.removeFromLeft(60));
        firstColumnHeight += rowHeight;
    }

    if (image) {
        if (isPlugin) {
            if (!toggleRowUsed) {
                firstColumnHeight += rowHeight;
                toggleRowUsed = true;
            }
            invertImage.setBounds(toggleBounds.removeFromLeft(toggleWidth));
        } else {
            invertImage.setBounds(firstColumn.removeFromTop(rowHeight));
            firstColumnHeight += rowHeight;
        }

        auto secondColumn = area;
        threshold.setBounds(secondColumn.removeFromTop(effectRowHeight));
        secondColumnHeight += effectRowHeight;
        stride.setBounds(secondColumn.removeFromTop(effectRowHeight));
        secondColumnHeight += effectRowHeight;
    }

    if (toggleRowUsed && firstColumnHeight < rowHeight)
        firstColumnHeight = rowHeight;

    heightUsed += juce::jmax(firstColumnHeight, secondColumnHeight);
    cachedPreferredHeight = heightUsed;
}

void FrameSettingsComponent::update()
{
    offsetBox.setValue(audioProcessor.animationOffset->getValueUnnormalised(), false, 2);
}

void FrameSettingsComponent::parameterValueChanged(int, float)
{
    triggerAsyncUpdate();
}

void FrameSettingsComponent::parameterGestureChanged(int, bool) {}

void FrameSettingsComponent::handleAsyncUpdate()
{
    update();
}

void FrameSettingsComponent::setAnimated(bool shouldBeAnimated)
{
    animated = shouldBeAnimated;
    const bool showAnimationControls = animated && !juce::JUCEApplicationBase::isStandaloneApp();
    sync.setVisible(showAnimationControls);
    offsetBox.setVisible(showAnimationControls);
    offsetLabel.setVisible(showAnimationControls);
}

void FrameSettingsComponent::setImage(bool shouldBeImage)
{
    image = shouldBeImage;
    invertImage.setVisible(image);
    threshold.setVisible(image);
    stride.setVisible(image);
}

int FrameSettingsComponent::getPreferredHeight() const
{
    return cachedPreferredHeight > 0 ? cachedPreferredHeight : 70;
}
