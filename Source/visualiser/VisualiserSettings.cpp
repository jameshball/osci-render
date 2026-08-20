#include "VisualiserSettings.h"
#include "VisualiserComponent.h"
#include "RecordingSettings.h"
#ifndef SOSCI
#include "../PluginProcessor.h"
#endif

namespace {
constexpr int outerHorizontalPadding = 20;
constexpr int outerVerticalPadding = 10;
constexpr int sectionGap = 8;
constexpr int controlRowHeight = 30;
constexpr int displayHorizontalPadding = 10;
constexpr int displayBottomPadding = 7;
constexpr int overlayLabelWidth = 120;
constexpr int overlayControlGap = 8;
constexpr int toggleGridTopGap = 8;
constexpr int sweepHorizontalPadding = 12;
constexpr int sweepHeaderPaintedHeight = 30;
constexpr int sweepToggleWidth = 30;
constexpr int sweepToggleHeight = 24;
constexpr int sweepTitleGap = 6;
constexpr int sweepControlGap = 4;
constexpr int sweepSelectorTopGap = 10;
constexpr int selectorLabelHeight = 18;
constexpr int selectorLabelGap = 4;
constexpr int selectorControlHeight = 32;
constexpr int selectorRowGap = 10;
constexpr int selectorColumnGap = 12;
constexpr int twoColumnMinimumWidth = 400;
constexpr int sweepBottomPadding = 10;
constexpr int upgradeButtonHeight = 36;

constexpr int selectorHeight = selectorLabelHeight + selectorLabelGap + selectorControlHeight;
}

VisualiserSettings::VisualiserSettings(VisualiserParameters& p, int numChannels, RecordingParameters& recordingParameters)
    : parameters(p), recordingParameters(recordingParameters), numChannels(numChannels) {
    addAndMakeVisible(displayOptions);
    addAndMakeVisible(lineColour);
    addAndMakeVisible(lightEffects);
    addAndMakeVisible(videoEffects);
    addAndMakeVisible(lineEffects);
    addAndMakeVisible(sweepSettings);
    lineColourSwatch = std::make_shared<osci::ColourSwatchComponent>([this] { return getCurrentLineColour(); }, "Current line colour");
    lineColour.getEffect(0)->setComponent(lineColourSwatch);

    displayOptions.addAndMakeVisible(screenOverlayLabel);
    displayOptions.addAndMakeVisible(screenOverlay);
    displayOptions.addAndMakeVisible(optionToggleGrid);
    optionToggleGrid.setUseViewport(false);
    optionToggleGrid.setMinItemWidth(135);
    optionToggleGrid.setItemHeight(36);
    optionToggleGrid.setItemMargin(3);
    sweepSettings.addAndMakeVisible(sweepToggle);
    sweepSettings.addAndMakeVisible(sweepTitle);
    sweepSettings.addAndMakeVisible(sweepMs);
    sweepSettings.addAndMakeVisible(triggerValue);
    sweepSettings.addAndMakeVisible(triggerSourceLabel);
    sweepSettings.addAndMakeVisible(triggerSourceBox);
    sweepSettings.addAndMakeVisible(triggerSlopeLabel);
    sweepSettings.addAndMakeVisible(triggerSlopeBox);
    sweepTitle.setFont(juce::Font{juce::FontOptions{15.0f}});
    sweepTitle.setColour(juce::Label::textColourId, osci::Colours::text());
    sweepTitle.setJustificationType(juce::Justification::centredLeft);
    sweepTitle.setInterceptsMouseClicks(false, false);
    triggerSourceLabel.setFont(juce::Font{juce::FontOptions{14.0f}});
    triggerSlopeLabel.setFont(juce::Font{juce::FontOptions{14.0f}});
    triggerSourceLabel.setColour(juce::Label::textColourId, osci::Colours::text());
    triggerSlopeLabel.setColour(juce::Label::textColourId, osci::Colours::text());
#if OSCI_PREMIUM
    addAndMakeVisible(scale);
    addAndMakeVisible(position);
    addAndMakeVisible(screenColour);
    optionToggleGrid.addItem(transparentBackgroundToggle);
    optionToggleGrid.addItem(flipVerticalToggle);
    optionToggleGrid.addItem(flipHorizontalToggle);
    optionToggleGrid.addItem(goniometerToggle);
    optionToggleGrid.addItem(shutterSyncToggle);
#endif
#if OSCI_GUI_ENABLE_CHOWDSP_RESAMPLING
    optionToggleGrid.addItem(upsamplingToggle);
#endif
#if !OSCI_PREMIUM
    addAndMakeVisible(upgradeButton);
    upgradeButton.setColour(juce::TextButton::buttonColourId, osci::Colours::accentColor());
    upgradeButton.setColour(juce::TextButton::textColourOffId, osci::Colours::veryDark());
    upgradeButton.onClick = [this] {
        if (onUpgradeRequested) {
            onUpgradeRequested();
        }
    };
#endif

    for (int i = 1; i <= parameters.screenOverlay->max; i++) {
        screenOverlay.addItem(parameters.screenOverlay->getText(parameters.screenOverlay->getNormalisedValue(i)), i);
    }
    screenOverlay.setSelectedId(parameters.screenOverlay->getValueUnnormalised(), juce::dontSendNotification);
    screenOverlay.onChange = [this] {
        parameters.screenOverlay->setUnnormalisedValueNotifyingHost(screenOverlay.getSelectedId());
    };

    triggerSourceBox.addItem("Left", 1);
    triggerSourceBox.addItem("Right", 2);
    triggerSourceBox.onChange = [this] {
        parameters.triggerSource->setUnnormalisedValueNotifyingHost(triggerSourceBox.getSelectedId() - 1);
    };
    triggerSlopeBox.addItem("Rising", 1);
    triggerSlopeBox.addItem("Falling", 2);
    triggerSlopeBox.onChange = [this] {
        parameters.triggerSlope->setUnnormalisedValueNotifyingHost(triggerSlopeBox.getSelectedId() - 1);
    };

    sweepMs.setEnabled(parameters.sweepEnabled->getBoolValue());
    updateTriggerControls();

    sweepMs.slider.setSkewFactorFromMidPoint(100);

    parameters.screenOverlay->addListener(this);
    parameters.sweepEnabled->addListener(this);
    parameters.triggerSource->addListener(this);
    parameters.triggerSlope->addListener(this);
    recordingParameters.canvasWidth.addListener(this);
    recordingParameters.canvasHeight.addListener(this);
    updateScreenOverlayItemsEnabled();
}

