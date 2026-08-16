#include "MainMenuBarModel.h"

#include "../../CommonPluginEditor.h"
#include "../../CommonPluginProcessor.h"

MainMenuBarModel::MainMenuBarModel() {}

MainMenuBarModel::~MainMenuBarModel() {}

void MainMenuBarModel::addTopLevelMenu(const juce::String& name) {
    topLevelMenuNames.add(name);
    menuItems.push_back({});
    menuItemsChanged();
}

void MainMenuBarModel::addStandardTopLevelMenus() {
    addTopLevelMenu("File");
    addTopLevelMenu("Edit");
    addTopLevelMenu("About");
    addTopLevelMenu("Video");
    addTopLevelMenu("Audio");
    addTopLevelMenu("Interface");
}

void MainMenuBarModel::addMenuItem(int topLevelMenuIndex, const juce::String& name, std::function<void()> action, const juce::String& shortcutKey) {
    menuItems[topLevelMenuIndex].push_back({ name, std::move(action), {}, false, shortcutKey });
    menuItemsChanged();
}

void MainMenuBarModel::addMenuSeparator(int topLevelMenuIndex) {
    MenuItem separator;
    separator.isSeparator = true;
    menuItems[topLevelMenuIndex].push_back(std::move(separator));
    menuItemsChanged();
}

void MainMenuBarModel::addToggleMenuItem(int topLevelMenuIndex, const juce::String& name, std::function<void()> action, std::function<bool()> isTicked, const juce::String& shortcutKey) {
    menuItems[topLevelMenuIndex].push_back({ name, std::move(action), std::move(isTicked), true, shortcutKey });
    menuItemsChanged();
}

void MainMenuBarModel::addDiagnosticsMenuItems(int topLevelMenuIndex, CommonAudioProcessor& processor) {
    addMenuItem(topLevelMenuIndex, "Open Log File", [&processor] {
        processor.applicationFolder.getChildFile(juce::String(JucePlugin_Name) + ".log").revealToUser();
    });
    if (processor.wrapperType == juce::AudioProcessor::WrapperType::wrapperType_Standalone) {
        addMenuItem(topLevelMenuIndex, "Open App Settings File", [] {
            CommonAudioProcessor::getAppSettingsFile().revealToUser();
        });
    }
    addMenuItem(topLevelMenuIndex, "Open Global Settings File", [&processor] {
        processor.globalSettings.getFile().revealToUser();
    });
}

void MainMenuBarModel::addSupportMenuItems(int topLevelMenuIndex, CommonAudioProcessor& processor, CommonPluginEditor& editor) {
    addMenuItem(topLevelMenuIndex, "License and Updates...", [&editor] { editor.openLicenseAndUpdates(); });
    addMenuItem(topLevelMenuIndex, "Send Feedback...", [&editor] { editor.openFeedback(); });
    addDiagnosticsMenuItems(topLevelMenuIndex, processor);
}

void MainMenuBarModel::addEditMenuItems(int topLevelMenuIndex, CommonAudioProcessor& processor) {
   #if JUCE_MAC
    const juce::String undoShortcut = "Cmd+Z";
    const juce::String redoShortcut = "Cmd+Shift+Z";
   #else
    const juce::String undoShortcut = "Ctrl+Z";
    const juce::String redoShortcut = "Ctrl+Shift+Z";
   #endif
    addMenuItem(topLevelMenuIndex, "Undo", [&processor] { processor.getUndoManager().undo(); }, undoShortcut);
    addMenuItem(topLevelMenuIndex, "Redo", [&processor] { processor.getUndoManager().redo(); }, redoShortcut);
}

void MainMenuBarModel::addListenForSpecialKeysMenuItem(int topLevelMenuIndex, CommonAudioProcessor& processor) {
    addToggleMenuItem(topLevelMenuIndex, "Listen for Special Keys", [this, &processor] {
        processor.setAcceptsKeys(! processor.getAcceptsKeys());
    }, [&processor] { return processor.getAcceptsKeys(); });
}

void MainMenuBarModel::addProjectMenuItems(int topLevelMenuIndex, CommonAudioProcessor& processor, CommonPluginEditor& editor) {
    addMenuItem(topLevelMenuIndex, "Open Project", [&editor] { editor.openProject(); }, JUCE_MAC ? "Cmd+O" : "Ctrl+O");
    addMenuItem(topLevelMenuIndex, "Save Project", [&editor] { editor.saveProject(); }, JUCE_MAC ? "Cmd+S" : "Ctrl+S");
    addMenuItem(topLevelMenuIndex, "Save Project As", [&editor] { editor.saveProjectAs(); }, JUCE_MAC ? "Cmd+Shift+S" : "Ctrl+Shift+S");
    if (processor.wrapperType == juce::AudioProcessor::WrapperType::wrapperType_Standalone) {
        addMenuItem(topLevelMenuIndex, "Create New Project", [&editor] { editor.resetToDefault(); }, JUCE_MAC ? "Cmd+N" : "Ctrl+N");
    }
}

void MainMenuBarModel::addRecentProjectsSubmenu(juce::PopupMenu& menu, CommonAudioProcessor& processor, int recentBaseId, int clearRecentId) {
    juce::PopupMenu recentMenu;
    const int added = processor.createRecentProjectsPopupMenuItems(recentMenu, recentBaseId, true, true);
    if (added == 0) {
        recentMenu.addItem(recentBaseId, "(No Recent Projects)", false);
    }
    recentMenu.addSeparator();
    recentMenu.addItem(clearRecentId, "Clear Recent Projects", added > 0);
    menu.addSubMenu("Open Recent", recentMenu);
}

