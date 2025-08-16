#include "MainMenuBarModel.h"

MainMenuBarModel::MainMenuBarModel() {}

MainMenuBarModel::~MainMenuBarModel() {}

void MainMenuBarModel::addTopLevelMenu(const juce::String& name) {
    topLevelMenuNames.add(name);
    menuItems.push_back({});
    menuItemsChanged();
}

void MainMenuBarModel::addMenuItem(int topLevelMenuIndex, const juce::String& name, std::function<void()> action) {
    menuItems[topLevelMenuIndex].push_back({ name, std::move(action), {}, false });
    menuItemsChanged();
}

void MainMenuBarModel::addToggleMenuItem(int topLevelMenuIndex, const juce::String& name, std::function<void()> action, std::function<bool()> isTicked) {
    menuItems[topLevelMenuIndex].push_back({ name, std::move(action), std::move(isTicked), true });
    menuItemsChanged();
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