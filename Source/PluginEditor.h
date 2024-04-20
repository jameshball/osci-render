#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "SettingsComponent.h"
#include "MidiComponent.h"
#include "components/VolumeComponent.h"
#include "components/MainMenuBarModel.h"
#include "LookAndFeel.h"
#include "components/ErrorCodeEditorComponent.h"
#include "components/LuaConsole.h"


class DemoToolbarItemFactory : public juce::ToolbarItemFactory
{
public:
    DemoToolbarItemFactory() {}

    void getAllToolbarItemIds (juce::Array<int>& ids) {
        for (int i = 1; i < 128; ++i) {
            ids.add(i);
        }
    }

    void getDefaultItemSet (juce::Array<int>& ids) {
        for (int i = 1; i < 10; ++i) {
            ids.add(i);
        }
    }

    juce::ToolbarItemComponent* createItem (int itemId) {
        return new TabComponent(itemId);
    }

private:
    juce::StringArray iconNames;

    class TabComponent : public juce::ToolbarItemComponent {
    public:
        TabComponent(const int id) : juce::ToolbarItemComponent (id, "Custom Toolbar Item", false), label("demo toolbar combo box"), id(id) {
            addAndMakeVisible(&label);

            label.setText("tab " + juce::String(id), juce::dontSendNotification);
            
        }

        ~TabComponent() override {
            if (toolbar != nullptr) {
                toolbar->removeMouseListener(this);
            }
        }

        bool getToolbarItemSizes(int /*toolbarDepth*/, bool isToolbarVertical, int& preferredSize, int& minSize, int& maxSize) {
            if (isToolbarVertical) {
                return false;
            }

            preferredSize = 250;
            minSize = 80;
            maxSize = 300;
            return true;
        }

        void paintButtonArea(juce::Graphics&, int, int, bool, bool) {}

        void contentAreaChanged(const juce::Rectangle<int>& contentArea) {
            label.setSize(contentArea.getWidth() - 2, juce::jmin(contentArea.getHeight() - 2, 22));
            label.setCentrePosition(contentArea.getCentreX(), contentArea.getCentreY());
        }

        void mouseDown(const juce::MouseEvent& e) {
            auto temp = getToolbar();
            if (temp != nullptr) {
                toolbar = temp;
            }
            if (getScreenBounds().contains(e.getScreenPosition())) {
                label.setText("clicked!", juce::dontSendNotification);
            }
        }

        void mouseUp(const juce::MouseEvent& e) {
            label.setText("tab " + juce::String(id), juce::dontSendNotification);
        }

    private:
        juce::Label label;
        juce::Toolbar* toolbar;
        int id;
    };
};


class OscirenderAudioProcessorEditor : public juce::AudioProcessorEditor, private juce::CodeDocument::Listener, public juce::AsyncUpdater, public juce::ChangeListener {
public:
    OscirenderAudioProcessorEditor(OscirenderAudioProcessor&);
    ~OscirenderAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    
    void initialiseCodeEditors();
    void addCodeEditor(int index);
    void removeCodeEditor(int index);
    void fileUpdated(juce::String fileName, bool shouldOpenEditor = false);
    void handleAsyncUpdate() override;
    void changeListenerCallback(juce::ChangeBroadcaster* source) override;
    void toggleLayout(juce::StretchableLayoutManager& layout, double prefSize);

    void editCustomFunction(bool enabled);

    void newProject();
    void openProject();
    void saveProject();
    void saveProjectAs();
    void updateTitle();
    void openAudioSettings();
    void resetToDefault();

private:
    OscirenderAudioProcessor& audioProcessor;
public:

    const double CLOSED_PREF_SIZE = 30.0;
    const double RESIZER_BAR_SIZE = 7.0;

    OscirenderLookAndFeel lookAndFeel;

    std::atomic<bool> editingCustomFunction = false;

    VisualiserComponent visualiser{2, audioProcessor};
    std::atomic<bool> visualiserFullScreen = false;
    SettingsComponent settings{audioProcessor, *this};

    juce::ComponentAnimator codeEditorAnimator;
    LuaComponent lua{audioProcessor, *this};
    VolumeComponent volume{audioProcessor};

    LuaConsole console;

    DemoToolbarItemFactory factory;
    juce::Toolbar toolbar;

    std::vector<std::shared_ptr<juce::CodeDocument>> codeDocuments;
    std::vector<std::shared_ptr<OscirenderCodeEditorComponent>> codeEditors;
    juce::CodeEditorComponent::ColourScheme colourScheme;
    juce::LuaTokeniser luaTokeniser;
    juce::XmlTokeniser xmlTokeniser;
	juce::ShapeButton collapseButton;
    std::shared_ptr<juce::CodeDocument> customFunctionCodeDocument = std::make_shared<juce::CodeDocument>();
    std::shared_ptr<OscirenderCodeEditorComponent> customFunctionCodeEditor = std::make_shared<OscirenderCodeEditorComponent>(*customFunctionCodeDocument, &luaTokeniser, audioProcessor, CustomEffect::UNIQUE_ID, CustomEffect::FILE_NAME);

    std::unique_ptr<juce::FileChooser> chooser;
    MainMenuBarModel menuBarModel{*this};
    juce::MenuBarComponent menuBar;

    juce::StretchableLayoutManager layout;
    juce::StretchableLayoutResizerBar resizerBar{&layout, 1, true};

    juce::StretchableLayoutManager luaLayout;
    juce::StretchableLayoutResizerBar luaResizerBar{&luaLayout, 1, false};

    juce::TooltipWindow tooltipWindow{this, 0};
    juce::DropShadower tooltipDropShadow{juce::DropShadow(juce::Colours::black.withAlpha(0.5f), 6, {0,0})};

    std::atomic<bool> updatingDocumentsWithParserLock = false;

    bool usingNativeMenuBar = false;

#if !JUCE_MAC
    juce::OpenGLContext openGlContext;
#endif

	void codeDocumentTextInserted(const juce::String& newText, int insertIndex) override;
	void codeDocumentTextDeleted(int startIndex, int endIndex) override;
    void updateCodeDocument();
    void updateCodeEditor(bool shouldOpenEditor = false);

    bool keyPressed(const juce::KeyPress& key) override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseMove(const juce::MouseEvent& event) override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OscirenderAudioProcessorEditor)
};
