#include "NodeGraphComponent.h"
#include "../../LookAndFeel.h"

// ============================================================================
// HandleComponent
// ============================================================================

NodeGraphComponent::HandleComponent::HandleComponent(NodeGraphComponent& o) : owner(o) {
    setSize(kHandleSize, kHandleSize);
    setRepaintsOnMouseActivity(true);
    setInterceptsMouseClicks(false, false);
}

void NodeGraphComponent::HandleComponent::paint(juce::Graphics& g) {
    // Check visibility callback before drawing
    if (owner.isHandleVisible && nodeIndex >= 0 && !owner.isHandleVisible(nodeIndex))
        return;

    const bool hot = isMouseOverOrDragging(true) || (owner.hoverHandle == this);
    const float scale = hot ? 0.78f : 0.72f;
    const float size = (float)getWidth() * scale;
    const float inset = ((float)getWidth() - size) * 0.5f;

    g.setColour(owner.findColour(nodeColourId).withAlpha(hot ? 1.0f : 0.95f));
    g.fillEllipse(inset, inset, size, size);
    g.setColour(owner.findColour(nodeOutlineColourId).withAlpha(hot ? 1.0f : 0.85f));
    g.drawEllipse(inset, inset, size, size, kCurveStrokeThickness);
}

void NodeGraphComponent::HandleComponent::updateFromNode() {
    if (nodeIndex < 0 || nodeIndex >= (int)owner.nodes.size()) return;
    auto& node = owner.nodes[nodeIndex];
    int x = (int)owner.convertDomainToPixels(node.time);
    int y = (int)owner.convertValueToPixels(node.value);
    setTopLeftPosition(x, y);
}

void NodeGraphComponent::HandleComponent::updateNodeFromPosition() {
    if (nodeIndex < 0 || nodeIndex >= (int)owner.nodes.size()) return;
    auto& node = owner.nodes[nodeIndex];
    node.time = owner.convertPixelsToDomain(getX());
    node.value = owner.convertPixelsToValue(getY());
    owner.applyConstraints(nodeIndex);
}

// ============================================================================
// NodeGraphComponent
// ============================================================================

NodeGraphComponent::NodeGraphComponent() {
    setColour(backgroundColourId, Colours::veryDark);
    setColour(gridLineColourId, juce::Colours::white.withAlpha(0.1f));
    setColour(lineColourId, juce::Colour(0xFF00E5FF));
    setColour(fillColourId, juce::Colour(0xFF00E5FF).withAlpha(0.15f));
    setColour(nodeColourId, juce::Colour(0xFF00E5FF));
    setColour(nodeOutlineColourId, juce::Colours::white.withAlpha(0.6f));
}

NodeGraphComponent::~NodeGraphComponent() {
    handles.clear();
}

// --- Configuration ---

void NodeGraphComponent::setDomainRange(double min, double max) {
    if (domainMin != min || domainMax != max) {
        domainMin = min;
        domainMax = max;
        invalidateGrid();
        repositionHandles();
        repaint();
    }
}

void NodeGraphComponent::setValueRange(double min, double max) {
    if (valueMin != min || valueMax != max) {
        valueMin = min;
        valueMax = max;
        invalidateGrid();
        repositionHandles();
        repaint();
    }
}

void NodeGraphComponent::setNodeLimits(int min, int max) {
    minNodes = min;
    maxNodes = max;
}

void NodeGraphComponent::setGridDisplay(bool domain, bool value) {
    if (showDomainGrid != domain || showValueGrid != value) {
        showDomainGrid = domain;
        showValueGrid = value;
        invalidateGrid();
        repaint();
    }
}

void NodeGraphComponent::setAllowNodeAddRemove(bool allow) {
    allowAddRemove = allow;
}

// --- Node Management ---

void NodeGraphComponent::setNodes(const std::vector<GraphNode>& newNodes) {
    nodes = newNodes;
    if ((int)newNodes.size() == handles.size()) {
        repositionHandles();
    } else {
        rebuildHandles();
    }
    repaint();
}

std::vector<GraphNode> NodeGraphComponent::getNodes() const {
    return nodes;
}

int NodeGraphComponent::getNumNodes() const {
    return (int)nodes.size();
}

// --- Evaluation ---

float NodeGraphComponent::lookup(float time) const {
    return evaluateGraphCurve(nodes, time, useBezierInterpolation);
}

// --- Coordinate Conversion ---

double NodeGraphComponent::convertDomainToPixels(double domain) const {
    int w = getWidth() - kHandleSize;
    if (w <= 0 || domainMax <= domainMin) return 0.0;
    return (domain - domainMin) / (domainMax - domainMin) * w;
}

double NodeGraphComponent::convertPixelsToDomain(int px) const {
    int w = getWidth() - kHandleSize;
    if (w <= 0) return domainMin;
    return (double)px / w * (domainMax - domainMin) + domainMin;
}

double NodeGraphComponent::convertValueToPixels(double value) const {
    int h = getHeight() - kHandleSize;
    if (h <= 0 || valueMax <= valueMin) return 0.0;
    return h - ((value - valueMin) / (valueMax - valueMin) * h);
}

double NodeGraphComponent::convertPixelsToValue(int py) const {
    int h = getHeight() - kHandleSize;
    if (h <= 0) return valueMin;
    return (double)(h - py) / h * (valueMax - valueMin) + valueMin;
}

// --- Grid ---

void NodeGraphComponent::invalidateGrid() {
    gridDirty = true;
}

void NodeGraphComponent::setGridStyle(GridStyle style) {
    if (gridStyle != style) {
        gridStyle = style;
        invalidateGrid();
        repaint();
    }
}

void NodeGraphComponent::setSnapStyle(SnapStyle style) {
    snapStyle = style;
}

