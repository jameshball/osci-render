#include "TimelineView.h"

// Moving about: zoom, scrolling (the bars, the wheel, a drag at an edge),
// following the playhead, and the layout's sizes.

void MotionTimelineView::zoomBy(double factor) {
    // Around the playhead when it is in view, like Premiere; else the centre.
    const auto playheadX = timeX(processor.position.load()) - namesWidth;
    const auto centreX = playheadX >= 0 && playheadX <= getWidth() - namesWidth ? playheadX : std::max(0, (getWidth() - namesWidth) / 2);
    const auto anchor = scrollTime + centreX / pixelsPerSecond;
    const auto zoom = clampZoom(pixelsPerSecond * factor);
    animateView(zoom, std::max(0.0, anchor - centreX / zoom));
}

void MotionTimelineView::animateView(double zoom, double scroll) {
    animationFrom = {pixelsPerSecond, scrollTime};
    animationTo = {zoom, scroll};
    animationStart = juce::Time::getMillisecondCounterHiRes();
    viewAnimation.startTimerHz(60);
}

void MotionTimelineView::revealTime(double seconds) {
    cancelClipGesture();
    if (!std::isfinite(seconds) || seconds < 0) { return; }
    // A zoom or Fit gliding on would undo the reveal.
    viewAnimation.stopTimer();
    const auto visible = std::max(1, getWidth() - namesWidth - 20) / pixelsPerSecond;
    if (seconds < scrollTime || seconds > scrollTime + visible) {
        scrollTime = std::max(0.0, seconds - visible * .5);
        fitRowsToBar();
        repaint();
    }
}

double MotionTimelineView::contentEnd() const {
    // Only project state sets it, and every change to that bumps the revision.
    const auto key = std::make_pair(processor.document.scopeGeneration(), processor.document.revision());
    if (contentEndKey != key) {
        const auto& project = processor.document.project();
        auto end = project.duration;
        for (const auto& track : project.tracks) {
            for (const auto& clip : track.clips) { end = std::max(end, clip.timing(project.tempo()).end()); }
        }
        contentEndCache = end;
        contentEndKey = key;
    }
    return contentEndCache;
}

double MotionTimelineView::timelineSpan() const {
    const auto visible = visibleSpan();
    return std::max({contentEnd() * 1.05, scrollTime + visible, 0.001});
}

juce::Rectangle<int> MotionTimelineView::horizontalThumb() const {
    const auto bar = horizontalBar();
    const auto span = timelineSpan();
    const auto visible = visibleSpan();
    const auto width = std::max(24, juce::roundToInt(bar.getWidth() * std::min(1.0, visible / span)));
    const auto x = bar.getX() + juce::roundToInt((bar.getWidth() - width) * std::clamp(scrollTime / std::max(1e-9, span - visible), 0.0, 1.0));
    return {x, bar.getY(), std::min(width, bar.getWidth()), bar.getHeight()};
}

juce::Rectangle<int> MotionTimelineView::verticalThumb() const {
    const auto bar = verticalBar();
    const auto total = std::max(1, contentHeight + bottomRoom);
    const auto height = std::max(24, juce::roundToInt(bar.getHeight() * std::min(1.0, static_cast<double>(viewHeight()) / total)));
    const auto y = bar.getY() + juce::roundToInt((bar.getHeight() - height) * std::clamp(static_cast<double>(scrollY) / std::max(1, maximumScrollY()), 0.0, 1.0));
    return {bar.getX(), y, bar.getWidth(), std::min(height, bar.getHeight())};
}

void MotionTimelineView::paintScrollBars(juce::Graphics& g) const {
    // At rest only a slim thumb along the outer edge, clear of the clips'
    // ends and trim grips; hovered or dragged, the full bar and its track.
    const auto draw = [&](juce::Rectangle<int> bar, juce::Rectangle<int> thumb, bool hot, bool horizontal) {
        if (hot) {
            g.setColour(juce::Colours::white.withAlpha(.04f));
            g.fillRoundedRectangle(bar.toFloat(), barSize * .5f);
        } else {
            thumb = horizontal ? thumb.removeFromBottom(restingBar) : thumb.removeFromRight(restingBar);
        }
        g.setColour(juce::Colours::white.withAlpha(hot ? .4f : .2f));
        g.fillRoundedRectangle(thumb.toFloat(), (hot ? barSize : restingBar) * .5f);
    };
    const auto* drag = active<BarDrag>();
    const auto vertical = drag != nullptr && drag->mode == BarDrag::Mode::scrollRows;
    if (horizontalBarShown()) { draw(horizontalBar(), horizontalThumb(), (drag != nullptr && !vertical) || hoveredBar == 1, true); }
    if (verticalBarShown()) { draw(verticalBar(), verticalThumb(), vertical || hoveredBar == 2, false); }
}

int MotionTimelineView::barAt(juce::Point<int> point) const {
    if (horizontalBarShown() && horizontalBar().expanded(0, 3).contains(point)) { return 1; }
    if (verticalBarShown() && verticalBar().expanded(3, 0).contains(point)) { return 2; }
    return 0;
}

