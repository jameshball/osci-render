#pragma once

#include <JuceHeader.h>
#include <osci_gui/osci_gui.h>

namespace osci::installer {

class LinuxInstallLocationsComponent final : public juce::Component {
public:
    LinuxInstallLocationsComponent() {
        addAndMakeVisible (heading);
        heading.setText ("Installation locations", juce::dontSendNotification);
        heading.setFont (juce::FontOptions (18.0f, juce::Font::bold));

        configureRow (standaloneLabel, standaloneEditor, standaloneBrowseButton,
                      "Standalone application", "Standalone application directory");
        configureRow (vst3Label, vst3Editor, vst3BrowseButton, "VST3 plugins", "VST3 plugin directory");
        standaloneBrowseButton.onClick = [this] { chooseDirectory (standaloneEditor, "Choose application directory"); };
        vst3BrowseButton.onClick = [this] { chooseDirectory (vst3Editor, "Choose VST3 directory"); };

        addAndMakeVisible (useDefaultsButton);
        useDefaultsButton.setButtonText ("Use default locations");
        styleSecondaryButton (useDefaultsButton);
        useDefaultsButton.onClick = [this] {
            requiresEdit = false;
            setLocations (osci::LinuxInstallSettings::defaults());
        };

        addAndMakeVisible (statusLabel);
        statusLabel.setFont (juce::FontOptions (12.5f));
        statusLabel.setJustificationType (juce::Justification::centredLeft);
        statusLabel.setMinimumHorizontalScale (0.8f);

        addAndMakeVisible (cancelButton);
        cancelButton.setButtonText ("Cancel");
        styleSecondaryButton (cancelButton);
        cancelButton.onClick = [this] {
            if (onCancel) {
                onCancel();
            }
        };

        addAndMakeVisible (installButton);
        installButton.setButtonText ("Install");
        installButton.setTitle ("Confirm installation");
        stylePrimaryButton (installButton);
        installButton.onClick = [this] {
            const auto result = validate (osci::LinuxInstaller::MissingDirectoryPolicy::Create);
            if (result.failed()) {
                showValidationResult (result);
                return;
            }
            if (onConfirm) {
                onConfirm (getLocations());
            }
        };
        setSize (620, 210);
    }

    std::function<void (osci::LinuxInstallLocations)> onConfirm;
    std::function<void()> onCancel;

    void load (juce::StringRef productSlug) {
        osci::LinuxInstallLocations locations;
        const auto result = osci::LinuxInstallSettings (productSlug).load (locations);
        requiresEdit = result.failed();
        setLocations (result.wasOk() ? locations : osci::LinuxInstallSettings::defaults());
    }

    osci::LinuxInstallLocations getLocations() const {
        return { juce::File (standaloneEditor.getText().trim()), juce::File (vst3Editor.getText().trim()) };
    }

    juce::Result validate (osci::LinuxInstaller::MissingDirectoryPolicy policy) const {
        if (requiresEdit) {
            return juce::Result::fail ("Saved installation locations are incomplete. Choose both locations.");
        }
        if (!juce::File::isAbsolutePath (standaloneEditor.getText().trim())) {
            return juce::Result::fail ("Enter an absolute standalone application directory.");
        }
        if (!juce::File::isAbsolutePath (vst3Editor.getText().trim())) {
            return juce::Result::fail ("Enter an absolute VST3 plugin directory.");
        }
        return osci::LinuxInstaller::validateLocations (getLocations(), policy);
    }

    void setBusy (bool busy) {
        this->busy = busy;
        standaloneEditor.setEnabled (!busy);
        vst3Editor.setEnabled (!busy);
        standaloneBrowseButton.setEnabled (!busy);
        vst3BrowseButton.setEnabled (!busy);
        useDefaultsButton.setEnabled (!busy);
        cancelButton.setEnabled (!busy);
        refreshValidation();
    }

    void resized() override {
        auto area = getLocalBounds().reduced (2, 4);
        auto buttons = area.removeFromBottom (38).withSizeKeepingCentre (264, 38);
        cancelButton.setBounds (buttons.removeFromLeft (126));
        buttons.removeFromLeft (12);
        installButton.setBounds (buttons.removeFromLeft (126));
        area.removeFromBottom (10);

        auto headingRow = area.removeFromTop (28);
        heading.setBounds (headingRow.removeFromLeft (220));
        useDefaultsButton.setBounds (headingRow.removeFromRight (160));
        area.removeFromTop (8);
        layoutRow (area, standaloneLabel, standaloneEditor, standaloneBrowseButton);
        area.removeFromTop (8);
        layoutRow (area, vst3Label, vst3Editor, vst3BrowseButton);
        area.removeFromTop (6);
        statusLabel.setBounds (area.removeFromTop (24));
    }

private:
    juce::Label heading;
    juce::Label standaloneLabel;
    juce::Label vst3Label;
    osci::TextEditor standaloneEditor;
    osci::TextEditor vst3Editor;
    juce::TextButton standaloneBrowseButton;
    juce::TextButton vst3BrowseButton;
    juce::TextButton useDefaultsButton;
    juce::Label statusLabel;
    juce::TextButton cancelButton;
    juce::TextButton installButton;
    std::unique_ptr<juce::FileChooser> chooser;
    bool requiresEdit = false;
    bool busy = false;

