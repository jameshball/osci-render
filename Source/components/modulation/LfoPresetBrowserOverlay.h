#pragma once

#include <JuceHeader.h>
#include "../../audio/modulation/LfoState.h"
#include "../../audio/modulation/LfoPresetManager.h"
#include "../../LookAndFeel.h"

// Inline LFO preset browser panel.  Shown as a child of LfoComponent,
// replacing the graph area while the browser is open.
class LfoPresetBrowserOverlay : public juce::Component {
public:
    struct Listener {
        virtual ~Listener() = default;
        virtual void presetBrowserFactorySelected(LfoPreset preset) = 0;
        virtual void presetBrowserUserSelected(const juce::File& file) = 0;
        virtual void presetBrowserUserDeleted(const juce::File& file) = 0;
        virtual void presetBrowserSaveRequested(const juce::String& name) = 0;
        virtual void presetBrowserImportRequested() = 0;
    };

    LfoPresetBrowserOverlay(LfoPresetManager& manager, Listener* listener);

    void show(LfoPreset currentFactoryPreset, const juce::String& currentUserName);
    void refresh(LfoPreset currentFactoryPreset, const juce::String& currentUserName);
    std::function<void()> onDismissRequested;

    void resized() override;
    bool keyPressed(const juce::KeyPress& key) override;

private:
    static constexpr int kRowHeight = 24;
    static constexpr int kSectionHeaderHeight = 22;
    static constexpr int kDeleteButtonSize = 18;
    static constexpr int kBottomBarHeight = 30;
    static constexpr int kImportBarHeight = 26;

    LfoPresetManager& presetManager;
    Listener* browserListener;

    LfoPreset activeFactoryPreset = LfoPreset::Triangle;
    juce::String activeUserName;

    struct BrowserPanel : public juce::Component {
        void paint(juce::Graphics& g) override;
        void resized() override;

        juce::Viewport viewport;
        juce::Component contentComp;
        juce::TextEditor saveEditor;
        juce::TextButton saveButton;
        juce::TextButton importButton;
        int contentHeight = 0;
    };

    BrowserPanel browserPanel;
    juce::Viewport& viewport = browserPanel.viewport;
    juce::Component& content = browserPanel.contentComp;
    juce::TextEditor& saveEditor = browserPanel.saveEditor;
    juce::TextButton& saveButton = browserPanel.saveButton;
    juce::TextButton& importButton = browserPanel.importButton;
    int contentHeight = 0;

    struct PresetRow : public juce::Component {
        juce::String name;
        bool isActive = false;
        bool isUserPreset = false;
        juce::File file;
        std::function<void()> onSelect;
        std::function<void()> onDelete;

        void paint(juce::Graphics& g) override;
        void mouseDown(const juce::MouseEvent& e) override;
        void mouseEnter(const juce::MouseEvent&) override { repaint(); }
        void mouseExit(const juce::MouseEvent&) override { repaint(); }
        void mouseMove(const juce::MouseEvent&) override;
        void paintOverChildren(juce::Graphics& g) override;
    };

    struct SectionHeader : public juce::Component {
        juce::String title;
        void paint(juce::Graphics& g) override;
    };

    juce::OwnedArray<juce::Component> rows;
    int numUserPresets = 0;   // cached count from last rebuildContent()
    int numVitalPresets = 0;  // cached count from last rebuildContent()

    void rebuildContent();
    void layoutRows();
    void doSave();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LfoPresetBrowserOverlay)
};