bool MotionTimelineView::barDown(const juce::MouseEvent& event) {
    const auto bar = barAt(event.getPosition());
    if (bar == 0 || !event.mods.isLeftButtonDown()) { return false; }
    if (bar == 1) {
        auto thumb = horizontalThumb();
        if (!thumb.contains(event.x, thumb.getCentreY())) {
            // Click beside the thumb: centre the view there.
            const auto span = timelineSpan();
            const auto visible = visibleSpan();
            scrollTime = std::max(0.0, (event.x - horizontalBar().getX()) / static_cast<double>(horizontalBar().getWidth()) * span - visible / 2);
            userScrolled();
            thumb = horizontalThumb();
        }
        const auto mode = event.x <= thumb.getX() + 5 ? BarDrag::Mode::zoomLeft : event.x >= thumb.getRight() - 5 ? BarDrag::Mode::zoomRight : BarDrag::Mode::scrollTime;
        gesture = BarDrag {mode, event.x, scrollTime, pixelsPerSecond, scrollY};
    } else {
        if (!verticalThumb().contains(verticalThumb().getCentreX(), event.y)) {
            scrollY = std::clamp(juce::roundToInt((event.y - verticalBar().getY()) / static_cast<double>(std::max(1, verticalBar().getHeight())) * maximumScrollY()), 0, maximumScrollY());
        }
        gesture = BarDrag {BarDrag::Mode::scrollRows, event.y, scrollTime, pixelsPerSecond, scrollY};
    }
    resized();
    repaint();
    return true;
}

void MotionTimelineView::barDragged(const juce::MouseEvent& event) {
    const auto& drag = std::get<BarDrag>(gesture);
    if (drag.mode == BarDrag::Mode::scrollRows) {
        const auto bar = verticalBar(), thumb = verticalThumb();
        const auto travel = std::max(1, bar.getHeight() - thumb.getHeight());
        scrollY = std::clamp(drag.scrollY + juce::roundToInt((event.y - drag.down) / static_cast<double>(travel) * maximumScrollY()), 0, maximumScrollY());
        resized();
        repaint();
        return;
    }
    const auto bar = horizontalBar();
    const auto span = timelineSpan();
    const auto secondsPerPixel = span / std::max(1, bar.getWidth());
    const auto delta = (event.x - drag.down) * secondsPerPixel;
    const auto width = std::max(1, getWidth() - namesWidth);
    const auto visible = width / drag.pixelsPerSecond;
    if (drag.mode == BarDrag::Mode::scrollTime) {
        scrollTime = std::max(0.0, drag.scrollTime + delta);
    } else if (drag.mode == BarDrag::Mode::zoomRight) {
        pixelsPerSecond = clampZoom(width / std::max(1.0 / 120, visible + delta));
        scrollTime = drag.scrollTime;
    } else {
        const auto end = drag.scrollTime + visible;
        const auto start = std::clamp(drag.scrollTime + delta, 0.0, std::max(0.0, end - 1.0 / 120));
        pixelsPerSecond = clampZoom(width / (end - start));
        scrollTime = start;
    }
    userScrolled();
    repaint();
}

double MotionTimelineView::edgeSpeed(int x) const {
    constexpr int zone = 36;
    const auto left = namesWidth + zone - x;
    const auto right = x - (getWidth() - zone);
    const auto depth = std::max(left, right);
    if (depth <= 0) { return 0; }
    // Gentle at the zone's inner edge, quicker at the edge, and quicker
    // still past it.
    const auto inside = std::min(1.0, depth / static_cast<double>(zone));
    const auto speed = std::min(40.0, 1.0 + 11.0 * inside * inside + std::max(0, depth - zone) * 0.4);
    return left > right ? -speed : speed;
}

int* MotionTimelineView::dragOrigin() {
    auto* clip = active<ClipDrag>();
    auto* marker = active<MarkerDrag>();
    auto* key = active<KeyDrag>();
    auto* cameraKey = active<CameraKeyDrag>();
    auto* cut = active<CutDrag>();
    auto* clips = active<ClipMarquee>();
    auto* keys = active<KeyMarquee>();
    return clip != nullptr ? &clip->downX : marker != nullptr ? &marker->downX : key != nullptr ? &key->downX : cameraKey != nullptr ? &cameraKey->downX
         : cut != nullptr ? &cut->downX : clips != nullptr ? &clips->start.x : keys != nullptr ? &keys->start.x : nullptr;
}

void MotionTimelineView::followEdge(const juce::MouseEvent& event) {
    edgeEvent.reset();
    if (!scrollsAtEdge() || edgeSpeed(event.x) == 0) {
        edgeScroll.stopTimer();
        return;
    }
    edgeEvent.emplace(event);
    if (!edgeScroll.isTimerRunning()) { edgeScroll.startTimerHz(60); }
}