VisualiserSettings::~VisualiserSettings() {
    parameters.screenOverlay->removeListener(this);
    parameters.sweepEnabled->removeListener(this);
    parameters.triggerSource->removeListener(this);
    parameters.triggerSlope->removeListener(this);
    recordingParameters.canvasWidth.removeListener(this);
    recordingParameters.canvasHeight.removeListener(this);
}

#ifndef SOSCI
void VisualiserSettings::wireModulation(OscirenderAudioProcessor& processor) {
    lineColour.wireModulation(processor);
    lightEffects.wireModulation(processor);
    videoEffects.wireModulation(processor);
    lineEffects.wireModulation(processor);
    sweepMs.wireModulation(processor);
    triggerValue.wireModulation(processor);
#if OSCI_PREMIUM
    screenColour.wireModulation(processor);
    scale.wireModulation(processor);
    position.wireModulation(processor);
#endif
}
#endif

void VisualiserSettings::paint(juce::Graphics& g) {
    g.fillAll(osci::Colours::darker());
}

void VisualiserSettings::updateScreenOverlayItemsEnabled() {
    auto selectedOverlay = static_cast<ScreenOverlay>((int)parameters.screenOverlay->getValueUnnormalised());
#if OSCI_GUI_ENABLE_ADVANCED_VISUALISER_FEATURES
    const auto canvasSize = recordingParameters.getCanvasSize();
    const bool realisticOverlaysEnabled = VisualiserGeometry::isSquare(canvasSize);
    screenOverlay.setItemEnabled(static_cast<int>(ScreenOverlay::Real), realisticOverlaysEnabled);
    screenOverlay.setItemEnabled(static_cast<int>(ScreenOverlay::VectorDisplay), realisticOverlaysEnabled);
    selectedOverlay = getScreenOverlayForRenderSize(selectedOverlay, canvasSize);
#endif
    screenOverlay.setSelectedId(static_cast<int>(selectedOverlay), juce::dontSendNotification);
}

void VisualiserSettings::updateTriggerControls() {
    const bool enabled = parameters.sweepEnabled->getBoolValue();
    sweepMs.setEnabled(enabled);
    triggerValue.setEnabled(enabled);
    triggerSourceLabel.setEnabled(enabled);
    triggerSourceBox.setEnabled(enabled);
    triggerSlopeLabel.setEnabled(enabled);
    triggerSlopeBox.setEnabled(enabled);
    triggerSourceBox.setSelectedId(parameters.triggerSource->getValueUnnormalised() + 1, juce::dontSendNotification);
    triggerSlopeBox.setSelectedId(parameters.triggerSlope->getValueUnnormalised() + 1, juce::dontSendNotification);
}

