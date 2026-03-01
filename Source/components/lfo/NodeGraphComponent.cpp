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
    rebuildHandles();
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
    return evaluateGraphCurve(nodes, time);
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

void NodeGraphComponent::paintGrid(juce::Graphics& g) {
    const int w = getWidth();
    const int h = getHeight();
    if (w <= 0 || h <= 0) return;

    if (gridDirty || gridCacheWidth != w || gridCacheHeight != h || !gridCache.isValid()) {
        gridCache = juce::Image(juce::Image::ARGB, w, h, true);
        gridCacheWidth = w;
        gridCacheHeight = h;
        gridDirty = false;

        juce::Graphics gg(gridCache);
        gg.setColour(findColour(backgroundColourId));
        gg.fillRect(0, 0, w, h);

        auto gridColour = findColour(gridLineColourId);

        if (showDomainGrid && domainMax > domainMin && snapDivisions > 0) {
            double range = domainMax - domainMin;
            double step = range / (double)snapDivisions;
            for (double d = domainMin + step; d < domainMax - step * 0.1; d += step) {
                float x = (float)(convertDomainToPixels(d) + kHandleSize * 0.5);
                gg.setColour(gridColour);
                gg.drawVerticalLine((int)x, 0.0f, (float)h);
            }
        }

        if (showValueGrid && valueMax > valueMin) {
            double range = valueMax - valueMin;
            double step = range / 4.0;
            for (double v = valueMin + step; v < valueMax - step * 0.1; v += step) {
                float y = (float)(convertValueToPixels(v) + kHandleSize * 0.5);
                gg.setColour(gridColour);
                gg.drawHorizontalLine((int)y, 0.0f, (float)w);
            }
        }
    }

    g.drawImageAt(gridCache, 0, 0);
}

// --- Handle Management ---

void NodeGraphComponent::rebuildHandles() {
    handles.clear();
    for (int i = 0; i < (int)nodes.size(); ++i) {
        auto* h = new HandleComponent(*this);
        h->nodeIndex = i;
        addAndMakeVisible(h);
        h->updateFromNode();
        handles.add(h);
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
    for (auto* h : handles) {
        auto center = h->getBoundsInParent().getCentre().toFloat();
        float d = center.getDistanceFrom(pos.toFloat());
        if (d < bestDist) {
            bestDist = d;
            closest = h;
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

    // Stroke curve
    g.setColour(findColour(lineColourId).withAlpha(0.90f));
    g.strokePath(path, juce::PathStrokeType(kCurveStrokeThickness));
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

    if (prevHover != hoverHandle || prevCurveHover != hoverCurveIndex)
        repaint();
}

void NodeGraphComponent::mouseExit(const juce::MouseEvent&) {
    mouseIsOver = false;
    hoverHandle = nullptr;
    hoverCurveIndex = -1;
    repaint();
}

void NodeGraphComponent::mouseDown(const juce::MouseEvent& e) {
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
    // Curve handle dragging
    if (activeCurveIndex >= 1 && activeCurveIndex < (int)nodes.size()) {
        constexpr double kCurveRange = 50.0;
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

        double next = juce::jlimit(-50.0, 50.0, currentCurve + delta);
        nodes[activeCurveIndex].curve = (float)next;
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

        // Constrain X to stay ordered between neighboring nodes
        int minX = 0;
        if (nodeIdx > 0 && handles[nodeIdx - 1] != nullptr)
            minX = handles[nodeIdx - 1]->getX();

        int constrainedMaxX = maxX;
        if (nodeIdx < (int)handles.size() - 1 && handles[nodeIdx + 1] != nullptr)
            constrainedMaxX = handles[nodeIdx + 1]->getX();

        int clampedX = juce::jlimit(minX, juce::jmax(minX, constrainedMaxX), activeHandle->getX());
        int clampedY = juce::jlimit(0, juce::jmax(0, maxY), activeHandle->getY());
        activeHandle->setTopLeftPosition(clampedX, clampedY);

        activeHandle->updateNodeFromPosition();

        // Apply grid snapping
        if (snapDivisions > 0) {
            nodes[nodeIdx].time = snapToGrid(nodes[nodeIdx].time, domainMin, domainMax);
        }

        // Re-clamp position after constraints
        activeHandle->updateFromNode();
        if (onNodesChanged) onNodesChanged();
        repaint();
    }
}

void NodeGraphComponent::mouseUp(const juce::MouseEvent&) {
    if (activeHandle != nullptr) {
        activeHandle->isDragging = false;
        activeHandle = nullptr;
        if (onDragEnded) onDragEnded();
    }
    if (activeCurveIndex >= 1) {
        activeCurveIndex = -1;
        curveDragLastNorm = 0.0;
        setMouseCursor(juce::MouseCursor::NormalCursor);
        if (onDragEnded) onDragEnded();
    }
}

void NodeGraphComponent::mouseDoubleClick(const juce::MouseEvent& e) {
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
    // Only snap if close to a grid line (within 20% of a step)
    if (std::abs(snapped - value) < step * 0.2)
        return juce::jlimit(rangeMin, rangeMax, snapped);
    return value;
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
    // Accumulate scroll with a sensitivity multiplier
    scrollAccumulator += wheel.deltaY * 8.0f;
    int steps = (int)scrollAccumulator;
    if (steps == 0) return;
    scrollAccumulator -= (float)steps;

    int newDiv = juce::jlimit(2, 64, snapDivisions + steps);
    setSnapDivisions(newDiv);
}
