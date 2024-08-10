#include "MainMenuBarModel.h"
#include "../PluginEditor.h"

juce::StringArray MainMenuBarModel::getMenuBarNames() {
    if (editor.processor.wrapperType == juce::AudioProcessor::WrapperType::wrapperType_Standalone) {
        return juce::StringArray("File", "About", "Audio");
    } else {
        return juce::StringArray("File", "About");
    }
}

juce::PopupMenu MainMenuBarModel::getMenuForIndex(int topLevelMenuIndex, const juce::String& menuName) {
    juce::PopupMenu menu;

    if (topLevelMenuIndex == 0) {
        menu.addItem(1, "Open");
        menu.addItem(2, "Save");
        menu.addItem(3, "Save As");
        if (editor.processor.wrapperType == juce::AudioProcessor::WrapperType::wrapperType_Standalone) {
            menu.addItem(4, "Create New Project");
        }
    } else if (topLevelMenuIndex == 1) {
        menu.addItem(1, "About osci-render");
    } else if (topLevelMenuIndex == 2) {
        menu.addItem(1, "Settings");
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
                case 4:
                    editor.resetToDefault();
                    break;
                default:
                    break;
            }
            break;
        case 1: {
            juce::DialogWindow::LaunchOptions options;
            AboutComponent* about = new AboutComponent();
            options.content.setOwned(about);
            options.content->setSize(500, 250);
            options.dialogTitle = "About";
            options.dialogBackgroundColour = Colours::dark;
            options.escapeKeyTriggersCloseButton = true;
            options.useNativeTitleBar = true;
            options.resizable = false;
            
            juce::DialogWindow* dw = options.launchAsync();
        } break;
        case 2:
            editor.openAudioSettings();
            break;
        default:
            break;
    }
    
}

void MainMenuBarModel::menuBarActivated(bool isActive) {}
