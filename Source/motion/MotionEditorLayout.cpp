#include "MotionEditor.h"

// Where the panels go, and remember going: the window laid out, painted and
// its layout saved and restored.

// The workspace layout is a per-user preference, not part of a project.
void MotionEditor::loadLayout() {
    auto& settings = processor.globalSettings;
    timelineFraction = std::clamp(settings.getDouble("motion.layout.timeline", timelineFraction), 0.25, 0.65);
    previewFraction = std::clamp(settings.getDouble("motion.layout.preview", previewFraction), 0.25, 0.75);
    const MotionTimelineView::Layout defaults;
    timeline.setLayout({settings.getInt("motion.layout.names", defaults.namesWidth), settings.getInt("motion.layout.trackHeight", defaults.trackHeight), settings.getBool("motion.layout.follow", defaults.follow)});
    timeline.onDefaultTrackHeight = [this](int) { saveLayout(); };
    timeline.refreshTracks();
}

void MotionEditor::saveLayout() {
    auto& settings = processor.globalSettings;
    settings.set("motion.layout.timeline", timelineFraction);
    settings.set("motion.layout.preview", previewFraction);
    const auto layout = timeline.layout();
    settings.set("motion.layout.names", layout.namesWidth);
    settings.set("motion.layout.trackHeight", layout.trackHeight);
    settings.set("motion.layout.follow", layout.follow);
    settings.save();
}

