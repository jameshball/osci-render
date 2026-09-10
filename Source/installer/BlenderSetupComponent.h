#pragma once

#include "BlenderInstaller.h"

namespace osci::installer {

class BlenderSetupComponent final : public osci::OverlayComponent {
public:
    BlenderSetupComponent() {
        setOverlayTitle("Blender integration");
        versions.setTitle("Blender version");
        versions.setTextWhenNothingSelected("Choose a Blender installation");
        versions.onChange = [this] {
            if (versions.getSelectedId() == 10000) {
                versions.setSelectedItemIndex(targets.empty() ? -1 : 0, juce::dontSendNotification);
                chooseBlender();
            } else {
                updateSelection();
            }
        };
        replace.setButtonText("Replace existing add-on");
        replace.setTooltip("Disable an older osci-render add-on and use the repository version instead. Other add-ons are unchanged.");
        install.setButtonText("Install");
        install.setEnabled(false);
        install.onClick = [this] {
            if (finished) {
                requestDismiss();
            } else if (retryDiscovery) {
                discover();
            } else if (targets.empty()) {
                chooseBlender();
            } else {
                performInstall();
            }
        };
        close.setButtonText("Back");
        close.onClick = [this] {
            if (!working) {
                requestDismiss();
            }
        };
        manual.setButtonText("Manual installation");
        manual.onClick = [] { juce::URL(BlenderInstaller::manualUrl).launchInDefaultBrowser(); };
        logButton.setButtonText("Details");
        logButton.onClick = [this] { logFile.revealToUser(); };
        status.setJustificationType(juce::Justification::topLeft);
        status.setMinimumHorizontalScale(1.0f);
        status.setFont(juce::FontOptions(14.0f));
        status.setColour(juce::Label::textColourId, juce::Colour(0xffc6c8cb));
        versions.setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff414449));
        versions.setColour(juce::ComboBox::textColourId, juce::Colour(0xfff5f5f5));
        versions.setColour(juce::ComboBox::arrowColourId, juce::Colour(0xfff5f5f5));
        versions.setColour(juce::PopupMenu::backgroundColourId, juce::Colour(0xff303236));
        for (auto* button : { &close, &manual, &logButton }) {
            button->setColour(juce::TextButton::buttonColourId, juce::Colour(0xff414449));
            button->setColour(juce::TextButton::textColourOffId, juce::Colour(0xfff5f5f5));
        }
        install.setColour(juce::TextButton::buttonColourId, osci::Colours::accentColor());
        install.setColour(juce::TextButton::textColourOffId, osci::Colours::veryDark());
        for (auto* component : std::initializer_list<juce::Component*> { &versions,
                &replace, &install, &close, &manual, &logButton, &status }) {
            addPanelContentAndMakeVisible(*component);
        }
        logButton.setVisible(false);
        replace.setVisible(false);
        manual.setVisible(false);
        juce::MessageManager::callAsync([safe = juce::Component::SafePointer<BlenderSetupComponent>(this)] {
            if (safe != nullptr) {
                safe->discover();
            }
        });
    }

    std::function<void(bool)> onBusyChanged;

    juce::Point<int> getPreferredPanelSize() const override {
        return { 504, 290 };
    }

    void resizeContent(juce::Rectangle<int> area) override {
        versions.setBounds(area.removeFromTop(40));
        replace.setBounds(area.removeFromTop(30));
        area.removeFromTop(12);
        auto buttons = area.removeFromBottom(36);
        close.setBounds(buttons.removeFromLeft(110));
        install.setBounds(buttons.removeFromRight(160));
        area.removeFromBottom(10);
        auto links = area.removeFromBottom(30);
        manual.setBounds(links.removeFromLeft(170));
        logButton.setBounds(links.removeFromRight(90));
        status.setBounds(area);
    }