bool MainMenuBarModel::handleRecentProjectMenuItem(int menuItemId, CommonAudioProcessor& processor, CommonPluginEditor& editor, int recentBaseId, int clearRecentId) {
    if (menuItemId == clearRecentId) {
        processor.clearRecentProjectFiles();
        return true;
    }
    if (menuItemId < recentBaseId || menuItemId >= recentBaseId + 10) {
        return false;
    }

    const juce::File file = processor.getRecentProjectFile(menuItemId - recentBaseId);
    if (file.existsAsFile()) {
        editor.openProject(file);
    }
    return true;
}

void MainMenuBarModel::addRecentRecordingsSubmenu(juce::PopupMenu& menu, CommonAudioProcessor& processor, int recentBaseId) {
    juce::PopupMenu recordingsMenu;
    const int added = processor.createRecentRecordingsPopupMenuItems(recordingsMenu, recentBaseId);
    if (added == 0) {
        recordingsMenu.addItem(recentBaseId, "(No Recent Recordings)", false);
    }
    menu.addSubMenu("Recent Recordings", recordingsMenu);
}

bool MainMenuBarModel::handleRecentRecordingMenuItem(int menuItemId, CommonAudioProcessor& processor, int recentBaseId) {
    if (menuItemId < recentBaseId || menuItemId >= recentBaseId + 10) {
        return false;
    }

    const juce::File file = processor.getRecentRecordingFile(menuItemId - recentBaseId);
    if (file.existsAsFile()) {
        file.startAsProcess();
    }
    return true;
}

void MainMenuBarModel::addRecordingPreferencesMenuItems(int topLevelMenuIndex, CommonAudioProcessor& processor, CommonPluginEditor& editor) {
    addToggleMenuItem(topLevelMenuIndex, "Show Video After Export", [&processor] {
        const bool enabled = processor.globalSettings.getBool("showVideoAfterExport", false);
        processor.globalSettings.set("showVideoAfterExport", !enabled);
        processor.globalSettings.save();
    }, [&processor] { return processor.globalSettings.getBool("showVideoAfterExport", false); });
    addMenuSeparator(topLevelMenuIndex);
    addMenuItem(topLevelMenuIndex, "Recording Settings...", [&editor] { editor.openRecordingSettings(); });
}

void MainMenuBarModel::addMuteMenuItem(int topLevelMenuIndex, CommonAudioProcessor& processor) {
    addToggleMenuItem(topLevelMenuIndex, "Mute", [&processor] {
        processor.muteParameter->setBoolValueNotifyingHost(!processor.muteParameter->getBoolValue());
    }, [&processor] { return processor.muteParameter->getBoolValue(); }, JUCE_MAC ? "Cmd+Shift+M" : "Ctrl+Shift+M");
}

void MainMenuBarModel::addStandaloneAudioSettingsMenuItem(int topLevelMenuIndex, CommonAudioProcessor& processor, CommonPluginEditor& editor) {
    if (processor.wrapperType == juce::AudioProcessor::WrapperType::wrapperType_Standalone) {
        addMenuSeparator(topLevelMenuIndex);
        addMenuItem(topLevelMenuIndex, "Settings...", [&editor] { editor.openAudioSettings(); });
    }
}

void MainMenuBarModel::addCommonInterfaceMenuItems(int topLevelMenuIndex, CommonAudioProcessor& processor, CommonPluginEditor& editor) {
    addListenForSpecialKeysMenuItem(topLevelMenuIndex, processor);
#if OSCI_PREMIUM
    addToggleMenuItem(topLevelMenuIndex, "Keep Oscilloscope Window on Top", [&editor] {
        editor.visualiser.setPopoutAlwaysOnTop(!editor.visualiser.isPopoutAlwaysOnTop());
    }, [&editor] { return editor.visualiser.isPopoutAlwaysOnTop(); });
#endif
    if (processor.wrapperType == juce::AudioProcessor::WrapperType::wrapperType_Standalone) {
        addToggleMenuItem(topLevelMenuIndex, "Full Screen", [&editor] { editor.toggleFullScreen(); }, [&editor] { return editor.isFullScreen(); }, "F11");
        addMenuItem(topLevelMenuIndex, "Reset Window Size and Position", [&editor] { editor.resetWindowSizeAndPosition(); });
    }
}

juce::StringArray MainMenuBarModel::getMenuBarNames() {
    return topLevelMenuNames;
}

juce::PopupMenu MainMenuBarModel::getMenuForIndex(int topLevelMenuIndex, const juce::String& menuName) {
    juce::PopupMenu menu;

    if (customMenuLogic) {
        customMenuLogic(menu, topLevelMenuIndex);
    }

    for (int i = 0; i < (int) menuItems[topLevelMenuIndex].size(); i++) {
        auto& mi = menuItems[topLevelMenuIndex][i];
        if (mi.isSeparator) {
            menu.addSeparator();
            continue;
        }
        juce::PopupMenu::Item item(mi.name);
        item.itemID = i + 1;
        if (mi.shortcutKey.isNotEmpty())
            item.shortcutKeyDescription = mi.shortcutKey;
        if (mi.hasTick && mi.isTicked)
            item.setTicked(mi.isTicked());
        menu.addItem(item);
    }

    return menu;
}

void MainMenuBarModel::menuItemSelected(int menuItemID, int topLevelMenuIndex) {
    if (customMenuSelectedLogic && customMenuSelectedLogic(menuItemID, topLevelMenuIndex)) {
        return;
    }
    auto& mi = menuItems[topLevelMenuIndex][menuItemID - 1];
    if (mi.action)
        mi.action();
}

void MainMenuBarModel::menuBarActivated(bool isActive) {}

void MainMenuBarModel::resetMenuItems() {
    topLevelMenuNames.clear();
    menuItems.clear();
}