// Shared helper for computing logarithmic grid steps and visibility.
bool NodeGraphComponent::computeLogGridInfo(int widthPx, LogGridInfo& out) const {
    if (widthPx <= kHandleSize || domainMax <= domainMin)
        return false;

    const double domainRange = domainMax - domainMin;
    out.secondsPerPixel = domainRange / (double)(widthPx - kHandleSize);
    const double targetPx = 70.0;
    const double idealStep = out.secondsPerPixel * targetPx;

    constexpr double kBaseStep = 8.0;
    const double ratio = juce::jmax(idealStep / kBaseStep, 1.0e-9);
    const double level = std::log(ratio) / std::log(0.25);
    const double lowerIndex = std::floor(level);
    out.lowerStep = kBaseStep * std::pow(0.25, lowerIndex);
    out.upperStep = kBaseStep * std::pow(0.25, lowerIndex + 1.0);

    const double stepDelta = juce::jmax(1.0e-9, out.lowerStep - out.upperStep);
    const float t = (float)juce::jlimit(0.0, 1.0, (out.lowerStep - idealStep) / stepDelta);

    constexpr float kBaseAlpha = 0.42f;
    out.lowerAlpha = kBaseAlpha;
    out.upperAlpha = kBaseAlpha * t;

    double lowerPxStep = out.lowerStep / out.secondsPerPixel;
    double upperPxStep = out.upperStep / out.secondsPerPixel;
    out.lowerLineVis = (float)juce::jlimit(0.0, 1.0, (lowerPxStep - 40.0) / 40.0);
    out.upperLineVis = (float)juce::jlimit(0.0, 1.0, (upperPxStep - 40.0) / 40.0);

    return true;
}

void NodeGraphComponent::paintGrid(juce::Graphics& g) {
    const int w = getWidth();
    const int h = getHeight();
    if (w <= 0 || h <= 0) return;

    // Delegate to custom grid painter if provided
    if (customGridPainter) {
        if (gridDirty || gridCacheWidth != w || gridCacheHeight != h || !gridCache.isValid()) {
            gridCache = juce::Image(juce::Image::ARGB, w, h, true);
            gridCacheWidth = w;
            gridCacheHeight = h;
            gridDirty = false;

            juce::Graphics gg(gridCache);
            customGridPainter(gg, w, h);
        }
        g.drawImageAt(gridCache, 0, 0);
        return;
    }

    if (gridDirty || gridCacheWidth != w || gridCacheHeight != h || !gridCache.isValid()) {
        gridCache = juce::Image(juce::Image::ARGB, w, h, true);
        gridCacheWidth = w;
        gridCacheHeight = h;
        gridDirty = false;

        juce::Graphics gg(gridCache);

        switch (gridStyle) {
            case GridStyle::LogarithmicTime:
                paintLogarithmicTimeGrid(gg, w, h);
                break;
            case GridStyle::Uniform:
            default:
                paintUniformGrid(gg, w, h);
                break;
        }
    }

    g.drawImageAt(gridCache, 0, 0);
}

void NodeGraphComponent::paintUniformGrid(juce::Graphics& g, int w, int h) {
    g.setColour(findColour(backgroundColourId));
    g.fillRect(0, 0, w, h);

    auto gridColour = findColour(gridLineColourId);

    if (showDomainGrid && domainMax > domainMin && snapDivisions > 0) {
        double range = domainMax - domainMin;
        double step = range / (double)snapDivisions;
        for (double d = domainMin + step; d < domainMax - step * 0.1; d += step) {
            float x = (float)(convertDomainToPixels(d) + kHandleSize * 0.5);
            g.setColour(gridColour);
            g.drawVerticalLine((int)x, 0.0f, (float)h);
        }
    }

    if (showValueGrid && valueMax > valueMin) {
        double range = valueMax - valueMin;
        double step = range / 4.0;
        for (double v = valueMin + step; v < valueMax - step * 0.1; v += step) {
            float y = (float)(convertValueToPixels(v) + kHandleSize * 0.5);
            g.setColour(gridColour);
            g.drawHorizontalLine((int)y, 0.0f, (float)w);
        }
    }
}

void NodeGraphComponent::paintLogarithmicTimeGrid(juce::Graphics& g, int w, int h) {
    g.setColour(findColour(backgroundColourId));
    g.fillRect(0, 0, w, h);

    if (!showDomainGrid) return;

    LogGridInfo info;
    if (!computeLogGridInfo(w, info)) return;

    auto gridColour = findColour(gridLineColourId);

    auto formatTimeLabel = [](double seconds) -> juce::String {
        if (seconds >= 1.0) {
            if (seconds >= 10.0)
                return juce::String((int)std::round(seconds)) + "s";
            return juce::String(seconds, 1) + "s";
        }
        return juce::String((int)std::round(seconds * 1000.0)) + "ms";
    };

    auto drawVerticalGrid = [&](double step, float alpha, float lineVis, double majorStep) {
        if (step <= 0.0 || alpha <= 0.001f)
            return;

        const double start = std::ceil(domainMin / step) * step;
        const double end = domainMax + (step * 0.5);

        juce::Font labelFont(11.0f, juce::Font::bold);
        const float labelY = (float)h - 2.0f - labelFont.getHeight();

        for (double domain = start; domain <= end; domain += step) {
            if (majorStep > 0.0) {
                const double rem = std::fmod(domain, majorStep);
                if (rem < (step * 0.001) || (majorStep - rem) < (step * 0.001))
                    continue;
            }
            const float x = (float)(convertDomainToPixels(domain) + (kHandleSize * 0.5f));
            if (x < -1.0f || x > (float)w + 1.0f)
                continue;
            g.setColour(gridColour.withAlpha(alpha * lineVis));
            g.drawVerticalLine((int)std::round(x), 0.0f, (float)h);

            if (lineVis > 0.01f) {
                const juce::String label = formatTimeLabel(domain);
                g.setFont(labelFont);
                const float textAlpha = juce::jlimit(0.0f, 1.0f, alpha * 1.25f * lineVis);
                g.setColour(juce::Colours::black.withAlpha(textAlpha * 0.65f));
                g.drawText(label, (int)x + 5, (int)labelY + 1, 70, (int)labelFont.getHeight(), juce::Justification::left);
                g.setColour(gridColour.withAlpha(textAlpha));
                g.drawText(label, (int)x + 4, (int)labelY, 70, (int)labelFont.getHeight(), juce::Justification::left);
            }
        }
    };

    drawVerticalGrid(info.lowerStep, info.lowerAlpha, info.lowerLineVis, 0.0);
    drawVerticalGrid(info.upperStep, info.upperAlpha, info.upperLineVis, info.lowerStep);
}

