#include "OsciMainMenuBarModel.h"
#include "../PluginEditor.h"
#include "../PluginProcessor.h"

OsciMainMenuBarModel::OsciMainMenuBarModel(OscirenderAudioProcessor& p, OscirenderAudioProcessorEditor& e) : audioProcessor(p), editor(e) {
    addTopLevelMenu("File");
    addTopLevelMenu("About");
    addTopLevelMenu("Recording");
    if (editor.processor.wrapperType == juce::AudioProcessor::WrapperType::wrapperType_Standalone) {
        addTopLevelMenu("Audio");
    }

    addMenuItem(0, "Open", [this] { editor.openProject(); });
    addMenuItem(0, "Save", [this] { editor.saveProject(); });
    addMenuItem(0, "Save As", [this] { editor.saveProjectAs(); });
    if (editor.processor.wrapperType == juce::AudioProcessor::WrapperType::wrapperType_Standalone) {
        addMenuItem(0, "Create New Project", [this] { editor.resetToDefault(); });
    }

    addMenuItem(1, "About osci-render", [this] {
        juce::DialogWindow::LaunchOptions options;
        AboutComponent* about = new AboutComponent(BinaryData::logo_png, BinaryData::logo_pngSize,
            juce::String(ProjectInfo::projectName) + " by " + ProjectInfo::companyName + "\n"
#if SOSCI_FEATURES
            "Thank you for purchasing osci-render premium!\n"
#else
            "Free version\n"
#endif
            "Version " + ProjectInfo::versionString + "\n\n"
            "A huge thank you to:\n"
            "DJ_Level_3, for contributing several features to osci-render\n"
            "BUS ERROR Collective, for providing the source code for the Hilligoss encoder\n"
            "Jean Perbet (@jeanprbt) for the osci-render macOS icon\n"
            "All the community, for suggesting features and reporting issues!"
        );
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

#if !SOSCI_FEATURES
    addMenuItem(1, "Purchase osci-render premium!", [this] {
        juce::URL("https://osci-render.com/sosci").launchInDefaultBrowser();
    });
#endif

    addMenuItem(2, "Settings...", [this] {
        editor.openRecordingSettings();
    });

    if (editor.processor.wrapperType == juce::AudioProcessor::WrapperType::wrapperType_Standalone) {
        addMenuItem(3, "Settings...", [this] {
            editor.openAudioSettings();
        });
    }
}