juce::Colour VisualiserSettings::getCurrentLineColour() const {
    const auto hue = juce::jlimit(0.0f, 1.0f, parameters.hueEffect->getActualValue() / 360.0f);
    const auto saturation = juce::jlimit(0.0f, 1.0f, parameters.lineSaturationEffect->getActualValue());
    const auto intensityParameter = parameters.intensityEffect->parameters[0];
    const auto intensity = juce::jmap(parameters.intensityEffect->getActualValue(), static_cast<float>(intensityParameter->min), static_cast<float>(intensityParameter->max), 0.0f, 1.0f);
    return juce::Colour::fromHSV(hue, saturation, juce::jlimit(0.0f, 1.0f, intensity), 1.0f);
}

int VisualiserSettings::getDisplayOptionsHeight(int width) const {
    const int gridWidth = juce::jmax(1, width - displayHorizontalPadding * 2);
    const int gridHeight = optionToggleGrid.getNumItems() > 0 ? optionToggleGrid.calculateRequiredHeight(gridWidth) : 0;
    return SettingsSection::headerHeight + controlRowHeight + (gridHeight > 0 ? toggleGridTopGap + gridHeight : 0) + displayBottomPadding;
}

int VisualiserSettings::getSweepSettingsHeight(int width) const {
    const bool useTwoColumns = width >= twoColumnMinimumWidth;
    const int fixedBodyHeight = controlRowHeight + sweepControlGap + controlRowHeight + sweepSelectorTopGap;
    return SettingsSection::headerHeight + fixedBodyHeight + selectorHeight * (useTwoColumns ? 1 : 2)
         + (useTwoColumns ? 0 : selectorRowGap) + sweepBottomPadding;
}

std::vector<VisualiserSettings::LayoutItem> VisualiserSettings::getLayoutItems(int sectionWidth) {
    std::vector<LayoutItem> items;
    items.reserve(10);
    items.push_back({&displayOptions, getDisplayOptionsHeight(sectionWidth)});
    items.push_back({&lineColour, lineColour.getPreferredHeight()});
#if OSCI_PREMIUM
    items.push_back({&screenColour, screenColour.getPreferredHeight()});
#endif
    items.push_back({&lightEffects, lightEffects.getPreferredHeight()});
    items.push_back({&videoEffects, videoEffects.getPreferredHeight()});
    items.push_back({&lineEffects, lineEffects.getPreferredHeight()});
#if OSCI_PREMIUM
    items.push_back({&scale, scale.getPreferredHeight()});
    items.push_back({&position, position.getPreferredHeight()});
#endif
    items.push_back({&sweepSettings, getSweepSettingsHeight(sectionWidth)});
#if !OSCI_PREMIUM
    items.push_back({&upgradeButton, upgradeButtonHeight});
#endif
    return items;
}

int VisualiserSettings::getPreferredHeight(int width) {
    const int sectionWidth = juce::jmax(1, width - outerHorizontalPadding * 2);
    const auto items = getLayoutItems(sectionWidth);
    int height = outerVerticalPadding * 2;
    for (const auto& item : items) {
        height += item.height;
    }
    if (items.size() > 1) {
        height += (static_cast<int>(items.size()) - 1) * sectionGap;
    }
    return height;
}

void VisualiserSettings::setSizeToFitWidth(int width) {
    const int contentWidth = juce::jmax(1, width);
    setSize(contentWidth, getPreferredHeight(contentWidth));
}

void VisualiserSettings::fitToViewport(juce::Viewport& viewport) {
    int contentWidth = juce::jmax(1, viewport.getWidth());
    if (getPreferredHeight(contentWidth) > viewport.getHeight()) {
        contentWidth = juce::jmax(1, contentWidth - viewport.getScrollBarThickness());
    }
    setSizeToFitWidth(contentWidth);
}

