#pragma once

#include <JuceHeader.h>
#include <vector>
#include "../audio/DahdsrEnvelope.h"

// A single node in the breakpoint graph.
struct GraphNode {
    double time = 0.0;   // Domain position
    double value = 0.0;  // Value position
    float curve = 0.0f;  // Exponential curvature for incoming segment (-50 to +50, 0 = linear)
};

// A reusable breakpoint graph component with draggable nodes and per-segment curve editing.
// Designed to be used by both the DAHDSR envelope and LFO waveform editors.
class NodeGraphComponent : public juce::Component {
public:
    NodeGraphComponent();
    ~NodeGraphComponent() override;

    // --- Configuration ---
    void setDomainRange(double min, double max);
    void setValueRange(double min, double max);
    void setNodeLimits(int minNodes, int maxNodes);
    void setGridDisplay(bool showDomain, bool showValue);
    void setAllowNodeAddRemove(bool allow);

    // --- Node Management ---
    void setNodes(const std::vector<GraphNode>& nodes);
    std::vector<GraphNode> getNodes() const;
    int getNumNodes() const;

    // --- Evaluation ---
    // Evaluate the piecewise curve at a given domain position.
    float lookup(float time) const;

    // --- Coordinate Conversion ---
    double convertDomainToPixels(double domain) const;
    double convertPixelsToDomain(int px) const;
    double convertValueToPixels(double value) const;
    double convertPixelsToValue(int py) const;

    // --- Callbacks ---
    std::function<void()> onNodesChanged;
    std::function<void()> onDragStarted;
    std::function<void()> onDragEnded;

    // Called after a node is moved/added/removed to allow the consumer to
    // apply constraints (e.g. lock first/last node position).
    // Parameters: nodeIndex, time (in/out), value (in/out)
    std::function<void(int, double&, double&)> constrainNode;

    // Called to decide which segments have curve handles.
    // Returns true if segment ending at nodeIndex should show a curve handle.
    // If not set, all segments (index >= 1) have curve handles.
    std::function<bool(int nodeIndex)> hasCurveHandle;

    // --- Colour IDs ---
    enum ColourIds {
        backgroundColourId = 0x7001000,
        gridLineColourId   = 0x7001001,
        lineColourId       = 0x7001002,
        fillColourId       = 0x7001003,
        nodeColourId       = 0x7001004,
        nodeOutlineColourId = 0x7001005,
    };

    // --- Component overrides ---
    void paint(juce::Graphics& g) override;
    void paintOverChildren(juce::Graphics& g) override;
    void resized() override;

    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseMove(const juce::MouseEvent& e) override;
    void mouseEnter(const juce::MouseEvent& e) override;
    void mouseExit(const juce::MouseEvent& e) override;
    void mouseDoubleClick(const juce::MouseEvent& e) override;
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;

    // Set the value of the last node (used for seamless looping constraints).
    void setLastNodeValue(double value);

    // Snap configuration
    int getSnapDivisions() const { return snapDivisions; }
    void setSnapDivisions(int divisions);
    bool isSnapEnabled() const { return snapDivisions > 0; }

    // Maximum distance (px) from a handle for it to be selectable.
    // <= 0 means no limit (always pick closest).
    void setHoverThreshold(float px) { hoverThreshold = px; }

    // Corner radius for rounded clipping (0 = no rounding).
    void setCornerRadius(float r) { cornerRadius = r; }

private:
    static constexpr int kHandleSize = 11;
    static constexpr float kCurveStrokeThickness = 1.75f;
    static constexpr float kCurveHandleRadius = 3.0f;
    static constexpr float kHoverRingRadius = 10.0f;
    static constexpr float kHoverRingThickness = 1.5f;
    static constexpr float kHoverOuterRingThickness = 4.5f;

    // --- Internal handle component ---
    class HandleComponent : public juce::Component {
    public:
        HandleComponent(NodeGraphComponent& owner);
        void paint(juce::Graphics& g) override;
        void updateFromNode();
        void updateNodeFromPosition();

        NodeGraphComponent& owner;
        int nodeIndex = -1;
        juce::ComponentDragger dragger;
        bool isDragging = false;
    };

    // --- Grid rendering ---
    void paintGrid(juce::Graphics& g);
    void invalidateGrid();
    juce::Image gridCache;
    int gridCacheWidth = 0, gridCacheHeight = 0;
    bool gridDirty = true;

    // --- Node and handle management ---
    void rebuildHandles();
    void repositionHandles();
    void applyConstraints(int nodeIndex);
    HandleComponent* findClosestHandle(juce::Point<int> pos, float& outDist) const;
    int findClosestCurveHandle(juce::Point<int> pos, float& outDist) const;
    juce::Point<float> getCurveHandlePosition(int nodeIndex) const;
    bool shouldShowCurveHandle(int nodeIndex) const;

    // --- Data ---
    std::vector<GraphNode> nodes;
    juce::OwnedArray<HandleComponent> handles;

    double domainMin = 0.0, domainMax = 1.0;
    double valueMin = 0.0, valueMax = 1.0;
    int minNodes = 2, maxNodes = 100;
    bool showDomainGrid = false, showValueGrid = false;
    bool allowAddRemove = true;

    // --- Interaction state ---
    HandleComponent* activeHandle = nullptr;
    HandleComponent* hoverHandle = nullptr;
    int hoverCurveIndex = -1;
    int activeCurveIndex = -1;
    double curveDragLastNorm = 0.0;
    bool mouseIsOver = false;

    // --- Proximity ---
    float hoverThreshold = 0.0f; // max pixels from handle for selection (0 = unlimited)
    float cornerRadius = 0.0f;  // rounded clip radius

    // --- Snapping ---
    int snapDivisions = 8; // Number of grid divisions for snapping (0 = off)
    float scrollAccumulator = 0.0f;
    double snapToGrid(double value, double rangeMin, double rangeMax) const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NodeGraphComponent)
};
