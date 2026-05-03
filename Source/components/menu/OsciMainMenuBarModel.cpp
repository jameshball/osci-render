#include "OsciMainMenuBarModel.h"

#include "../../PluginEditor.h"
#include "../../PluginProcessor.h"
#include "InternalSampleRateMenu.h"

OsciMainMenuBarModel::OsciMainMenuBarModel(OscirenderAudioProcessor& p, OscirenderAudioProcessorEditor& e) : audioProcessor(p), editor(e) {
    resetMenuItems();
}

void OsciMainMenuBarModel::resetMenuItems() {
    MainMenuBarModel::resetMenuItems();

    constexpr int RECENT_BASE_ID = 1000;
    constexpr int SAMPLE_RATE_BASE_ID = 2000;
    constexpr int audioMenuIndex = 4;

    customMenuLogic = [this](juce::PopupMenu& menu, int topLevelMenuIndex) {
        if (topLevelMenuIndex == 0) {
            juce::PopupMenu recentMenu;
            const int added = audioProcessor.createRecentProjectsPopupMenuItems(recentMenu,
                                                                                RECENT_BASE_ID,
                                                                                true,
                                                                                true);
            if (added == 0) {
                recentMenu.addItem(RECENT_BASE_ID, "(No Recent Projects)", false);
            }

            menu.addSubMenu("Open Recent", recentMenu);
            menu.addSeparator();
        } else if (topLevelMenuIndex == audioMenuIndex) {
            InternalSampleRateMenu::addSubmenu(menu, audioProcessor, SAMPLE_RATE_BASE_ID);
        }
    };

    customMenuSelectedLogic = [this](int menuItemID, int topLevelMenuIndex) {
        if (topLevelMenuIndex == 0 && menuItemID >= RECENT_BASE_ID) {
            const auto file = audioProcessor.getRecentProjectFile(menuItemID - RECENT_BASE_ID);
            if (file != juce::File() && file.existsAsFile()) {
                editor.openProject(file);
            }
            return true;
        }
        if (topLevelMenuIndex == audioMenuIndex
            && InternalSampleRateMenu::handleMenuId(menuItemID,
                                                    SAMPLE_RATE_BASE_ID,
                                                    audioProcessor,
                                                    [this] { editor.showPremiumSplashScreen(); })) {
            resetMenuItems();
            return true;
        }
        return false;
    };

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
    const int audioMenu     = audioMenuIndex;
    const int interfaceMenu = 5;

    addMenuItem(fileMenu, "Open Project", [this] { editor.openProject(); });
    addMenuItem(fileMenu, "Save Project", [this] { editor.saveProject(); });
    addMenuItem(fileMenu, "Save Project As", [this] { editor.saveProjectAs(); });
    if (editor.processor.wrapperType == juce::AudioProcessor::WrapperType::wrapperType_Standalone) {
        addMenuItem(fileMenu, "Create New Project", [this] { editor.resetToDefault(); });
    }

    addMenuItem(editMenu, "Undo", [this] { audioProcessor.getUndoManager().undo(); }, juce::String::fromUTF8("\xe2\x8c\x98Z"));
    addMenuItem(editMenu, "Redo", [this] { audioProcessor.getUndoManager().redo(); }, juce::String::fromUTF8("\xe2\x87\xa7\xe2\x8c\x98Z"));

    addMenuItem(aboutMenu, "About osci-render", [this] {
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
            { "Ener-G",             "Provided his L-system fractal script that formed the basis for the L-system implementation" },
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

        options.launchAsync();
    });
    addMenuItem(aboutMenu, "Open Log File", [this] {
        audioProcessor.applicationFolder.getChildFile(juce::String(JucePlugin_Name) + ".log").revealToUser();
    });
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

    addToggleMenuItem(interfaceMenu, "Listen for Special Keys", [this] {
        audioProcessor.setAcceptsKeys(! audioProcessor.getAcceptsKeys());
        resetMenuItems();
    }, [this] { return audioProcessor.getAcceptsKeys(); });

    addToggleMenuItem(interfaceMenu, "Show MIDI Keyboard", [this] {
        bool current = audioProcessor.getGlobalBoolValue("showMidiKeyboard", true);
        audioProcessor.setGlobalValue("showMidiKeyboard", !current);
        audioProcessor.saveGlobalSettings();
        editor.settings.resized();
        resetMenuItems();
    }, [this] { return audioProcessor.getGlobalBoolValue("showMidiKeyboard", true); });
}

#if (JUCE_MAC || JUCE_WINDOWS) && OSCI_PREMIUM
void OsciMainMenuBarModel::openSyphonInputDialog() {
    editor.openSyphonInputDialog();
}
#endif
