#include "MainMenuBarModel.h"

MainMenuBarModel::MainMenuBarModel() {}

MainMenuBarModel::~MainMenuBarModel() {}

void MainMenuBarModel::addTopLevelMenu(const juce::String& name) {
    topLevelMenuNames.add(name);
    menuItems.push_back(std::vector<std::pair<juce::String, std::function<void()>>>());
    menuItemsChanged();
}

void MainMenuBarModel::addMenuItem(int topLevelMenuIndex, const juce::String& name, std::function<void()> action) {
    menuItems[topLevelMenuIndex].push_back(std::make_pair(name, action));
    menuItemsChanged();
}

juce::StringArray MainMenuBarModel::getMenuBarNames() {
    return topLevelMenuNames;
}

juce::PopupMenu MainMenuBarModel::getMenuForIndex(int topLevelMenuIndex, const juce::String& menuName) {
    juce::PopupMenu menu;

    for (int i = 0; i < menuItems[topLevelMenuIndex].size(); i++) {
        menu.addItem(i + 1, menuItems[topLevelMenuIndex][i].first);
    }

    return menu;
}

void MainMenuBarModel::menuItemSelected(int menuItemID, int topLevelMenuIndex) {
    menuItems[topLevelMenuIndex][menuItemID - 1].second();
}

void MainMenuBarModel::menuBarActivated(bool isActive) {}
