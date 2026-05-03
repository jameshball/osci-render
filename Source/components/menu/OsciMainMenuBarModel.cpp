#include "OsciMainMenuBarModel.h"

#include "../../PluginEditor.h"
#include "../../PluginProcessor.h"

OsciMainMenuBarModel::OsciMainMenuBarModel(OscirenderAudioProcessor& p, OscirenderAudioProcessorEditor& e) : audioProcessor(p), editor(e) {
    resetMenuItems();
}

void OsciMainMenuBarModel::resetMenuItems() {
    MainMenuBarModel::resetMenuItems();

    constexpr int RECENT_BASE_ID = 1000;

    customMenuLogic = [this](juce::PopupMenu& menu, int topLevelMenuIndex) {
        if (topLevelMenuIndex != 0)
            return;

        juce::PopupMenu recentMenu;
        const int added = audioProcessor.createRecentProjectsPopupMenuItems(recentMenu,
                                                                            RECENT_BASE_ID,
                                                                            true,
                                                                            true);
        if (added == 0)
            recentMenu.addItem(RECENT_BASE_ID, "(No Recent Projects)", false);

        menu.addSubMenu("Open Recent", recentMenu);
        menu.addSeparator();
    };

    customMenuSelectedLogic = [this](int menuItemID, int topLevelMenuIndex) {
        if (topLevelMenuIndex != 0)
            return false;

        if (menuItemID < RECENT_BASE_ID)
            return false;

        const int index = menuItemID - RECENT_BASE_ID;
        const auto file = audioProcessor.getRecentProjectFile(index);
        if (file != juce::File() && file.existsAsFile())
            editor.openProject(file);

        return true;
    };

    addTopLevelMenu("File");
    addTopLevelMenu("Edit");
    addTopLevelMenu("About");
    addTopLevelMenu("Video");
    if (editor.processor.wrapperType == juce::AudioProcessor::WrapperType::wrapperType_Standalone) {
        addTopLevelMenu("Audio");
    }
    addTopLevelMenu("Interface");

    const int fileMenu      = 0;
    const int editMenu      = 1;
    const int aboutMenu     = 2;
    const int videoMenu     = 3;
    int nextMenu = 4;
    const int audioMenu     = (editor.processor.wrapperType == juce::AudioProcessor::WrapperType::wrapperType_Standalone) ? nextMenu++ : -1;
    const int interfaceMenu = nextMenu;

    addMenuItem(fileMenu, "Open Project", [this] { editor.openProject(); });
    addMenuItem(fileMenu, "Save Project", [this] { editor.saveProject(); });
    addMenuItem(fileMenu, "Save Project As", [this] { editor.saveProjectAs(); });
    if (editor.processor.wrapperType == juce::AudioProcessor::WrapperType::wrapperType_Standalone) {
        addMenuItem(fileMenu, "Create New Project", [this] { editor.resetToDefault(); });
    }

    addEditMenuItems(editMenu, audioProcessor);

    addMenuItem(aboutMenu, "About osci-render", [this] {
        AboutComponent::Info aboutInfo;
        aboutInfo.imageData = BinaryData::logo_png;
        aboutInfo.imageSize = BinaryData::logo_pngSize;
        aboutInfo.productName = ProjectInfo::projectName;
        aboutInfo.companyName = ProjectInfo::companyName;
        aboutInfo.versionString = ProjectInfo::versionString;
#if OSCI_PREMIUM
        aboutInfo.isPremium = true;
#else
        aboutInfo.isPremium = false;
#endif
        aboutInfo.betaUpdatesEnabled = osci::UpdateSettings(audioProcessor.getProductSlug()).betaUpdatesEnabled();
        aboutInfo.onBetaUpdatesChanged = [this] (bool enabled) {
            osci::UpdateSettings updateSettings(audioProcessor.getProductSlug());
            updateSettings.setReleaseTrack(enabled ? osci::ReleaseTrack::Beta
                                                   : osci::ReleaseTrack::Stable);
            editor.refreshBetaUpdatesButton();
            editor.resized();
        };
        aboutInfo.websiteUrl = "https://osci-render.com";
        aboutInfo.githubUrl = "https://github.com/jameshball/osci-render";
        aboutInfo.credits = {
            { "DJ_Level_3",          "Contributed several features to osci-render" },
            { "Anthony Hall",        "Added many new effects, and improved existing ones" },
            { "BUS ERROR Collective", "Provided source code for the Hilligoss encoder" },
            { "Ener-G",             "Provided his L-system fractal script that formed the basis for the L-system implementation" },
            { "TheDumbDude",         "Contributed several example Lua files" },
            { "LottieFiles",         "Free Lottie animations used as examples (lottiefiles.com)" },
        };
        aboutInfo.blenderPort = std::any_cast<int>(audioProcessor.getProperty("objectServerPort"));

       #if JUCE_WINDOWS
        const bool useNativeTitleBar = editor.processor.wrapperType == juce::AudioProcessor::WrapperType::wrapperType_Standalone;
       #else
        const bool useNativeTitleBar = true;
       #endif
        AboutComponent::launchAsDialog(aboutInfo, useNativeTitleBar);
    });
    addMenuItem(aboutMenu, "License and Updates...", [this] {
        editor.openLicenseAndUpdates();
    });
    addDiagnosticsMenuItems(aboutMenu, audioProcessor);
    addMenuItem(aboutMenu, "Randomize Blender Port", [this] {
        audioProcessor.setObjectServerPort(juce::Random::getSystemRandom().nextInt(juce::Range<int>(51600, 51700)));
    });

#if !OSCI_PREMIUM
    addMenuItem(aboutMenu, "Purchase osci-render premium!", [this] {
        editor.showPremiumSplashScreen();
    });
#endif

    addMenuItem(videoMenu, "Recording Settings...", [this] {
        editor.openRecordingSettings();
    });

    addMenuItem(videoMenu, "Render Audio File to Video...", [this] {
#if OSCI_PREMIUM
        editor.renderAudioFileToVideo();
#else
        editor.showPremiumSplashScreen();
#endif
    });

#if JUCE_MAC || JUCE_WINDOWS
    // Add Syphon/Spout input menu item under Recording
    juce::String syphonMenuLabel =
#if OSCI_PREMIUM
        audioProcessor.syphonInputActive ? "Disconnect Syphon/Spout Input" : "Select Syphon/Spout Input...";
#else
        "Select Syphon/Spout Input...";
#endif

    addMenuItem(videoMenu, syphonMenuLabel, [this] {
#if OSCI_PREMIUM
        if (audioProcessor.syphonInputActive) {
            editor.disconnectSyphonInput();
        } else {
            openSyphonInputDialog();
        }
#else
        editor.showPremiumSplashScreen();
#endif
    });
#endif

    if (editor.processor.wrapperType == juce::AudioProcessor::WrapperType::wrapperType_Standalone) {
        addMenuItem(audioMenu, "Settings...", [this] {
            editor.openAudioSettings();
        });
    }

    // Interface menu
    addToggleMenuItem(interfaceMenu, "Preview effect on hover", [this] {
        bool current = audioProcessor.globalSettings.getBool("previewEffectOnHover", true);
        bool newValue = ! current;
        audioProcessor.globalSettings.set("previewEffectOnHover", newValue);
        audioProcessor.globalSettings.save();
        if (! newValue) {
            juce::SpinLock::ScopedLockType lock(audioProcessor.effectsLock);
            audioProcessor.clearPreviewEffect();
        }
        resetMenuItems(); // update tick state
        }, [this] { return audioProcessor.globalSettings.getBool("previewEffectOnHover", true);
    });

    addListenForSpecialKeysMenuItem(interfaceMenu, audioProcessor);

    addToggleMenuItem(interfaceMenu, "Show MIDI Keyboard", [this] {
        bool current = audioProcessor.globalSettings.getBool("showMidiKeyboard", true);
        audioProcessor.globalSettings.set("showMidiKeyboard", !current);
        audioProcessor.globalSettings.save();
        editor.settings.resized();
        resetMenuItems();
    }, [this] { return audioProcessor.globalSettings.getBool("showMidiKeyboard", true); });
}

#if (JUCE_MAC || JUCE_WINDOWS) && OSCI_PREMIUM
void OsciMainMenuBarModel::openSyphonInputDialog() {
    editor.openSyphonInputDialog();
}
#endif
