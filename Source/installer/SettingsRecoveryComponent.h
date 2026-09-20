#pragma once

namespace osci::installer {

// Resolve these through the same factory as the apps; never delete a settings directory.
class SettingsRecoveryComponent final : public osci::OverlayComponent {
public:
    std::function<void()> onSettingsReset;

    explicit SettingsRecoveryComponent(juce::String initialProduct) {
        setOverlayTitle("Repair app settings");
        product.setTitle("App");
        product.addItem("osci-render", 1);
        product.addItem("sosci", 2);
        product.addItem("All apps", 3);
        product.setSelectedId(initialProduct == "sosci" ? 2 : 1, juce::dontSendNotification);
        product.onChange = [this] { refresh(); };
        description.setText("Close the apps and any DAWs using their plugins before resetting.\nProjects and recordings will not be deleted.", juce::dontSendNotification);
        description.setFont(juce::FontOptions(14.0f));
        description.setJustificationType(juce::Justification::topLeft);
        globals.setButtonText("Preferences & recent files");
        globals.setTooltip("Interface options, recent files, detached visualiser settings and other app preferences.");
        globals.setToggleState(true, juce::dontSendNotification);
        session.setButtonText("Startup project & audio settings");
        session.setTooltip("The project restored at startup, window position and standalone audio device settings.");
        session.setToggleState(true, juce::dontSendNotification);
        shared.setButtonText("License, updates & install locations");
        shared.setTooltip("Affects all apps: license activation, legal consent, update settings and install locations. You will need to activate again.");
        for (auto* toggle : { &globals, &session, &shared }) {
            toggle->onClick = [this] { refresh(); };
        }
        openGlobals.setButtonText("Show file");
        openSession.setButtonText("Show file");
        openShared.setButtonText("Show file");
        openGlobals.setTitle("Open global preferences");
        openSession.setTitle("Open saved session");
        openShared.setTitle("Open shared settings");
        openGlobals.onClick = [this] { reveal(false); };
        openSession.onClick = [this] { reveal(true); };
        openShared.onClick = [] { revealFile(osci::SettingsStore::optionsForSharedLicensing().getDefaultFile()); };
        reset.setButtonText("Back up & reset selected...");
        reset.onClick = [this] {
            if (!confirming) {
                confirming = true;
                reset.setButtonText("Confirm backup & reset");
                reset.setColour(juce::TextButton::buttonColourId, juce::Colours::darkred.withAlpha(0.72f));
                reset.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
                cancel.setVisible(true);
                status.setText("Back up and reset the selected settings? Each existing file will be kept beside the original."
                               + juce::String(shared.getToggleState() ? "\nLicensing settings will be reset for both apps, so you may need to activate again." : ""), juce::dontSendNotification);
                setSelectionEnabled(false);
                return;
            }
            performReset();
        };
        cancel.setButtonText("Cancel");
        cancel.onClick = [this] { refresh(); };
        status.setJustificationType(juce::Justification::topLeft);
        status.setFont(juce::FontOptions(14.0f));
        status.setMinimumHorizontalScale(1.0f);
        for (auto* component : std::initializer_list<juce::Component*> { &product, &description,
                &globals, &session, &shared, &openGlobals, &openSession, &openShared, &status, &reset, &cancel }) {
            addPanelContentAndMakeVisible(*component);
        }
        refresh();
    }

    juce::Point<int> getPreferredPanelSize() const override { return { 560, 380 }; }

    void resizeContent(juce::Rectangle<int> area) override {
        product.setBounds(area.removeFromTop(34));
        area.removeFromTop(12);
        description.setBounds(area.removeFromTop(46));
        for (auto pair : { std::pair { &globals, &openGlobals }, { &session, &openSession }, { &shared, &openShared } }) {
            auto row = area.removeFromTop(42);
            pair.second->setBounds(row.removeFromRight(100).reduced(0, 5));
            pair.first->setBounds(row.withTrimmedRight(12));
        }
        area.removeFromTop(12);
        auto buttons = area.removeFromBottom(36);
        cancel.setBounds(buttons.removeFromLeft(90));
        reset.setBounds(buttons.removeFromRight(240));
        area.removeFromBottom(12);
        status.setBounds(area);
    }

private:
    juce::ComboBox product;
    juce::Label description, status;
    juce::ToggleButton globals, session, shared;
    juce::TextButton openGlobals, openSession, openShared, reset, cancel;
    bool confirming = false;