    void configureRow (juce::Label& label, osci::TextEditor& editor, juce::TextButton& browseButton,
                       juce::StringRef labelText, juce::StringRef accessibleName) {
        addAndMakeVisible (label);
        label.setText (labelText, juce::dontSendNotification);
        label.setFont (juce::FontOptions (13.5f, juce::Font::bold));
        label.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.86f));

        addAndMakeVisible (editor);
        editor.setName (accessibleName);
        editor.setTitle (accessibleName);
        editor.setColour (juce::TextEditor::backgroundColourId, osci::Colours::veryDark());
        editor.setColour (juce::TextEditor::textColourId, juce::Colours::white.withAlpha (0.9f));
        editor.setColour (juce::TextEditor::outlineColourId, osci::Colours::grey().withAlpha (0.35f));
        editor.setColour (juce::TextEditor::focusedOutlineColourId, osci::Colours::accentColor());
        editor.setColour (juce::CaretComponent::caretColourId, juce::Colours::white);
        editor.setSelectAllWhenFocused (true);
        editor.onTextChange = [this] {
            requiresEdit = false;
            refreshValidation();
        };
        editor.onFocusLost = [this, &editor] {
            editor.setCaretPosition (0);
            refreshValidation();
        };
        editor.onReturnKey = [this] {
            if (installButton.isEnabled()) {
                installButton.triggerClick();
            }
        };

        addAndMakeVisible (browseButton);
        browseButton.setButtonText ("Browse");
        browseButton.setTitle ("Browse for " + juce::String (accessibleName));
        styleSecondaryButton (browseButton);
    }

    void chooseDirectory (osci::TextEditor& editor, juce::StringRef title) {
        auto initial = juce::File (editor.getText().trim());
        while (initial != juce::File() && !initial.isDirectory()) {
            const auto parent = initial.getParentDirectory();
            if (parent == initial) {
                initial = juce::File::getSpecialLocation (juce::File::userHomeDirectory);
                break;
            }
            initial = parent;
        }

        const auto editsStandalone = &editor == &standaloneEditor;
        chooser = std::make_unique<juce::FileChooser> (juce::String (title), initial);
        auto safeThis = juce::Component::SafePointer<LinuxInstallLocationsComponent> (this);
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                              [safeThis, editsStandalone] (const juce::FileChooser& selectedChooser) {
            if (safeThis == nullptr) {
                return;
            }
            const auto selected = selectedChooser.getResult();
            if (selected != juce::File()) {
                auto& target = editsStandalone ? safeThis->standaloneEditor : safeThis->vst3Editor;
                target.setText (selected.getFullPathName(), true);
                target.setCaretPosition (target.getTotalNumChars());
            }
            safeThis->chooser = nullptr;
        });
    }

    void setLocations (const osci::LinuxInstallLocations& locations) {
        standaloneEditor.setText (locations.standaloneDirectory.getFullPathName(), false);
        vst3Editor.setText (locations.vst3Directory.getFullPathName(), false);
        standaloneEditor.setCaretPosition (0);
        vst3Editor.setCaretPosition (0);
        refreshValidation();
    }

    void refreshValidation() {
        const auto result = validate (osci::LinuxInstaller::MissingDirectoryPolicy::Allow);
        showValidationResult (result);
    }

    void showValidationResult (const juce::Result& result) {
        const auto valid = result.wasOk();
        const auto customVst3 = valid && getLocations().vst3Directory != osci::LinuxInstallSettings::defaults().vst3Directory;
        statusLabel.setColour (juce::Label::textColourId,
                               valid ? juce::Colours::white.withAlpha (0.68f) : juce::Colours::orange);
        statusLabel.setText (!valid ? result.getErrorMessage()
                                   : customVst3 ? "Add this custom VST3 directory to your DAW's plugin search paths."
                                                : "The application launcher and icon are registered automatically.",
                            juce::dontSendNotification);
        installButton.setEnabled (!busy && valid);
        stylePrimaryButton (installButton);
        if (!installButton.isEnabled()) {
            installButton.setColour (juce::TextButton::buttonColourId, osci::Colours::veryDark().brighter (0.18f));
            installButton.setColour (juce::TextButton::textColourOffId, juce::Colours::white);
        }
    }

    static void layoutRow (juce::Rectangle<int>& area, juce::Label& label,
                           osci::TextEditor& editor, juce::TextButton& browseButton) {
        auto row = area.removeFromTop (34);
        label.setBounds (row.removeFromLeft (juce::jmin (145, row.getWidth() / 3)));
        row.removeFromLeft (8);
        browseButton.setBounds (row.removeFromRight (76));
        row.removeFromRight (8);
        editor.setBounds (row);
    }

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
