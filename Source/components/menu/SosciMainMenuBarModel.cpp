#include "SosciMainMenuBarModel.h"

#include "../../SosciPluginEditor.h"
#include "../../SosciPluginProcessor.h"

SosciMainMenuBarModel::SosciMainMenuBarModel(SosciPluginEditor& e, SosciAudioProcessor& p) : editor(e), processor(p) {
    resetMenuItems();
}

void SosciMainMenuBarModel::resetMenuItems() {
    MainMenuBarModel::resetMenuItems();

    constexpr int CLEAR_RECENT_PROJECTS_ID = 900;
    constexpr int RECENT_BASE_ID = 1000;
    constexpr int RECENT_RECORDING_BASE_ID = 2000;

    addTopLevelMenu("File");
    addTopLevelMenu("Edit");
    addTopLevelMenu("About");
    addTopLevelMenu("Video");
    addTopLevelMenu("Audio");
    addTopLevelMenu("Interface");

    const int fileMenu      = 0;
    const int editMenu      = 1;
    const int aboutMenu     = 2;
    const int videoMenu     = 3;
    const int audioMenu     = 4;
    const int interfaceMenu = 5;

    std::vector<std::tuple<juce::String, const void*, int>> examples = {
        {"default.sosci", BinaryData::default_sosci, BinaryData::default_sosciSize},
        {"clean.sosci", BinaryData::clean_sosci, BinaryData::clean_sosciSize},
        {"vector_display.sosci", BinaryData::vector_display_sosci, BinaryData::vector_display_sosciSize},
        {"real_oscilloscope.sosci", BinaryData::real_oscilloscope_sosci, BinaryData::real_oscilloscope_sosciSize},
        {"rainbow.sosci", BinaryData::rainbow_sosci, BinaryData::rainbow_sosciSize},
    };

    // This is a hack - ideally I would improve the MainMenuBarModel class to allow for submenus
    customMenuLogic = [this, examples, fileMenu, videoMenu](juce::PopupMenu& menu, int topLevelMenuIndex) {
        if (topLevelMenuIndex == videoMenu) {
            juce::PopupMenu recordingsMenu;
            const int added = processor.createRecentRecordingsPopupMenuItems(recordingsMenu, RECENT_RECORDING_BASE_ID);
            if (added == 0) {
                recordingsMenu.addItem(RECENT_RECORDING_BASE_ID, "(No Recent Recordings)", false);
            }
            menu.addSubMenu("Recent Recordings", recordingsMenu);
            menu.addSeparator();
            return;
        }

        if (topLevelMenuIndex != fileMenu) {
            return;
        }

        juce::PopupMenu recentMenu;
        const int added = processor.createRecentProjectsPopupMenuItems(recentMenu,
                                                                       RECENT_BASE_ID,
                                                                       true,
                                                                       true);
        if (added == 0) {
            recentMenu.addItem(RECENT_BASE_ID, "(No Recent Projects)", false);
        }
        recentMenu.addSeparator();
        recentMenu.addItem(CLEAR_RECENT_PROJECTS_ID, "Clear Recent Projects", added > 0);

        menu.addSubMenu("Open Recent", recentMenu);

        juce::PopupMenu submenu;
        for (int i = 0; i < (int) examples.size(); i++) {
            submenu.addItem(SUBMENU_ID + i, std::get<0>(examples[i]));
        }

        menu.addSubMenu("Examples", submenu);
        menu.addSeparator();
    };

    customMenuSelectedLogic = [this, examples, fileMenu, videoMenu](int menuItemID, int topLevelMenuIndex) {
        if (topLevelMenuIndex == videoMenu && menuItemID >= RECENT_RECORDING_BASE_ID && menuItemID < RECENT_RECORDING_BASE_ID + 10) {
            const auto file = processor.getRecentRecordingFile(menuItemID - RECENT_RECORDING_BASE_ID);
            if (file.existsAsFile()) {
                file.startAsProcess();
            }
            return true;
        }

        if (topLevelMenuIndex != fileMenu) {
            return false;
        }

        if (menuItemID == CLEAR_RECENT_PROJECTS_ID) {
            processor.clearRecentProjectFiles();
            return true;
        }

        if (menuItemID >= RECENT_BASE_ID) {
            const int index = menuItemID - RECENT_BASE_ID;
            const auto file = processor.getRecentProjectFile(index);
            if (file != juce::File() && file.existsAsFile())
                editor.openProject(file);
            return true;
        }

        if (menuItemID >= SUBMENU_ID) {
            int index = menuItemID - SUBMENU_ID;
            processor.setStateInformation(std::get<1>(examples[index]), std::get<2>(examples[index]));
            return true;
        }

        return false;
    };

    addMenuItem(fileMenu, "Open Audio File", [&]() {
        fileChooser = std::make_unique<juce::FileChooser>("Open Audio File", processor.getLastOpenedDirectory(), "*.wav;*.aiff;*.flac;*.ogg;*.mp3");
        auto flags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;
        fileChooser->launchAsync(flags, [&](const juce::FileChooser& chooser) {
            auto file = chooser.getResult();
            if (file != juce::File()) {
                processor.loadAudioFile(file);
                processor.setLastOpenedDirectory(file.getParentDirectory());
            }
        });
    });
    addMenuItem(fileMenu, "Open Project", [&]() { editor.openProject(); }, JUCE_MAC ? "Cmd+O" : "Ctrl+O");
    addMenuItem(fileMenu, "Save Project", [&]() { editor.saveProject(); }, JUCE_MAC ? "Cmd+S" : "Ctrl+S");
    addMenuItem(fileMenu, "Save Project As", [&]() { editor.saveProjectAs(); }, JUCE_MAC ? "Cmd+Shift+S" : "Ctrl+Shift+S");
    if (editor.processor.wrapperType == juce::AudioProcessor::WrapperType::wrapperType_Standalone) {
        addMenuItem(fileMenu, "Create New Project", [&]() { editor.resetToDefault(); }, JUCE_MAC ? "Cmd+N" : "Ctrl+N");
    }

    addEditMenuItems(editMenu, processor);

    addMenuItem(aboutMenu, "About sosci", [&]() {
        AboutComponent::Info aboutInfo;
        aboutInfo.imageData = BinaryData::sosci_logo_png;
        aboutInfo.imageSize = BinaryData::sosci_logo_pngSize;
        aboutInfo.productName = ProjectInfo::projectName;
        aboutInfo.companyName = ProjectInfo::companyName;
        aboutInfo.versionString = ProjectInfo::versionString;
#if OSCI_PREMIUM
        aboutInfo.isPremium = true;
#else
        aboutInfo.isPremium = false;
#endif
        aboutInfo.betaUpdatesEnabled = osci::UpdateSettings(processor.getProductSlug()).betaUpdatesEnabled();
        aboutInfo.onBetaUpdatesChanged = [this] (bool enabled) {
            osci::UpdateSettings updateSettings(processor.getProductSlug());
            updateSettings.setReleaseTrack(enabled ? osci::ReleaseTrack::Beta
                                                   : osci::ReleaseTrack::Stable);
            editor.refreshBetaUpdatesButton();
            editor.resized();
        };
        aboutInfo.websiteUrl = "https://osci-render.com";
        aboutInfo.onSendFeedback = [this] { editor.openFeedback(); };
        aboutInfo.credits = {
            { "Neil Thapen",    "Allowing adaptation of the brilliant dood.al/oscilloscope" },
            { "Kevin Kripper",  "Guiding much of the features and development of sosci" },
            { "DJ_Level_3",     "Testing throughout and helping add features" },
        };

        editor.showOverlay(AboutComponent::createOverlay(aboutInfo));
    });
    addMenuItem(aboutMenu, "License and Updates...", [&]() {
        editor.openLicenseAndUpdates();
    });
    addMenuItem(aboutMenu, "Send Feedback...", [this] {
        editor.openFeedback();
    });
    addDiagnosticsMenuItems(aboutMenu, processor);

    addMenuItem(videoMenu, "Render Audio File to Video...", [this] {
        editor.renderAudioFileToVideo();
    });
    addToggleMenuItem(videoMenu, "Show Video After Export", [this] {
        const bool enabled = processor.globalSettings.getBool("showVideoAfterExport", false);
        processor.globalSettings.set("showVideoAfterExport", !enabled);
        processor.globalSettings.save();
        resetMenuItems();
    }, [this] { return processor.globalSettings.getBool("showVideoAfterExport", false); });
    addMenuSeparator(videoMenu);
    addMenuItem(videoMenu, "Recording Settings...", [this] {
        editor.openRecordingSettings();
    });

    addMenuItem(audioMenu, "Force Disable Brightness Input", [&]() {
        processor.forceDisableBrightnessInput = !processor.forceDisableBrightnessInput;
        if (processor.forceDisableBrightnessInput) {
            // Disabling brightness should also disable RGB
            processor.forceDisableRgbInput = true;
        }
        menuItemsChanged();
    });
    addMenuItem(audioMenu, "Force Disable RGB Input", [&]() {
        processor.forceDisableRgbInput = !processor.forceDisableRgbInput;
        if (!processor.forceDisableRgbInput) {
            // Enabling RGB implies brightness is allowed too
            processor.forceDisableBrightnessInput = false;
        }
        menuItemsChanged();
    });

    addToggleMenuItem(audioMenu, "Mute", [this] {
        processor.muteParameter->setBoolValueNotifyingHost(!processor.muteParameter->getBoolValue());
    }, [this] { return processor.muteParameter->getBoolValue(); }, JUCE_MAC ? "Cmd+Shift+M" : "Ctrl+Shift+M");
    if (editor.processor.wrapperType == juce::AudioProcessor::WrapperType::wrapperType_Standalone) {
        addMenuSeparator(audioMenu);
        addMenuItem(audioMenu, "Settings...", [&]() { editor.openAudioSettings(); });
    }

    // Interface menu
    addListenForSpecialKeysMenuItem(interfaceMenu, processor);
    if (editor.processor.wrapperType == juce::AudioProcessor::WrapperType::wrapperType_Standalone) {
        addToggleMenuItem(interfaceMenu, "Full Screen", [this] { editor.toggleFullScreen(); }, [this] { return editor.isFullScreen(); }, "F11");
        addMenuItem(interfaceMenu, "Reset Window Size and Position", [this] { editor.resetWindowSizeAndPosition(); });
    }
}
