#include "LookAndFeel.h"

#include "audio/modulation/ModulationTypes.h"
#include "components/CustomMidiKeyboardComponent.h"
#include "components/SwitchButton.h"
#include "components/modulation/EnvelopeComponent.h"
#include "components/modulation/NodeGraphComponent.h"

namespace {
    osci::LookAndFeel::TypefaceData makeTypefaceData() {
        return {
            BinaryData::FiraSansRegular_ttf,
            BinaryData::FiraSansRegular_ttfSize,
            BinaryData::FiraSansSemiBold_ttf,
            BinaryData::FiraSansSemiBold_ttfSize,
            BinaryData::FiraSansItalic_ttf,
            BinaryData::FiraSansItalic_ttfSize,
        };
    }
}

PluginLookAndFeel::PluginLookAndFeel()
    : osci::LookAndFeel (makeTypefaceData()) {
    applyPluginColours (*this);
}

PluginLookAndFeel& PluginLookAndFeel::getSharedInstance() {
    static PluginLookAndFeel instance;
    return instance;
}

void PluginLookAndFeel::applyPluginColours (juce::LookAndFeel& lookAndFeel) {
    lookAndFeel.setColour (jux::SwitchButton::switchColour, juce::Colours::white);
    lookAndFeel.setColour (jux::SwitchButton::switchOnBackgroundColour, osci::Colours::accentColor());
    lookAndFeel.setColour (jux::SwitchButton::switchOffBackgroundColour, osci::Colours::grey());

    lookAndFeel.setColour (EnvelopeComponent::Node, osci::Colours::veryDark());
    lookAndFeel.setColour (EnvelopeComponent::ReleaseNode, osci::Colours::veryDark());
    lookAndFeel.setColour (EnvelopeComponent::LoopNode, osci::Colours::veryDark());
    lookAndFeel.setColour (EnvelopeComponent::Line, juce::Colours::white);
    lookAndFeel.setColour (EnvelopeComponent::LoopLine, juce::Colours::white);
    lookAndFeel.setColour (EnvelopeComponent::LegendText, juce::Colours::white);
    lookAndFeel.setColour (EnvelopeComponent::LegendBackground, osci::Colours::veryDark());
    lookAndFeel.setColour (EnvelopeComponent::LineBackground, juce::Colours::white);

    lookAndFeel.setColour (NodeGraphComponent::backgroundColourId, osci::Colours::veryDark());
    lookAndFeel.setColour (NodeGraphComponent::gridLineColourId, osci::Colours::dark());
    lookAndFeel.setColour (NodeGraphComponent::nodeOutlineColourId, juce::Colours::white);

    lookAndFeel.setColour (juce::CustomMidiKeyboardComponent::whiteNoteColourId, osci::Colours::dark().brighter (0.12f));
    lookAndFeel.setColour (juce::CustomMidiKeyboardComponent::blackNoteColourId, osci::Colours::veryDark().brighter (0.03f));
    lookAndFeel.setColour (juce::CustomMidiKeyboardComponent::keySeparatorLineColourId, osci::Colours::veryDark().brighter (0.16f));
    lookAndFeel.setColour (juce::CustomMidiKeyboardComponent::mouseOverKeyOverlayColourId, osci::Colours::accentColor().withAlpha (0.28f));
    lookAndFeel.setColour (juce::CustomMidiKeyboardComponent::keyDownOverlayColourId, osci::Colours::accentColor().withAlpha (0.62f));
    lookAndFeel.setColour (juce::CustomMidiKeyboardComponent::shadowColourId, juce::Colours::transparentBlack);
    lookAndFeel.setColour (juce::CustomMidiKeyboardComponent::upDownButtonBackgroundColourId, osci::Colours::veryDark());
    lookAndFeel.setColour (juce::CustomMidiKeyboardComponent::upDownButtonArrowColourId, juce::Colours::white);
}

void PluginLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height,
                                          float sliderPos, float minSliderPos, float maxSliderPos,
                                          const juce::Slider::SliderStyle style, juce::Slider& slider) {
    auto& props = slider.getProperties();

    if (slider.isHorizontal()) {
        const auto& modTypes = getModulationTypes();

        for (const auto& modType : modTypes) {
            if (! static_cast<bool> (props.getWithDefault (modType.propPrefix + "_active", false))) {
                continue;
            }

            const auto modPos = static_cast<float> (static_cast<double> (props.getWithDefault (modType.propPrefix + "_mod_pos", 0.0)));
            const auto colourArgb = static_cast<juce::uint32> (static_cast<juce::int64> (
                props.getWithDefault (modType.propPrefix + "_colour", static_cast<juce::int64> (modType.defaultColour))));
            const auto modColour = juce::Colour (colourArgb);

            const auto trackHeight = 6.0f;
            const auto trackY = static_cast<float> (y) + static_cast<float> (height) * 0.5f - trackHeight * 0.5f;
            const auto barLeft = juce::jmin (sliderPos, modPos);
            const auto barRight = juce::jmax (sliderPos, modPos);
            const auto barWidth = barRight - barLeft;

            if (barWidth > 0.5f) {
                const auto modRect = juce::Rectangle<float> (barLeft, trackY, barWidth, trackHeight);
                g.setColour (modColour.withAlpha (0.2f));
                g.fillRoundedRectangle (modRect.expanded (1.5f, 2.0f), trackHeight * 0.5f + 1.5f);
            }
        }
    }

    osci::LookAndFeel::drawLinearSlider (g, x, y, width, height, sliderPos, minSliderPos, maxSliderPos, style, slider);
}

void PluginLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                                          float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                                          juce::Slider& slider) {
    auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat();
    const auto diameter = juce::jmin (bounds.getWidth(), bounds.getHeight());
    const auto centre = bounds.getCentre();
    const auto disabledAlpha = slider.isEnabled() ? 1.0f : 0.3f;
    const auto hovered = slider.isEnabled() && slider.isMouseOverOrDragging();
    const auto trackWidth = diameter * (hovered ? 0.108f : 0.09f);
    const auto radius = (diameter - trackWidth) * 0.5f - 4.0f;
    const auto valueAngle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);

    auto& props = slider.getProperties();
    const auto& modTypes = getModulationTypes();
    const auto angleRange = rotaryEndAngle - rotaryStartAngle;

    for (const auto& modType : modTypes) {
        if (! static_cast<bool> (props.getWithDefault (modType.propPrefix + "_active", false))) {
            continue;
        }

        const auto modNorm = static_cast<float> (static_cast<double> (props.getWithDefault (modType.propPrefix + "_mod_pos_norm", static_cast<double> (sliderPos))));
        const auto modAngle = rotaryStartAngle + modNorm * angleRange;
        const auto colourArgb = static_cast<juce::uint32> (static_cast<juce::int64> (
            props.getWithDefault (modType.propPrefix + "_colour", static_cast<juce::int64> (modType.defaultColour))));
        const auto modColour = juce::Colour (colourArgb);
        const auto arcStart = juce::jmin (valueAngle, modAngle);
        const auto arcEnd = juce::jmax (valueAngle, modAngle);

        if (arcEnd - arcStart > 0.01f) {
            juce::Path modArc;
            modArc.addCentredArc (centre.x, centre.y, radius, radius, 0.0f, arcStart, arcEnd, true);
            g.setColour (modColour.withAlpha (0.35f * disabledAlpha));
            g.strokePath (modArc, juce::PathStrokeType (trackWidth + 4.0f,
                                                        juce::PathStrokeType::curved,
                                                        juce::PathStrokeType::rounded));
        }
    }

    osci::LookAndFeel::drawRotarySlider (g, x, y, width, height, sliderPos, rotaryStartAngle, rotaryEndAngle, slider);
}
