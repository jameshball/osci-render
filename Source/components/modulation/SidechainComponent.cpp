#include "SidechainComponent.h"
#include "../effects/EffectComponent.h"
#include "../../PluginProcessor.h"
#include "../../LookAndFeel.h"

// ============================================================================
// Colours
// ============================================================================

juce::Colour SidechainComponent::getSidechainColour(int) {
    return juce::Colour(0xFFFF6B6B); // Coral red
}

// ============================================================================
// Config builder
// ============================================================================

static ModulationSourceConfig buildSidechainConfig(OscirenderAudioProcessor& proc) {
    ModulationSourceConfig cfg;
    cfg.sourceCount = NUM_SIDECHAINS;
    cfg.alwaysShowTabs = true;
    cfg.dragPrefix = "SC";
    cfg.getLabel = [](int) { return juce::String("INPUT"); };
    cfg.getSourceColour = &SidechainComponent::getSidechainColour;
    cfg.getCurrentValue = [&proc](int i) { return proc.sidechainParameters.getCurrentValue(i); };
    cfg.isSourceActive = [&proc](int i) { return proc.sidechainParameters.isActive(i); };
    cfg.getAssignments = [&proc]() { return proc.sidechainParameters.getAssignments(); };
    cfg.addAssignment = [&proc](const ModAssignment& a) { proc.sidechainParameters.addAssignment(a); };
    cfg.removeAssignment = [&proc](int idx, const juce::String& pid) { proc.sidechainParameters.removeAssignment(idx, pid); };
    cfg.getParamDisplayName = [&proc](const juce::String& pid) -> juce::String {
        return proc.getParamDisplayName(pid);
    };
    cfg.broadcaster = &proc.broadcaster;
    cfg.getActiveTab = [&proc]() { return proc.sidechainParameters.activeTab; };
    cfg.setActiveTab = [&proc](int i) { proc.sidechainParameters.activeTab = i; };
    return cfg;
}

// ============================================================================
// SidechainComponent
// ============================================================================

