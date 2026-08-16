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

    addStandardTopLevelMenus();

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
            addRecentRecordingsSubmenu(menu, processor, RECENT_RECORDING_BASE_ID);
            menu.addSeparator();
            return;
        }

        if (topLevelMenuIndex != fileMenu) {
            return;
        }

        addRecentProjectsSubmenu(menu, processor, RECENT_BASE_ID, CLEAR_RECENT_PROJECTS_ID);

        juce::PopupMenu submenu;
        for (int i = 0; i < (int) examples.size(); i++) {
            submenu.addItem(SUBMENU_ID + i, std::get<0>(examples[i]));
        }

        menu.addSubMenu("Examples", submenu);
        menu.addSeparator();
    };

    customMenuSelectedLogic = [this, examples, fileMenu, videoMenu](int menuItemID, int topLevelMenuIndex) {
        if (topLevelMenuIndex == videoMenu && handleRecentRecordingMenuItem(menuItemID, processor, RECENT_RECORDING_BASE_ID)) {
            return true;
        }

        if (topLevelMenuIndex != fileMenu) {
            return false;
        }

        if (handleRecentProjectMenuItem(menuItemID, processor, editor, RECENT_BASE_ID, CLEAR_RECENT_PROJECTS_ID)) {
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
    addProjectMenuItems(fileMenu, processor, editor);

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
    addSupportMenuItems(aboutMenu, processor, editor);

    addMenuItem(videoMenu, "Render Audio File to Video...", [this] {
        editor.renderAudioFileToVideo();
    });
    addRecordingPreferencesMenuItems(videoMenu, processor, editor);

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

    addMuteMenuItem(audioMenu, processor);
    addStandaloneAudioSettingsMenuItem(audioMenu, processor, editor);

    // Interface menu
    addCommonInterfaceMenuItems(interfaceMenu, processor, editor);
}
