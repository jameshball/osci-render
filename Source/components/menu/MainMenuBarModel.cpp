#include "MainMenuBarModel.h"

#include "../../CommonPluginEditor.h"
#include "../../CommonPluginProcessor.h"

namespace {
juce::String commandShortcut(const juce::String& keys) {
#if JUCE_MAC
    return "Cmd+" + keys;
#else
    return "Ctrl+" + keys;
#endif
}
}

MainMenuBarModel::MainMenuBarModel() {}

MainMenuBarModel::~MainMenuBarModel() {}

void MainMenuBarModel::addTopLevelMenu(const juce::String& name) {
    topLevelMenuNames.add(name);
    menuItems.push_back({});
    openSubMenus.push_back(-1);
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

void MainMenuBarModel::addItem(int topLevelMenuIndex, MenuItem item) {
    item.parent = openSubMenus[(size_t) topLevelMenuIndex];
    menuItems[(size_t) topLevelMenuIndex].push_back(std::move(item));
    menuItemsChanged();
}

void MainMenuBarModel::addMenuItem(int topLevelMenuIndex, const juce::String& name, std::function<void()> action, const juce::String& shortcutKey) {
    addItem(topLevelMenuIndex, { name, std::move(action), {}, false, shortcutKey });
}

void MainMenuBarModel::addMenuSeparator(int topLevelMenuIndex) {
    MenuItem separator;
    separator.isSeparator = true;
    addItem(topLevelMenuIndex, std::move(separator));
}

void MainMenuBarModel::beginSubMenu(int topLevelMenuIndex, const juce::String& name) {
    MenuItem subMenu;
    subMenu.name = name;
    subMenu.isSubMenu = true;
    addItem(topLevelMenuIndex, std::move(subMenu));
    openSubMenus[(size_t) topLevelMenuIndex] = (int) menuItems[(size_t) topLevelMenuIndex].size() - 1;
}

void MainMenuBarModel::endSubMenu(int topLevelMenuIndex) {
    auto& open = openSubMenus[(size_t) topLevelMenuIndex];
    if (open >= 0) {
        open = menuItems[(size_t) topLevelMenuIndex][(size_t) open].parent;
    }
}

void MainMenuBarModel::addToggleMenuItem(int topLevelMenuIndex, const juce::String& name, std::function<void()> action, std::function<bool()> isTicked, const juce::String& shortcutKey) {
    addItem(topLevelMenuIndex, { name, std::move(action), std::move(isTicked), true, shortcutKey });
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
    addMenuItem(topLevelMenuIndex, "Open Project", [&editor] { editor.openProject(); }, commandShortcut("O"));
    addMenuItem(topLevelMenuIndex, "Save Project", [&editor] { editor.saveProject(); }, commandShortcut("S"));
    addMenuItem(topLevelMenuIndex, "Save Project As", [&editor] { editor.saveProjectAs(); }, commandShortcut("Shift+S"));
    if (processor.wrapperType == juce::AudioProcessor::WrapperType::wrapperType_Standalone) {
        addMenuItem(topLevelMenuIndex, "Create New Project", [&editor] { editor.resetToDefault(); }, commandShortcut("N"));
    }
}

void MainMenuBarModel::addRecentProjectsSubmenu(juce::PopupMenu& menu, CommonAudioProcessor& processor, int recentBaseId, int clearRecentId, bool sentenceCase) {
    juce::PopupMenu recentMenu;
    const int added = processor.createRecentProjectsPopupMenuItems(recentMenu, recentBaseId, true, true);
    if (added == 0) {
        recentMenu.addItem(recentBaseId, sentenceCase ? "No recent projects" : "(No Recent Projects)", false);
    }
    recentMenu.addSeparator();
    recentMenu.addItem(clearRecentId, sentenceCase ? "Clear recent projects" : "Clear Recent Projects", added > 0);
    menu.addSubMenu(sentenceCase ? "Open recent" : "Open Recent", recentMenu);
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
    }, [&processor] { return processor.muteParameter->getBoolValue(); }, commandShortcut("Shift+M"));
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
#if JUCE_WINDOWS
        // OBS's Window Capture (Automatic or BitBlt) sees a Direct2D window as white.
        addToggleMenuItem(topLevelMenuIndex, "Draw for Screen Capture (OBS)", [&editor] { editor.setSoftwareRendering(!editor.usesSoftwareRendering()); }, [&editor] { return editor.usesSoftwareRendering(); });
#endif
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

    addItemsTo(menu, topLevelMenuIndex, -1);

    if (customMenuAfterLogic) {
        customMenuAfterLogic(menu, topLevelMenuIndex);
    }

    return menu;
}

// The items whose parent is `parent` (-1 for the menu itself), each submenu
// with its own items.
void MainMenuBarModel::addItemsTo(juce::PopupMenu& menu, int topLevelMenuIndex, int parent) {
    const auto& items = menuItems[(size_t) topLevelMenuIndex];
    for (int i = 0; i < (int) items.size(); i++) {
        const auto& mi = items[(size_t) i];
        if (mi.parent != parent) {
            continue;
        }
        if (mi.isSeparator) {
            menu.addSeparator();
            continue;
        }
        if (mi.isSubMenu) {
            juce::PopupMenu subMenu;
            addItemsTo(subMenu, topLevelMenuIndex, i);
            menu.addSubMenu(mi.name, subMenu, !mi.isEnabled || mi.isEnabled());
            continue;
        }
        juce::PopupMenu::Item item(mi.name);
        item.itemID = i + 1;
        if (mi.shortcutKey.isNotEmpty()) {
            item.shortcutKeyDescription = mi.shortcutKey;
        }
        if (mi.hasTick && mi.isTicked) {
            item.setTicked(mi.isTicked());
        }
        if (mi.isEnabled) {
            item.setEnabled(mi.isEnabled());
        }
        menu.addItem(item);
    }
}

void MainMenuBarModel::menuItemSelected(int menuItemID, int topLevelMenuIndex) {
    if (customMenuSelectedLogic && customMenuSelectedLogic(menuItemID, topLevelMenuIndex)) {
        return;
    }
    auto& mi = menuItems[topLevelMenuIndex][menuItemID - 1];
    if (mi.action)
        mi.action();
}

void MainMenuBarModel::setMenuItemEnabled(int topLevelMenuIndex, const juce::String& name, std::function<bool()> isEnabled) {
    if (topLevelMenuIndex < 0 || topLevelMenuIndex >= (int) menuItems.size())
        return;
    for (auto& item : menuItems[(size_t) topLevelMenuIndex]) {
        if (item.name == name)
            item.isEnabled = isEnabled;
    }
}

void MainMenuBarModel::menuBarActivated(bool isActive) {}

void MainMenuBarModel::resetMenuItems() {
    topLevelMenuNames.clear();
    menuItems.clear();
    openSubMenus.clear();
}
