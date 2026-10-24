#pragma once

#include "../CommonPluginEditor.h"
#include "export/Stretches.h"
#include "model/AnimateChange.h"
#include "model/LightPresets.h"
#include "model/MirrorPresets.h"
#include "MotionProcessor.h"
#include "SessionRecovery.h"
#include "ui/TimelineView.h"
#include "ui/BeamShares.h"
#include "ui/CompositionView.h"
#include "ui/AssetLibrary.h"
#include "ui/ScopeSettings.h"
#include "ui/CurveEditor.h"
#include "ui/CurveList.h"
#include "ui/SceneToolbar.h"
#include "ui/CameraPanel.h"
#include "ui/ClipTimingPanel.h"
#include "ui/EffectLibrary.h"
#include "ui/EffectStack.h"
#include "ui/ModulatorPanels.h"
#include "ui/KeyPanel.h"
#include "import/LuaSliderBakes.h"
#include "model/TapTempo.h"
#include "ui/PropertyInspector.h"
#include "ui/ExamplePictures.h"
#include "ui/StatusBar.h"
#include "ui/MotionIcons.h"
#include "ui/TextAnimationPanel.h"
#include "ui/ModelSizeInspector.h"
#include "ui/OutputPanel.h"
#include "ui/CompositionPanel.h"
#include "ui/ScopeTransport.h"
#include "ui/SearchBox.h"
#include <deque>
#include <variant>

class OfflineVisualiserParameters;
namespace motion::ui { class StartScreen; }

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
    // Opening another project, or a new one, first asks before unsaved
    // changes are lost.
    void openProject(const juce::File& file) override;
    void resetToDefault() override;
    void projectSaved() override { processor.document.markSaved(); }
    // The Scope's next frame becomes the project's picture as it is saved.
    void beforeSaving(std::function<void()> write) override;
    void showOverlay(std::unique_ptr<osci::OverlayComponent> overlay) override;
    // Where a popover goes: `best`, the side with room; `below`, under its
    // button (though wider than the column it is in); `left`, left of what
    // it edits, leaving it in view.
    enum class Placement { best, below, left };
    void showPopover(std::unique_ptr<juce::Component> content, juce::Rectangle<int> anchor, Placement placement = Placement::best);
    static void dismissPopover(juce::Component* content);
    using PopoverEdit = std::function<juce::Result(motion::Document&)>;
    // What an Apply needs unchanged since the popover opened: the whole
    // project state, or (for an edit that depends on nothing else) only
    // that it is the same project.
    enum class Requires { sameState, sameProject };
    std::function<void(PopoverEdit)> popoverEdit(juce::Component* panel, std::function<void(const juce::String&)> showError = {}, Requires requirement = Requires::sameState);


private:
    // Runs `then` at once when nothing would be lost, else once the user
    // agrees to discard the unsaved changes; `doing` says what discards them.
    void afterDiscarding(const juce::String& doing, std::function<void()> then);
    // `example`: one of those that ship with osci-motion, opened as a new
    // project of its own (saving asks where), not as that file.
    void loadProject(const juce::File& file, bool example = false);
    void openExample(const juce::String& resource);
    // Where an export goes unless told otherwise: in the last folder used,
    // named for the project. A name chosen without the extension gets it.
    juce::File defaultExportFile(const juce::String& extension) const;
    juce::String suggestedProjectName() const override { return processor.document.mainProject().name; }
    // Above the status bar, over the timeline: off the Scope, whose OpenGL
    // would draw over it.
    juce::Rectangle<int> updatePromptArea(int width, int height) const override { return {getWidth() - width - 14, getHeight() - height - 32, width, height}; }
    // How the project's export settings encode: its frame, codec, quality and
    // soundtrack, with the file and audio its codec takes.
    VideoEncodingConfiguration exportConfiguration(const motion::Project& project);
    static juce::File withExtension(juce::File chosen, const juce::String& extension);
    struct ExportState;
    // `then`, or first a question when the extension added names a file that
    // is already there (the save dialog checked only the name as typed).
    void whenReplaceable(const juce::File& chosen, const juce::File& destination, std::shared_ptr<ExportState> state, std::function<void()> then);
    // Says an export is done, with a way to find it.
    void exported(const juce::File& file);
    // True while Lua sliders are still baking: `resume` then runs once they
    // land (not if one fails or another project opens first).
    bool waitsForSliderBakes(std::function<void()> resume);

    // Construction, by area of the workspace.
    void setUpMenus();
    void setUpCompositionNavigation();
    void setUpScene();
    void setUpScope();
    // The canvas popover (any size), from the composition's Canvas field.
    void showCanvasSettings(juce::Component& anchor);
    void setUpTransport();
    void setUpLibrary();
    void setUpProperties();
    void setUpTimeline();
    void setUpGraph();
    // What Add source offers, each item adding its own; shown under its button.
    juce::PopupMenu addSourceMenu();
    void showAddSourceMenu();
    bool openSourceFile(const juce::File& file) override;
