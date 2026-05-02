#include "MainMenuBarModel.h"

#include "../../CommonPluginProcessor.h"

MainMenuBarModel::MainMenuBarModel() {}

MainMenuBarModel::~MainMenuBarModel() {}

void MainMenuBarModel::addTopLevelMenu(const juce::String& name) {
    topLevelMenuNames.add(name);
    menuItems.push_back({});
    menuItemsChanged();
}

void MainMenuBarModel::addMenuItem(int topLevelMenuIndex, const juce::String& name, std::function<void()> action, const juce::String& shortcutKey) {
    menuItems[topLevelMenuIndex].push_back({ name, std::move(action), {}, false, shortcutKey });
    menuItemsChanged();
}

void MainMenuBarModel::addToggleMenuItem(int topLevelMenuIndex, const juce::String& name, std::function<void()> action, std::function<bool()> isTicked) {
    menuItems[topLevelMenuIndex].push_back({ name, std::move(action), std::move(isTicked), true });
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
        processor.getGlobalSettingsFile().revealToUser();
    });
}

void MainMenuBarModel::addEditMenuItems(int topLevelMenuIndex, CommonAudioProcessor& processor) {
   #if JUCE_MAC
    const juce::String undoShortcut = juce::String::fromUTF8("\xe2\x8c\x98Z");       // ⌘Z
    const juce::String redoShortcut = juce::String::fromUTF8("\xe2\x87\xa7\xe2\x8c\x98Z"); // ⇧⌘Z
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
        resetMenuItems();
    }, [&processor] { return processor.getAcceptsKeys(); });
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