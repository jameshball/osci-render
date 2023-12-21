#include "MainMenuBarModel.h"
#include "../PluginEditor.h"

juce::StringArray MainMenuBarModel::getMenuBarNames() {
    if (editor.processor.wrapperType == juce::AudioProcessor::WrapperType::wrapperType_Standalone) {
        return juce::StringArray("File", "Options");
    } else {
        return juce::StringArray("File");
    }
}

juce::PopupMenu MainMenuBarModel::getMenuForIndex(int topLevelMenuIndex, const juce::String& menuName) {
    juce::PopupMenu menu;

    if (topLevelMenuIndex == 0) {
        menu.addItem(1, "Open");
        menu.addItem(2, "Save");
        menu.addItem(3, "Save As");
    } else if (topLevelMenuIndex == 1) {
        menu.addItem(1, "Audio Settings");
    }

    return menu;
}

void MainMenuBarModel::menuItemSelected(int menuItemID, int topLevelMenuIndex) {
    switch (topLevelMenuIndex) {
        case 0:
            switch (menuItemID) {
                case 1:
                    editor.openProject();
                    break;
                case 2:
                    editor.saveProject();
                    break;
                case 3:
                    editor.saveProjectAs();
                    break;
                default:
                    break;
            }
            break;
        case 1:
            editor.openAudioSettings();
            break;
        default:
            break;
    }
    
}

void MainMenuBarModel::menuBarActivated(bool isActive) {}
