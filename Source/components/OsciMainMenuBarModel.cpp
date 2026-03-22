#include "OsciMainMenuBarModel.h"

#include "../PluginEditor.h"
#include "../PluginProcessor.h"

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

    addTopLevelMenu("File");      // index 0
    addTopLevelMenu("About");     // index 1
    addTopLevelMenu("Video");     // index 2
    if (editor.processor.wrapperType == juce::AudioProcessor::WrapperType::wrapperType_Standalone) {
        addTopLevelMenu("Audio"); // index 3 (only if standalone)
    }
    addTopLevelMenu("Interface"); // index 3 (if not standalone) or 4 (if standalone)

    addMenuItem(0, "Open Project", [this] { editor.openProject(); });
    addMenuItem(0, "Save Project", [this] { editor.saveProject(); });
    addMenuItem(0, "Save Project As", [this] { editor.saveProjectAs(); });
    if (editor.processor.wrapperType == juce::AudioProcessor::WrapperType::wrapperType_Standalone) {
        addMenuItem(0, "Create New Project", [this] { editor.resetToDefault(); });
    }

    addMenuItem(1, "About osci-render", [this] {
        juce::DialogWindow::LaunchOptions options;
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
        aboutInfo.websiteUrl = "https://osci-render.com";
        aboutInfo.githubUrl = "https://github.com/jameshball/osci-render";
        aboutInfo.credits = {
            { "DJ_Level_3",          "Contributed several features to osci-render" },
            { "Anthony Hall",        "Added many new effects, and improved existing ones" },
            { "BUS ERROR Collective", "Provided source code for the Hilligoss encoder" },
            { "TheDumbDude",         "Contributed several example Lua files" },
        };
        aboutInfo.blenderPort = std::any_cast<int>(audioProcessor.getProperty("objectServerPort"));

        AboutComponent* about = new AboutComponent(aboutInfo);
        options.content.setOwned(about);
        options.dialogTitle = "About";
        options.dialogBackgroundColour = AboutComponent::dialogBackground();
        options.escapeKeyTriggersCloseButton = true;
#if JUCE_WINDOWS
        // if not standalone, use native title bar for compatibility with DAWs
        options.useNativeTitleBar = editor.processor.wrapperType == juce::AudioProcessor::WrapperType::wrapperType_Standalone;
#elif JUCE_MAC
        options.useNativeTitleBar = true;
#endif
        options.resizable = false;

        juce::DialogWindow* dw = options.launchAsync();
    });
    addMenuItem(1, "Randomize Blender Port", [this] {
        audioProcessor.setObjectServerPort(juce::Random::getSystemRandom().nextInt(juce::Range<int>(51600, 51700)));
    });

#if !OSCI_PREMIUM
    addMenuItem(1, "Purchase osci-render premium!", [this] {
        editor.showPremiumSplashScreen();
    });
#endif

    addMenuItem(2, "Recording Settings...", [this] {
        editor.openRecordingSettings();
    });

    addMenuItem(2, "Render Audio File to Video...", [this] {
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

    addMenuItem(2, syphonMenuLabel, [this] {
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
        addMenuItem(3, "Settings...", [this] {
            editor.openAudioSettings();
        });
    }

    // Interface menu index depends on whether Audio menu exists
    int interfaceMenuIndex = (editor.processor.wrapperType == juce::AudioProcessor::WrapperType::wrapperType_Standalone) ? 4 : 3;
    addToggleMenuItem(interfaceMenuIndex, "Preview effect on hover", [this] {
        bool current = audioProcessor.getGlobalBoolValue("previewEffectOnHover", true);
        bool newValue = ! current;
        audioProcessor.setGlobalValue("previewEffectOnHover", newValue);
        audioProcessor.saveGlobalSettings();
        if (! newValue) {
            juce::SpinLock::ScopedLockType lock(audioProcessor.effectsLock);
            audioProcessor.clearPreviewEffect();
        }
        resetMenuItems(); // update tick state
        }, [this] { return audioProcessor.getGlobalBoolValue("previewEffectOnHover", true);
    });

    addToggleMenuItem(interfaceMenuIndex, "Listen for Special Keys", [this] {
        audioProcessor.setAcceptsKeys(! audioProcessor.getAcceptsKeys());
        resetMenuItems();
    }, [this] { return audioProcessor.getAcceptsKeys(); });

#if OSCI_PREMIUM
    addToggleMenuItem(interfaceMenuIndex, "Beginner Mode (applies on restart)", [this] {
        bool current = audioProcessor.getGlobalBoolValue("beginnerMode", false);
        audioProcessor.setGlobalValue("beginnerMode", !current);
        audioProcessor.saveGlobalSettings();
        resetMenuItems();
    }, [this] { return audioProcessor.getGlobalBoolValue("beginnerMode", false); });
#endif
}

#if (JUCE_MAC || JUCE_WINDOWS) && OSCI_PREMIUM
void OsciMainMenuBarModel::openSyphonInputDialog() {
    editor.openSyphonInputDialog();
}
#endif
