#pragma once

#include "../CommonPluginEditor.h"
#include "MotionProcessor.h"
#include "ui/TimelineView.h"
#include "ui/CompositionView.h"
#include "ui/AssetLibrary.h"
#include "ui/CurveEditor.h"
#include "ui/CameraPanel.h"
#include "ui/EffectLibrary.h"
#include "ui/EffectsPanel.h"
#include "ui/ModulationPanel.h"

class MotionEditor : public CommonPluginEditor, public juce::FileDragAndDropTarget, public juce::DragAndDropContainer, private juce::Timer, private juce::ChangeListener {
public:
    explicit MotionEditor(MotionProcessor& processor);
    ~MotionEditor() override;
    void paint(juce::Graphics& graphics) override;
    void paintOverChildren(juce::Graphics& graphics) override;
    void resized() override;
    bool isInterestedInFileDrag(const juce::StringArray&) override { return true; }
    void filesDropped(const juce::StringArray& files, int, int) override;
    bool keyPressed(const juce::KeyPress& key) override;


private:
    bool openSourceFile(const juce::File& file) override;
    void timerCallback() override;
    void changeListenerCallback(juce::ChangeBroadcaster*) override;
    void select(motion::Id id);
    void refreshInspector();
    bool audioSelected() const;
    const char* inspectorProperty(std::size_t index) const;
    void setProperty(int index, bool keyframe);
    void selectCurveTarget(motion::Id id, const std::string& property, bool camera);
    void exportSignal();
    void exportVideo();
    void showTimingMenu();
    void refreshTiming();
    void refreshOutputChoices();
    MotionProcessor& processor;
    MotionTimelineView timeline;
    MotionCompositionView composition;
    MotionAssetLibrary assetLibrary;
    MotionCurveEditor curveEditor;
    MotionCameraPanel cameraPanel;
    MotionEffectLibrary effectLibrary;
    MotionEffectsPanel effectsPanel;
    MotionModulationPanel modulationPanel;
    osci::TabBar libraryTabs;
    osci::TabBar inspectorTabs;
    osci::TabBar timelineTabs;
    juce::ComboBox curveProperty;
    osci::PanelDivider timelineDivider { false }, previewDivider { true };
    double timelineFraction = 0.34, previewFraction = 0.5;
    double dividerStart = 0;
    int previewWidth = 1, workspaceHeight = 1;
    juce::TextButton importButton { "Import" };
    juce::TextButton playButton { "Play" };
    juce::TextButton splitButton { "Split" };
    juce::Label timeLabel;
    juce::Label tempoValue, tempoLabel;
    juce::TextButton timingButton;
    juce::ComboBox monitorOutput;
    juce::Label selectionLabel;
    std::array<juce::Label, 13> values;
    std::array<osci::KeyframeButton, 13> keyButtons;
    motion::Id selection = 0;
    motion::Id curveTarget = 0;
    std::string curvePropertyName = "position.x";
    std::vector<std::string> curveProperties;
    bool cameraCurve = false;
    bool updatingInspector = false;
    juce::ThreadPool imports { 1 };
    struct ImportState {
        std::atomic<bool> cancelled { false };
        std::atomic<double> progress { 0.0 };
        juce::String name;
        std::uint64_t generation = 0;
    };
    std::vector<std::shared_ptr<ImportState>> pendingImports;
    struct ExportState {
        std::atomic<bool> cancelled { false };
        std::atomic<double> progress { 0.0 };
        std::atomic<double> soundtrackProgress { 0.0 };
        bool videoWithAudio = false;
    };
    std::shared_ptr<ExportState> exportState;
    juce::ThreadPool exports { 1 };
    double exportProgress = 0;
    juce::ProgressBar exportBar { exportProgress };
    juce::TextButton cancelExport { "Cancel" };
    juce::String importError;

    MainMenuBarModel menus;
    osci::PanelHeader libraryHeader { "Assets" };
    osci::PanelHeader viewportHeader { "Composition" };
    osci::PanelHeader outputHeader { "Output" };
    osci::PanelHeader inspectorHeader { "Inspector" };
    osci::PanelHeader timelineHeader { "Timeline" };
    juce::Rectangle<int> libraryBounds, viewportBounds, inspectorBounds, timelineBounds;
};