// --- Handle Management ---

void NodeGraphComponent::rebuildHandles() {
    hoverHandle = nullptr;
    activeHandle = nullptr;
    clearCurvesForOverlappingNodes();
    handles.clear();
    for (int i = 0; i < (int)nodes.size(); ++i) {
        auto* h = new HandleComponent(*this);
        h->nodeIndex = i;
        addAndMakeVisible(h);
        h->updateFromNode();
        handles.add(h);
    }
}

void NodeGraphComponent::clearCurvesForOverlappingNodes() {
    for (int i = 1; i < (int)nodes.size(); ++i) {
        double prevPx = convertDomainToPixels(nodes[i - 1].time);
        double currPx = convertDomainToPixels(nodes[i].time);
        if (std::abs(currPx - prevPx) < (double)kHandleSize)
            nodes[i].curve = 0.0f;
    }
}

void NodeGraphComponent::repositionHandles() {
    for (auto* h : handles) {
        h->updateFromNode();
    }
}

void NodeGraphComponent::applyConstraints(int nodeIndex) {
    if (constrainNode && nodeIndex >= 0 && nodeIndex < (int)nodes.size()) {
        constrainNode(nodeIndex, nodes[nodeIndex].time, nodes[nodeIndex].value);
    }
    // Clamp to domain/value range
    if (nodeIndex >= 0 && nodeIndex < (int)nodes.size()) {
        nodes[nodeIndex].time = juce::jlimit(domainMin, domainMax, nodes[nodeIndex].time);
        nodes[nodeIndex].value = juce::jlimit(valueMin, valueMax, nodes[nodeIndex].value);
    }
}

NodeGraphComponent::HandleComponent* NodeGraphComponent::findClosestHandle(juce::Point<int> pos, float& outDist) const {
    HandleComponent* closest = nullptr;
    float bestDist = std::numeric_limits<float>::max();
    constexpr float kTieEpsilon = 4.0f; // px — treat handles within this distance as tied

    for (auto* h : handles) {
        // Skip hidden handles
        if (isHandleVisible && h->nodeIndex >= 0 && !isHandleVisible(h->nodeIndex))
            continue;
        auto center = h->getBoundsInParent().getCentre().toFloat();
        float d = center.getDistanceFrom(pos.toFloat());

        if (d < bestDist - kTieEpsilon) {
            // Clearly closer — always wins
            bestDist = d;
            closest = h;
        } else if (d <= bestDist + kTieEpsilon && closest != nullptr) {
            // Within epsilon — tiebreaker: prefer the earlier handle if cursor
            // is to the right of the handles, later handle if cursor is to the left.
            float handleCenterX = center.x;
            bool cursorIsRight = (float)pos.x >= handleCenterX;
            bool newIsLater = h->nodeIndex > closest->nodeIndex;
            if (cursorIsRight ? newIsLater : !newIsLater) {
                bestDist = d;
                closest = h;
            }
        }
    }
    outDist = bestDist;
    return closest;
}

int NodeGraphComponent::findClosestCurveHandle(juce::Point<int> pos, float& outDist) const {
    int closestIndex = -1;
    float bestDist = std::numeric_limits<float>::max();
    for (int i = 1; i < (int)nodes.size(); ++i) {
        if (!shouldShowCurveHandle(i)) continue;
        auto center = getCurveHandlePosition(i);
        float d = center.getDistanceFrom(pos.toFloat());
        if (d < bestDist) {
            bestDist = d;
            closestIndex = i;
        }
    }
    outDist = bestDist;
    return closestIndex;
}

juce::Point<float> NodeGraphComponent::getCurveHandlePosition(int nodeIndex) const {
    if (nodeIndex <= 0 || nodeIndex >= (int)nodes.size()) return {};
    double midTime = (nodes[nodeIndex - 1].time + nodes[nodeIndex].time) * 0.5;
    float value = lookup((float)midTime);
    float x = (float)convertDomainToPixels(midTime) + kHandleSize * 0.5f;
    float y = (float)convertValueToPixels(value) + kHandleSize * 0.5f;
    return { x, y };
}

bool NodeGraphComponent::shouldShowCurveHandle(int nodeIndex) const {
    if (nodeIndex <= 0 || nodeIndex >= (int)nodes.size()) return false;
    // Hide curve handle when the segment is too short to meaningfully adjust curvature.
    double prevPx = convertDomainToPixels(nodes[nodeIndex - 1].time);
    double currPx = convertDomainToPixels(nodes[nodeIndex].time);
    if (std::abs(currPx - prevPx) < 3.0)
        return false;
    if (hasCurveHandle) return hasCurveHandle(nodeIndex);
    return true; // All segments by default
}

// --- Paint ---

