#pragma once
#include <JuceHeader.h>

class CommonAudioProcessor;

class MainMenuBarModel : public juce::MenuBarModel {
public:
    MainMenuBarModel();
    ~MainMenuBarModel();

    void addTopLevelMenu(const juce::String& name);
    void addMenuItem(int topLevelMenuIndex, const juce::String& name, std::function<void()> action, const juce::String& shortcutKey = {});
    // Adds a toggle (ticked) menu item whose tick state is provided dynamically via isTicked()
    void addToggleMenuItem(int topLevelMenuIndex, const juce::String& name, std::function<void()> action, std::function<bool()> isTicked);
    // Adds the common "Open Log File", "Open App Settings File", "Open Global Settings File"
    // items shared across all apps.
    void addDiagnosticsMenuItems(int topLevelMenuIndex, CommonAudioProcessor& processor);
    // Adds the common "Undo"/"Redo" items backed by the processor's shared UndoManager.
    void addEditMenuItems(int topLevelMenuIndex, CommonAudioProcessor& processor);
    // Adds the shared "Listen for Special Keys" toggle item.
    void addListenForSpecialKeysMenuItem(int topLevelMenuIndex, CommonAudioProcessor& processor);
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
        juce::String shortcutKey;
    };

    juce::StringArray topLevelMenuNames;
    std::vector<std::vector<MenuItem>> menuItems;
};
