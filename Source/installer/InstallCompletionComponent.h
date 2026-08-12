#pragma once

#include <JuceHeader.h>
#include <osci_gui/osci_gui.h>

namespace osci::installer {

class InstallCompletionComponent final : public juce::Component {
public:
    InstallCompletionComponent() {
        addAndMakeVisible (heading);
        heading.setFont (juce::FontOptions (25.0f, juce::Font::bold));
        heading.setJustificationType (juce::Justification::centred);

        addAndMakeVisible (details);
        details.setFont (juce::FontOptions (14.0f));
        details.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.82f));
        details.setJustificationType (juce::Justification::topLeft);
        details.setMinimumHorizontalScale (0.75f);

        addAndMakeVisible (installAnotherButton);
        installAnotherButton.setButtonText ("Install another product");
        styleSecondaryButton (installAnotherButton);
        installAnotherButton.onClick = [this] {
            if (onInstallAnother) {
                onInstallAnother();
            }
        };

        addAndMakeVisible (closeButton);
        closeButton.setButtonText ("Close");
        stylePrimaryButton (closeButton);
        closeButton.onClick = [this] {
            if (onClose) {
                onClose();
            }
        };
    }

    std::function<void()> onInstallAnother;
    std::function<void()> onClose;

    void showResult (juce::StringRef productName, juce::StringRef variant, juce::StringRef version,
                     const osci::LinuxInstaller::Report& report) {
        heading.setText (report.warnings.isEmpty() ? "Installation succeeded"
                                                   : "Installation succeeded with warnings",
                         juce::dontSendNotification);
        heading.setColour (juce::Label::textColourId,
                           report.warnings.isEmpty() ? osci::Colours::text() : osci::Colours::warning());
        auto detailText = juce::String (productName) + " " + variant + " " + version + " is installed.\n\n";
        juce::StringArray plugins;
        for (const auto& path : report.vst3Paths) {
            plugins.add (path.getFullPathName());
        }
        detailText << "Application\n" << report.standalonePath.getFullPathName()
                   << "\n\nVST3 plugins\n" << plugins.joinIntoString ("\n");
        if (!report.warnings.isEmpty()) {
            detailText << "\n\nWarnings\n" << report.warnings.joinIntoString ("\n");
        }
        details.setText (detailText, juce::dontSendNotification);
    }

    void resized() override {
        auto area = getLocalBounds().reduced (42, 32);
        heading.setBounds (area.removeFromTop (42));
        area.removeFromTop (16);
        auto buttons = area.removeFromBottom (42).withSizeKeepingCentre (390, 42);
        area.removeFromBottom (18);
        details.setBounds (area);
        installAnotherButton.setBounds (buttons.removeFromLeft (210));
        buttons.removeFromLeft (14);
        closeButton.setBounds (buttons.removeFromLeft (166));
    }

private:
    juce::Label heading;
    juce::Label details;
    juce::TextButton installAnotherButton;
    juce::TextButton closeButton;

    static void stylePrimaryButton (juce::TextButton& button) {
        button.setColour (juce::TextButton::buttonColourId, osci::Colours::accentColor());
        button.setColour (juce::TextButton::buttonOnColourId, osci::Colours::accentColor().brighter (0.12f));
        button.setColour (juce::TextButton::textColourOffId, osci::Colours::veryDark());
        button.setColour (juce::TextButton::textColourOnId, osci::Colours::veryDark());
    }

    static void styleSecondaryButton (juce::TextButton& button) {
        button.setColour (juce::TextButton::buttonColourId, osci::Colours::veryDark().brighter (0.14f));
        button.setColour (juce::TextButton::buttonOnColourId, osci::Colours::dark().brighter (0.12f));
        button.setColour (juce::TextButton::textColourOffId, juce::Colours::white);
        button.setColour (juce::TextButton::textColourOnId, juce::Colours::white);
    }
};

} // namespace osci::installer