void NodeGraphComponent::paint(juce::Graphics& g) {
    if (cornerRadius > 0) {
        juce::Path clip;
        clip.addRoundedRectangle(getLocalBounds().toFloat(), cornerRadius);
        g.reduceClipRegion(clip);
    }
    paintGrid(g);

    if (nodes.size() < 2) return;

    juce::Path path;
    float halfHandle = kHandleSize * 0.5f;

    // Start at first node
    float startX = (float)convertDomainToPixels(nodes[0].time) + halfHandle;
    float startY = (float)convertValueToPixels(nodes[0].value) + halfHandle;
    path.startNewSubPath(startX, startY);

    for (size_t i = 1; i < nodes.size(); ++i) {
        float prevTime = (float)nodes[i - 1].time;
        float nextTime = (float)nodes[i].time;
        float handleTime = nextTime - prevTime;

        // Adaptive sampling based on pixel width and curve intensity
        float x0 = (float)convertDomainToPixels(prevTime);
        float x1 = (float)convertDomainToPixels(nextTime);
        float dxPx = std::abs(x1 - x0);
        float curveAmount = std::abs(nodes[i].curve);
        float curveFactor = 1.0f + curveAmount / 20.0f;
        int steps = juce::jlimit(16, 500, (int)std::round(juce::jmax(1.0f, dxPx) * 0.45f * curveFactor));
        float timeInc = handleTime / (float)steps;

        float t = prevTime;
        for (int j = 0; j < steps; ++j) {
            float v = lookup(t);
            path.lineTo((float)convertDomainToPixels(t) + halfHandle,
                        (float)convertValueToPixels(v) + halfHandle);
            t += timeInc;
        }

        // End exactly at the node
        float endX = (float)convertDomainToPixels(nextTime) + halfHandle;
        float endY = (float)convertValueToPixels(nodes[i].value) + halfHandle;
        path.lineTo(endX, endY);
    }

    // Fill under curve
    juce::Path fillPath(path);
    float lastX = (float)convertDomainToPixels(nodes.back().time) + halfHandle;
    fillPath.lineTo(lastX, (float)getHeight());
    fillPath.lineTo(startX, (float)getHeight());
    fillPath.closeSubPath();

    g.setColour(findColour(fillColourId));
    g.fillPath(fillPath);

    // Flow trail (between fill and stroke)
    float trailGlowStrength = 0.0f;
    if (flowTrailRenderer) {
        auto trailColour = findColour(lineColourId).interpolatedWith(juce::Colours::white, 0.99f);
        flowTrailRenderer->paintTrail(g, fillPath, trailColour, trailGlowStrength);
    }

    // Glow effect from trail
    if (trailGlowStrength > 0.01f) {
        const float glow = juce::jlimit(0.0f, 1.0f, trailGlowStrength);
        const float glowThickness = kCurveStrokeThickness + (2.0f * glow);
        g.setColour(findColour(lineColourId).withAlpha(0.25f + 0.55f * glow));
        g.strokePath(path, juce::PathStrokeType(glowThickness, juce::PathStrokeType::beveled, juce::PathStrokeType::rounded));
    }

    // Marker heads
    if (flowTrailRenderer) {
        auto markerColour = flowMarkerColourSet
            ? flowMarkerColour
            : findColour(lineColourId).withAlpha(0.7f);
        flowTrailRenderer->paintMarkerHeads(g, fillPath, markerColour);
    }

    // Stroke curve
    g.setColour(findColour(lineColourId).withAlpha(0.90f));
    g.strokePath(path, juce::PathStrokeType(kCurveStrokeThickness, juce::PathStrokeType::beveled, juce::PathStrokeType::rounded));
}

void NodeGraphComponent::paintOverChildren(juce::Graphics& g) {
    if (cornerRadius > 0) {
        juce::Path clip;
        clip.addRoundedRectangle(getLocalBounds().toFloat(), cornerRadius);
        g.reduceClipRegion(clip);
    }

    if (nodes.size() < 2) return;

    // Draw curve handles – small solid white dots
    for (int i = 1; i < (int)nodes.size(); ++i) {
        if (!shouldShowCurveHandle(i)) continue;
        auto center = getCurveHandlePosition(i);
        float r = kCurveHandleRadius;
        g.setColour(juce::Colours::white.withAlpha(0.9f));
        g.fillEllipse(center.x - r, center.y - r, r * 2.0f, r * 2.0f);
    }

    // Hover ring
    if (mouseIsOver) {
        juce::Point<float> center;
        float ringR = kHoverRingRadius;

        if (hoverCurveIndex >= 1) {
            center = getCurveHandlePosition(hoverCurveIndex);
        } else if (hoverHandle != nullptr) {
            center = hoverHandle->getBoundsInParent().getCentre().toFloat();
        } else {
            return;
        }

        g.setColour(juce::Colours::white.withAlpha(0.85f));
        g.drawEllipse(center.x - ringR, center.y - ringR, ringR * 2.0f, ringR * 2.0f, kHoverRingThickness);

        bool isDragging = (hoverCurveIndex >= 1 && activeCurveIndex == hoverCurveIndex) ||
                          (hoverHandle != nullptr && activeHandle == hoverHandle);
        if (isDragging) {
            float outerR = ringR + 2.0f;
            g.setColour(juce::Colours::white.withAlpha(0.35f));
            g.drawEllipse(center.x - outerR, center.y - outerR, outerR * 2.0f, outerR * 2.0f, kHoverOuterRingThickness);
        }
    }
}

void NodeGraphComponent::resized() {
    repositionHandles();
    invalidateGrid();
    if (flowTrailRenderer)
        flowTrailRenderer->resized();
}

// --- Mouse Interaction ---

void NodeGraphComponent::mouseEnter(const juce::MouseEvent& e) {
    mouseIsOver = true;
    mouseMove(e);
}

void NodeGraphComponent::mouseMove(const juce::MouseEvent& e) {
    mouseIsOver = true;
    auto prevHover = hoverHandle;
    int prevCurveHover = hoverCurveIndex;

    float nodeDist = 0, curveDist = 0;
    auto* closestNode = findClosestHandle(e.getPosition(), nodeDist);
    int closestCurve = findClosestCurveHandle(e.getPosition(), curveDist);

    // Apply proximity threshold if set
    bool nodeInRange = (closestNode != nullptr) && (hoverThreshold <= 0 || nodeDist <= hoverThreshold);
    bool curveInRange = (closestCurve >= 1) && (hoverThreshold <= 0 || curveDist <= hoverThreshold);

    if (curveInRange && (!nodeInRange || curveDist <= nodeDist)) {
        hoverHandle = nullptr;
        hoverCurveIndex = closestCurve;
    } else if (nodeInRange) {
        hoverHandle = closestNode;
        hoverCurveIndex = -1;
    } else {
        hoverHandle = nullptr;
        hoverCurveIndex = -1;
    }

    if (prevHover != hoverHandle || prevCurveHover != hoverCurveIndex) {
        // Fire hover callback
        if (onNodeHover) {
            int hoverIdx = (hoverCurveIndex >= 1) ? hoverCurveIndex
                         : (hoverHandle != nullptr) ? hoverHandle->nodeIndex : -1;
            bool isCurve = (hoverCurveIndex >= 1);
            if (hoverIdx != prevHoverNodeIndex || isCurve != prevHoverIsCurve) {
                prevHoverNodeIndex = hoverIdx;
                prevHoverIsCurve = isCurve;
                onNodeHover(hoverIdx, isCurve);
            }
        }
        repaint();
    }
}

