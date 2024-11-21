#include "SosciMainMenuBarModel.h"
#include "../SosciPluginEditor.h"
#include "../SosciPluginProcessor.h"

SosciMainMenuBarModel::SosciMainMenuBarModel(SosciPluginEditor& editor, SosciAudioProcessor& processor) : editor(editor), processor(processor) {}

SosciMainMenuBarModel::~SosciMainMenuBarModel() {}

juce::StringArray SosciMainMenuBarModel::getMenuBarNames() {
    return juce::StringArray("File", "About", "Audio");
}

juce::PopupMenu SosciMainMenuBarModel::getMenuForIndex(int topLevelMenuIndex, const juce::String& menuName) {
    juce::PopupMenu menu;

    if (topLevelMenuIndex == 0) {
        menu.addItem(1, "Open");
        menu.addItem(2, "Save");
        menu.addItem(3, "Save As");
        if (editor.processor.wrapperType == juce::AudioProcessor::WrapperType::wrapperType_Standalone) {
            menu.addItem(4, "Create New Project");
        }
    } else if (topLevelMenuIndex == 1) {
        menu.addItem(1, "About sosci");
    } else if (topLevelMenuIndex == 2) {
        menu.addItem(1, "Force Disable Brightness Input", true, processor.forceDisableBrightnessInput);
        if (editor.processor.wrapperType == juce::AudioProcessor::WrapperType::wrapperType_Standalone) {
            menu.addItem(2, "Settings...");
        }
    }

    return menu;
}

void SosciMainMenuBarModel::menuItemSelected(int menuItemID, int topLevelMenuIndex) {
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
            AboutComponent* about = new AboutComponent(BinaryData::logo_png, BinaryData::logo_pngSize, "Sosci");
            options.content.setOwned(about);
            options.content->setSize(500, 270);
            options.dialogTitle = "About";
            options.dialogBackgroundColour = Colours::dark;
            options.escapeKeyTriggersCloseButton = true;
#if JUCE_WINDOWS
            // if not standalone, use native title bar for compatibility with DAWs
            options.useNativeTitleBar = editor.processor.wrapperType == juce::AudioProcessor::WrapperType::wrapperType_Standalone;
#elif JUCE_MAC
            options.useNativeTitleBar = true;
#endif
            options.resizable = false;
            
            juce::DialogWindow* dw = options.launchAsync();
        } break;
        case 2:
            switch (menuItemID) {
                case 1:
                    processor.forceDisableBrightnessInput = !processor.forceDisableBrightnessInput;
                    menuItemsChanged();
                    break;
                case 2:
                    editor.openAudioSettings();
                    break;
                default:
                    break;
            }
            break;
        default:
            break;
    }
    
}

void SosciMainMenuBarModel::menuBarActivated(bool isActive) {}