    juce::StringArray products() const {
        if (product.getSelectedId() == 3) {
            return { "osci-render", "sosci" };
        }
        return { product.getSelectedId() == 2 ? "sosci" : "osci-render" };
    }

    static void revealFile(const juce::File& file) {
        auto existing = file;
        while (!existing.exists() && existing != existing.getParentDirectory()) {
            existing = existing.getParentDirectory();
        }
        existing.revealToUser();
    }

    void reveal(bool standalone) {
        for (const auto& name : products()) {
            revealFile((standalone ? osci::SettingsStore::optionsForStandaloneApp(name)
                                   : osci::SettingsStore::optionsForProductGlobals(name)).getDefaultFile());
        }
    }

    juce::Array<juce::File> selectedFiles() const {
        juce::Array<juce::File> files;
        for (const auto& name : products()) {
            if (globals.getToggleState()) {
                files.add(osci::SettingsStore::optionsForProductGlobals(name).getDefaultFile());
            }
            if (session.getToggleState()) {
                files.add(osci::SettingsStore::optionsForStandaloneApp(name).getDefaultFile());
            }
        }
        if (shared.getToggleState()) {
            files.add(osci::SettingsStore::optionsForSharedLicensing().getDefaultFile());
        }
        return files;
    }

    void setSelectionEnabled(bool enabled) {
        for (auto* component : std::initializer_list<juce::Component*> {
                 &product, &globals, &session, &shared, &openGlobals, &openSession, &openShared }) {
            component->setEnabled(enabled);
        }
    }

    void refresh() {
        confirming = false;
        setSelectionEnabled(true);
        reset.setButtonText("Back up & reset selected...");
        reset.removeColour(juce::TextButton::buttonColourId);
        reset.removeColour(juce::TextButton::textColourOffId);
        cancel.setVisible(false);
        const bool globalsAvailable = anyProductSettingsFileExists(false);
        const bool sessionAvailable = anyProductSettingsFileExists(true);
        const bool sharedAvailable = osci::SettingsStore::optionsForSharedLicensing().getDefaultFile().existsAsFile();
        globals.setEnabled(globalsAvailable);
        session.setEnabled(sessionAvailable);
        shared.setEnabled(sharedAvailable);
        openGlobals.setEnabled(globalsAvailable);
        openSession.setEnabled(sessionAvailable);
        openShared.setEnabled(sharedAvailable);
        int existing = 0;
        for (const auto& file : selectedFiles()) {
            if (file.existsAsFile()) {
                ++existing;
            }
        }
        reset.setEnabled(existing > 0);
        status.setText(existing > 0 ? juce::String(existing) + (existing == 1 ? " settings file selected. " : " settings files selected. ")
                                       + "Licenses are kept unless licensing settings are selected."
                                   : "No saved settings found for this selection.", juce::dontSendNotification);
    }

    bool anyProductSettingsFileExists(bool standalone) const {
        for (const auto& name : products()) {
            const auto file = (standalone ? osci::SettingsStore::optionsForStandaloneApp(name)
                                          : osci::SettingsStore::optionsForProductGlobals(name)).getDefaultFile();
            if (file.existsAsFile()) {
                return true;
            }
        }
        return false;
    }

    void performReset() {
        juce::StringArray failures;
        int resetCount = 0;
        for (const auto& file : selectedFiles()) {
            if (!file.exists()) {
                continue;
            }
            const auto backup = file.getSiblingFile(file.getFileName() + ".backup-"
                + juce::Time::getCurrentTime().formatted("%Y%m%d-%H%M%S")).getNonexistentSibling();
            if (!file.existsAsFile() || !file.moveFileTo(backup)) {
                failures.add(file.getFullPathName());
            } else {
                ++resetCount;
            }
        }
        refresh();
        status.setText(failures.isEmpty() ? "Reset " + juce::String(resetCount) + (resetCount == 1 ? " settings file. " : " settings files. ")
                                               + "You can reopen the apps now. Backups are beside the original files."
                                         : "Could not reset:\n" + failures.joinIntoString("\n"), juce::dontSendNotification);
        if (resetCount > 0 && onSettingsReset) {
            onSettingsReset();
        }
    }
};

}