SidechainComponent::SidechainComponent(OscirenderAudioProcessor& processor)
    : ModulationSourceComponent(buildSidechainConfig(processor)),
      audioProcessor(processor) {

    auto colour = getSidechainColour(0);

    // Setup transfer curve graph — domain is in dB, value is output [0,1]
    graph.setDomainRange((double)sidechain::DB_FLOOR, (double)sidechain::DB_CEILING);
    graph.setValueRange(0.0, 1.0);
    graph.setNodeLimits(4, 4);       // 2 corner nodes + 2 draggable handles
    graph.setGridDisplay(true, true);
    graph.setGridStyle(NodeGraphComponent::GridStyle::Decibel);
    graph.setSnapStyle(NodeGraphComponent::SnapStyle::None);
    graph.setAllowNodeAddRemove(false);
    graph.setHoverThreshold(20.0f);
    graph.setCornerRadius(6.0f);
    graph.setSmoothMode(false);
    graph.setSnapDivisions(0);

    // 4 nodes: [0] bottom-left corner, [1] start handle, [2] end handle, [3] top-right corner
    // Corner nodes are invisible — they just cause the graph to draw extension lines.
    // Domain is in dB: -60 dB (silence) to 0 dB (full scale)
    std::vector<GraphNode> defaultNodes;
    defaultNodes.push_back({ (double)sidechain::DB_FLOOR, 0.0, 0.0f }); // corner: bottom-left
    defaultNodes.push_back({ (double)sidechain::DB_FLOOR, 0.0, 0.0f }); // handle: input min
    defaultNodes.push_back({ (double)sidechain::DB_CEILING, 1.0, 0.0f }); // handle: input max
    defaultNodes.push_back({ (double)sidechain::DB_CEILING, 1.0, 0.0f }); // corner: top-right
    graph.setNodes(defaultNodes);

    graph.constrainNode = [](int nodeIndex, double& time, double& value) {
        if (nodeIndex == 0) {
            // Bottom-left corner: locked at dB floor, value 0
            time = (double)sidechain::DB_FLOOR;
            value = 0.0;
        } else if (nodeIndex == 1) {
            // Start handle: time movable in dB range, value locked to 0
            time = juce::jlimit((double)sidechain::DB_FLOOR, (double)sidechain::DB_CEILING, time);
            value = 0.0;
        } else if (nodeIndex == 2) {
            // End handle: time movable in dB range, value locked to 1
            time = juce::jlimit((double)sidechain::DB_FLOOR, (double)sidechain::DB_CEILING, time);
            value = 1.0;
        } else if (nodeIndex == 3) {
            // Top-right corner: locked at 0 dB, value 1
            time = (double)sidechain::DB_CEILING;
            value = 1.0;
        }
    };

    // Only the main curve segment (handle 1 → handle 2) gets a curve power control
    graph.hasCurveHandle = [](int nodeIndex) { return nodeIndex == 2; };

    // Corner nodes are invisible and non-interactive
    graph.isHandleVisible = [](int nodeIndex) {
        return nodeIndex == 1 || nodeIndex == 2;
    };

    graph.onNodesChanged = [this]() {
        if (!isSyncingGraph)
            syncProcessorFromGraph();
    };

    graph.setColour(NodeGraphComponent::lineColourId, colour);
    graph.setColour(NodeGraphComponent::fillColourId, colour.withAlpha(0.15f));
    graph.setColour(NodeGraphComponent::nodeColourId, colour);
    graph.setFlowMarkerColour(colour);
    syncGraphColours();

    addAndMakeVisible(graph);

    // Attack knob (0-2 seconds, default 0.3s, skew centre 0.3)
    attackKnob.bindToParam(audioProcessor.sidechainParameters.attack, 0.3, 3);
    attackKnob.getKnob().setTextValueSuffix(" s");
    attackKnob.setAccentColour(getSidechainColour(0));
    attackKnob.wireModulation(audioProcessor);
    addAndMakeVisible(attackKnob);

    // Release knob (0-2 seconds, default 0.3s, skew centre 0.3)
    releaseKnob.bindToParam(audioProcessor.sidechainParameters.release, 0.3, 3);
    releaseKnob.getKnob().setTextValueSuffix(" s");
    releaseKnob.setAccentColour(getSidechainColour(0));
    releaseKnob.wireModulation(audioProcessor);
    addAndMakeVisible(releaseKnob);

    // Wire up the graph's undo manager so node changes are undoable
    graph.setUndoManager(&audioProcessor.getUndoManager());

    // Disable scroll fade overlays — single tab is always fully visible
    tabViewport.setSidesEnabled(false, false);

    // Register as listener on sidechain parameters so undo/redo triggers a UI sync
    paramSync.track(audioProcessor.sidechainParameters.attack);
    paramSync.track(audioProcessor.sidechainParameters.release);

    disabledOverlay.setText("NO SIDECHAIN INPUT");
    disabledOverlay.setSubText("(click to open audio settings)");
    disabledOverlay.onClick = [this]() {
        if (onDisabledClicked) onDisabledClicked();
    };
    addChildComponent(disabledOverlay);

    syncFromProcessorState();
}

SidechainComponent::~SidechainComponent() {
}

void SidechainComponent::timerCallback() {
    ModulationSourceComponent::timerCallback();

    // Show the current input level as a vertical marker on the graph
    float inputLevel = audioProcessor.sidechainParameters.getInputLevel(0);
    bool active = audioProcessor.sidechainParameters.isActive(0);

    if (active) {
        double pos = (double)inputLevel;
        graph.setFlowMarkerDomainPositions(&pos, 1);
    } else {
        graph.clearFlowMarkers();
    }
}

void SidechainComponent::paint(juce::Graphics& g) {
    ModulationSourceComponent::paint(g);

    // Draw a background panel behind the knob area (below the graph)
    if (!knobAreaBounds.isEmpty()) {
        float r = OscirenderLookAndFeel::RECT_RADIUS;
        auto bf = knobAreaBounds.toFloat();
        g.setColour(Colours::darker());
        g.fillRoundedRectangle(bf, r);
    }
}

void SidechainComponent::paintOverChildren(juce::Graphics& g) {
    // Skip base class seam shadow — single-tab sidechain doesn't need it.
    // Just draw the source-coloured outline around the graph.
    auto& cfg = getConfig();
    if (cfg.getSourceColour) {
        auto colour = cfg.getSourceColour(activeSourceIndex);
        auto graphBounds = graph.getBounds().toFloat();
        g.setColour(colour.withAlpha(0.25f));
        g.drawRoundedRectangle(graphBounds, 6.0f, 1.0f);
    }
}