void MotionTimelineView::scrollAtEdge() {
    if (!edgeEvent.has_value() || !scrollsAtEdge() || !juce::Desktop::getInstance().getMainMouseSource().isDragging()) {
        edgeScroll.stopTimer();
        return;
    }
    const auto speed = edgeSpeed(edgeEvent->x);
    // Whole pixels, so the drag's origin moves exactly with the tracks; to
    // the right no further than just past the end.
    const auto limit = std::max(scrollTime, timelineSpan() - (getWidth() - namesWidth) / pixelsPerSecond * 0.5);
    const auto target = std::clamp(scrollTime + speed / pixelsPerSecond, 0.0, limit);
    const auto pixels = juce::roundToInt((target - scrollTime) * pixelsPerSecond);
    if (pixels == 0) { return; }
    viewAnimation.stopTimer();
    scrollTime += pixels / pixelsPerSecond;
    auto* origin = dragOrigin();
    if (origin != nullptr) { *origin -= pixels; }
    const auto event = *edgeEvent;
    resized();
    repaint();
    mouseDrag(event);
}

void MotionTimelineView::mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) {
    ensureTrackRows();
    if (dragging()) { return; }
    using Gesture = motion::ui::WheelGesture;
    const Gesture gesture(event.mods, wheel);
    if (gesture.kind == Gesture::Kind::zoomTime) {
        zoomAround(event.x, gesture.factor);
    } else if (gesture.kind == Gesture::Kind::scaleOther) {
        scaleTrackHeights(event.y, gesture.factor);
    } else {
        if (gesture.dx != 0) {
            scrollTime = std::max(0.0, scrollTime - gesture.dx * Gesture::pixelsPerUnit / pixelsPerSecond);
            userScrolled();
        }
        if (gesture.dy != 0) { scrollY = std::clamp(scrollY - juce::roundToInt(gesture.dy * Gesture::pixelsPerUnit), 0, maximumScrollY()); }
    }
    resized();
    repaint();
}

void MotionTimelineView::mouseMagnify(const juce::MouseEvent& event, float scale) {
    if (dragging() || !(scale > 0)) { return; }
    zoomAround(event.x, scale);
}

void MotionTimelineView::zoomAround(int x, double factor) {
    const auto anchorX = std::clamp(x - namesWidth, 0, std::max(0, getWidth() - namesWidth));
    const auto anchorTime = scrollTime + anchorX / pixelsPerSecond;
    pixelsPerSecond = clampZoom(pixelsPerSecond * factor);
    scrollTime = std::max(0.0, anchorTime - anchorX / pixelsPerSecond);
    userScrolled();
    repaint();
}

void MotionTimelineView::userScrolled() {
    viewAnimation.stopTimer();
    follow.userMoved();
    fitRowsToBar();
}

void MotionTimelineView::scaleTrackHeights(int y, double factor) {
    const auto row = visualRowAt(y);
    const auto offset = row >= 0 && row < static_cast<int>(rows.size()) ? rowY(row) : 0;
    const auto next = std::clamp(juce::roundToInt(defaultTrackHeight * factor), motion::Track::minimumHeight, 120);
    if (next == defaultTrackHeight) { return; }
    // Tracks with their own height scale with the rest (collected first:
    // setting heights replaces the project being read).
    const auto ratio = static_cast<double>(next) / defaultTrackHeight;
    std::vector<std::pair<motion::Id, int>> heights;
    for (const auto& track : processor.document.project().tracks) {
        if (track.height > 0) { heights.emplace_back(track.id, juce::roundToInt(track.height * ratio)); }
    }
    for (const auto& group : processor.document.project().groups) {
        if (group.height > 0) { heights.emplace_back(group.id, juce::roundToInt(group.height * ratio)); }
    }
    processor.document.setTrackHeights(heights);
    setDefaultTrackHeight(next);
    if (row >= 0 && row < static_cast<int>(rows.size())) { scrollY = std::clamp(scrollY + rowY(row) - offset, 0, maximumScrollY()); }
}

void MotionTimelineView::setLayout(const Layout& next) {
    namesWidth = std::clamp(next.namesWidth, narrowestNames, widestNames);
    defaultTrackHeight = std::clamp(next.trackHeight, motion::Track::minimumHeight, 120);
    followEnabled = next.follow;
    layoutRows();
    resized();
    repaint();
}

void MotionTimelineView::setDefaultTrackHeight(int height) {
    defaultTrackHeight = std::clamp(height, motion::Track::minimumHeight, 120);
    layoutRows();
    if (onDefaultTrackHeight) { onDefaultTrackHeight(defaultTrackHeight); }
    resized();
    repaint();
}

void MotionTimelineView::followPlayhead(double time, bool playing) {
    const auto x = timeX(time);
    const auto visible = x >= namesWidth && x <= getWidth() - 16;
    if (!follow.update(playing, visible) || !followEnabled || clipGesture() || getWidth() <= namesWidth) { return; }
    scrollTime = std::max(0.0, time - 16 / pixelsPerSecond);
    fitRowsToBar();
    repaint();
}

void MotionTimelineView::fitProject() {
    cancelClipGesture();
    const auto end = contentEnd();
    const auto duration = std::isfinite(end) ? std::max(0.001, end) : 1.0;
    scrollY = 0;
    animateView(std::clamp(std::max(1, getWidth() - namesWidth - 20) / duration, 0.000001, 500.0), 0.0);
    resized();
    repaint();
}
