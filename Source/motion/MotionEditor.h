#pragma once

#include "../CommonPluginEditor.h"
#include "MotionProcessor.h"
#include "ui/TimelineView.h"
#include "ui/CompositionView.h"
#include "ui/AssetLibrary.h"
#include "ui/ScopeSettings.h"
#include "ui/CurveEditor.h"
#include "ui/CurveList.h"
#include "ui/SceneToolbar.h"
#include "ui/NotesEditor.h"
#include "ui/CameraPanel.h"
#include "ui/ClipTimingPanel.h"
#include "ui/EffectLibrary.h"
#include "ui/EffectStack.h"
#include "ui/ModulatorPanels.h"
#include "render/LuaSliderBakes.h"
#include "model/TapTempo.h"
#include "model/TempoDetection.h"
#include "ui/PropertyInspector.h"
#include "ui/StatusBar.h"
#include "ui/ShortcutsOverlay.h"
#include "ui/MotionIcons.h"
#include "ui/DrawingEditor.h"
#include "ui/TextSourceEditor.h"
#include "ui/LuaSourceEditor.h"
#include "ui/TextAnimationPanel.h"
#include "ui/CompositionPanel.h"
#include <deque>
#include <variant>

class MotionEditor : public CommonPluginEditor, public juce::FileDragAndDropTarget, public juce::DragAndDropContainer, private juce::Timer, private juce::ChangeListener, private juce::FocusChangeListener {
public:
    explicit MotionEditor(MotionProcessor& processor);
    ~MotionEditor() override;
    void paint(juce::Graphics& graphics) override;
    void paintOverChildren(juce::Graphics& graphics) override;
    void resized() override;
    bool isInterestedInFileDrag(const juce::StringArray&) override { return true; }
    void filesDropped(const juce::StringArray& files, int, int) override;
    bool keyPressed(const juce::KeyPress& key) override;
    void openProject(const juce::File& file) override;
    void showOverlay(std::unique_ptr<osci::OverlayComponent> overlay) override;
    void showPopover(std::unique_ptr<juce::Component> content, juce::Rectangle<int> anchor);
    static void dismissPopover(juce::Component* content);
    using PopoverEdit = std::function<juce::Result(motion::Document&)>;
    std::function<void(PopoverEdit)> popoverEdit(juce::Component* panel, std::function<void(const juce::String&)> showError = {});


private:
    // Construction, by area of the workspace.
    void setUpMenus();
    void setUpCompositionNavigation();
    void setUpScene();
    void setUpScope();
    void setUpTransport();
    void setUpLibrary();
    void setUpProperties();
    void setUpTimeline();
    void setUpGraph();
    bool openSourceFile(const juce::File& file) override;
#if OSCI_PREMIUM
    juce::String offlineRenderTitle() const override { return "Export video"; }
#endif
    struct SourceRequest {
        juce::File file;
        double time = 0;
        std::uint64_t generation = 0;
        std::shared_ptr<const motion::Asset> replacement;
        std::optional<juce::String> editedText;
        motion::Id uniqueClip = 0;
        std::optional<motion::BakeSettings> retrySettings;
        juce::String preparationError;
        std::optional<motion::TextSettings> textSettings;
        std::optional<int> fractalDepth;
        motion::Id relink = 0; // source replaced in place by this file
        motion::Id track = 0;  // dropped on this timeline track (0: a new track)
    };
    void beginSourceImport(SourceRequest request, motion::BakeSettings settings = {}, motion::RasterSettings rasterSettings = {});
    void showNextPreparationSettings();
    void showBlenderSettings(motion::Id id = 0);
    void chooseSourceFile();
    bool importSourceFile(const juce::File& file, motion::Id relink, std::optional<std::pair<double, motion::Id>> placement = std::nullopt);
    void replaceSourceFile(motion::Id asset);
    std::deque<SourceRequest> preparationRequests;
    bool preparationSettingsOpen = false;
    struct ProjectLoad {
        std::atomic<bool> cancelled {false};
        motion::Project prepared;
        std::unique_ptr<juce::XmlElement> xml;
    };
    std::shared_ptr<ProjectLoad> projectLoad;
    juce::Component::SafePointer<osci::OverlayComponent> projectLoadOverlay;
    void timerCallback() override;
    void changeListenerCallback(juce::ChangeBroadcaster*) override;
    // Brings every view up to date with the document after an edit.
    void refreshFromDocument();
    void select(motion::Id id);
    void enterComposition(motion::Id clip, bool fromLibrary = false);
    void leaveComposition();
    struct ScopeView {
        motion::Id scope = 0, selection = 0, curveTarget = 0;
        double position = 0, timelineFraction = 0.34;
        int timelineTab = 0;
        bool cameraCurve = false;
        std::string property = "position.x";
        MotionTimelineView::ViewState timeline;
        MotionCompositionView::ViewState preview;
        MotionCurveEditor::ViewState graph;
        MotionNotesEditor::ViewState notes;
    };
    std::vector<ScopeView> scopeHistory;
    juce::TextButton scopeBack { "Back to Main" };
    juce::Label scopeLabel, scopeShared;
    std::uint64_t scopeNameGeneration = 0;
    void refreshInspector();
    bool audioSelected() const;
    bool selectionExists() const;
    bool selectionIsCamera() const;
    motion::Id toolCamera() const;
    void refreshCameraTools();
    void addCamera(double time);
    void previewEffect(const std::string& type, std::optional<motion::Id> owner);
    void addEffectTo(const std::string& type, motion::Id owner);
    void dragOperationStarted(const juce::DragAndDropTarget::SourceDetails&) override;
    void dragOperationEnded(const juce::DragAndDropTarget::SourceDetails&) override;
    void importExample(const juce::String& resource);
    void showDrawingEditor(motion::Id asset);
    // Text sources are written in the Scene, with the Scope previewing them.
    void showTextEditor(SourceRequest request);
    void closeTextEditor();
    void previewText();
    std::unique_ptr<MotionTextSourceEditor> textEditor;
    void showLuaEditor(SourceRequest request);
    void closeLuaEditor();
    std::unique_ptr<MotionLuaSourceEditor> luaEditor;
    SourceRequest luaRequest;
    std::uint64_t luaGeneration = 0;
    bool luaSubmitted = false;
    SourceRequest textRequest;
    std::uint64_t textGeneration = 0;
    // Typing previews after a short pause rather than on every key.
    double textPreviewDue = 0;
    void closeDrawingEditor();
    void previewDrawing();
    std::unique_ptr<MotionDrawingEditor> drawingEditor;
    motion::Id drawingAsset = 0;
    std::uint64_t drawingGeneration = 0;
    // `chosen`: the user picked this property; otherwise a new target opens
    // on its first animated channel.
    void selectCurveTarget(motion::Id id, const std::string& property, bool camera, bool chosen = false);
    void exportSignal();
    void exportVideo();
    struct ExportState;
    void startVideoExport(std::shared_ptr<ExportState> state, motion::Project project, std::shared_ptr<OfflineVisualiserParameters> beamSnapshot,
        VisualiserRenderer::RenderMode renderMode, VideoEncodingConfiguration config, juce::File destination, std::function<void(bool)> finished);
    void handleCommandLine(const juce::String& commandLine) override;
    void continueCommandLineRender();
    void failCommandLineRender(const juce::String& message);
    juce::File commandLineRender;
    bool commandLineRenderStarted = false, commandLineProjectRequested = false, projectLoadFailed = false;
    juce::PopupMenu timingMenu();
    bool applyTiming(int result);
    double snapDivision(int index) const;
    static constexpr int timingMenuIndex = 6;
    void refreshTiming();
    void refreshOutputChoices();
    // Keyboard shortcuts are data: each command appears in a menu with its
    // shortcut and is dispatched from keyPressed when no focused view used it.
    struct Command {
        int menu = 0;
        juce::String name, shortcut;
        juce::KeyPress key;
        std::function<void()> action;
    };
    std::vector<Command> commands;
    void addCommand(int menu, juce::String name, juce::KeyPress key, juce::String shortcut, std::function<void()> action);
    void registerCommands();
    void buildFileMenu(juce::PopupMenu& menu);
    bool fileMenuItemSelected(int id);
    void copySelection(bool cut);
    void pasteClipboard();
    void stepFrames(int frames);
    void jumpToKey(bool forward);
    void jumpToEdit(bool forward);
    void toggleLoop();
    void setLoopEdge(bool start);
    void loopSelection();
    bool setLoop(double start, double end, bool enabled, juce::String label);
    void loadLayout();
    void saveLayout();
    // The panel holding keyboard focus is outlined, so it is clear where
    // shortcuts go.
    void globalFocusChanged(juce::Component*) override { repaint(); }
    juce::Rectangle<int> focusedPanel() const;
    void splitAtPlayhead();
    void placeClipAtPlayhead(bool start, bool trim);
    void recordArmedTrack();
    void showShortcuts();
    std::variant<std::monostate, std::vector<motion::Document::CopiedClip>, std::vector<motion::Document::CopiedKey>> clipboard;
    MotionProcessor& processor;
    motion::style::LookAndFeel motionLookAndFeel;
    // Restyles each dialog after the overlay lays it out.
    struct DialogStyle final : juce::ComponentListener {
        motion::style::DialogLookAndFeel look;
        void componentMovedOrResized(juce::Component& overlay, bool, bool) override { motion::style::restyleDialog(overlay, look); }
        void componentBeingDeleted(juce::Component& overlay) override { overlay.removeComponentListener(this); }
    } dialogStyle;
    motion::LuaSliderBakes sliderBakes { processor.document };
    MotionTimelineView timeline;
    MotionCompositionView composition;
    MotionAssetLibrary assetLibrary;
    MotionCurveEditor curveEditor;
    MotionNotesEditor notesEditor;
    MotionCameraRig cameraRig;
    MotionScopePanel scopePanel { processor };
    MotionScopeHeading scopeHeading;
    MotionClipTimingPanel clipTimingPanel;
    MotionEffectLibrary effectLibrary;
    MotionEffectStack effectStack;
    MotionRoutingPanel routingPanel;
    // The graph's side column scrolls: oscillator settings, then routing.
    juce::Viewport graphSideViewport;
    juce::Component graphSide;
    void layoutGraphSide();
    MotionModulatorLibrary modulatorLibrary;
    osci::TabBar libraryTabs;
    juce::Label inspectorTitle;
    osci::TabBar timelineTabs;
    MotionCurveList curveList;
    std::set<std::string> shownCurves, hiddenCurves;
    void refreshCurveList();
    MotionDivider timelineDivider { false }, previewDivider { true };
    double timelineFraction = 0.34, previewFraction = 0.5;
    double dividerStart = 0;
    int previewWidth = 1, workspaceHeight = 1;
    juce::Label compositionTitle;
    MotionSceneToolbar sceneTools;
    MotionScopeToolbar scopeTools;
    // Properties lead: a clip's timing, then a text clip's character animation.
    MotionSectionStack inspectorLead;
    MotionTextAnimationPanel textAnimation { processor };
    MotionCompositionPanel compositionSettings { processor };
    struct QueuedTextAnimation {
        motion::Id asset;
        motion::TextSettings settings;
        std::uint64_t generation;
    };
    std::optional<QueuedTextAnimation> queuedTextAnimation;
    juce::Component::SafePointer<juce::CallOutBox> popover;
    // The Scope filling the window (its full-screen control or Escape).
    bool scopeFullScreen = false;
    void setScopeFullScreen(bool value);
    juce::TextButton sceneView { "Views" };
    void showSceneViewMenu(bool atMouse);