private:
    juce::Label status;
    juce::ComboBox versions;
    juce::TextButton install, close, manual, logButton;
    juce::ToggleButton replace;
    std::unique_ptr<juce::FileChooser> chooser;
    std::vector<BlenderInstaller::Target> targets;
    juce::File logFile;
    bool working = false;
    bool finished = false;
    bool retryDiscovery = false;

    void setWorking(bool value, const juce::String& message) {
        working = value;
        setDismissible(!value);
        versions.setEnabled(!value);
        replace.setEnabled(!value);
        close.setEnabled(!value);
        install.setEnabled(!value);
        status.setText(message, juce::dontSendNotification);
        if (onBusyChanged) {
            onBusyChanged(value);
        }
    }

    void updateSelection() {
        const auto index = versions.getSelectedItemIndex();
        if (index >= 0 && index < static_cast<int>(targets.size())) {
            const auto& target = targets[static_cast<size_t>(index)];
            finished = false;
            versions.setTooltip(target.command.joinIntoString(" "));
            install.setButtonText(target.enabled ? "Update" : "Install");
            replace.setVisible(target.conflicts);
            replace.setToggleState(false, juce::dontSendNotification);
            status.setText(target.conflicts ? "An older add-on is enabled." : "", juce::dontSendNotification);
        }
    }

    void discover(juce::StringArray chosen = {}) {
        retryDiscovery = false;
        finished = false;
        setWorking(true, "Finding Blender...");
        auto safe = juce::Component::SafePointer<BlenderSetupComponent>(this);
        juce::Thread::launch([safe, chosen] {
            BlenderInstaller backend;
            const auto running = BlenderInstaller::blenderRunning();
            auto commands = running ? std::vector<juce::StringArray> {} : chosen.isEmpty() ? BlenderInstaller::candidates() : std::vector<juce::StringArray> { chosen };
            std::vector<BlenderInstaller::Target> found;
            juce::StringArray profiles;
            juce::String error;
            for (const auto& command : commands) {
                BlenderInstaller::Target target;
                const auto result = backend.probe(command, target);
                if (result.wasOk() && !profiles.contains(target.profile)) {
                    profiles.add(target.profile);
                    found.push_back(target);
                } else if (result.failed()) {
                    error = result.getErrorMessage();
                }
            }
            const auto log = backend.saveLog();
            juce::MessageManager::callAsync([safe, found, error, log, running] {
                if (safe == nullptr) {
                    return;
                }
                if (running) {
                    safe->retryDiscovery = true;
                    safe->install.setButtonText("Retry");
                    safe->setWorking(false, "Close Blender to continue.");
                    return;
                }
                safe->targets = found;
                safe->logFile = log;
                safe->logButton.setVisible(found.empty() && error.isNotEmpty() && log.existsAsFile());
                safe->manual.setVisible(found.empty());
                safe->versions.clear();
                safe->replace.setVisible(false);
                for (size_t i = 0; i < found.size(); ++i) {
                    safe->versions.addItem(found[i].label + (found[i].enabled ? " - installed" : ""), static_cast<int>(i + 1));
                }
                safe->versions.addItem("Choose another...", 10000);
                safe->versions.setVisible(!found.empty());
                safe->install.setButtonText(found.empty() ? "Choose Blender..." : "Install");
                safe->setWorking(false, found.empty() ? (error.isNotEmpty() ? error : "Choose Blender 4.2 or newer.") : "");
                if (!found.empty()) {
                    safe->versions.setSelectedItemIndex(0, juce::sendNotificationSync);
                }
            });
        });
    }

    void chooseBlender() {
        chooser = std::make_unique<juce::FileChooser>("Choose Blender 4.2 or newer", juce::File {},
#if JUCE_MAC
            "*.app"
#elif JUCE_WINDOWS
            "*.exe"
#else
            "*"
#endif
        );
        chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
            [safe = juce::Component::SafePointer<BlenderSetupComponent>(this)](const juce::FileChooser& selection) {
                const auto file = selection.getResult();
                if (safe != nullptr && file.exists()) {
                    safe->discover(BlenderInstaller::executableCommand(file));
                }
            });
    }

    void performInstall() {
        const auto index = versions.getSelectedItemIndex();
        if (index < 0 || index >= static_cast<int>(targets.size())) {
            return;
        }
        const auto target = targets[static_cast<size_t>(index)];
        const auto replacing = replace.getToggleState();
        if (target.conflicts && !replacing) {
            status.setText("Allow replacement to continue.", juce::dontSendNotification);
            return;
        }
        manual.setVisible(false);
        logButton.setVisible(false);
        setWorking(true, "Installing...");
        auto safe = juce::Component::SafePointer<BlenderSetupComponent>(this);
        juce::Thread::launch([safe, target, replacing] {
            BlenderInstaller backend;
            const auto result = backend.install(target, replacing);
            const auto log = backend.saveLog(result);
            juce::MessageManager::callAsync([safe, result, log] {
                if (safe == nullptr) {
                    return;
                }
                safe->logFile = log;
                safe->logButton.setVisible(result.failed() && log.existsAsFile());
                safe->manual.setVisible(result.failed());
                const auto error = result.getErrorMessage();
                const auto downloadFailed = error.containsIgnoreCase("sync:") || error.containsIgnoreCase("HTTP");
                safe->setWorking(false, result.wasOk() ? "Installed. Open Blender to get started."
                    : downloadFailed ? "Couldn't download the extension. Retry or use manual installation."
                    : error.length() > 150 ? "Couldn't install the extension. See Details." : error);
                if (result.wasOk()) {
                    auto& installedTarget = safe->targets[static_cast<size_t>(safe->versions.getSelectedItemIndex())];
                    installedTarget.enabled = true;
                    installedTarget.conflicts = false;
                    safe->replace.setVisible(false);
                    safe->finished = true;
                    safe->install.setButtonText("Done");
                }
            });
        });
    }
};

} // namespace osci::installer