void VisualiserSettings::layoutDisplayOptions() {
    auto area = displayOptions.getLocalBounds().reduced(displayHorizontalPadding, 0);
    area.removeFromTop(SettingsSection::headerHeight);

    auto overlayRow = area.removeFromTop(controlRowHeight);
    overlayRow = overlayRow.withSizeKeepingCentre(juce::jmin(520, overlayRow.getWidth()), overlayRow.getHeight());
    screenOverlayLabel.setBounds(overlayRow.removeFromLeft(overlayLabelWidth));
    overlayRow.removeFromLeft(overlayControlGap);
    screenOverlay.setBounds(overlayRow);

    if (optionToggleGrid.getNumItems() == 0) {
        return;
    }

    area.removeFromTop(toggleGridTopGap);
    optionToggleGrid.setBounds(area.removeFromTop(optionToggleGrid.calculateRequiredHeight(area.getWidth())));
}

void VisualiserSettings::layoutSweepSettings() {
    auto area = sweepSettings.getLocalBounds().reduced(sweepHorizontalPadding, 0);
    auto header = area.removeFromTop(SettingsSection::headerHeight);
    auto paintedHeader = header.removeFromTop(sweepHeaderPaintedHeight);
    sweepToggle.setBounds(paintedHeader.removeFromLeft(sweepToggleWidth).withSizeKeepingCentre(sweepToggleWidth, sweepToggleHeight).translated(0, 1));
    paintedHeader.removeFromLeft(sweepTitleGap);
    sweepTitle.setBounds(paintedHeader);

    sweepMs.setBounds(area.removeFromTop(controlRowHeight));
    area.removeFromTop(sweepControlGap);
    triggerValue.setBounds(area.removeFromTop(controlRowHeight));
    area.removeFromTop(sweepSelectorTopGap);

    auto layoutSelector = [](juce::Rectangle<int> selectorArea, juce::Label& label, juce::ComboBox& comboBox) {
        label.setBounds(selectorArea.removeFromTop(selectorLabelHeight));
        selectorArea.removeFromTop(selectorLabelGap);
        comboBox.setBounds(selectorArea.removeFromTop(selectorControlHeight));
    };

    if (sweepSettings.getWidth() >= twoColumnMinimumWidth) {
        auto selectors = area.removeFromTop(selectorHeight);
        auto sourceArea = selectors.removeFromLeft((selectors.getWidth() - selectorColumnGap) / 2);
        selectors.removeFromLeft(selectorColumnGap);
        layoutSelector(sourceArea, triggerSourceLabel, triggerSourceBox);
        layoutSelector(selectors, triggerSlopeLabel, triggerSlopeBox);
    } else {
        layoutSelector(area.removeFromTop(selectorHeight), triggerSourceLabel, triggerSourceBox);
        area.removeFromTop(selectorRowGap);
        layoutSelector(area.removeFromTop(selectorHeight), triggerSlopeLabel, triggerSlopeBox);
    }
}

void VisualiserSettings::resized() {
    auto area = getLocalBounds().reduced(outerHorizontalPadding, outerVerticalPadding);
    const auto items = getLayoutItems(area.getWidth());
    for (size_t index = 0; index < items.size(); ++index) {
        auto itemArea = area.removeFromTop(items[index].height);
#if !OSCI_PREMIUM
        if (items[index].component == &upgradeButton) {
            const int buttonWidth = juce::jlimit(180, 320, itemArea.getWidth());
            itemArea = itemArea.withSizeKeepingCentre(buttonWidth, itemArea.getHeight());
        }
#endif
        items[index].component->setBounds(itemArea);
        if (index + 1 < items.size()) {
            area.removeFromTop(sectionGap);
        }
    }

    layoutDisplayOptions();
    layoutSweepSettings();
}

void VisualiserSettings::parameterValueChanged(int parameterIndex, float newValue) {
    juce::ignoreUnused(newValue);

    const bool screenOverlayChanged = parameterIndex == parameters.screenOverlay->getParameterIndex();
    const bool triggerChanged = parameterIndex == parameters.triggerSource->getParameterIndex()
                             || parameterIndex == parameters.triggerSlope->getParameterIndex()
                             || parameterIndex == parameters.sweepEnabled->getParameterIndex();
    const bool canvasSizeChanged = parameterIndex == recordingParameters.canvasWidth.getParameterIndex()
                                || parameterIndex == recordingParameters.canvasHeight.getParameterIndex();

    if (screenOverlayChanged || canvasSizeChanged || triggerChanged) {
        juce::MessageManager::callAsync([safeThis = juce::Component::SafePointer<VisualiserSettings>(this)] {
            if (safeThis != nullptr) {
                safeThis->updateScreenOverlayItemsEnabled();
                safeThis->updateTriggerControls();
            }
        });
    }
}

void VisualiserSettings::parameterGestureChanged(int parameterIndex, bool gestureIsStarting) {}