    juce::TextButton importButton { "Add source" };
    motion::icons::Button playButton { "Play", motion::icons::Icon::play };
    motion::icons::Button startButton { "Go to start", motion::icons::Icon::start };
    motion::icons::Button endButton { "Go to end", motion::icons::Icon::end };
    motion::icons::Button loopButton { "Loop playback", motion::icons::Icon::loop };
    juce::Label timeLabel;
    motion::TimeGrid positionEditGrid;
    std::uint64_t positionEditGeneration = 0, positionEditRevision = 0;
    juce::Label tempoValue, tempoLabel;
    // Tap tempo: taps preview the tempo; it commits once tapping pauses.
    juce::TextButton tapButton {"Tap"};
    motion::TapTempo tapTempo;
    std::optional<double> tappedBpm;
    std::unique_ptr<juce::TimedCallback> tapCommit;
    bool detectingTempo = false;
    double lastPaintedPosition = -1;
    int ticks = 0;
    std::shared_ptr<const motion::LiveSourceFrames> lastLiveFrames;
    juce::Component* visualiserControls = nullptr;
    motion::Id soundtrackClip() const;
    void detectTempo();
    void tap();
    bool compactTransport = false;
    juce::Label outputLabel;
    juce::ComboBox monitorOutput;
    MotionPropertyInspector propertyInspector;
    MotionStatusBar statusBar;
    motion::Id selection = 0;
    motion::Id curveTarget = 0;
    std::string curvePropertyName = "position.x";
    std::vector<std::string> curveProperties;
    bool cameraCurve = false;
    juce::ThreadPool imports { 1 };
    struct ImportState {
        bool capture = false;
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
        double sampleRate = 48000;
    };
    std::shared_ptr<ExportState> exportState;
    juce::ThreadPool exports { 1 };
    double exportProgress = 0;
    juce::ProgressBar exportBar { exportProgress };
    juce::TextButton cancelExport { "Cancel" };
    juce::String importError, lastPreparationError;

    MainMenuBarModel menus;
    osci::PanelHeader viewportHeader;
    // Titled by outputTitle, which gives way when the header is tight.
    osci::PanelHeader outputHeader;
    juce::Label outputTitle;
    osci::PanelHeader inspectorHeader;
    osci::PanelHeader timelineHeader { "Timeline" };
    juce::Rectangle<int> libraryBounds, viewportBounds, inspectorBounds, timelineBounds;
};