void MotionEditor::resized() {
    CommonPluginEditor::resized();
    // One gutter between all panels; the dividers sit in theirs.
    constexpr auto gutter = osci::PanelHeader::panelGap;
    auto area = getLocalBounds().reduced(3);
    auto top = area.removeFromTop(30);
    // Menus keep their natural width; the transport follows them, past a
    // rule, then search fills the rest of the row up to undo and redo.
    int menuWidth = 0;
    const auto names = static_cast<juce::MenuBarModel&>(menus).getMenuBarNames();
    for (int index = 0; index < names.size(); ++index) { menuWidth += menuBar.getLookAndFeel().getMenuBarItemWidth(menuBar, index, names[index]); }
    // The step undo would take back, written out only where there is room to
    // spare (its tooltip has it otherwise), in a slot that never moves search.
    const auto described = getWidth() >= 1680;
    {
        auto undo = top.removeFromRight(described ? 56 + 180 : 56);
        redoButton.setBounds(undo.removeFromRight(28).reduced(1, 3));
        undoButton.setBounds(undo.removeFromRight(28).reduced(1, 3));
        undoDescription.setVisible(described);
        // One line, shortened at its end when long (its tooltip has it whole).
        const auto line = undo.withTrimmedRight(4);
        undoDescription.setBounds(line.withSizeKeepingCentre(line.getWidth(), 16));
        motion::style::tooltipWhenCut(undoDescription);
    }
    top.removeFromRight(12);
    constexpr int transportGap = 24, searchGap = 24, widestSearch = 640, narrowestSearch = 280;
    // The transport goes compact (a tighter readout) before search would be
    // too narrow to say what it is for.
    const auto fullTransport = top.getWidth() - menuWidth - transportGap - 424 - searchGap >= narrowestSearch;
    auto transport = top.withTrimmedLeft(menuWidth + transportGap).withWidth(fullTransport ? 424 : 378);
    menuRule = juce::Rectangle<int>(top.getX() + menuWidth + transportGap / 2, top.getCentreY() - 8, 1, 16);
    // Search: its right edge by undo, as wide as there is room up to a
    // readable width; a wider window leaves the room after the transport.
    {
        auto room = top.withLeft(transport.getRight() + searchGap);
        const auto width = std::min(room.getWidth(), widestSearch);
        searchField.setBounds(room.removeFromRight(width).withSizeKeepingCentre(width, motion::style::controlHeight));
    }
    menuBar.setBounds(top.withWidth(menuWidth));
    startButton.setBounds(transport.removeFromLeft(28).reduced(1, 3));
    playButton.setBounds(transport.removeFromLeft(32).reduced(1, 3));
    endButton.setBounds(transport.removeFromLeft(28).reduced(1, 3));
    speedButton.setBounds(transport.removeFromLeft(36).reduced(1, 4));
    loopButton.setBounds(transport.removeFromLeft(28).reduced(1, 3));
    // Auto-key is an editing mode, not playback: a gap sets it apart.
    transport.removeFromLeft(8);
    autoKeyButton.setBounds(transport.removeFromLeft(28).reduced(1, 3));
    // Narrow windows tighten the readout so the tempo never collapses to
    // nothing.
    const auto compact = transport.getWidth() < 236;
    transport.removeFromLeft(compact ? 4 : 8);
    timeLabel.setBounds(transport.removeFromLeft(compact ? 80 : 118).reduced(0, 4));
    transport.removeFromLeft(compact ? 4 : 8);
    tempoValue.setBounds(transport.removeFromLeft(64).reduced(0, 4));
    tapButton.setBounds(transport.removeFromLeft(38).reduced(1, 4));
    area.removeFromTop(gutter);
    auto statusArea = area.removeFromBottom(20);
    // Where the project plays, in the status bar beside its rates: routing
    // set once, and the audio state it belongs with.
    outputButton.setBounds(statusArea.removeFromRight(std::min(outputButton.idealWidth(), 260)).reduced(0, 1));
    statusArea.removeFromRight(8);
    statusBar.setBounds(statusArea);
    layoutAlert();
    area.removeFromBottom(2);
    workspaceHeight = area.getHeight();
    timelineBounds = area.removeFromBottom(std::clamp(juce::roundToInt(workspaceHeight * timelineFraction), 240, workspaceHeight - 370));
    auto timeline = timelineBounds;
    auto header = timeline.removeFromTop(30);
    timelineHeader.setBounds(header);
    timelineTabs.setBounds(header.removeFromLeft(timelineTabs.preferredWidth()));
    const bool nested = processor.document.editingComposition() != 0;
    scopeBack.setVisible(nested);
    scopeLabel.setVisible(nested);
    scopeShared.setVisible(nested);
    if (nested) {
        // Inset like the tool row below it.
        auto breadcrumb = timeline.removeFromTop(28).reduced(motion::style::padding, 2);
        scopeBack.setBounds(breadcrumb.removeFromLeft(scopeBack.getBestWidthForHeight(24) + 16));
        breadcrumb.removeFromLeft(motion::style::padding);
        scopeShared.setBounds(breadcrumb.removeFromRight(140));
        scopeLabel.setBounds(breadcrumb);
    }
    this->timeline.setBounds(timeline.withTrimmedTop(3));
    auto graph = timeline.withTrimmedTop(3);
    // The three columns stay put with or without a target, so the frame never jumps.
    curveList.setVisible(graphShown());
    curveList.setBounds(graph.removeFromLeft(std::clamp(graph.getWidth() / 7, 150, 210)));
    graph.removeFromLeft(2);
    graphSideViewport.setVisible(graphShown());
    graphSideViewport.setBounds(graph.removeFromRight(std::clamp(graph.getWidth() / 4, 230, 285)));
    layoutGraphSide();
    graph.removeFromRight(2);
    curveEditor.setBounds(graph);
    // Each divider reaches 2 px into the panels either side, for an easier grab.
    timelineDivider.setBounds(area.removeFromBottom(gutter).expanded(0, 2));
    // Side panels widen on large windows; the preview keeps the rest.
    const auto extra = std::max(0, getWidth() - 1440);
    // Wide enough for its three tabs (Sources, Effects, Modulators) side by side.
    libraryBounds = area.removeFromLeft(std::clamp(205 + extra / 10, 205, 280));
    auto library = libraryBounds;
    libraryTabs.setBounds(library.removeFromTop(30));
    effectLibrary.setBounds(library.reduced(4, 0));
    modulatorLibrary.setBounds(library);
    importButton.setBounds(library.removeFromTop(40).reduced(8, 0).withTrimmedTop(6).withTrimmedBottom(4));
    assetLibrary.setBounds(library.reduced(4, 0));
    area.removeFromLeft(gutter);
    inspectorBounds = area.removeFromRight(std::clamp(300 + extra * 3 / 20, 300, 420));
    // Properties names what it edits in the header it covers.
    inspectorHeader.setBounds(inspectorBounds.withHeight(30));
    propertyInspector.setBounds(inspectorBounds);
    area.removeFromRight(gutter);
    previewWidth = area.getWidth();
    auto editing = area.removeFromLeft(juce::roundToInt((previewWidth - gutter) * previewFraction));
    previewDivider.setBounds(area.removeFromLeft(gutter).expanded(2, 0));
    auto output = area;
    outputHeader.setBounds(output.removeFromTop(30));
    output.removeFromTop(1);
    visualiser.setBounds(output);
    // The header has the title at its left; at its right the tools, then
    // what remains of the visualiser's own bar (the recording stopwatch, an
    // ffmpeg download). The title gives way first; on a Scope narrower still
    // the tools float in its picture.
    {
        const auto controlsHere = visualiserControls != nullptr && visualiserControls->getParentComponent() == this;
        const auto controlsWidth = controlsHere ? visualiser.controlsPreferredWidth() : 0;
        auto bar = outputHeader.getBounds().reduced(4, 0);
        const auto docked = !scopeFullScreen && bar.getWidth() >= scopeTools.extent() + (controlsHere ? controlsWidth + 4 : 0);
        scopeTools.setDocked(docked);
        // Floating, inside the Scope: over an OpenGL view on Windows nothing
        // else would show.
        auto& host = docked ? static_cast<juce::Component&>(*this) : static_cast<juce::Component&>(visualiser);
        if (scopeTools.getParentComponent() != &host) { host.addChildComponent(scopeTools); }
        // Full screen, the transport is the only bar.
        scopeTools.setVisible(!scopeFullScreen);
        if (controlsHere) {
            const auto width = std::min(controlsWidth, std::max(0, bar.getWidth()));
            visualiserControls->setBounds(bar.removeFromRight(width).withSizeKeepingCentre(width, 24));
            visualiserControls->toFront(false);
            bar.removeFromRight(4);
        }
        if (docked) {
            scopeTools.setBounds(bar.removeFromRight(scopeTools.extent()));
        } else {
            // 8 px in from the Scope's top right, as the Scene's strip is from its top left.
            scopeTools.setBounds(output.getWidth() - 8 - scopeTools.preferredWidth(), 8, scopeTools.preferredWidth(), scopeTools.preferredHeight());
        }
        const auto titled = bar.getWidth() >= 68;
        outputTitle.setVisible(titled);
        outputTitle.setBounds(outputHeader.getBounds().reduced(8, 3).withWidth(60));
    }
    viewportBounds = editing;
    viewportHeader.setBounds(editing.removeFromTop(30));
    auto viewControls = viewportHeader.getBounds().reduced(8, 3);
    compositionTitle.setVisible(viewControls.getWidth() >= 160);
    if (compositionTitle.isVisible()) { compositionTitle.setBounds(viewControls.removeFromLeft(100)); }
    // The tool strip floats at the Scene's top left.
    const auto room = editing.getHeight() - 20;
    sceneTools.setCompact(room < sceneTools.fullHeight());
    sceneTools.setVisible(room >= sceneTools.preferredHeight());
    sceneTools.setBounds(editing.getX() + 8, editing.getY() + 9, sceneTools.preferredWidth(), sceneTools.preferredHeight());
    composition.setBounds(editing.withTrimmedTop(1));
    composition.setCoveredLeft(sceneTools.isVisible() ? sceneTools.getRight() - composition.getX() : 0);
    sceneTools.toFront(false);
    // The ways of picking parts open beside the parts tool while it is on.
    partPickTools.setVisible(sceneTools.isVisible() && composition.inPartMode());
    partPickTools.setBounds(sceneTools.getRight() + 4, sceneTools.getY() + sceneTools.parts.getY() - motion::icons::ToolStrip::inset, partPickTools.preferredWidth(), partPickTools.preferredHeight());
    partPickTools.toFront(false);
    // A drawing or text being edited takes over the Scene.
    if (sceneEditor != nullptr) {
        sceneEditor->setBounds(viewportBounds);
        for (auto* component : std::initializer_list<juce::Component*> {&composition, &sceneTools, &partPickTools, &compositionTitle, &viewportHeader}) { component->setVisible(false); }
    }
    // Full screen, the Scope covers everything with its transport along the
    // foot (the tool strip is hidden); the recording stopwatch, which stays,
    // sits at the top right.
    if (scopeFullScreen) {
        visualiser.setBounds(getLocalBounds());
        visualiser.toFront(false);
        // The transport along the foot, as wide as reads well.
        const auto transportWidth = std::min(720, getWidth() - 32);
        scopeTransport.setBounds((getWidth() - transportWidth) / 2, getHeight() - 16 - MotionScopeTransport::preferredHeight, transportWidth, MotionScopeTransport::preferredHeight);
        scopeTransport.toFront(false);
        if (visualiserControls != nullptr && visualiserControls->getParentComponent() == this) {
            const auto width = visualiser.controlsPreferredWidth();
            visualiserControls->setBounds(getWidth() - 8 - width, 8, width, 24);
            visualiserControls->toFront(false);
        }
    }
}