void NodeGraphComponent::mouseExit(const juce::MouseEvent&) {
    mouseIsOver = false;
    hoverHandle = nullptr;
    hoverCurveIndex = -1;
    if (onNodeHover && (prevHoverNodeIndex != -1 || prevHoverIsCurve)) {
        prevHoverNodeIndex = -1;
        prevHoverIsCurve = false;
        onNodeHover(-1, false);
    }
    repaint();
}

void NodeGraphComponent::mouseDown(const juce::MouseEvent& e) {
    // Paint mode: start painting
    if (paintMode) {
        double domain = convertPixelsToDomain(e.x - kHandleSize / 2);
        double value = convertPixelsToValue(e.y - kHandleSize / 2);
        domain = juce::jlimit(domainMin, domainMax, domain);
        value = juce::jlimit(valueMin, valueMax, value);
        isPainting = true;
        paintLastDomain = domain;
        paintLastValue = value;
        if (onDragStarted) onDragStarted();
        paintAtPosition(domain, value);
        rebuildHandles();
        if (onNodesChanged) onNodesChanged();
        repaint();
        return;
    }

    float nodeDist = 0, curveDist = 0;
    auto* closestNode = findClosestHandle(e.getPosition(), nodeDist);
    int closestCurve = findClosestCurveHandle(e.getPosition(), curveDist);

    bool nodeInRange = (closestNode != nullptr) && (hoverThreshold <= 0 || nodeDist <= hoverThreshold);
    bool curveInRange = (closestCurve >= 1) && (hoverThreshold <= 0 || curveDist <= hoverThreshold);

    // Prefer curve handle if closer and in range
    if (curveInRange && (!nodeInRange || curveDist <= nodeDist)) {
        activeCurveIndex = closestCurve;
        curveDragLastNorm = 0.0;
        activeHandle = nullptr;
        setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
        if (onDragStarted) onDragStarted();
        return;
    }

    // Add a new node on shift+click if allowed
    if (e.mods.isShiftDown() && allowAddRemove && (int)nodes.size() < maxNodes) {
        double newTime = convertPixelsToDomain(e.x - kHandleSize / 2);
        double newValue = convertPixelsToValue(e.y - kHandleSize / 2);
        newTime = juce::jlimit(domainMin, domainMax, newTime);
        newValue = juce::jlimit(valueMin, valueMax, newValue);

        // Insert sorted by time
        GraphNode newNode{ newTime, newValue, 0.0f };
        auto it = std::lower_bound(nodes.begin(), nodes.end(), newNode,
            [](const GraphNode& a, const GraphNode& b) { return a.time < b.time; });
        int insertIndex = (int)std::distance(nodes.begin(), it);
        nodes.insert(it, newNode);

        rebuildHandles();
        activeHandle = handles[insertIndex];
        activeHandle->isDragging = true;
        activeHandle->dragger.startDraggingComponent(activeHandle, e.getEventRelativeTo(activeHandle));
        if (onDragStarted) onDragStarted();
        if (onNodesChanged) onNodesChanged();
        repaint();
        return;
    }

    if (nodeInRange) {
        activeHandle = closestNode;
        activeHandle->isDragging = true;
        activeHandle->dragger.startDraggingComponent(activeHandle, e.getEventRelativeTo(activeHandle));
        if (onDragStarted) onDragStarted();
    }
}

void NodeGraphComponent::mouseDrag(const juce::MouseEvent& e) {
    // Paint mode: continue painting
    if (paintMode && isPainting) {
        double domain = convertPixelsToDomain(e.x - kHandleSize / 2);
        double value = convertPixelsToValue(e.y - kHandleSize / 2);
        domain = juce::jlimit(domainMin, domainMax, domain);
        value = juce::jlimit(valueMin, valueMax, value);

        if (paintLastDomain >= 0.0) {
            applyPaintStroke(paintLastDomain, domain, paintLastValue, value);
        } else {
            paintAtPosition(domain, value);
            rebuildHandles();
            if (onNodesChanged) onNodesChanged();
            repaint();
        }
        paintLastDomain = domain;
        paintLastValue = value;
        return;
    }

    // Curve handle dragging
    if (activeCurveIndex >= 1 && activeCurveIndex < (int)nodes.size()) {
        if (useBezierInterpolation) {
            // In bezier mode, map the mouse Y directly to the desired midpoint
            // value and compute the curve parameter from it.
            double mouseValue = convertPixelsToValue(e.y - kHandleSize / 2);
            mouseValue = juce::jlimit(valueMin, valueMax, mouseValue);
            double v0 = nodes[activeCurveIndex - 1].value;
            double v1 = nodes[activeCurveIndex].value;
            double linearMid = (v0 + v1) * 0.5;
            // B(0.5) = linearMid + (curve/kBezierCurveScale)*0.25
            // curve = (B(0.5) - linearMid) * kBezierCurveScale * 4
            double curve = (mouseValue - linearMid) * (double)osci_audio::kBezierCurveScale * 4.0;
            curve = juce::jlimit(-(double)osci_audio::kMaxBezierCurve, (double)osci_audio::kMaxBezierCurve, curve);
            nodes[activeCurveIndex].curve = (float)curve;
        } else {
            constexpr double kCurveRange = (double)osci_audio::kMaxBezierCurve;
            constexpr double kMinSpeed = 0.22;
            constexpr double kMaxSpeed = 3.2;
            constexpr double kExpK = 3.0;
            constexpr double kBase = 65.0;

            double distNorm = e.getDistanceFromDragStartY() / (double)getHeight();
            double dy = distNorm - curveDragLastNorm;
            curveDragLastNorm = distNorm;

            double currentCurve = (double)nodes[activeCurveIndex].curve;
            double curviness = juce::jlimit(0.0, 1.0, std::abs(currentCurve) / kCurveRange);
            double expNorm = (std::exp(kExpK * curviness) - 1.0) / (std::exp(kExpK) - 1.0);
            double speed = kMinSpeed + (kMaxSpeed - kMinSpeed) * expNorm;

            double delta = dy * kBase * speed;

            // Invert if segment slopes downward
            if (activeCurveIndex > 0 && nodes[activeCurveIndex - 1].value > nodes[activeCurveIndex].value)
                delta = -delta;

            double next = juce::jlimit(-kCurveRange, kCurveRange, currentCurve + delta);
            nodes[activeCurveIndex].curve = (float)next;
        }
        clearCurvesForOverlappingNodes();
        if (onNodesChanged) onNodesChanged();
        repaint();
        return;
    }

    // Node handle dragging
    if (activeHandle != nullptr && activeHandle->isDragging) {
        activeHandle->dragger.dragComponent(activeHandle, e.getEventRelativeTo(activeHandle), nullptr);

        // Clamp to bounds
        int maxX = getWidth() - kHandleSize;
        int maxY = getHeight() - kHandleSize;
        int nodeIdx = activeHandle->nodeIndex;

        // Constrain X: can't go left of predecessor or right of successor.
        int minX = 0;
        if (nodeIdx > 0 && handles[nodeIdx - 1] != nullptr)
            minX = handles[nodeIdx - 1]->getX();
        if (nodeIdx < (int)handles.size() - 1 && handles[nodeIdx + 1] != nullptr)
            maxX = handles[nodeIdx + 1]->getX();

        int clampedX = juce::jlimit(minX, maxX, activeHandle->getX());
        int clampedY = juce::jlimit(0, juce::jmax(0, maxY), activeHandle->getY());
        activeHandle->setTopLeftPosition(clampedX, clampedY);

        activeHandle->updateNodeFromPosition();

        // Apply grid snapping
        switch (snapStyle) {
            case SnapStyle::LogarithmicTime:
                nodes[nodeIdx].time = snapToLogarithmicGrid(nodes[nodeIdx].time);
                break;
            case SnapStyle::Uniform:
            default:
                if (snapDivisions > 0)
                    nodes[nodeIdx].time = snapToGrid(nodes[nodeIdx].time, domainMin, domainMax);
                break;
        }

        // Re-clamp position after constraints
        activeHandle->updateFromNode();
        clearCurvesForOverlappingNodes();
        if (onNodesChanged) onNodesChanged();
        repaint();
    }
}