void SidechainComponent::resized() {
    ModulationSourceComponent::resized();
    if (isCollapsed()) return;

    auto content = getContentBounds();

    // Knob row at the bottom
    static constexpr int knobRowHeight = 48;
    auto knobRow = content.removeFromBottom(knobRowHeight);
    content.removeFromBottom(4);

    // Store bounds for knob background painting
    knobAreaBounds = knobRow;

    // Two knobs side by side
    int knobW = knobRow.getWidth() / 2;
    attackKnob.setBounds(knobRow.removeFromLeft(knobW));
    releaseKnob.setBounds(knobRow);

    // Graph fills the remaining content area
    graph.setBounds(content);
    setOutlineBounds(content);

    // Disabled overlay covers the content area
    disabledOverlay.setBounds(getContentBounds());
}

void SidechainComponent::onActiveSourceChanged(int) {
    syncGraphFromProcessor();
    auto colour = getSidechainColour(0);
    attackKnob.setAccentColour(colour);
    releaseKnob.setAccentColour(colour);
}

void SidechainComponent::lookAndFeelChanged() {
    ModulationSourceComponent::lookAndFeelChanged();
    syncGraphColours();
}

void SidechainComponent::syncGraphColours() {
    // Override NodeGraphComponent's constructor defaults with LookAndFeel colours
    graph.setColour(NodeGraphComponent::backgroundColourId,  getLookAndFeel().findColour(NodeGraphComponent::backgroundColourId));
    graph.setColour(NodeGraphComponent::gridLineColourId,    getLookAndFeel().findColour(NodeGraphComponent::gridLineColourId));
    graph.setColour(NodeGraphComponent::nodeOutlineColourId, getLookAndFeel().findColour(NodeGraphComponent::nodeOutlineColourId));
    graph.invalidateGrid();
    graph.repaint();
}

void SidechainComponent::syncGraphFromProcessor() {
    juce::ScopedValueSetter<bool> guard(isSyncingGraph, true);

    auto curveNodes = audioProcessor.sidechainParameters.getTransferCurve(0);

    // Wrap the 2 processor nodes with invisible corner nodes for extension lines
    std::vector<GraphNode> displayNodes;
    displayNodes.push_back({ (double)sidechain::DB_FLOOR, 0.0, 0.0f }); // bottom-left corner
    for (auto& n : curveNodes)
        displayNodes.push_back(n);
    displayNodes.push_back({ (double)sidechain::DB_CEILING, 1.0, 0.0f }); // top-right corner
    graph.setNodes(displayNodes);

    auto colour = getSidechainColour(0);
    syncGraphColours();
    graph.setColour(NodeGraphComponent::lineColourId, colour);
    graph.setColour(NodeGraphComponent::fillColourId, colour.withAlpha(0.15f));
    graph.setColour(NodeGraphComponent::nodeColourId, colour);
}

void SidechainComponent::syncProcessorFromGraph() {
    auto allNodes = graph.getNodes();
    // Strip the first and last corner nodes — processor only stores the 2 handles
    if (allNodes.size() >= 4) {
        std::vector<GraphNode> curveNodes(allNodes.begin() + 1, allNodes.end() - 1);
        audioProcessor.sidechainParameters.setTransferCurve(0, curveNodes);
    }
}

void SidechainComponent::syncFromProcessorState() {
    ModulationSourceComponent::syncFromProcessorState();

    syncGraphFromProcessor();

    auto colour = getSidechainColour(0);
    attackKnob.setAccentColour(colour);
    attackKnob.getKnob().setValue(audioProcessor.sidechainParameters.getAttack(0), juce::dontSendNotification);
    releaseKnob.setAccentColour(colour);
    releaseKnob.getKnob().setValue(audioProcessor.sidechainParameters.getRelease(0), juce::dontSendNotification);
}

void SidechainComponent::setSidechainDisabled(bool disabled) {
    disabledOverlay.setDisabledWithSiblings(disabled, { graph, attackKnob, releaseKnob });
}
