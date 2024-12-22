#include "SosciMainMenuBarModel.h"
#include "../SosciPluginEditor.h"
#include "../SosciPluginProcessor.h"

SosciMainMenuBarModel::SosciMainMenuBarModel(SosciPluginEditor& editor, SosciAudioProcessor& processor) : editor(editor), processor(processor) {
    addTopLevelMenu("File");
    addTopLevelMenu("About");
    addTopLevelMenu("Audio");

    addMenuItem(0, "Open", [&]() { editor.openProject(); });
    addMenuItem(0, "Save", [&]() { editor.saveProject(); });
    addMenuItem(0, "Save As", [&]() { editor.saveProjectAs(); });
    if (editor.processor.wrapperType == juce::AudioProcessor::WrapperType::wrapperType_Standalone) {
        addMenuItem(0, "Create New Project", [&]() { editor.resetToDefault(); });
    }

    addMenuItem(1, "About sosci", [&]() {
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
    });

    addMenuItem(2, "Force Disable Brightness Input", [&]() {
        processor.forceDisableBrightnessInput = !processor.forceDisableBrightnessInput;
        menuItemsChanged();
    });

    if (editor.processor.wrapperType == juce::AudioProcessor::WrapperType::wrapperType_Standalone) {
        addMenuItem(2, "Settings...", [&]() { editor.openAudioSettings(); });
    }
}