void NodeGraphComponent::mouseUp(const juce::MouseEvent&) {
    if (paintMode && isPainting) {
        isPainting = false;
        paintLastDomain = -1.0;
        if (onDragEnded) onDragEnded();
        return;
    }
    if (activeHandle != nullptr) {
        activeHandle->isDragging = false;
        // Fire callback while active state is still queryable
        if (onDragEnded) onDragEnded();
        activeHandle = nullptr;
    }
    if (activeCurveIndex >= 1) {
        // Fire callback while active state is still queryable
        if (onDragEnded) onDragEnded();
        activeCurveIndex = -1;
        curveDragLastNorm = 0.0;
        setMouseCursor(juce::MouseCursor::NormalCursor);
    }
}

void NodeGraphComponent::mouseDoubleClick(const juce::MouseEvent& e) {
    if (paintMode) return; // No double-click actions in paint mode

    // Double-click on a curve handle resets its curve to 0
    float nodeDist = 0, curveDist = 0;
    findClosestHandle(e.getPosition(), nodeDist);
    int closestCurve = findClosestCurveHandle(e.getPosition(), curveDist);

    float effectiveThreshold = hoverThreshold > 0 ? hoverThreshold : 20.0f;
    bool curveInRange = closestCurve >= 1 && curveDist <= effectiveThreshold;
    bool nodeInRange = nodeDist <= effectiveThreshold;

    if (curveInRange && (!nodeInRange || curveDist <= nodeDist)) {
        if (onDragStarted) onDragStarted();
        nodes[closestCurve].curve = 0.0f;
        if (onNodesChanged) onNodesChanged();
        if (onDragEnded) onDragEnded();
        repaint();
        return;
    }

    // Double-click on a non-edge node removes it
    float dist = 0;
    auto* closest = findClosestHandle(e.getPosition(), dist);
    if (closest != nullptr && allowAddRemove && dist < effectiveThreshold) {
        int idx = closest->nodeIndex;
        if (idx > 0 && idx < (int)nodes.size() - 1 && (int)nodes.size() > minNodes) {
            if (onDragStarted) onDragStarted();
            nodes.erase(nodes.begin() + idx);
            activeHandle = nullptr;
            hoverHandle = nullptr;
            rebuildHandles();
            if (onNodesChanged) onNodesChanged();
            if (onDragEnded) onDragEnded();
            repaint();
            return;
        }
    }

    // Double-click on empty space adds a new node
    if (allowAddRemove && (int)nodes.size() < maxNodes) {
        double newTime = convertPixelsToDomain(e.x - kHandleSize / 2);
        double newValue = convertPixelsToValue(e.y - kHandleSize / 2);
        newTime = juce::jlimit(domainMin, domainMax, newTime);
        newValue = juce::jlimit(valueMin, valueMax, newValue);

        GraphNode newNode{ newTime, newValue, 0.0f };
        auto it = std::lower_bound(nodes.begin(), nodes.end(), newNode,
            [](const GraphNode& a, const GraphNode& b) { return a.time < b.time; });
        nodes.insert(it, newNode);

        rebuildHandles();
        if (onNodesChanged) onNodesChanged();
        repaint();
    }
}

// --- Snapping ---

double NodeGraphComponent::snapToGrid(double value, double rangeMin, double rangeMax) const {
    if (snapDivisions <= 0) return value;
    double range = rangeMax - rangeMin;
    double step = range / (double)snapDivisions;
    double snapped = std::round((value - rangeMin) / step) * step + rangeMin;
    // Pixel-based snap threshold — only snap when close enough in screen space
    constexpr double kSnapPx = 6.0;
    double domainPerPixel = range / (double)juce::jmax(1, getWidth() - kHandleSize);
    if (std::abs(snapped - value) < kSnapPx * domainPerPixel)
        return juce::jlimit(rangeMin, rangeMax, snapped);
    return value;
}