// Centred over the foot of the window, just above the status bar.
void MotionEditor::layoutAlert() {
    const auto width = alert.widthFor(std::min(560, getWidth() - 32));
    const auto height = alert.heightFor(width);
    alert.setBounds((getWidth() - width) / 2, statusBar.getY() - height - 10, width, height);
}

void MotionEditor::paintOverChildren(juce::Graphics& graphics) {
    if (scopeFullScreen) { return; }
    // Every panel has the same rounded corners, whatever its content paints.
    const auto scope = outputHeader.getBounds().getUnion(visualiser.getBounds());
    juce::Graphics::ScopedSaveState state(graphics);
    // Dialogs sit above the panels, so the corners stay off them.
    for (const auto& overlay : activeOverlays) { graphics.excludeClipRegion(overlay->getPanelBounds() + overlay->getPosition()); }
    if (alert.isVisible()) { graphics.excludeClipRegion(alert.getBounds()); }
    graphics.setColour(osci::Colours::veryDark());
    for (const auto& panel : {libraryBounds, viewportBounds, scope, inspectorBounds, timelineBounds}) {
        if (panel.isEmpty()) { continue; }
        juce::Path corners;
        corners.addRectangle(panel.toFloat());
        corners.addRoundedRectangle(panel.toFloat(), motion::style::radiusPanel);
        corners.setUsingNonZeroWinding(false);
        graphics.fillPath(corners);
    }
    if (findActiveOverlay<osci::OverlayComponent>() != nullptr) { return; }
    const auto panel = focusedPanel();
    if (!panel.isEmpty()) {
        // Which panel the keyboard acts on, quietly: an outline, not a frame.
        graphics.setColour(motion::style::accent().withAlpha(.22f));
        graphics.drawRoundedRectangle(panel.toFloat().reduced(.5f), motion::style::radiusPanel, 1.0f);
    }
}

