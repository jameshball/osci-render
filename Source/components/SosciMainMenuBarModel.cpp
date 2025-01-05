#include "SosciMainMenuBarModel.h"
#include "../SosciPluginEditor.h"
#include "../SosciPluginProcessor.h"

SosciMainMenuBarModel::SosciMainMenuBarModel(SosciPluginEditor& e, SosciAudioProcessor& p) : editor(e), processor(p) {
    addTopLevelMenu("File");
    addTopLevelMenu("About");
    addTopLevelMenu("Recording");
    addTopLevelMenu("Audio");
    
    std::vector<std::tuple<juce::String, const void *, int>> examples = {
        {"default.sosci", BinaryData::default_sosci, BinaryData::default_sosciSize},
    };
    
    // This is a hack - ideally I would improve the MainMenuBarModel class to allow for submenus
    customMenuLogic = [this, examples](juce::PopupMenu& menu, int topLevelMenuIndex) {
        if (topLevelMenuIndex == 0) {
            juce::PopupMenu submenu;
            
            for (int i = 0; i < examples.size(); i++) {
                submenu.addItem(SUBMENU_ID + i, std::get<0>(examples[i]));
            }
            
            menu.addSubMenu("Examples", submenu);
        }
    };
    
    customMenuSelectedLogic = [this, examples](int menuItemID, int topLevelMenuIndex) {
        if (topLevelMenuIndex == 0) {
            if (menuItemID >= SUBMENU_ID) {
                int index = menuItemID - SUBMENU_ID;
                processor.setStateInformation(std::get<1>(examples[index]), std::get<2>(examples[index]));
                return true;
            }
        }
        return false;
    };

    addMenuItem(0, "Open Audio File", [&]() {
        fileChooser = std::make_unique<juce::FileChooser>("Open Audio File", processor.lastOpenedDirectory, "*.wav;*.aiff");
        auto flags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;
        fileChooser->launchAsync(flags, [&](const juce::FileChooser& chooser) {
            auto file = chooser.getResult();
            if (file != juce::File()) {
                processor.loadAudioFile(file);
            }
        });
    });
    addMenuItem(0, "Stop Audio File", [&]() {
        processor.stopAudioFile();
    });
    addMenuItem(0, "Open Project", [&]() { editor.openProject(); });
    addMenuItem(0, "Save Project", [&]() { editor.saveProject(); });
    addMenuItem(0, "Save Project As", [&]() { editor.saveProjectAs(); });
    if (editor.processor.wrapperType == juce::AudioProcessor::WrapperType::wrapperType_Standalone) {
        addMenuItem(0, "Create New Project", [&]() { editor.resetToDefault(); });
    }

    addMenuItem(1, "About sosci", [&]() {
        juce::DialogWindow::LaunchOptions options;
        AboutComponent* about = new AboutComponent(BinaryData::sosci_logo_png, BinaryData::sosci_logo_pngSize,
            juce::String(ProjectInfo::projectName) + " by " + ProjectInfo::companyName + "\n"
#if SOSCI_FEATURES
            "Thank you for purchasing sosci!\n"
#else
            "Free version\n"
#endif
            "Version " + ProjectInfo::versionString + "\n\n"
            "A huge thank you to:\n"
            "Neil Thapen, for allowing me to adapt the brilliant dood.al/oscilloscope\n"
            "Kevin Kripper, for guiding much of the features and development of sosci\n"
            "DJ_Level_3, for testing throughout and helping add features\n"
            "All the community, for suggesting features and reporting issues!"
        );
        options.content.setOwned(about);
        options.content->setSize(500, 270);
        options.dialogTitle = "About";
        options.dialogBackgroundColour = Colours::veryDark;
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
    
    addMenuItem(2, "Settings...", [this] {
        editor.openRecordingSettings();
    });

    addMenuItem(3, "Force Disable Brightness Input", [&]() {
        processor.forceDisableBrightnessInput = !processor.forceDisableBrightnessInput;
        menuItemsChanged();
    });

    if (editor.processor.wrapperType == juce::AudioProcessor::WrapperType::wrapperType_Standalone) {
        addMenuItem(3, "Settings...", [&]() { editor.openAudioSettings(); });
    }
}
