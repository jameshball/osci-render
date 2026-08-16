#include "OsciMainMenuBarModel.h"

#include "../../PluginEditor.h"
#include "../../PluginProcessor.h"
#include "InternalSampleRateMenu.h"

OsciMainMenuBarModel::OsciMainMenuBarModel(OscirenderAudioProcessor& p, OscirenderAudioProcessorEditor& e) : audioProcessor(p), editor(e) {
    resetMenuItems();
}

void OsciMainMenuBarModel::resetMenuItems() {
    MainMenuBarModel::resetMenuItems();

    constexpr int CLEAR_RECENT_PROJECTS_ID = 900;
    constexpr int RECENT_BASE_ID = 1000;
    constexpr int TEXTURE_INPUT_DISCONNECT_ID = 2000;
    constexpr int TEXTURE_INPUT_SOURCE_BASE_ID = 2100;
    constexpr int SAMPLE_RATE_BASE_ID = 3000;
    constexpr int MIDI_CHANNEL_BASE_ID = 4000;
    constexpr int MIDI_PANIC_ID = 4100;
    constexpr int MIDI_KILL_ID = 4101;
    constexpr int RECENT_RECORDING_BASE_ID = 6000;
    constexpr int fileMenu = 0;
    constexpr int editMenu = 1;
    constexpr int aboutMenu = 2;
    constexpr int videoMenu = 3;
    int nextMenu = 4;
    const int audioMenu = nextMenu++;
    const int interfaceMenu = nextMenu;

    customMenuLogic = [this, fileMenu, videoMenu, audioMenu](juce::PopupMenu& menu, int topLevelMenuIndex) {
        if (topLevelMenuIndex == fileMenu) {
            juce::PopupMenu recentMenu;
            const int added = audioProcessor.createRecentProjectsPopupMenuItems(recentMenu,
                                                                                RECENT_BASE_ID,
                                                                                true,
                                                                                true);
            if (added == 0) {
                recentMenu.addItem(RECENT_BASE_ID, "(No Recent Projects)", false);
            }
            recentMenu.addSeparator();
            recentMenu.addItem(CLEAR_RECENT_PROJECTS_ID, "Clear Recent Projects", added > 0);

            menu.addSubMenu("Open Recent", recentMenu);
            menu.addSeparator();
            return;
        }

        if (topLevelMenuIndex == audioMenu) {
            InternalSampleRateMenu::addSubmenu(menu, audioProcessor, SAMPLE_RATE_BASE_ID);

            juce::PopupMenu channelMenu;
            const int selectedChannel = audioProcessor.midiInputChannel->getValueUnnormalised();
            channelMenu.addItem(MIDI_CHANNEL_BASE_ID, "Omni", true, selectedChannel == 0);
            for (int channel = 1; channel <= 16; channel++) {
                channelMenu.addItem(MIDI_CHANNEL_BASE_ID + channel, "Channel " + juce::String(channel), true, selectedChannel == channel);
            }

            juce::PopupMenu midiMenu;
            midiMenu.addSubMenu("Input Channel", channelMenu);
            midiMenu.addSeparator();
            midiMenu.addItem(MIDI_PANIC_ID, "Panic (Release All Notes)");
            midiMenu.addItem(MIDI_KILL_ID, "Kill Voices Immediately");
            menu.addSubMenu("MIDI", midiMenu);
            return;
        }

        if (topLevelMenuIndex != videoMenu) {
            return;
        }

#if JUCE_MAC || JUCE_WINDOWS
#if !OSCI_PREMIUM
        menu.addItem(TEXTURE_INPUT_SOURCE_BASE_ID, "Select Texture Input...");
        menu.addSeparator();
#else
        juce::PopupMenu sourceMenu;
        textureInputMenuSources.clear();
        const osci::texture::BackendStatus status = osci::texture::getOpenGLBackendStatus();
        if (!status.isAvailable()) {
            const juce::String message = status.message.isNotEmpty() ? status.message : "Texture input is not available in this build.";
            sourceMenu.addItem(TEXTURE_INPUT_SOURCE_BASE_ID, message, false);
        } else {
            if (editor.isTextureInputActive() || audioProcessor.getFileController().isTextureInputActive()) {
                sourceMenu.addItem(TEXTURE_INPUT_DISCONNECT_ID, "Disconnect Texture Input");
                sourceMenu.addSeparator();
            }

            textureInputMenuSources = osci::texture::listOpenGLSources();
            if (textureInputMenuSources.empty()) {
                sourceMenu.addItem(TEXTURE_INPUT_SOURCE_BASE_ID, "No texture sources available", false);
            } else {
                for (int i = 0; i < static_cast<int>(textureInputMenuSources.size()); i++) {
                    const osci::texture::SourceInfo& source = textureInputMenuSources[static_cast<size_t>(i)];
                    juce::String label = source.displayName.isNotEmpty() ? source.displayName : "Texture Source";
                    if (source.applicationName.isNotEmpty()) {
                        label += " (" + source.applicationName + ")";
                    }
                    if (source.width > 0 && source.height > 0) {
                        label += " - " + juce::String(source.width) + "x" + juce::String(source.height);
                    }
                    sourceMenu.addItem(TEXTURE_INPUT_SOURCE_BASE_ID + i, label, source.connectable);
                }
            }
        }

        menu.addSubMenu("Select Texture Input...", sourceMenu);
        menu.addSeparator();
#endif
#endif

        juce::PopupMenu recordingsMenu;
        const int added = audioProcessor.createRecentRecordingsPopupMenuItems(recordingsMenu, RECENT_RECORDING_BASE_ID);
        if (added == 0) {
            recordingsMenu.addItem(RECENT_RECORDING_BASE_ID, "(No Recent Recordings)", false);
        }
        menu.addSubMenu("Recent Recordings", recordingsMenu);
        menu.addSeparator();
    };

    customMenuSelectedLogic = [this, fileMenu, videoMenu, audioMenu](int menuItemID, int topLevelMenuIndex) {
        if (topLevelMenuIndex == audioMenu && menuItemID >= MIDI_CHANNEL_BASE_ID && menuItemID <= MIDI_CHANNEL_BASE_ID + 16) {
            audioProcessor.midiInputChannel->setUnnormalisedValueNotifyingHost(menuItemID - MIDI_CHANNEL_BASE_ID);
            return true;
        }
        if (topLevelMenuIndex == audioMenu && menuItemID == MIDI_PANIC_ID) {
            audioProcessor.sendMidiPanic(false);
            return true;
        }
        if (topLevelMenuIndex == audioMenu && menuItemID == MIDI_KILL_ID) {
            audioProcessor.sendMidiPanic(true);
            return true;
        }
        if (topLevelMenuIndex == audioMenu
            && InternalSampleRateMenu::handleMenuId(menuItemID, SAMPLE_RATE_BASE_ID, audioProcessor, [this] { editor.showPremiumSplashScreen(); })) {
            resetMenuItems();
            return true;
        }

        if (topLevelMenuIndex == fileMenu && menuItemID == CLEAR_RECENT_PROJECTS_ID) {
            audioProcessor.clearRecentProjectFiles();
            return true;
        }

        if (topLevelMenuIndex == fileMenu && menuItemID >= RECENT_BASE_ID) {
            const int index = menuItemID - RECENT_BASE_ID;
            const auto file = audioProcessor.getRecentProjectFile(index);
            if (file != juce::File() && file.existsAsFile()) {
                editor.openProject(file);
            }

            return true;
        }

        if (topLevelMenuIndex == videoMenu && menuItemID >= RECENT_RECORDING_BASE_ID && menuItemID < RECENT_RECORDING_BASE_ID + 10) {
            const auto file = audioProcessor.getRecentRecordingFile(menuItemID - RECENT_RECORDING_BASE_ID);
            if (file.existsAsFile()) {
                file.startAsProcess();
            }
            return true;
        }

#if JUCE_MAC || JUCE_WINDOWS
#if !OSCI_PREMIUM
        if (topLevelMenuIndex == videoMenu && menuItemID == TEXTURE_INPUT_SOURCE_BASE_ID) {
            editor.showPremiumSplashScreen();
            return true;
        }
#else
        if (topLevelMenuIndex == videoMenu && menuItemID == TEXTURE_INPUT_DISCONNECT_ID) {
            editor.stopTextureInput();
            return true;
        }

        if (topLevelMenuIndex == videoMenu && menuItemID >= TEXTURE_INPUT_SOURCE_BASE_ID) {
            const int sourceIndex = menuItemID - TEXTURE_INPUT_SOURCE_BASE_ID;
            if (sourceIndex < 0 || sourceIndex >= static_cast<int>(textureInputMenuSources.size())) {
                return true;
            }

            editor.setTextureInputSource(textureInputMenuSources[static_cast<size_t>(sourceIndex)]);
            return true;
        }
#endif
#endif

        if (topLevelMenuIndex != fileMenu) {
            return false;
        }

        if (menuItemID < RECENT_BASE_ID) {
            return false;
        }

        return true;
    };

    addTopLevelMenu("File");
    addTopLevelMenu("Edit");
    addTopLevelMenu("About");
    addTopLevelMenu("Video");
    addTopLevelMenu("Audio");
    addTopLevelMenu("Interface");

    addMenuItem(fileMenu, "Open Project", [this] { editor.openProject(); }, JUCE_MAC ? "Cmd+O" : "Ctrl+O");
    addMenuItem(fileMenu, "Save Project", [this] { editor.saveProject(); }, JUCE_MAC ? "Cmd+S" : "Ctrl+S");
    addMenuItem(fileMenu, "Save Project As", [this] { editor.saveProjectAs(); }, JUCE_MAC ? "Cmd+Shift+S" : "Ctrl+Shift+S");
    if (editor.processor.wrapperType == juce::AudioProcessor::WrapperType::wrapperType_Standalone) {
        addMenuItem(fileMenu, "Create New Project", [this] { editor.resetToDefault(); }, JUCE_MAC ? "Cmd+N" : "Ctrl+N");
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
        aboutInfo.onSendFeedback = [this] { editor.openFeedback(); };
        aboutInfo.credits = {
            { "DJ_Level_3",          "Contributed several features to osci-render" },
            { "Anthony Hall",        "Added many new effects, and improved existing ones" },
            { "BUS ERROR Collective", "Provided source code for the Hilligoss encoder" },
            { "Ener-G",             "Provided his L-system fractal script that formed the basis for the L-system implementation" },
            { "TheDumbDude",         "Contributed several example Lua files" },
        };
        aboutInfo.blenderPort = std::any_cast<int>(audioProcessor.getProperty("objectServerPort"));

        editor.showOverlay(AboutComponent::createOverlay(aboutInfo));
    });
    addMenuItem(aboutMenu, "License and Updates...", [this] {
        editor.openLicenseAndUpdates();
    });
    addMenuItem(aboutMenu, "Send Feedback...", [this] {
        editor.openFeedback();
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

    addMenuItem(videoMenu, "Render Audio File to Video...", [this] {
#if OSCI_PREMIUM
        editor.renderAudioFileToVideo();
#else
        editor.showPremiumSplashScreen();
#endif
    });
    addToggleMenuItem(videoMenu, "Show Video After Export", [this] {
        const bool enabled = audioProcessor.globalSettings.getBool("showVideoAfterExport", false);
        audioProcessor.globalSettings.set("showVideoAfterExport", !enabled);
        audioProcessor.globalSettings.save();
    }, [this] { return audioProcessor.globalSettings.getBool("showVideoAfterExport", false); });
    addMenuSeparator(videoMenu);
    addMenuItem(videoMenu, "Recording Settings...", [this] {
        editor.openRecordingSettings();
    });

    addToggleMenuItem(audioMenu, "Mute", [this] {
        audioProcessor.muteParameter->setBoolValueNotifyingHost(!audioProcessor.muteParameter->getBoolValue());
    }, [this] { return audioProcessor.muteParameter->getBoolValue(); }, JUCE_MAC ? "Cmd+Shift+M" : "Ctrl+Shift+M");
    addToggleMenuItem(audioMenu, "Swap X/Y", [this] {
        audioProcessor.swapXYOutput->setBoolValueNotifyingHost(!audioProcessor.swapXYOutput->getBoolValue());
    }, [this] { return audioProcessor.swapXYOutput->getBoolValue(); });
    addToggleMenuItem(audioMenu, "Invert X", [this] {
        audioProcessor.invertXOutput->setBoolValueNotifyingHost(!audioProcessor.invertXOutput->getBoolValue());
    }, [this] { return audioProcessor.invertXOutput->getBoolValue(); });
    addToggleMenuItem(audioMenu, "Invert Y", [this] {
        audioProcessor.invertYOutput->setBoolValueNotifyingHost(!audioProcessor.invertYOutput->getBoolValue());
    }, [this] { return audioProcessor.invertYOutput->getBoolValue(); });
    if (editor.processor.wrapperType == juce::AudioProcessor::WrapperType::wrapperType_Standalone) {
        addMenuSeparator(audioMenu);
        addMenuItem(audioMenu, "Settings...", [this] {
            editor.openAudioSettings();
        });
    }
    // Interface menu
    addToggleMenuItem(interfaceMenu, "Preview effect on hover", [this] {
        bool current = audioProcessor.globalSettings.getBool("previewEffectOnHover", true);
        bool newValue = !current;
        audioProcessor.globalSettings.set("previewEffectOnHover", newValue);
        audioProcessor.globalSettings.save();
        if (!newValue) {
            {
                juce::SpinLock::ScopedLockType lock(audioProcessor.effectsLock);
                audioProcessor.clearPreviewEffect();
            }
            audioProcessor.clearPreviewLfoAssignments();
        }
        resetMenuItems(); // update tick state
        }, [this] { return audioProcessor.globalSettings.getBool("previewEffectOnHover", true);
    });

    addListenForSpecialKeysMenuItem(interfaceMenu, audioProcessor);

    if (editor.processor.wrapperType == juce::AudioProcessor::WrapperType::wrapperType_Standalone) {
        addToggleMenuItem(interfaceMenu, "Full Screen", [this] { editor.toggleFullScreen(); }, [this] { return editor.isFullScreen(); }, "F11");
        addMenuItem(interfaceMenu, "Reset Window Size and Position", [this] { editor.resetWindowSizeAndPosition(); });
    }

    addToggleMenuItem(interfaceMenu, "Show MIDI Keyboard", [this] {
        bool current = audioProcessor.globalSettings.getBool("showMidiKeyboard", true);
        audioProcessor.globalSettings.set("showMidiKeyboard", !current);
        audioProcessor.globalSettings.save();
        editor.settings.resized();
        resetMenuItems();
    }, [this] { return audioProcessor.globalSettings.getBool("showMidiKeyboard", true); });
}