juce::Rectangle<int> MotionEditor::focusedPanel() const {
    const auto* focused = juce::Component::getCurrentlyFocusedComponent();
    if (focused == nullptr) { return {}; }
    const auto within = [focused](const juce::Component& component) { return &component == focused || component.isParentOf(focused); };
    if (within(timeline) || within(curveEditor) || within(curveList) || within(graphSideViewport)) { return timelineBounds; }
    if (within(composition) || within(sceneTools) || within(partPickTools)) { return viewportBounds; }
    if (within(assetLibrary) || within(effectLibrary) || within(modulatorLibrary)) { return libraryBounds; }
    if (within(propertyInspector) || within(clipTimingPanel)) { return inspectorBounds; }
    return {};
}

void MotionEditor::paint(juce::Graphics& graphics) {
    graphics.fillAll(osci::Colours::veryDark());
    for (const auto& panel : {libraryBounds, viewportBounds, inspectorBounds, timelineBounds}) {
        motion::style::fillPanel(graphics, panel);
    }
    graphics.setColour(motion::style::hairline());
    graphics.fillRect(menuRule);
}

// The column fills the side, the selected key above Routing; it scrolls
// only when its content is taller.
void MotionEditor::layoutGraphSide() {
    const auto keyHeight = keyPanel.isVisible() ? keyPanel.preferredHeight() + 1 : 0;
    const auto scrolls = keyHeight + routingPanel.preferredHeight() > graphSideViewport.getHeight();
    const auto width = graphSideViewport.getWidth() - (scrolls ? graphSideViewport.getScrollBarThickness() : 0);
    keyPanel.setBounds(0, 0, width, std::max(0, keyHeight - 1));
    routingPanel.setBounds(0, keyHeight, width, std::max(routingPanel.preferredHeight(), graphSideViewport.getHeight() - keyHeight));
    graphSide.setSize(width, routingPanel.getBottom());
}