double NodeGraphComponent::snapToLogarithmicGrid(double domainValue) const {
    LogGridInfo info;
    if (!computeLogGridInfo(getWidth(), info))
        return domainValue;

    // Snap threshold in pixels — keep it tight so handles don't feel sticky
    constexpr double kSnapPx = 6.0;
    const double snapDomain = kSnapPx * info.secondsPerPixel;

    double bestSnap = domainValue;
    double bestDist = std::numeric_limits<double>::max();

    auto trySnap = [&](double step, float lineVis) {
        if (step <= 0.0) return;
        if (lineVis < 0.5f) return; // only snap when line is sufficiently visible

        double snapped = std::round(domainValue / step) * step;
        double dist = std::abs(snapped - domainValue);
        if (dist < snapDomain && dist < bestDist) {
            bestDist = dist;
            bestSnap = snapped;
        }
    };

    trySnap(info.lowerStep, info.lowerLineVis);
    trySnap(info.upperStep, info.upperLineVis);
    return bestSnap;
}

void NodeGraphComponent::setSnapDivisions(int divisions) {
    if (snapDivisions != divisions) {
        snapDivisions = juce::jmax(0, divisions);
        invalidateGrid();
        repaint();
    }
}

void NodeGraphComponent::setLastNodeValue(double value) {
    if (!nodes.empty()) {
        nodes.back().value = juce::jlimit(valueMin, valueMax, value);
        if (!handles.isEmpty())
            handles.getLast()->updateFromNode();
    }
}

void NodeGraphComponent::mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& wheel) {
    if (wheel.deltaY == 0.0f) return;

    if (wheelMode == WheelMode::DomainZoom) {
        // Zoom the domain axis
        const double base = 0.98;
        const double zoomFactor = std::pow(base, wheel.deltaY * 20.0);
        const double newMax = juce::jlimit(domainZoomMin, domainZoomMax, domainMax * zoomFactor);
        setDomainRange(domainMin, newMax);
        return;
    }

    // SnapDivisions mode (default)
    scrollAccumulator += wheel.deltaY * 8.0f;
    int steps = (int)scrollAccumulator;
    if (steps == 0) return;
    scrollAccumulator -= (float)steps;

    int newDiv = juce::jlimit(2, 64, snapDivisions + steps);
    setSnapDivisions(newDiv);
}

void NodeGraphComponent::lookAndFeelChanged() {
    invalidateGrid();
    repaint();
}

// ============================================================================
// Paint Mode
// ============================================================================

void NodeGraphComponent::setPaintMode(bool enabled) {
    if (paintMode != enabled) {
        paintMode = enabled;
        isPainting = false;
        paintLastDomain = -1.0;
        repaint();
    }
}

void NodeGraphComponent::setBezierMode(bool bezier) {
    if (useBezierInterpolation == bezier) return;
    useBezierInterpolation = bezier;
    // Zero out all curves when switching to straight-line mode
    if (!bezier) {
        for (auto& node : nodes)
            node.curve = 0.0f;
        rebuildHandles();
        if (onNodesChanged) onNodesChanged();
    }
    repaint();
}

void NodeGraphComponent::setPaintShape(PaintShape shape) {
    paintShape = shape;
}

float NodeGraphComponent::evaluatePaintShape(PaintShape shape, float t) {
    t = juce::jlimit(0.0f, 1.0f, t);
    switch (shape) {
        case PaintShape::Step:
            return 1.0f;
        case PaintShape::Half:
            return t < 0.5f ? 1.0f : 0.0f;
        case PaintShape::Down:
            return 1.0f - t;
        case PaintShape::Up:
            return t;
        case PaintShape::Tri:
            return t < 0.5f ? (t * 2.0f) : (2.0f - t * 2.0f);
        case PaintShape::Bump: {
            // Sine-like bump for preview (actual shape uses bezier curves on 0-valued nodes)
            return std::sin(t * juce::MathConstants<float>::pi);
        }
    }
    return 1.0f;
}

