#pragma once

namespace osci::installer {

// Resolve these through the same factory as the apps; never delete a settings directory.
class SettingsRecoveryComponent final : public osci::OverlayComponent {
public:
    explicit SettingsRecoveryComponent(juce::String initialProduct) {
        setOverlayTitle("Settings & recovery");
        product.setTitle("App settings");
        product.addItem("osci-render", 1);
        product.addItem("sosci", 2);
        product.addItem("All apps", 3);
        product.setSelectedId(initialProduct == "sosci" ? 2 : 1, juce::dontSendNotification);
        product.onChange = [this] { refresh(); };
        description.setText("Close the apps and any DAWs using their plugins before resetting.\nYour exported projects and recordings are kept.", juce::dontSendNotification);
        description.setFont(juce::FontOptions(14.0f));
        description.setJustificationType(juce::Justification::topLeft);
        globals.setButtonText("Global preferences");
        globals.setTooltip("Detached window, recent files, interface and global app preferences.");
        globals.setToggleState(true, juce::dontSendNotification);
        session.setButtonText("Saved session & audio setup");
        session.setTooltip("The automatically restored project, window position and audio device settings.");
        session.setToggleState(true, juce::dontSendNotification);
        shared.setButtonText("Shared installer & licensing data");
        shared.setTooltip("Affects all apps: licenses, legal consent, updates and install locations. You will need to activate again.");
        for (auto* toggle : { &globals, &session, &shared }) {
            toggle->onClick = [this] { refresh(); };
        }
        openGlobals.setButtonText("Open file");
        openSession.setButtonText("Open file");
        openShared.setButtonText("Open file");
        openGlobals.setTitle("Open global preferences");
        openSession.setTitle("Open saved session");
        openShared.setTitle("Open shared settings");
        openGlobals.onClick = [this] { reveal(false); };
        openSession.onClick = [this] { reveal(true); };
        openShared.onClick = [] { revealFile(osci::SettingsStore::optionsForSharedLicensing().getDefaultFile()); };
        reset.setButtonText("Reset selected settings...");
        reset.onClick = [this] {
            if (!confirming) {
                confirming = true;
                reset.setButtonText("Confirm reset");
                back.setButtonText("Cancel");
                status.setText("Reset the selected settings? A backup of each existing file will be kept beside it."
                               + juce::String(shared.getToggleState() ? "\nShared data reset removes activations for all apps." : ""), juce::dontSendNotification);
                setSelectionEnabled(false);
                return;
            }
            performReset();
        };
        back.setButtonText("Back");
        back.onClick = [this] {
            if (confirming) {
                refresh();
            } else {
                requestDismiss();
            }
        };
        status.setJustificationType(juce::Justification::topLeft);
        status.setFont(juce::FontOptions(14.0f));
        status.setMinimumHorizontalScale(1.0f);
        reset.setColour(juce::TextButton::buttonColourId, osci::Colours::accentColor());
        reset.setColour(juce::TextButton::textColourOffId, osci::Colours::veryDark());
        for (auto* component : std::initializer_list<juce::Component*> { &product, &description,
                &globals, &session, &shared, &openGlobals, &openSession, &openShared, &status, &reset, &back }) {
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
        back.setBounds(buttons.removeFromLeft(90));
        reset.setBounds(buttons.removeFromRight(240));
        area.removeFromBottom(12);
        status.setBounds(area);
    }

private:
    juce::ComboBox product;
    juce::Label description, status;
    juce::ToggleButton globals, session, shared;
    juce::TextButton openGlobals, openSession, openShared, reset, back;
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
        for (auto* component : std::initializer_list<juce::Component*> { &product, &globals, &session, &shared }) {
            component->setEnabled(enabled);
        }
    }

    void refresh() {
        confirming = false;
        setSelectionEnabled(true);
        reset.setButtonText("Reset selected settings...");
        back.setButtonText("Back");
        int existing = 0;
        for (const auto& file : selectedFiles()) {
            if (file.existsAsFile()) {
                ++existing;
            }
        }
        reset.setEnabled(existing > 0);
        status.setText(existing > 0 ? juce::String(existing) + " settings file(s) selected. Licenses are kept unless shared data is selected."
                                   : "No saved settings found for this selection.", juce::dontSendNotification);
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
        status.setText(failures.isEmpty() ? "Reset " + juce::String(resetCount) + " settings file(s). You can reopen the apps now. Backups are beside the original files."
                                         : "Could not reset:\n" + failures.joinIntoString("\n"), juce::dontSendNotification);
    }
};

}
