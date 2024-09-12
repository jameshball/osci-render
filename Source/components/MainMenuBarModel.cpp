#include "MainMenuBarModel.h"
#include "../PluginEditor.h"
#include "../PluginProcessor.h"

MainMenuBarModel::MainMenuBarModel(OscirenderAudioProcessor& p, OscirenderAudioProcessorEditor& editor) : audioProcessor(p), editor(editor) {
    audioProcessor.visualiserParameters.legacyVisualiserEnabled->addListener(this);
}

MainMenuBarModel::~MainMenuBarModel() {
    audioProcessor.visualiserParameters.legacyVisualiserEnabled->removeListener(this);
}

void MainMenuBarModel::parameterValueChanged(int parameterIndex, float legacyVisualiserEnabled) {
    editor.visualiser.setVisualiserType(legacyVisualiserEnabled >= 0.5f);
    menuItemsChanged();
}

void MainMenuBarModel::parameterGestureChanged(int parameterIndex, bool gestureIsStarting) {}

juce::StringArray MainMenuBarModel::getMenuBarNames() {
    if (editor.processor.wrapperType == juce::AudioProcessor::WrapperType::wrapperType_Standalone) {
        return juce::StringArray("File", "View", "About", "Audio");
    } else {
        return juce::StringArray("File", "View", "About");
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
        menu.addItem(1, "Use Legacy Visualiser", true, audioProcessor.visualiserParameters.legacyVisualiserEnabled->getBoolValue());
    } else if (topLevelMenuIndex == 2) {
        menu.addItem(1, "About osci-render");
    } else if (topLevelMenuIndex == 3) {
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
            audioProcessor.visualiserParameters.legacyVisualiserEnabled->setBoolValueNotifyingHost(!audioProcessor.visualiserParameters.legacyVisualiserEnabled->getBoolValue());
            menuItemsChanged();
        } break;
        case 2: {
            juce::DialogWindow::LaunchOptions options;
            AboutComponent* about = new AboutComponent();
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
        case 3:
            editor.openAudioSettings();
            break;
        default:
            break;
    }
    
}

void MainMenuBarModel::menuBarActivated(bool isActive) {}