void NodeGraphComponent::paintAtPosition(double domain, double value) {
    if (snapDivisions <= 0) return;

    double range = domainMax - domainMin;
    double step = range / (double)snapDivisions;

    // Find which grid cell this position falls in
    int cellIndex = (int)std::floor((domain - domainMin) / step);
    cellIndex = juce::jlimit(0, snapDivisions - 1, cellIndex);
    double cellLeft = domainMin + cellIndex * step;
    double cellRight = juce::jmin(domainMax, cellLeft + step);

    if (cellLeft >= domainMax) return;

    double snappedValue = juce::jlimit(valueMin, valueMax, value);

    constexpr double kEps = 1e-9;

    // Offset the rightmost node slightly inward so adjacent cells don't
    // overwrite each other's peaks/valleys at shared boundaries.
    double boundaryEps = step * 0.002;
    double cellRightInner = cellRight - boundaryEps;

    // Remove all interior nodes strictly within the cell (keeps boundary nodes)
    nodes.erase(std::remove_if(nodes.begin(), nodes.end(), [&](const GraphNode& n) {
        return n.time > cellLeft + kEps && n.time < cellRight - kEps;
    }), nodes.end());

    // Helper: find a node at a given time (within epsilon)
    auto findAt = [&](double t) -> GraphNode* {
        for (auto& n : nodes)
            if (std::abs(n.time - t) < kEps) return &n;
        return nullptr;
    };

    // Helper: insert or update a node at a given time with optional curve
    auto setNodeAt = [&](double t, double v, float curveVal = 0.0f) {
        auto* existing = findAt(t);
        if (existing) {
            existing->value = v;
            existing->curve = curveVal;
        } else {
            GraphNode newNode{ t, v, curveVal };
            if (constrainNode)
                constrainNode(-1, newNode.time, newNode.value);
            newNode.time = juce::jlimit(domainMin, domainMax, newNode.time);
            newNode.value = juce::jlimit(valueMin, valueMax, newNode.value);
            auto pos = std::lower_bound(nodes.begin(), nodes.end(), newNode,
                [](const GraphNode& a, const GraphNode& b) { return a.time < b.time; });
            nodes.insert(pos, newNode);
        }
    };

    // Insert the minimum nodes needed per shape.
    if (paintShape == PaintShape::Step) {
        setNodeAt(cellLeft, snappedValue);
        setNodeAt(cellRightInner, snappedValue);
    } else if (paintShape == PaintShape::Up) {
        setNodeAt(cellLeft, valueMin);
        setNodeAt(cellRightInner, snappedValue);
    } else if (paintShape == PaintShape::Down) {
        setNodeAt(cellLeft, snappedValue);
        setNodeAt(cellRightInner, valueMin);
    } else if (paintShape == PaintShape::Tri) {
        double cellMid = (cellLeft + cellRight) * 0.5;
        setNodeAt(cellLeft, valueMin);
        setNodeAt(cellMid, snappedValue);
        setNodeAt(cellRightInner, valueMin);
    } else if (paintShape == PaintShape::Half) {
        double cellMid = (cellLeft + cellRight) * 0.5;
        setNodeAt(cellLeft, valueMin);
        setNodeAt(cellLeft + boundaryEps, snappedValue);
        setNodeAt(cellMid, snappedValue);
        setNodeAt(cellMid + boundaryEps, valueMin);
        setNodeAt(cellRightInner, valueMin);
    } else if (paintShape == PaintShape::Bump) {
        // Bump uses bezier curves: boundary nodes at valueMin, curve creates the hump.
        // B(0.5) = linearMid + (curve/kBezierCurveScale)*0.25
        // For nodes at valueMin: linearMid = valueMin, so B(0.5) = valueMin + curve/(kBezierCurveScale*4)
        // Solving for curve: curve = (snappedValue - valueMin) * kBezierCurveScale * 4
        float curveStrength = (float)((snappedValue - valueMin) / (valueMax - valueMin))
                              * osci_audio::kBezierCurveScale * 4.0f;
        // Preserve existing left node curve so adjacent bumps stay seamless
        auto* leftNode = findAt(cellLeft);
        if (leftNode)
            leftNode->value = valueMin;
        else
            setNodeAt(cellLeft, valueMin, 0.0f);
        setNodeAt(cellRight, valueMin, curveStrength);
    }

    // Ensure we still have boundary nodes
    if (nodes.empty() || nodes.front().time > domainMin + kEps)
        setNodeAt(domainMin, valueMin);
    if (nodes.back().time < domainMax - kEps)
        setNodeAt(domainMax, valueMin);

    // Enforce first/last node constraints for LFO looping
    if (constrainNode) {
        constrainNode(0, nodes.front().time, nodes.front().value);
        constrainNode((int)nodes.size() - 1, nodes.back().time, nodes.back().value);
    }
}

void NodeGraphComponent::applyPaintStroke(double domainStart, double domainEnd,
                                           double valueAtStart, double valueAtEnd) {
    if (snapDivisions <= 0) return;

    double range = domainMax - domainMin;
    double step = range / (double)snapDivisions;

    // Determine the cells covered by this stroke
    double startCell = std::floor((domainStart - domainMin) / step) * step + domainMin;
    double endCell = std::floor((domainEnd - domainMin) / step) * step + domainMin;

    if (startCell > endCell) std::swap(startCell, endCell);

    // Paint each cell in the range
    for (double cell = startCell; cell <= endCell + step * 0.5; cell += step) {
        double cellDomain = juce::jlimit(domainMin, domainMax, cell);
        double t = (domainEnd != domainStart) ?
            (cellDomain - domainStart) / (domainEnd - domainStart) : 0.5;
        t = juce::jlimit(0.0, 1.0, t);
        double value = valueAtStart + t * (valueAtEnd - valueAtStart);
        paintAtPosition(cellDomain, value);
    }

    // Rebuild once after all cells are painted
    rebuildHandles();
    if (onNodesChanged) onNodesChanged();
    repaint();
}

int NodeGraphComponent::getActiveNodeIndex() const {
    if (activeHandle != nullptr)
        return activeHandle->nodeIndex;
    return -1;
}

int NodeGraphComponent::getActiveCurveNodeIndex() const {
    return activeCurveIndex;
}

// ============================================================================
// Flow Trail
// ============================================================================

void NodeGraphComponent::ensureFlowTrailCreated() {
    if (!flowTrailRenderer) {
        flowTrailRenderer = std::make_unique<FlowTrailRenderer>(
            [this](double domainVal) -> float {
                return (float)convertDomainToPixels(domainVal) + kHandleSize * 0.5f;
            },
            [this]() { return getWidth(); },
            [this]() { return getHeight(); }
        );
    }
}

void NodeGraphComponent::ensureFlowRepaintTimerRunning() {
    if (!isTimerRunning())
        startTimerHz(60);
}

void NodeGraphComponent::setFlowMarkerDomainPositions(const double* positions, int numPositions) {
    ensureFlowTrailCreated();
    flowTrailRenderer->setFlowMarkerTimes(positions, numPositions);
    ensureFlowRepaintTimerRunning();
    repaint();
}

void NodeGraphComponent::clearFlowMarkers() {
    if (flowTrailRenderer)
        flowTrailRenderer->clear();
    ensureFlowRepaintTimerRunning();
    repaint();
}

void NodeGraphComponent::resetFlowTrail() {
    if (flowTrailRenderer)
        flowTrailRenderer->reset();
    repaint();
}

void NodeGraphComponent::setFlowTrailWrapping(bool shouldWrap, double domainMin, double domainMax) {
    ensureFlowTrailCreated();
    flowTrailRenderer->setWrapping(shouldWrap, domainMin, domainMax);
}

void NodeGraphComponent::timerCallback() {
    if (!flowTrailRenderer || !flowTrailRenderer->hasTrailData()) {
        stopTimer();
        return;
    }
    const double nowMs = juce::Time::getMillisecondCounterHiRes();
    if (flowTrailRenderer->shouldStopRepaint(nowMs)) {
        stopTimer();
        repaint();
        return;
    }
    repaint();
}