#if OSCI_PREMIUM
    // The title of the sheet that prepares it, so the two steps read as one.
    juce::String offlineRenderTitle() const override { return "Exporting video"; }
    void showOfflineRenderFailure(const juce::String& errorMessage, const juce::File& outputFile) override;
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
        std::optional<bool> originalSize; // a 3D object kept at its file's size
        // A video's frames, prepared in its settings sheet with these
        // settings, as the cache they are saved as.
        std::shared_ptr<const juce::MemoryBlock> preparedVideo;
        motion::Id relink = 0; // source replaced in place by this file
        motion::Id track = 0;  // dropped on this timeline track (0: a new track)
    };
    void beginSourceImport(SourceRequest request, motion::BakeSettings settings = {}, motion::RasterSettings rasterSettings = {});
    void moveEffectTo(motion::Id effect, motion::Id owner, bool copy);
    // Splits picked shapes off a clip's source into a clip of their own.
    void extractParts(const std::map<motion::Id, MotionCompositionView::Picked>& picks, const juce::String& name);
    // The clips parts were extracted from, selected again if the extraction is undone.
    std::vector<motion::Id> selectionBeforeExtract;
    void showNextPreparationSettings();
    void showBlenderSettings(motion::Id id = 0);
    // A picture another app shares (Syphon, Spout): added, with a clip at the
    // playhead; and how it's traced, beside the live picture.
    void addTextureSource(const motion::texture::Sender& sender);
    void showTextureSettings(motion::Id id);
    // A live source's capture, Blender's or a shared picture's alike: begun,
    // or kept if one runs; cancelled; kept as a source of its own (and its
    // settings closed). One that ends by itself is kept as it ends.
    bool liveCapturing(motion::Id id);
    juce::Result recordLiveCapture(motion::Id id);
    void cancelLiveCapture(motion::Id id);
    juce::Result keepLiveCapture(motion::Id id);
    void keepEndedCaptures();
    // The settings of a live source of either kind.
    void showLiveSettings(motion::Id id);
    void chooseSourceFile();
    bool importSourceFile(const juce::File& file, motion::Id relink, std::optional<std::pair<double, motion::Id>> placement = std::nullopt);
    void importSourceFiles(juce::Array<juce::File> files, std::optional<std::pair<double, motion::Id>> placement);
    void replaceSourceFile(motion::Id asset);
    std::deque<SourceRequest> preparationRequests;
    // Videos waiting for FFmpeg, out of the queue so the rest can go on.
    std::vector<SourceRequest> videosAwaitingFFmpeg;
    bool preparationSettingsOpen = false;
    struct ProjectLoad {
        std::atomic<bool> cancelled {false};
        motion::ProjectFile file;
    };
    std::shared_ptr<ProjectLoad> projectLoad;
    juce::Component::SafePointer<osci::OverlayComponent> projectLoadOverlay;
    void timerCallback() override;
    void displayFrame();
    double lastDisplayFrame = 0, shownPlayhead = 0, sceneDue = 0;
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
    };
    std::vector<ScopeView> scopeHistory;
    juce::TextButton scopeBack { "Back to Main" };
    juce::Label scopeLabel, scopeShared;
    std::uint64_t scopeNameGeneration = 0;
    void refreshInspector();
    bool selectionExists() const;
    bool selectionIsCamera() const;
    void refreshCameraTools();
    // `fromView`: where the Scene is looking from, rather than as the camera
    // on the output at `time` is.
    motion::Id addCamera(double time, bool fromView = false);
    // Looks through the selected camera, else the one on the Scope at the
    // playhead (following its cuts), else a new one made from the view.
    void setLookingThrough(bool on);
    // Selects `camera` and looks through it (a double-click or a menu).
    void lookThrough(motion::Id camera) {
        select(camera);
        setLookingThrough(true);
    }
    // An effect dragged over `owner` plays there: a new one of `type` at the
    // end of its stack, or the card `effect` moved (copied, with Alt) there.
    void previewEffect(const std::string& type, std::optional<motion::Id> owner);
    void previewEffectMove(motion::Id effect, std::optional<motion::Id> owner);
    // The Scene and the output show `project`, a preview; nullptr: the
    // document again.
    void previewProject(const motion::Project* project);
    void addEffectTo(const std::string& type, motion::Id owner);
    void dragOperationStarted(const juce::DragAndDropTarget::SourceDetails&) override;
    void dragOperationEnded(const juce::DragAndDropTarget::SourceDetails&) override;
    void importExample(const juce::String& resource);
    // A source being drawn or written takes over the Scene, one at a time,
    // until it is saved or cancelled, or another project opens.
    std::unique_ptr<juce::Component> sceneEditor;
    // Whether an editor already fills the Scene: if so it says to finish it
    // first, and gives it the keyboard.
    bool sceneEditorOpen();
    std::uint64_t sceneEditorGeneration = 0;
    template <typename Editor>
    Editor* sceneEditorAs() const { return dynamic_cast<Editor*>(sceneEditor.get()); }
    template <typename Editor>
    Editor& openSceneEditor(std::unique_ptr<Editor> editor);
    void closeSceneEditor();
    void showDrawingEditor(motion::Id asset);
    // A generator's clip, named for a preset when it has one, starting with
    // `settings` over its defaults.
    void addGenerator(const motion::GeneratorDefinition& definition, std::optional<motion::LightPreset> preset = std::nullopt, std::string_view named = {}, const motion::PropertyMap& settings = {});
    void addMirror(const motion::MirrorPreset& preset);
    void previewDrawing();
    // A new drawing becomes a source with a clip of its own at the playhead
    // as soon as it has a stroke, and is edited from then on.
    void addDrawingSource();
    motion::Id drawingAsset = 0;
    // The source and track a new drawing made, taken away again by Cancel.
    std::optional<std::pair<motion::Id, motion::Id>> drawingMade;
    // Text sources are written in the Scene, with the Scope previewing them.
    void showTextEditor(SourceRequest request);
    void previewText();
    SourceRequest textRequest;
    // Typing previews after a short pause rather than on every key.
    double textPreviewDue = 0;
    // A script being written plays on the Scope a moment after each change:
    // baked as it stands, off the message thread, the newest only.
    void previewLua();
    void showLuaPreview(std::shared_ptr<motion::Asset> asset);
    // Ids no document hands out, for what a live preview adds to a copy of
    // the project.
    struct PreviewId {
        static constexpr motion::Id effect = std::numeric_limits<motion::Id>::max(), asset = effect - 1, track = effect - 2, clip = effect - 3;
    };
    // A source being written on the output, in place of the source
    // `replacing`, or with none, new on a track of its own (named
    // `trackName`) for the whole project.
    void previewSource(std::shared_ptr<motion::Asset> asset, motion::Id replacing, const juce::String& trackName);
    double luaPreviewDue = 0;
    // Playback started so the script shows running, stopped again with it.
    bool luaStartedPlayback = false;
    std::shared_ptr<std::atomic<bool>> luaPreviewCancel;
    std::uint64_t luaPreviewGeneration = 0;
    juce::ThreadPool luaPreviews {1};
    // The Scope as drawn a moment ago (every couple of seconds), so saving
    // can take its picture without waiting for the next frame.
    juce::Image recentFrame;
    double recentFrameRequested = 0;
    void showLuaEditor(SourceRequest request);
    // A new Lua source, written from a short working script.
    void newScript();
    SourceRequest luaRequest;
    bool luaSubmitted = false;
    // `chosen`: the user picked this property; otherwise a new target opens
    // on its first animated channel.
    void selectCurveTarget(motion::Id id, const std::string& property, bool camera, bool chosen = false);
    // A modulate glyph's click, wherever the row is: its curve, shown in the Graph.
    void modulate(motion::Id id, const std::string& property, bool camera) {
        selectCurveTarget(id, property, camera, true);
        timelineTabs.setSelectedIndex(1);
    }
    // `leaveOutLive`: export without the clips of live sources.
    // Audio as an Output choice plays it: the project's audio, or the beam.
    void exportSignal(MotionProcessor::OutputMode mode, bool leaveOutLive = false);
    void exportVideo(bool leaveOutLive = false);
    // True when FFmpeg is ready. Otherwise offers to download it, shows the
    // download in the status bar, and calls `resume` once it is in place.
    bool ffmpegReady(std::function<void()> resume);
    // Export draws every frame offline, so a live source has to be captured
    // first. Says so before anything else is asked, with the ways on:
    // record a capture (its clips then draw it) or export the rest. False
    // when export can go ahead.
    bool waitsForCapture(bool video, std::function<void()> exportWithout);
    // The live source a capture is being recorded for, to export with:
    // once saved, its clips draw the capture and Export is offered again.
    motion::Id captureForExport = 0;
    std::uint64_t captureForExportGeneration = 0;
    bool captureForVideo = false;
    MotionProcessor::OutputMode captureForSignal = MotionProcessor::OutputMode::xyrgb;
    // A capture of the live source `live` was saved as `capture`.
    // `why` it ended by itself, if it did.
    void captured(motion::Id live, motion::Id capture, const juce::String& why = {});
    void startVideoExport(std::shared_ptr<ExportState> state, motion::Project project, std::shared_ptr<OfflineVisualiserParameters> beamSnapshot,
        VisualiserRenderer::RenderMode renderMode, VideoEncodingConfiguration config, juce::File destination, std::function<void(bool)> finished);
    void handleCommandLine(const juce::String& commandLine) override;
    void continueCommandLineRender();
    void failCommandLineRender(const juce::String& message);
    juce::File commandLineRender;
    // --stills=t1,t2: a render of just the stretches up to those times.
    std::vector<double> commandLineStills;
    bool commandLineRenderStarted = false;
    // The project a command-line render asked for: renders only once it is
    // the one open, and fails if it never becomes so.
    enum class CommandLineProject { none, waiting, loaded, failed };
    CommandLineProject commandLineProject = CommandLineProject::none;
    juce::File commandLineProjectFile;
    // Files given at launch wait for the last session to be restored, then
    // open over it: restoring it as they load would replace them.
    bool launched = false;
    juce::String launchFiles;
    // A load of `file` that ended without opening it (failed, cancelled, or
    // the project changed meanwhile).
    void projectNotLoaded(const juce::File& file);
    juce::PopupMenu timingMenu();
    bool applyTiming(int result);
    double snapDivision(int index) const;
    void refreshTiming();
    void refreshOutputChoices();
    // A device chosen since that has too few channels for the beam's
    // brightness plays it on X and Y, and says so.
    void keepOutputPlayable();
    // Keyboard shortcuts are data: each command appears in a menu with its
    // shortcut and is dispatched from keyPressed when no focused view used it.
    struct Command {
        int menu = 0;
        juce::String name, shortcut;
        juce::KeyPress key;
        std::function<void()> action;
    };
    std::vector<Command> commands;
    // `label` names it in its menu when that differs from its name (in a submenu).
    void addCommand(int menu, juce::String name, juce::KeyPress key, juce::String shortcut, std::function<void()> action, juce::String label = {});
    void registerCommands();
    void buildFileMenu(juce::PopupMenu& menu);
    bool fileMenuItemSelected(int id);
    void copySelection(bool cut);
    // Whether the Graph is showing in place of the timeline.
    bool graphShown() const { return timelineTabs.getCurrentTabIndex() == 1; }
    void pasteClipboard();
    void stepFrames(int frames);
    void jumpToKey(bool forward);
    void jumpToEdit(bool forward);
    void togglePlayback();
    void seekAndReveal(double time);
    void toggleLoop();
    void setLoopEdge(bool start);
    void loopSelection();
    bool setLoop(double start, double end, bool enabled, juce::String label);
    void loadLayout();
    void saveLayout();
    // The panel holding keyboard focus is outlined, so it is clear where
    // shortcuts go.
    void globalFocusChanged(juce::Component* focused) override;
    juce::Rectangle<int> focusedPanel() const;
    void splitAtPlayhead();
    // Whether the selected clip is under the playhead, to split there.
    bool canSplitAtPlayhead() const;
    // Whether a clip is selected for the Clip menu to act on.
    bool clipSelected() const;
    void placeClipAtPlayhead(bool start, bool trim);
    void showShortcuts();
    // Search: a field in the top bar (Cmd+F) that finds every action,
    // setting and property, with a list under it of what matches.
    motion::ui::SearchField searchField;
    std::unique_ptr<motion::ui::SearchPopup> searchPopup;
    // A list closed by a click, kept a moment so a second click lands on it
    // rather than on what was under it.
    std::unique_ptr<motion::ui::SearchPopup> searchPopupClosing;
    motion::search::Index searchIndex;
    // While the menus are built to be searched, not opened.
    bool indexingMenus = false;
    // A refresh waiting for the typing to pause.
    bool searchRefreshPending = false;
    juce::String searchedText;
    void setUpSearch();
    // Focuses the field (with `query`, if given) and lists what matches.
    void openSearch(const juce::String& query = {});
    // `byMouse`: a click chose; the list stays a moment to catch a double-click.
    void closeSearch(bool byMouse = false);
    void refreshSearch();
    void runSearchResult(const motion::search::Result& result, bool byMouse = false);
    // Why search can't act now (a sheet, the start screen, an editor over
    // the Scene), or empty.
    juce::String searchBlocked();
    // Menus held open one after another, as when looking for something:
    // once a session (and only the first few times ever, until search has
    // been used), a note by the field points to search once the menu closes.
    void menuOpened();
    void menusActivated(bool active);
    std::vector<double> menuOpenings, menusLookedIn;
    bool searchHintShown = false;
    // The last component other than search's own to have the keys, to give
    // them back when search closes.
    juce::Component::SafePointer<juce::Component> lastWorkFocus;
    void restoreFocusAfterSearch();
    void buildSearchIndex();
    // The project and its state the index was built from.
    std::uint64_t searchIndexGeneration = 0, searchIndexRevision = 0;
    // The parts of the index: the menus, what can be added, the selection's
    // own, the settings, the project's objects, tools and places, goals.
    void indexMenus();
    void indexAdding();
    void indexSelection();
    void indexSettings();
    void indexObjects();
    void indexTools();
    void indexGoals();
    // A menu's items, and its submenus', as entries found under `path`;
    // `choose` runs an item that has an id rather than an action;
    // `ticked` says whether an item (by id) is ticked now; `unavailable`:
    // why a greyed submenu holding them is greyed.
    void addMenuEntries(const juce::PopupMenu& menu, const juce::String& path, std::function<void(int)> choose, std::function<bool(int)> ticked, const juce::String& unavailable = {});
    // Whether the item `id` of menu `index` is ticked as the menu stands.
    bool menuItemTicked(int index, int id);
    // Says what came of a result by the field, with a way to act on it.
    std::unique_ptr<motion::ui::SearchNote> searchNote;
    // Notes said so far, so a note after a run knows whether the run said its own.
    int searchNotesSaid = 0;
    // Commands whose keys a note has taught this session.
    juce::StringArray searchTaught;
    void sayAfterSearch(const juce::String& text, const juce::String& actionLabel = {}, std::function<void()> act = {});
    // Selects `target` (unless it is selected) and then does `then`; when that
    // changed the selection, says it shows `what`, with a way back.
    void goToForSearch(motion::Id target, const juce::String& what, std::function<void(MotionEditor&)> then);
    // Shows `property` of the selection in Properties, or says it can't.
    void revealForSearch(const std::string& property, const juce::String& what);
    // What is selected, by name, and what browsing suggests for it.
    juce::String selectedSearchName;
    juce::StringArray suggestedSearchIds;
    // The start screen: recent projects and the examples, as pictures, and
    // the ways to begin. Shown as osci-motion opens unless turned off.
    void showStartScreen();
    // As osci-motion opens: the work a crash cut short reopened, then the
    // start screen.
    void atLaunch();
    bool restoreAtLaunch();
    void showStartScreenOnceReady();
    // The session written every little while for a crash to leave behind;
    // none for a launch that only renders.
    std::unique_ptr<motion::SessionRecovery> recovery;
    std::tuple<std::uint64_t, std::uint64_t, bool> recoveredState {};
    double recoveryDue = 0;
    void keepRecovery();
    juce::Component::SafePointer<osci::OverlayComponent> startScreenOverlay;
    juce::Component::SafePointer<motion::ui::StartScreen> startScreen;
    std::variant<std::monostate, std::vector<motion::Document::CopiedClip>, std::vector<motion::Document::CopiedKey>> clipboard;
    MotionProcessor& processor;
    motion::style::LookAndFeel motionLookAndFeel;
    // The examples' pictures for the Add source menu, made in the background.
    motion::ui::ExamplePictures examplePictures;
    // Restyles each dialog after the overlay lays it out.
    struct DialogStyle final : juce::ComponentListener {
        motion::style::DialogLookAndFeel look;
        void componentMovedOrResized(juce::Component& overlay, bool, bool) override { motion::style::restyleDialog(overlay, look); }
        void componentBeingDeleted(juce::Component& overlay) override { overlay.removeComponentListener(this); }
    } dialogStyle;
    motion::LuaSliderBakes sliderBakes { processor.document };
    std::function<void()> afterSliderBakes;
    std::uint64_t afterSliderBakesGeneration = 0;
    static constexpr const char* waitingForSliderBakes = "Exporting once the Lua sliders finish baking...";
    MotionTimelineView timeline;
    MotionCompositionView composition;
    // The beam's shares at the playhead, for Properties.
    MotionBeamShares beamShares;
    MotionAssetLibrary assetLibrary;
    MotionCurveEditor curveEditor;
    MotionCameraRig cameraRig;
    MotionScopePanel scopePanel { processor };
    MotionClipTimingPanel clipTimingPanel;
    MotionEffectLibrary effectLibrary;
    MotionEffectStack effectStack;
    MotionRoutingPanel routingPanel;
    MotionKeyPanel keyPanel;
    // The graph's side column scrolls: oscillator settings, then routing.
    osci::ScrollFadeViewport graphSideViewport;
    juce::Component graphSide;
    void layoutGraphSide();
    void refreshKeyPanel();
    MotionModulatorLibrary modulatorLibrary;
    osci::TabBar libraryTabs;
    osci::TabBar timelineTabs;
    MotionCurveList curveList;
    std::set<std::string> shownCurves, hiddenCurves;
    void refreshCurveList();
    osci::PanelDivider timelineDivider { false }, previewDivider { true };
    double timelineFraction = 0.34, previewFraction = 0.5;
    double dividerStart = 0;
    int previewWidth = 1, workspaceHeight = 1;
    juce::Label compositionTitle;
    MotionSceneToolbar sceneTools;
    MotionPartPickToolbar partPickTools;
    MotionScopeToolbar scopeTools;
    // Along the foot of the Scope while full screen.
    MotionScopeTransport scopeTransport;
    void showSpeedMenu(juce::Component& anchor);
    void changeSpeed(int steps);
    void refreshSpeed();
    void showInComposition(std::function<juce::Component&()> part);
    // Properties lead: a clip's timing, then a text clip's character animation.
    osci::SectionStack inspectorLead;
    MotionTextAnimationPanel textAnimation { processor };
    MotionModelSizeInspector modelSize { processor };
    // A 3D object's size, from Properties: fitted or its file's own.
    void setModelOriginalSize(motion::Id asset, bool original);
    MotionCompositionPanel compositionSettings { processor };
    // With nothing selected, Properties ends with the composition's effects
    // and then the project's description.
    osci::SectionStack compositionTrail;
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
    // On an XY-only scope in a lit scene, offers to shade by hatching.
    void offerHatchingForXY();
    // The Scene's right-click menu: framing, views along an axis, keys.
    // `forSearch`: also what needs a selection, greyed without one.
    juce::PopupMenu sceneMenu(bool forSearch = false);
    void showSceneMenu();
    void sceneMenuChosen(int result);

    juce::TextButton importButton { "Add source" };
    motion::icons::Button playButton { "Play", motion::icons::Icon::play };
    motion::icons::Button startButton { "Go to start", motion::icons::Icon::start };
    motion::icons::Button endButton { "Go to end", motion::icons::Icon::end };
    motion::icons::Button loopButton { "Loop playback", motion::icons::Icon::loop };
    MotionSpeedChip speedButton;
    // Auto-key: a change animates, from where its clip starts to the
    // playhead, without asking (off when osci-motion opens).
    motion::icons::Button autoKeyButton { "Auto-key", motion::icons::Icon::autoKey };
    void setAutoKey(bool on);
    // A change to what wasn't animated, made past its clip's start, offers
    // to animate (or with auto-key, does). The changes wait here for it.
    void editRecorded(const motion::Project& before);
    void animatePending();
    std::vector<motion::StaticChange> pendingAnimation;
    double pendingAnimationTime = 0;
    bool animating = false;
    // Undo and redo in the transport's style; the shared control stays out.
    // Right-clicked, each lists its steps, to go back (or on) several at once.
    osci::WithPopupMenu<motion::icons::Button> undoButton { "Undo", motion::icons::Icon::undo }, redoButton { "Redo", motion::icons::Icon::redo };
    juce::Label undoDescription;
    void refreshUndo();
    // The menu bar's menus, in order, and ids clear of the menu model's own
    // (1-based per item) for the items added to them here.
    enum MenuIndex { fileMenu, editMenu, clipMenu, transportMenu, viewMenu, timingMenuIndex, helpMenu };
    static constexpr int undoMenuId = 3001, redoMenuId = 3002;
    // View > Interface size: everything drawn larger or smaller, saved as a preference.
    static constexpr int interfaceSizeMenuId = 3300, softwareRenderingMenuId = 3400;
    static constexpr std::array<float, 8> interfaceSizes { .8f, .9f, 1.0f, 1.1f, 1.25f, 1.5f, 1.75f, 2.0f };
    void setInterfaceSize(float scale, bool keepWindowSize);
    // The add menu's items, and the first id of each list in it.
    struct AddMenuId {
        static constexpr int importFile = 1, blenderSource = 2, drawing = 3, newScript = 4;
        static constexpr int firstExample = 100, firstGenerator = 1000, firstLightPreset = 2000, firstMirror = 3000, firstTexture = 4000;
    };
    juce::Label timeLabel;
    motion::TimeGrid positionEditGrid;
    std::uint64_t positionEditGeneration = 0, positionEditRevision = 0;
    // The project tempo, as motion::style::bpm writes it: "120 BPM", "121.35 BPM"; double-click to type one.
    juce::Label tempoValue;
    // Tap tempo: taps preview the tempo; it commits once tapping pauses.
    juce::TextButton tapButton {"Tap"};
    motion::TapTempo tapTempo;
    std::optional<double> tappedBpm;
    std::unique_ptr<juce::TimedCallback> tapCommit;
    // The project and composition tapping began in: the tempo goes to them alone.
    std::uint64_t tapGeneration = 0, tapScope = 0;
    bool detectingTempo = false;
    double lastPaintedPosition = -1;
    int ticks = 0;
    std::shared_ptr<const motion::LiveSourceFrames> lastLiveFrames;
    juce::Component* visualiserControls = nullptr;
    motion::Id soundtrackClip() const;
    void detectTempo();
    void tap();
    MotionOutputButton outputButton;
    juce::Component::SafePointer<MotionOutputPanel> outputPanel;
    // The device the routing is for.
    juce::String routingDevice;
    // Where the sound and the beam play: the Output panel, and applying a
    // routing (the device opened with outputs enough for it).
    void showOutputPanel();
    juce::Result applyRouting(motion::OutputRouting routing);
    bool restoreRouting();
    MotionOutputPanel::Device outputDevice() const;
    MotionPropertyInspector propertyInspector;
    MotionStatusBar statusBar;
    // Errors and warnings, raised above the status bar so they're seen.
    MotionStatusAlert alert;
    void layoutAlert();
    // When a frame was last simplified to fit the beam's time.
    double overloadSeenAt = -1.0e9;
    motion::Id selection = 0;
    motion::Id curveTarget = 0;
    std::string curvePropertyName = "position.x";
    std::vector<std::string> curveProperties;
    bool cameraCurve = false;
    juce::ThreadPool imports { 1 };
    // A project opens on its own thread, not behind a long import.
    juce::ThreadPool projectLoads { 1 };
    struct ImportState {
        bool capture = false;
        std::atomic<bool> cancelled { false };
        std::atomic<double> progress { 0.0 };
        juce::String name;
        std::uint64_t generation = 0;
    };
    std::vector<std::shared_ptr<ImportState>> pendingImports;
    // An import ended, however it ended: a text animation change waiting for
    // it goes ahead.
    void importEnded(const std::shared_ptr<ImportState>& task);
    // Every import stops, and so does a text animation change waiting for one.
    void cancelImports();
    // The live source settings open, if any, and whose.
    juce::Component::SafePointer<motion::ui::Sheet> liveSettings;
    motion::Id liveSettingsId = 0;
    struct ExportState {
        std::atomic<bool> cancelled { false };
        std::atomic<double> progress { 0.0 };
        std::atomic<double> soundtrackProgress { 0.0 };
        bool videoWithAudio = false;
        double sampleRate = 48000;
        // Stills: only the stretches up to them are rendered.
        std::optional<motion::Stretches> stretches;
    };
    std::shared_ptr<ExportState> exportState;
    juce::ThreadPool exports { 1 };
    // An audio export's line in the status bar ("Exporting name.wav"),
    // while one runs.
    juce::String exportTask;
    juce::String lastPreparationError;

    MainMenuBarModel menus;
    osci::PanelHeader viewportHeader;
    // Titled by outputTitle, which gives way when the header is tight.
    osci::PanelHeader outputHeader;
    juce::Label outputTitle;
    osci::PanelHeader inspectorHeader;
    osci::PanelHeader timelineHeader { "Timeline" };
    juce::Rectangle<int> libraryBounds, viewportBounds, inspectorBounds, timelineBounds;
    // Between the menus and the transport.
    juce::Rectangle<int> menuRule;
    // Last, so it stops before anything it moves goes.
    juce::VBlankAttachment displaySync { this, [this] { displayFrame(); } };
};
