#pragma once
#include <JuceHeader.h>

class MainMenuBarModel : public juce::MenuBarModel {
public:
    MainMenuBarModel();
    ~MainMenuBarModel();

    void addTopLevelMenu(const juce::String& name);
    void addMenuItem(int topLevelMenuIndex, const juce::String& name, std::function<void()> action);
    // Adds a toggle (ticked) menu item whose tick state is provided dynamically via isTicked()
    void addToggleMenuItem(int topLevelMenuIndex, const juce::String& name, std::function<void()> action, std::function<bool()> isTicked);
    void resetMenuItems();

    std::function<void(juce::PopupMenu&, int)> customMenuLogic;
    std::function<bool(int, int)> customMenuSelectedLogic;

private:
    juce::StringArray getMenuBarNames() override;
    juce::PopupMenu getMenuForIndex(int topLevelMenuIndex, const juce::String& menuName) override;
    void menuItemSelected(int menuItemID, int topLevelMenuIndex) override;
    void menuBarActivated(bool isActive);

    struct MenuItem
    {
        juce::String name;
        std::function<void()> action;
        std::function<bool()> isTicked; // optional tick state
        bool hasTick = false;
    };

    juce::StringArray topLevelMenuNames;
    std::vector<std::vector<MenuItem>> menuItems;
};
