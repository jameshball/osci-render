#include "CompositionView.h"
#include "KeyPositions.h"
#include "../model/AnchorEdit.h"
#include <numbers>

MotionCompositionView::MotionCompositionView(MotionProcessor& processor) : processor(processor) {
    setName("Composition preview");
    setWantsKeyboardFocus(true);
    cameraSync.tick = [this] { syncCamera(); };
    preparation.tick = [this] {
        preparation.stopTimer();
        if (waiting.has_value()) {
            const auto project = std::move(*waiting);
            prepare(project);
        }
    };
    partCount.setFont(motion::style::body());
    partCount.setColour(juce::Label::textColourId, osci::Colours::text());
    partCount.setJustificationType(juce::Justification::centred);
    partCount.setBorderSize({0, 0, 0, 0});
    partCount.setInterceptsMouseClicks(false, false);
    extractButton.setTitle("Extract parts");
    extractButton.setTooltip("Make the picked parts an object of their own (E)");
    extractButton.setColour(juce::TextButton::buttonColourId, motion::style::accent().withAlpha(.5f));
    extractButton.setWantsKeyboardFocus(false);
    extractButton.onClick = [this] { extractPicked(); };
    addChildComponent(partCount);
    addChildComponent(extractButton);
    scenePreview = std::make_unique<motion::ScenePreview>([owner = juce::Component::SafePointer<MotionCompositionView>(this)] {
        juce::MessageManager::callAsync([owner] {
            if (owner != nullptr) { owner->repaint(); }
        });
    });
}

MotionCompositionView::~MotionCompositionView() {
    cameraSync.stopTimer();
    preparation.stopTimer();
    stopTimer();
    cancelGesture();
    if (navigating) { releaseCursor(); }
}

void MotionCompositionView::setTool(MotionTransformTool value) {
    // Choosing a transform tool ends fly navigation and part picking, like
    // any modal tool.
    setPartMode(false);
    setNavigating(false);
    cancelGesture();
    tool = value;
    hoverHandle = -1;
    dragHint.clear();
    if (onToolChanged) { onToolChanged(tool); }
    repaint();
}

std::optional<double> MotionCompositionView::selectedKeyContentTime(motion::Id id) const {
    editingTime();
    return pathKey.has_value() && pathKey->selection == id ? std::optional<double>(pathKey->contentTime) : std::nullopt;
}

void MotionCompositionView::setSolidView(bool on) {
    if (solidView == on) { return; }
    solidView = on;
    repaint();
}

void MotionCompositionView::setMotionPathVisible(bool visible) {
    showMotionPath = visible;
    if (onMotionPathChanged) { onMotionPathChanged(visible); }
    repaint();
}

void MotionCompositionView::restoreView(const ViewState& state) {
    setNavigating(false);
    cancelGesture();
    camera = state.camera;
    setTool(state.tool);
    setMotionPathVisible(state.motionPath);
}

void MotionCompositionView::setNavigating(bool enabled) {
    if (navigating == enabled) { return; }
    cancelGesture();
    navigating = enabled;
    if (enabled) {
        savedCursor = juce::Desktop::getInstance().getMainMouseSource().getScreenPosition();
        setMouseCursor(juce::MouseCursor::NoCursor);
        grabKeyboardFocus();
        lastTick = juce::Time::getMillisecondCounterHiRes();
        centreCursor();
        startTimerHz(60);
    } else {
        stopTimer();
        releaseCursor();
    }
    if (onNavigationChanged) { onNavigationChanged(enabled); }
    repaint();
}

void MotionCompositionView::frameSelection() {
    const auto liveFrames = processor.liveSourcePreview();
    cancelGesture();
    if (prepared == nullptr) { return; }
    const auto time = editingTime();
    const auto hasSelection = std::any_of(prepared->clips.begin(), prepared->clips.end(), [&](const auto& clip) { return clip.editorId() == selected && clip.active(time); });
    motion::Vec3 minimum { 1e12, 1e12, 1e12 }, maximum { -1e12, -1e12, -1e12 };
    bool found = false;
    for (const auto& clip : prepared->clips) {
        if (!clip.active(time) || (hasSelection && clip.editorId() != selected)) { continue; }
        for (int index = 0; index < 512; ++index) {
            const auto sample = clip.sample(time, index / 512.0, 0, 0, liveFrames.get());
            if (sample.r == 0 && sample.g == 0 && sample.b == 0) { continue; }
            const auto point = worldPoint(sample, time);
            if (!point.finite()) { continue; }
            minimum = { std::min(minimum.x, point.x), std::min(minimum.y, point.y), std::min(minimum.z, point.z) };
            maximum = { std::max(maximum.x, point.x), std::max(maximum.y, point.y), std::max(maximum.z, point.z) };
            found = true;
        }
    }
    if (found) {
        // Fit what is clear of the tool strip, when it covers the output's
        // left edge (a narrow view), and centre there.
        const auto view = outputFrame();
        const auto clear = view.withLeft(std::max(view.getX(), static_cast<float>(coveredLeft + 8)));
        const auto radius = std::max(0.05, (maximum - minimum).length() * 0.5) * view.getWidth() / std::max(1.0f, clear.getWidth());
        if (camera.frame((minimum + maximum) * 0.5, radius)) { camera.pan(clear.getCentreX() - view.getCentreX(), 0, view.getHeight()); }
    }
    repaint();
}

juce::String MotionCompositionView::driveBlocker(motion::Id id) const {
    const auto& project = processor.document.project();
    const auto found = std::find_if(project.cameras.begin(), project.cameras.end(), [id](const auto& item) { return item.id == id; });
    if (found == project.cameras.end()) { return "Select a camera to look through it"; }
    if (found->target != 0) { return "This camera is aimed by Look at"; }
    if (found->parent != 0) { return "This camera is carried by a group"; }
    for (const auto& [name, curve] : found->properties) {
        if (curve.link.has_value()) { return "This camera has a linked property"; }
        if (name == "rotation.z" && !level(curve)) { return "This camera is rolled (Z rotation)"; }
    }
    const auto& routes = project.routes;
    if (std::any_of(routes.begin(), routes.end(), [id](const auto& route) { return route.target == id; })) { return "This camera is modulated"; }
    return {};
}

void MotionCompositionView::setDrivenCamera(motion::Id id) {
    if (id == lockedCamera || (id != 0 && !canDriveCamera(id))) { return; }
    if (lockedCamera == 0 && id != 0) { viewBeforeLock = camera; }
    lockedCamera = id;
    lastPose.reset();
    if (id != 0) {
        syncCamera();
        cameraSync.startTimerHz(30);
    } else {
        cameraSync.stopTimer();
        if (viewBeforeLock.has_value()) { camera = *viewBeforeLock; }
        viewBeforeLock.reset();
    }
    if (onDrivenCameraChanged) { onDrivenCameraChanged(lockedCamera); }
    repaint();
}

void MotionCompositionView::setViewPreset(ViewPreset preset) {
    cancelGesture();
    setNavigating(false);
    const auto distance = std::clamp(camera.distance() > 0 ? camera.distance() : 4.0, motion::editor::Camera::minimumDistance, motion::editor::Camera::maximumDistance);
    const auto pi = juce::MathConstants<double>::pi;
    const auto limit = motion::editor::Camera::pitchLimit;
    camera.pitch = preset == ViewPreset::top ? -limit : preset == ViewPreset::bottom ? limit : 0.0;
    camera.yaw = preset == ViewPreset::back ? pi : preset == ViewPreset::right ? -pi / 2 : preset == ViewPreset::left ? pi / 2 : 0.0;
    camera.position = camera.pivot - camera.forward() * distance;
    repaint();
}

void MotionCompositionView::preview(const motion::Project& project) {
    const auto since = juce::Time::getMillisecondCounterHiRes() - preparedAt;
    const auto dragging = juce::ModifierKeys::currentModifiers.isAnyMouseButtonDown();
    if (prepared != nullptr && dragging && since < framePeriod) {
        waiting = project;
        if (!preparation.isTimerRunning()) { preparation.startTimer(std::max(1, juce::roundToInt(framePeriod - since))); }
        return;
    }
    prepare(project);
}

void MotionCompositionView::prepare(const motion::Project& project) {
    preparation.stopTimer();
    waiting.reset();
    pathDirty = true;
    prepared = std::make_unique<motion::PreparedComposition>(project, 48000, nullptr, motion::CompositionPurpose::editorGeometry);
    shownProject = std::make_shared<const motion::Project>(project);
    ++preparedSerial;
    const auto scope = processor.document.scopeGeneration();
    if (scope != lightingScope) {
        lightingScope = scope;
        lightingSince = preparedSerial;
    }
    prunePicked();
    repaint();
    // From when the build is done, so a slow build leaves the frame after it
    // for everything else.
    preparedAt = juce::Time::getMillisecondCounterHiRes();
    if (onPrepared) { onPrepared(); }
}

void MotionCompositionView::paint(juce::Graphics& g) {
    const auto liveFrames = processor.liveSourcePreview();
    g.fillAll(osci::Colours::veryDark());
    // The ground grid; its two axes take X's and Y's colours. Seen edge-on
    // its lines would pile into one bright line, so they fade as the view
    // turns along the grid; the axes stay.
    const auto gridFade = static_cast<float>(std::clamp(std::abs(camera.forward().z) / .25, 0.0, 1.0));
    for (int line = -5; line <= 5; ++line) {
        const auto value = static_cast<double>(line);
        if (line != 0 && gridFade <= 0) { continue; }
        g.setColour(line == 0 ? motion::style::axisY().withAlpha(.3f) : osci::Colours::text().withAlpha(.045f * gridFade));
        drawWorldLine(g, {value, -5, 0}, {value, 5, 0});
        g.setColour(line == 0 ? motion::style::axisX().withAlpha(.3f) : osci::Colours::text().withAlpha(.045f * gridFade));
        drawWorldLine(g, {-5, value, 0}, {5, value, 0});
    }
    if (prepared == nullptr || prepared->clips.empty()) {
        // On a pill of its own, so the axes never run through it; clear of
        // the tool strip on the left, and on two lines in a narrow Scene.
        const auto message = prepared == nullptr ? juce::String("Preparing...") : emptyMessage();
        const auto room = getLocalBounds().withTrimmedLeft(48).reduced(12, 0);
        const auto width = juce::TextLayout::getStringWidth(motion::style::body(), message) + 28;
        const auto lines = width <= static_cast<float>(room.getWidth()) ? 1 : 2;
        const auto pill = room.toFloat().withSizeKeepingCentre(std::min(width, static_cast<float>(room.getWidth())), lines * 18.0f + 12.0f);
        motion::style::fillFloatingPanel(g, pill, osci::Colours::veryDark());
        g.setColour(osci::Colours::text().withAlpha(0.6f));
        g.setFont(motion::style::body());
        g.drawFittedText(message, pill.toNearestInt().reduced(14, 6), juce::Justification::centred, lines);
        paintOrientation(g);
        return;
    }
    juce::Graphics::ScopedSaveState sceneState(g);
    const auto time = editingTime();
    // With an object selected, the others step back so the selection reads
    // whatever their colour.
    const auto anySelected = !partMode && (marquee.has_value() ? !marqueeClips.empty() || (marqueeAdds && selected != 0) : selected != 0);
    const motion::PreparedClip* clip = nullptr;
    bool highlighted = false, pickable = false;
    auto chosen = picked.end();
    auto boxed = marqueeParts.end();
    for (const auto& piece : screenPieces()) {
        if (piece.clip != clip) {
            clip = piece.clip;
            const auto id = clip->editorId();
            // While a box is dragged, what it would select lights up; with
            // Shift the selection it adds to stays lit too.
            const auto current = id == selected || (isSelected && isSelected(id)) || inSelectedGroup(id);
            const auto boxing = marquee.has_value() && !partMode;
            const auto inBox = std::find(marqueeClips.begin(), marqueeClips.end(), id) != marqueeClips.end();
            highlighted = !partMode && ((boxing ? inBox || (marqueeAdds && current) : current) || (dropHover.has_value() && *dropHover != 0 && id == *dropHover));
            // Picking parts, objects that can only be picked whole step back.
            pickable = partMode && partDrawing(*clip) != nullptr;
            chosen = pickable && (!marquee.has_value() || marqueeAdds) ? picked.find(id) : picked.end();
            boxed = pickable && marquee.has_value() ? marqueeParts.find(id) : marqueeParts.end();
        }
        // Long, fast jumps in a traced beam fade so the shape reads over them.
        auto alpha = piece.shape < 0 ? std::min(1.0f, 12.0f / std::max(1.0f, piece.line.getLength())) * .8f : .85f;
        if (partMode && !pickable) { alpha *= .3f; }
        if (anySelected && !highlighted) { alpha *= .55f; }
        // Lines beyond the orbit pivot fade with depth, so 3D reads at a glance.
        alpha *= depthFade(piece.depth);
        const auto shape = static_cast<std::size_t>(std::max(0, piece.shape));
        const auto isChosen = piece.shape >= 0 && ((chosen != picked.end() && chosen->second.shapes.contains(shape)) || (boxed != marqueeParts.end() && boxed->second.shapes.contains(shape)));
        const auto hovered = pickable && piece.shape >= 0 && hoverPart.has_value() && hoverPart->clip == clip->editorId() && hoverShapes.contains(shape);
        if (highlighted || isChosen) {
            highlightedLines.add(piece.line, motion::style::selection().withAlpha(alpha));
        } else if (hovered) {
            hoveredLines.add(piece.line, juce::Colours::white);
        } else {
            // Unpicked parts are neutral and quiet, so picked and hovered ones stand out.
            lines.add(piece.line, pickable ? osci::Colours::text().withAlpha(.38f * depthFade(piece.depth)) : piece.colour.withMultipliedAlpha(alpha));
        }
    }
    lines.stroke(g, 1.0f);
    highlightedLines.stroke(g, 1.4f);
    hoveredLines.stroke(g, 2.0f);
    paintCameras(g, time);
    paintMotionPath(g);
    if (!partMode) { currentGizmo().paint(g, edit.active() && !handleDrag.has_value() ? dragAxis : hoverHandle); }
    paintGeneratorHandles(g);
    if (marquee.has_value()) {
        const auto region = marqueeRegion();
        g.setColour(motion::style::accent().withAlpha(.08f));
        g.fillPath(region);
        g.setColour(motion::style::accent().withAlpha(.7f));
        const float dashes[] {4.0f, 3.0f};
        juce::Path dashed;
        juce::PathStrokeType(1.0f).createDashedStroke(dashed, region, dashes, 2);
        g.fillPath(dashed);
    }
    paintOrientation(g);
    if (partCount.isVisible()) {
        motion::style::fillFloatingPanel(g, partBar().toFloat(), osci::Colours::veryDark());
    }
}

void MotionCompositionView::paintCameras(juce::Graphics& g, double time) const {
    const auto* active = prepared->activeCamera(time);
    const auto view = camera.view();
    for (const auto& camera : prepared->cameras) {
        if (camera.id == lockedCamera) { continue; }
        const auto frame = camera.frame(time);
        if (!frame.has_value()) { continue; }
        const auto& [position, right, up, forward, focalLength] = *frame;
        const auto rectangle = [&](double depth) {
            const auto half = depth / focalLength;
            const auto centre = position + forward * depth;
            return std::array<motion::Vec3, 4> {centre - right * half + up * half, centre + right * half + up * half, centre + right * half - up * half, centre - right * half - up * half};
        };
        const bool isActive = &camera == active, isSelected = camera.id == selected;
        const auto colour = isSelected ? motion::style::selection() : isActive ? osci::Colours::text() : osci::Colours::textMuted();
        // Lines through the eye or out of view are cut where they leave it,
        // so a camera stays drawn however near it is; it only fades as the
        // view reaches its own position (a camera added where the view is).
        const auto nearness = std::clamp(((this->camera.position - position).length() - .15) / .5, 0.0, 1.0);
        g.setColour(colour.withAlpha((isSelected || isActive ? .85f : .5f) * static_cast<float>(nearness)));
        const auto corners = rectangle(.45);
        for (std::size_t index = 0; index < corners.size(); ++index) {
            drawWorldLine(g, position, corners[index]);
            drawWorldLine(g, corners[index], corners[(index + 1) % corners.size()]);
        }
        // The triangle above the frame marks which way is up.
        const auto top = (corners[0] + corners[1]) * .5;
        const auto width = (corners[1] - corners[0]) * .3;
        drawWorldLine(g, top - width, top + up * .12);
        drawWorldLine(g, top + width, top + up * .12);
        if (!isActive) { continue; }
        const auto depth = (motion::Vec3 {} - position).dot(forward);
        if (depth <= .5) { continue; }
        const auto shot = rectangle(depth);
        // The frame faces the camera; seen edge-on it fades rather than
        // drawing as stray lines across the view.
        const auto facing = static_cast<float>(std::clamp(std::abs(this->camera.forward().dot(forward)) / .3, 0.0, 1.0));
        if (facing <= 0) { continue; }
        g.setColour(colour.withAlpha((isSelected ? .5f : .22f) * facing));
        for (std::size_t index = 0; index < shot.size(); ++index) {
            const auto line = screenLine(view, shot[index], shot[(index + 1) % shot.size()]);
            if (!line.has_value()) { continue; }
            const float dashes[] {5.0f, 4.0f};
            g.drawDashedLine(*line, dashes, 2, 1.0f);
        }
    }
}

bool MotionCompositionView::lookingAlong(ViewPreset preset) const {
    const auto forward = camera.forward();
    const auto along = preset == ViewPreset::front ? motion::Vec3 {0, 0, -1} : preset == ViewPreset::back ? motion::Vec3 {0, 0, 1}
        : preset == ViewPreset::right ? motion::Vec3 {-1, 0, 0} : preset == ViewPreset::left ? motion::Vec3 {1, 0, 0}
        : preset == ViewPreset::top ? motion::Vec3 {0, -1, 0} : motion::Vec3 {0, 1, 0};
    return forward.dot(along) > .999;
}

MotionCompositionView::ViewPreset MotionCompositionView::opposite(ViewPreset preset) {
    switch (preset) {
        case ViewPreset::front: return ViewPreset::back;
        case ViewPreset::back: return ViewPreset::front;
        case ViewPreset::right: return ViewPreset::left;
        case ViewPreset::left: return ViewPreset::right;
        case ViewPreset::top: return ViewPreset::bottom;
        case ViewPreset::bottom: return ViewPreset::top;
    }
    return preset;
}

std::optional<MotionCompositionView::ViewPreset> MotionCompositionView::orientationHit(juce::Point<float> position) const {
    const auto view = camera.view();
    if (!view.has_value() || getWidth() < 240 || getHeight() < 160 || position.getDistanceFrom(orientationCentre()) > 44) { return std::nullopt; }
    std::optional<ViewPreset> hit;
    float nearest = 9;
    const std::array<std::pair<motion::Vec3, ViewPreset>, 6> axes {{{{1, 0, 0}, ViewPreset::right}, {{-1, 0, 0}, ViewPreset::left}, {{0, 1, 0}, ViewPreset::top},
        {{0, -1, 0}, ViewPreset::bottom}, {{0, 0, 1}, ViewPreset::front}, {{0, 0, -1}, ViewPreset::back}}};
    // Ends that overlap go to the nearer one, which paint draws on top.
    double nearestDepth = 2;
    for (const auto& [direction, preset] : axes) {
        const auto end = orientationCentre() + juce::Point<float>(static_cast<float>(direction.dot(view->right)), static_cast<float>(-direction.dot(view->up))) * 30.0f;
        const auto distance = end.getDistanceFrom(position);
        const auto depth = direction.dot(view->forward);
        if (distance < nearest - .5f || (distance <= nearest + .5f && hit.has_value() && depth < nearestDepth)) {
            nearest = std::min(nearest, distance);
            nearestDepth = depth;
            hit = preset;
        } else if (!hit.has_value() && distance < nearest) {
            nearest = distance;
            nearestDepth = depth;
            hit = preset;
        }
    }
    return hit;
}

// Positive axes are lettered dots on a line; negative ones are rings. The
// nearer end draws last.
void MotionCompositionView::paintOrientation(juce::Graphics& g) const {
    const auto view = camera.view();
    if (!view.has_value() || getWidth() < 240 || getHeight() < 160) { return; }
    const auto centre = orientationCentre();
    // A quiet backing keeps lines behind it from running through the axes.
    g.setColour(osci::Colours::veryDark().withAlpha(.6f));
    g.fillEllipse(juce::Rectangle<float>(80, 80).withCentre(centre));
    if (hoverCorner) {
        g.setColour(motion::style::hoverFill());
        g.fillEllipse(juce::Rectangle<float>(80, 80).withCentre(centre));
    }
    struct Axis { motion::Vec3 direction; juce::Colour colour; const char* label; ViewPreset preset; };
    std::array<Axis, 6> axes {{{{1, 0, 0}, motion::style::axisX(), "X", ViewPreset::right}, {{0, 1, 0}, motion::style::axisY(), "Y", ViewPreset::top}, {{0, 0, 1}, motion::style::axisZ(), "Z", ViewPreset::front},
        {{-1, 0, 0}, motion::style::axisX(), "", ViewPreset::left}, {{0, -1, 0}, motion::style::axisY(), "", ViewPreset::bottom}, {{0, 0, -1}, motion::style::axisZ(), "", ViewPreset::back}}};
    std::sort(axes.begin(), axes.end(), [&](const Axis& a, const Axis& b) { return a.direction.dot(view->forward) > b.direction.dot(view->forward); });
    for (const auto& axis : axes) {
        const auto end = centre + juce::Point<float>(static_cast<float>(axis.direction.dot(view->right)), static_cast<float>(-axis.direction.dot(view->up))) * 30.0f;
        const auto lit = hoverAxis == axis.preset;
        const auto dot = juce::Rectangle<float>(16, 16).withCentre(end);
        if (*axis.label != 0) {
            g.setColour(axis.colour.withAlpha(.7f));
            g.drawLine({centre, end}, 2.0f);
            g.setColour(lit ? axis.colour.brighter(.4f) : axis.colour);
            g.fillEllipse(dot);
            // The letter's own outline is centred, not its text box, whose
            // ascent and side bearings sit it off centre.
            juce::GlyphArrangement glyph;
            glyph.addLineOfText(motion::style::heading(), axis.label, 0, 0);
            juce::Path letter;
            glyph.createPath(letter);
            const auto ink = letter.getBounds();
            g.setColour(osci::Colours::veryDark());
            g.fillPath(letter, juce::AffineTransform::translation(dot.getCentreX() - ink.getCentreX(), dot.getCentreY() - ink.getCentreY()));
        } else {
            g.setColour(axis.colour.withAlpha(lit ? .45f : .2f));
            g.fillEllipse(dot.reduced(2));
            g.setColour(axis.colour.withAlpha(lit ? .9f : .55f));
            g.drawEllipse(dot.reduced(2.5f), 1.5f);
        }
    }
}

void MotionCompositionView::mouseDoubleClick(const juce::MouseEvent& event) {
    if (navigating || prepared == nullptr || !event.mods.isLeftButtonDown() || event.mods.isAltDown() || orientationHit(event.position).has_value()) { return; }
    if (partMode) {
        const auto part = pickPart(event.position);
        if (part.has_value()) { pickPath(*part, event.mods.isShiftDown()); }
        return;
    }
    const auto hit = pickAt(event.position, nullptr);
    if (hit == 0 || !onOpenSource) { return; }
    cancelGesture();
    onOpenSource(hit);
}

void MotionCompositionView::mouseDown(const juce::MouseEvent& event) {
    if (navigating) { setNavigating(false); return; }
    grabKeyboardFocus();
    cancelGesture();
    dragHint.clear();
    resetMarquee();
    navigationDrag = event.mods.isAltDown() || event.mods.isMiddleButtonDown();
    panDrag = event.mods.isShiftDown();
    cameraAtDown = camera;
    down = event.position;
    // Looking through a camera, the drag moves it: one undo step, which
    // Escape takes back.
    if (navigationDrag && lockedCamera != 0) { processor.document.beginHeld("camera-view:" + juce::String(static_cast<juce::int64>(lockedCamera))); }
    if (navigationDrag) { return; }
    if (event.mods.isPopupMenu()) {
        // Like a left click, the menu acts on what is under the pointer.
        if (prepared != nullptr) {
            const auto hit = pickTarget(event.position, nullptr);
            // Right-clicking one of several selected clips keeps the set.
            const auto inSelection = isSelected && isSelected(hit);
            if (hit != 0 && hit != selected && !inSelection) {
                selected = hit;
                if (onSelection) { onSelection(hit); }
            }
        }
        if (onContextMenu) { onContextMenu(); }
        return;
    }
    if (!event.mods.isLeftButtonDown()) {
        return;
    }
    const auto axis = orientationHit(event.position);
    if (axis.has_value()) {
        // Clicking the axis already looked along turns the view around, as in Blender.
        setViewPreset(lookingAlong(*axis) ? opposite(*axis) : *axis);
        return;
    }
    if (prepared == nullptr) { return; }
    const auto adding = event.mods.isShiftDown();
    if (partMode) {
        // A drag picks with the box or lasso from anywhere; a click picks the
        // part under the pointer (Shift toggles it), or clears on empty space.
        marqueeArmed = true;
        marqueeAdds = adding;
        pressedPart = pickPart(event.position);
        lasso.clear();
        if (partPick == PartPick::lasso) { lasso.startNewSubPath(event.position); }
        if (!pressedPart.has_value()) {
            const auto whole = pickAt(event.position, nullptr);
            if (whole != 0 && onStatus) { onStatus(partBlocker(whole)); }
        }
        return;
    }
    if (showMotionPath && seekMotionKey(event.position)) { return; }
    const auto handleIndex = handleAt(event.position);
    if (handleIndex >= 0) {
        const auto context = handleContext();
        const auto shown = context.has_value() ? shownHandles(*context) : std::vector<ShownHandle> {};
        const auto found = std::find_if(shown.begin(), shown.end(), [&](const auto& item) { return static_cast<int>(item.index) == handleIndex; });
        if (found != shown.end()) {
            handleDrag = HandleDrag {found->index, found->handle, *context, context->values, found->handle.at, selected};
            editTime = editingTime();
            editSelection = selected;
            companions.clear();
            edit.begin();
            dragStarted = false;
            repaint();
            return;
        }
    }
    const auto gizmo = currentGizmo();
    const auto handle = gizmo.hitTest(event.position);
    if (handle >= 0) {
        dragAnchor = gizmoFrame->parent.worldOrigin;
        if (beginGesture(editingTime())) {
            dragAxis = handle;
            gesture = tool == MotionTransformTool::move || tool == MotionTransformTool::anchor ? (handle == 3 ? Gesture::plane : Gesture::moveAxis)
                : tool == MotionTransformTool::rotate ? Gesture::rotateAxis
                : handle == 3 ? Gesture::uniformScale : Gesture::scaleAxis;
            if (handle < 3) {
                dragDirection = gizmo.axes[handle].direction;
                if (!gizmo.axes[handle].points.empty()) { scaleScreenAxis = gizmo.axes[handle].points.back() - gizmo.origin; }
            }
            lastRotationPointer = normalized(event.position);
            rotationDelta = 0;
        }
        repaint();
        return;
    }
    const auto time = editingTime();
    auto object = pickAt(event.position, &dragAnchor);
    // A double-click opens the source instead, so a click cycles only after a pause.
    const auto again = lastClick.has_value() && lastClick->getDistanceFrom(event.position) <= 4.0f && event.getNumberOfClicks() == 1;
    lastClick = event.position;
    const auto underneath = again && !adding && selected != 0 ? hitsAt(event.position) : std::vector<motion::Id> {};
    cycleOnRelease = underneath.size() > 1 && std::find(underneath.begin(), underneath.end(), selected) != underneath.end();
    // Pressed again on the selection: a drag still moves it (from its own
    // nearest point, so on its own depth); a click (on release) moves on.
    if (cycleOnRelease) {
        object = selected;
        float nearest = std::numeric_limits<float>::max();
        for (const auto& piece : screenPieces()) {
            juce::Point<float> closest;
            const auto distance = piece.clip->editorId() == selected ? piece.line.getDistanceFromPoint(event.position, closest) : nearest;
            if (distance < nearest) {
                nearest = distance;
                dragAnchor = nearestOnSegment(event.position, piece.a, piece.b);
            }
        }
    }
    // Empty space starts a marquee; Shift adds to the selection.
    if (object == 0) {
        marqueeArmed = true;
        marqueeAdds = adding;
        if (adding) { return; }
    } else if (adding) {
        if (onSelectClips) { onSelectClips({object}, SelectionChange::toggle); }
        return;
    }
    // With a group selected, dragging anything in it moves the group.
    const auto hit = inSelectedGroup(object) ? selected : object;
    const auto others = selectedObjects ? selectedObjects() : std::vector<motion::Id> {};
    if (hit != 0 && others.size() > 1 && std::find(others.begin(), others.end(), hit) != others.end()) {
        // One of several selected: they stay selected, led by this one, so a
        // drag takes them all.
        std::vector<motion::Id> led {hit};
        for (const auto id : others) {
            if (id != hit) { led.push_back(id); }
        }
        selected = hit;
        if (onSelectClips) { onSelectClips(led, SelectionChange::replace); }
    } else {
        selected = hit;
        if (onSelection) {
            onSelection(hit);
        }
    }
    // Move and anchor drag from anywhere on the object, as in After Effects.
    if (hit != 0 && (tool == MotionTransformTool::move || tool == MotionTransformTool::anchor)) {
        gesture = Gesture::plane;
        dragAxis = 3;
        beginGesture(time);
    }
    repaint();
}

void MotionCompositionView::mouseDrag(const juce::MouseEvent& event) {
    if (navigating) { mouseMove(event); return; }
    if (navigationDrag) {
        const auto delta = event.position - down;
        camera = cameraAtDown;
        if (panDrag) {
            camera.pan(delta.x, delta.y, outputFrame().getHeight());
        } else {
            camera.orbit(-delta.x * 0.006, delta.y * 0.006);
        }
        repaint();
        return;
    }
    if (marqueeArmed) {
        if (marquee.has_value() || event.getDistanceFromDragStart() >= 3) {
            marquee = juce::Rectangle<float>(down, event.position);
            if (partMode && partPick == PartPick::lasso) {
                lasso.lineTo(event.position);
                marquee = lasso.getBounds();
            }
            // What the drag would pick lights up as it goes.
            if (partMode) {
                marqueeParts = partsIn(marqueeRegion(), partPick == PartPick::pieces);
            } else {
                marqueeClips = clipsIn(*marquee);
            }
            repaint();
        }
        return;
    }
    if (handleDrag.has_value()) {
        if (!validGesture()) { handleDrag.reset(); return; }
        if (!dragStarted) {
            if (event.getDistanceFromDragStart() < 3) { return; }
            dragStarted = true;
        }
        dragHandle(event);
        return;
    }
    if (!validGesture() || !gizmoAtDown.has_value()) { return; }
    if (!dragStarted) {
        if (event.getDistanceFromDragStart() < 3) { return; }
        // Playback carries on: the drag edits at the time it began.
        dragStarted = true;
    }
    std::array<double, 3> offsets { 0, 0, 0 };
    double scaleFactor = 1;
    std::optional<motion::Vec3> worldDelta;
    if (gesture == Gesture::plane || gesture == Gesture::moveAxis) {
        if (gesture == Gesture::plane) {
            worldDelta = cameraAtDown.translationOnFacingPlane(normalized(down), normalized(event.position), dragAnchor);
        } else {
            const auto distance = motion::editor::gizmo::axisDragDistance(cameraAtDown, normalized(down), normalized(event.position), gizmoAtDown->parent.worldOrigin, dragDirection);
            if (distance.has_value()) { worldDelta = dragDirection * *distance; }
        }
        if (!worldDelta.has_value()) { return; }
        const auto delta = gizmoAtDown->parent.positionDelta(*worldDelta);
        if (!delta.has_value()) { return; }
        offsets = { delta->x, delta->y, delta->z };
        // An axis handle edits only its authored position channel, avoiding
        // numerical cross-axis keys after inverse rotated parent transforms.
        if (gesture == Gesture::moveAxis) {
            for (int axis = 0; axis < 3; ++axis) { if (axis != dragAxis) { offsets[axis] = 0; } }
        }
    } else if (gesture == Gesture::rotateAxis) {
        const auto first = cameraAtDown.ray(lastRotationPointer), last = cameraAtDown.ray(normalized(event.position));
        if (!first.has_value() || !last.has_value()) { return; }
        const auto firstLocal = gizmoAtDown->rotationRay(*first, dragAxis), lastLocal = gizmoAtDown->rotationRay(*last, dragAxis);
        if (!firstLocal.has_value() || !lastLocal.has_value()) { return; }
        const std::array<motion::Vec3, 3> axes { motion::Vec3 { 1, 0, 0 }, motion::Vec3 { 0, 1, 0 }, motion::Vec3 { 0, 0, 1 } };
        const auto delta = motion::editor::gizmo::rotationDragAngle(*firstLocal, *lastLocal, {}, axes[dragAxis]);
        if (!delta.has_value()) { return; }
        rotationDelta += *delta;
        lastRotationPointer = normalized(event.position);
        offsets[dragAxis] = rotationDelta * 180 / std::numbers::pi;
    } else {
        const auto screenDelta = event.position - down;
        const auto distance = gesture == Gesture::uniformScale ? screenDelta.x - screenDelta.y
            : (screenDelta.x * scaleScreenAxis.x + screenDelta.y * scaleScreenAxis.y) / scaleScreenAxis.getDistanceFromOrigin();
        const auto factor = motion::editor::gizmo::uniformScaleFactor(distance);
        if (!factor.has_value()) { return; }
        scaleFactor = *factor;
    }
    if (tool == MotionTransformTool::anchor && (gesture == Gesture::plane || gesture == Gesture::moveAxis)) {
        moveAnchor({offsets[0], offsets[1], offsets[2]});
        return;
    }
    auto project = edit.start();
    const auto contentTime = pathKey.has_value() && pathKey->selection == editSelection ? std::optional(pathKey->contentTime) : std::nullopt;
    auto anyChange = applyTransform(project, editSelection, offsets, scaleFactor, contentTime);
    for (const auto& [id, frame] : companions) {
        // As far in the world, in its own parent's space; or as much turned
        // or scaled about its own anchor.
        auto own = offsets;
        if (worldDelta.has_value()) {
            const auto delta = frame.parent.positionDelta(*worldDelta);
            if (!delta.has_value()) { continue; }
            own = {delta->x, delta->y, delta->z};
        }
        anyChange = applyTransform(project, id, own, scaleFactor, std::nullopt) || anyChange;
    }
    edit.show(std::move(project), anyChange);
    if (pathKey.has_value()) { pathKey->revision = processor.document.revision(); }
}

bool MotionCompositionView::applyTransform(motion::Project& project, motion::Id id, const std::array<double, 3>& offsets, double scaleFactor, std::optional<double> contentTime) {
    const auto target = motion::findPropertyTarget(project, id);
    if (!target.has_value()) { return false; }
    const auto localTime = contentTime.value_or(target->localTime(editTime));
    bool anyChange = false;
    for (int axis = 0; axis < 3; ++axis) {
        const auto scaling = gesture == Gesture::uniformScale || gesture == Gesture::scaleAxis;
        if (scaling && gesture == Gesture::scaleAxis && axis != dragAxis) { continue; }
        const auto prefix = scaling ? "scale." : gesture == Gesture::rotateAxis ? "rotation." : "position.";
        const auto property = std::string(prefix) + "xyz"[axis];
        auto* curve = target->curve(property);
        if (curve == nullptr) { continue; }
        const auto base = curve->evaluateBase(localTime);
        // A zero scale can be recovered by dragging; negative scales keep their sign.
        const auto value = scaling ? (base == 0 ? scaleFactor - 1 : base * scaleFactor) : base + offsets[axis];
        if (!std::isfinite(value)) { return false; }
        if (std::abs(value - base) <= 1e-10) { continue; }
        if (curve->animated()) {
            curve->setKeyValue(localTime, value);
        } else {
            curve->base = value;
        }
        if (!anyChange && id == editSelection) { editedProperty = property; }
        anyChange = true;
    }
    // Position, rotation and scale key as one property, as in After
    // Effects: when any axis is animated, every axis gets this key.
    if (anyChange) {
        const auto scaling = gesture == Gesture::uniformScale || gesture == Gesture::scaleAxis;
        const std::string prefix = scaling ? "scale." : gesture == Gesture::rotateAxis ? "rotation." : "position.";
        std::array<motion::Curve*, 3> axes {};
        bool animated = false;
        for (int axis = 0; axis < 3; ++axis) {
            axes[static_cast<std::size_t>(axis)] = target->curve(prefix + "xyz"[axis]);
            animated = animated || (axes[static_cast<std::size_t>(axis)] != nullptr && axes[static_cast<std::size_t>(axis)]->animated());
        }
        for (auto* curve : axes) {
            if (animated && curve != nullptr) { curve->setKeyValue(localTime, curve->evaluateBase(localTime)); }
        }
    }
    return anyChange;
}

void MotionCompositionView::moveAnchor(motion::Vec3 delta) {
    auto project = edit.start();
    const auto target = motion::findPropertyTarget(project, editSelection);
    if (!target.has_value() || !gizmoAtDown.has_value()) { return; }
    const auto localTime = pathKey.has_value() && pathKey->selection == editSelection ? pathKey->contentTime : target->localTime(editTime);
    // The anchor moves with the pointer; in the object's own space that is
    // the drag undone by its rotation and scale.
    const motion::editor::transform_detail::Affine own {{}, gizmoAtDown->eulerRadians, gizmoAtDown->evaluatedScale, {}};
    const auto changed = motion::anchor::shift(project, editSelection, localTime, delta, own.inverseDirection(delta));
    if (changed) { editedProperty = "anchor.x"; }
    edit.show(std::move(project), changed);
}

juce::String MotionCompositionView::centreAnchor(motion::Id id) {
    if (prepared == nullptr) { return "The Scene is still preparing"; }
    const auto time = editingTime();
    const auto clip = std::find_if(prepared->clips.begin(), prepared->clips.end(), [&](const auto& item) { return item.editorId() == id && item.ancestors.empty() && item.active(clipTime(item, time)); });
    if (clip == prepared->clips.end()) { return "Select an object at the playhead to centre its anchor"; }
    const auto* track = motion::findClipTrack(processor.document.project(), id);
    if (track != nullptr && track->locked) { return "This object's track is locked"; }
    // The middle of the source's own geometry at the playhead, before effects.
    const auto* source = clip->resolveSource(processor.liveSourcePreview().get());
    if (source == nullptr) { return "This object has nothing to centre on"; }
    const auto frame = source->frameIndex(clip->localTime(clipTime(*clip, time)));
    motion::Vec3 low {1.0e300, 1.0e300, 1.0e300}, high {-1.0e300, -1.0e300, -1.0e300};
    bool any = false;
    const auto include = [&](const osci::Point& point) {
        if (!point.hasFinitePosition()) { return; }
        low = {std::min(low.x, static_cast<double>(point.x)), std::min(low.y, static_cast<double>(point.y)), std::min(low.z, static_cast<double>(point.z))};
        high = {std::max(high.x, static_cast<double>(point.x)), std::max(high.y, static_cast<double>(point.y)), std::max(high.z, static_cast<double>(point.z))};
        any = true;
    };
    const auto* drawing = source->drawingAt(frame);
    if (drawing != nullptr) {
        for (std::size_t index = 0; index < drawing->shapeCount(); ++index) {
            auto* shape = drawing->shape(index);
            if (shape == nullptr || !(shape->length() > 0)) { continue; }
            for (int step = 0; step <= 8; ++step) { include(shape->nextVector(static_cast<float>(step) / 8)); }
        }
    } else {
        const auto count = source->previewSampleCount();
        for (std::size_t index = 0; index < count; ++index) {
            const auto point = source->sampleFrame(frame, static_cast<double>(index) / count);
            if (point.r != 0 || point.g != 0 || point.b != 0) { include(point); }
        }
    }
    if (!any) { return "This object has nothing to centre on"; }
    const auto centre = (low + high) * .5;
    const auto pose = motion::editor::gizmoFrameForClip(processor.document.project(), id, time);
    if (!pose.has_value()) { return "This object's transform can't be edited here"; }
    const motion::editor::transform_detail::Affine own {{}, pose->eulerRadians, pose->evaluatedScale, {}};
    const auto local = centre - pose->evaluatedAnchor;
    const auto parent = own.direction(local);
    processor.document.tryEdit("Centre anchor", [&](motion::Project& project) {
        const auto target = motion::findPropertyTarget(project, id);
        return target.has_value() && motion::anchor::shift(project, id, target->localTime(time), parent, local);
    });
    repaint();
    return {};
}

void MotionCompositionView::mouseUp(const juce::MouseEvent& event) {
    if (std::exchange(cycleOnRelease, false) && event.getDistanceFromDragStart() < 3) {
        const auto hits = hitsAt(event.position);
        const auto at = std::find(hits.begin(), hits.end(), selected);
        if (at != hits.end()) {
            cancelGesture();
            selected = std::next(at) != hits.end() ? *std::next(at) : hits.front();
            if (onSelection) { onSelection(selected); }
            repaint();
            return;
        }
    }
    if (navigationDrag && lockedCamera != 0) { processor.document.endHeld(); }
    navigationDrag = false;
    if (marqueeArmed) {
        marqueeArmed = false;
        const auto area = marquee;
        marquee.reset();
        marqueeParts.clear();
        marqueeClips.clear();
        if (area.has_value() && partMode) {
            marquee = area;
            const auto region = marqueeRegion();
            marquee.reset();
            pickParts(region, marqueeAdds, partPick == PartPick::pieces);
            lasso.clear();
        } else if (area.has_value()) {
            if (onSelectClips) { onSelectClips(clipsIn(*area), marqueeAdds ? SelectionChange::add : SelectionChange::replace); }
        } else if (partMode && pressedPart.has_value()) {
            clickPart(*pressedPart, marqueeAdds);
        } else if (partMode && !marqueeAdds && !picked.empty()) {
            picked.clear();
            refreshPartBar();
        }
        pressedPart.reset();
        repaint();
        return;
    }
    if (handleDrag.has_value()) {
        const auto label = "Change " + juce::String(handleDrag->handle.label);
        const auto clip = handleDrag->clip;
        // The Graph shows the number the drag changed.
        const auto& handle = handleDrag->handle;
        const auto parameter = handle.kind == motion::generator::Handle::Kind::count ? handle.count : (handle.parameters.empty() ? std::nullopt : std::optional(handle.parameters.front()));
        const auto property = parameter.has_value() ? std::string(handleDrag->context.definition->parameters[*parameter].id) : std::string();
        handleDrag.reset();
        if (validGesture() && edit.commit(label) && onPropertyEdited && !property.empty()) { onPropertyEdited(clip, property); }
        repaint();
        return;
    }
    if (validGesture()) {
        const auto noun = motion::findGroup(processor.document.project(), editSelection) != nullptr ? juce::String(" group") : juce::String(" object");
        const auto many = !companions.empty() && tool != MotionTransformTool::anchor;
        const auto label = tool == MotionTransformTool::anchor ? juce::String("Move anchor") : (tool == MotionTransformTool::move ? "Move" : tool == MotionTransformTool::rotate ? "Rotate" : "Scale") + (many ? juce::String(" objects") : noun);
        // The Graph shows the channel the drag changed.
        if (edit.commit(label) && onPropertyEdited && !editedProperty.empty()) { onPropertyEdited(editSelection, editedProperty); }
        if (pathKey.has_value()) { pathKey->revision = processor.document.revision(); }
    }
}

motion::Id MotionCompositionView::pickAt(juce::Point<float> position, motion::Vec3* anchor) const {
    float nearest = 18;
    const ScreenPiece* hit = nullptr;
    for (const auto& piece : screenPieces()) {
        juce::Point<float> closest;
        const auto distance = piece.line.getDistanceFromPoint(position, closest);
        if (distance < nearest) {
            nearest = distance;
            hit = &piece;
        }
    }
    if (hit == nullptr) { return 0; }
    if (anchor != nullptr) { *anchor = nearestOnSegment(position, hit->a, hit->b); }
    return hit->clip->editorId();
}

std::vector<motion::Id> MotionCompositionView::hitsAt(juce::Point<float> position) const {
    const auto ray = camera.ray(normalized(position));
    std::vector<std::pair<double, motion::Id>> nearest;
    for (const auto& piece : screenPieces()) {
        juce::Point<float> closest;
        if (piece.line.getDistanceFromPoint(position, closest) >= 18) { continue; }
        const auto id = piece.clip->editorId();
        const auto depth = ray.has_value() ? (nearestOnSegment(position, piece.a, piece.b) - ray->origin).length() : static_cast<double>(piece.depth);
        const auto found = std::find_if(nearest.begin(), nearest.end(), [&](const auto& item) { return item.second == id; });
        if (found == nearest.end()) {
            nearest.emplace_back(depth, id);
        } else {
            found->first = std::min(found->first, depth);
        }
    }
    std::sort(nearest.begin(), nearest.end());
    std::vector<motion::Id> ids;
    for (const auto& [depth, id] : nearest) { ids.push_back(id); }
    return ids;
}

// What a click acts on: the object under the pointer, or the selected group
// when that object is in it.
motion::Id MotionCompositionView::pickTarget(juce::Point<float> position, motion::Vec3* anchor) const {
    const auto hit = pickAt(position, anchor);
    return inSelectedGroup(hit) ? selected : hit;
}

bool MotionCompositionView::inSelectedGroup(motion::Id clip) const {
    const auto& project = processor.document.project();
    if (clip == 0 || motion::findGroup(project, selected) == nullptr) { return false; }
    const auto* track = motion::findClipTrack(project, clip);
    return track != nullptr && motion::groupHolds(project, selected, track->group);
}

// The point of a world segment nearest the pointer's ray: where the pointer
// is, at the segment's depth.
motion::Vec3 MotionCompositionView::nearestOnSegment(juce::Point<float> position, motion::Vec3 a, motion::Vec3 b) const {
    const auto ray = camera.ray(normalized(position));
    if (!ray.has_value()) { return a; }
    const auto along = b - a, offset = a - ray->origin;
    const auto length = along.dot(along), facing = along.dot(ray->direction), lean = along.dot(offset), reach = ray->direction.dot(offset);
    const auto denominator = length - facing * facing;
    const auto fraction = std::abs(denominator) > 1.0e-12 ? std::clamp((facing * reach - lean) / denominator, 0.0, 1.0) : 0.0;
    return a + along * fraction;
}

// Laid out as the tool strips are: 3 px in, a 28 px tall button, 8 px from
// the Scene's edge.
void MotionCompositionView::resized() {
    auto bar = partBar().reduced(motion::icons::ToolStrip::inset);
    extractButton.setBounds(bar.removeFromRight(extractWidth));
    partCount.setBounds(bar);
}

void MotionCompositionView::itemDragMove(const SourceDetails& details) {
    const auto description = details.description.toString();
    const auto hit = prepared != nullptr ? pickAt(details.localPosition.toFloat(), nullptr) : motion::Id(0);
    if (dropHover == hit) { return; }
    dropHover = hit;
    if (description.startsWith("motion-effect-instance:")) {
        if (onEffectMovePreview) { onEffectMovePreview(static_cast<motion::Id>(description.fromFirstOccurrenceOf(":", false, false).getLargeIntValue()), hit); }
    } else if (onEffectPreview) {
        onEffectPreview(description.fromFirstOccurrenceOf(":", false, false).toStdString(), hit);
    }
    repaint();
}

void MotionCompositionView::itemDragExit(const SourceDetails& details) {
    dropHover.reset();
    const auto description = details.description.toString();
    if (description.startsWith("motion-effect-instance:")) {
        if (onEffectMovePreview) { onEffectMovePreview(static_cast<motion::Id>(description.fromFirstOccurrenceOf(":", false, false).getLargeIntValue()), std::nullopt); }
    } else if (onEffectPreview) {
        onEffectPreview(description.fromFirstOccurrenceOf(":", false, false).toStdString(), std::nullopt);
    }
    repaint();
}

void MotionCompositionView::itemDropped(const SourceDetails& details) {
    const auto hit = dropHover.value_or(0);
    dropHover.reset();
    const auto description = details.description.toString();
    if (description.startsWith("motion-effect-instance:")) {
        const auto effect = static_cast<motion::Id>(description.fromFirstOccurrenceOf(":", false, false).getLargeIntValue());
        if (onEffectMovePreview) { onEffectMovePreview(effect, std::nullopt); }
        if (onEffectMoved) { onEffectMoved(effect, hit, juce::ModifierKeys::getCurrentModifiers().isAltDown()); }
        repaint();
        return;
    }
    const auto type = description.fromFirstOccurrenceOf(":", false, false).toStdString();
    if (onEffectPreview) { onEffectPreview(type, std::nullopt); }
    if (onEffectDropped) { onEffectDropped(type, hit); }
    repaint();
}

void MotionCompositionView::setEffectDragActive(bool active) {
    effectDrag = active;
    if (!active) { dropHover.reset(); }
    repaint();
}

void MotionCompositionView::paintOverChildren(juce::Graphics& g) {
    if (effectDrag) {
        // Over empty space the whole Scene is the target.
        const auto everything = dropHover.has_value() && *dropHover == 0;
        g.setColour(motion::style::accent().withAlpha(everything ? .9f : .3f));
        g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(2.0f), motion::style::radiusPanel, everything ? 2.0f : 1.0f);
        if (everything) {
            auto label = getLocalBounds().removeFromBottom(30).withSizeKeepingCentre(110, 22);
            g.setColour(motion::style::accent().withAlpha(.85f));
            g.fillRoundedRectangle(label.toFloat(), motion::style::radiusControl);
            g.setColour(juce::Colours::white);
            g.setFont(motion::style::body());
            g.drawText("Everything", label, juce::Justification::centred, false);
        }
    }
    using Key = motion::ui::KeyPosition;
    const juce::String help = navigating ? motion::ui::keysAtPositions({Key::w, Key::a, Key::s, Key::d}) + " / arrows move" + motion::style::dot() + motion::ui::keysAtPositions({Key::q}) + " " + motion::ui::keysAtPositions({Key::e}) + " down / up" + motion::style::dot() + "Shift faster" + motion::style::dot() + "Esc finishes"
        : (validGesture() || navigationDrag) && dragHint.isNotEmpty() ? dragHint : juce::String();
    if (help.isNotEmpty()) {
        auto strip = getLocalBounds().removeFromBottom(26).reduced(8, 3);
        strip = strip.withSizeKeepingCentre(std::min(strip.getWidth(), juce::GlyphArrangement::getStringWidthInt(motion::style::body(), help) + 24), strip.getHeight());
        g.setColour(osci::Colours::veryDark().withAlpha(.85f));
        g.fillRoundedRectangle(strip.toFloat(), motion::style::radiusPanel);
        g.setColour(osci::Colours::textMuted());
        g.setFont(motion::style::body());
        g.drawFittedText(help, strip.reduced(8, 0), juce::Justification::centred, 1);
    }
}

void MotionCompositionView::mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) {
    if (navigating || edit.active()) { return; }
    // Over a handle with a count, scrolling steps it.
    hoverGeneratorHandle = handleAt(event.position);
    if (hoverGeneratorHandle >= 0 && !wheel.isInertial && stepHandleCount(std::abs(wheel.deltaY) >= std::abs(wheel.deltaX) ? wheel.deltaY : -wheel.deltaX)) { return; }
    const auto pixels = juce::Point<double>(wheel.deltaX, wheel.deltaY) * 256.0;
    // Orbiting and panning carry on with the trackpad's momentum once the
    // fingers lift, as scrolling does anywhere; zooming stops where it is let go.
    if (!wheel.isSmooth || event.mods.isCommandDown() || event.mods.isCtrlDown()) {
        if (wheel.isInertial) { return; }
        camera.dolly(-(std::abs(wheel.deltaY) >= std::abs(wheel.deltaX) ? wheel.deltaY : wheel.deltaX) * 2.0);
    } else if (event.mods.isShiftDown()) {
        camera.pan(pixels.x, pixels.y, outputFrame().getHeight());
    } else {
        camera.orbit(-pixels.x * 0.006, pixels.y * 0.006);
    }
    repaint();
}

void MotionCompositionView::mouseMagnify(const juce::MouseEvent&, float scale) {
    if (navigating || edit.active() || !(scale > 0)) { return; }
    camera.dolly(-std::log(static_cast<double>(scale)));
    repaint();
}

void MotionCompositionView::mouseMove(const juce::MouseEvent& event) {
    if (!navigating) {
        const auto axis = orientationHit(event.position);
        const auto inCorner = getLocalBounds().toFloat().removeFromTop(96).removeFromRight(96).contains(event.position);
        if (axis != hoverAxis || inCorner != hoverCorner) {
            hoverAxis = axis;
            hoverCorner = inCorner;
            repaint();
        }
        if (partMode) {
            const auto part = axis.has_value() ? std::nullopt : pickPart(event.position);
            const auto changed = part.has_value() != hoverPart.has_value() || (part.has_value() && (part->clip != hoverPart->clip || part->shape != hoverPart->shape));
            hoverPart = part;
            if (changed) { hoverShapes = part.has_value() ? pickedBy(*part) : std::set<std::size_t>(); }
            if (changed) { repaint(); }
            return;
        }
        const auto handle = handleAt(event.position);
        if (handle != hoverGeneratorHandle) { hoverGeneratorHandle = handle; repaint(); }
        const auto hit = handle >= 0 ? -1 : currentGizmo().hitTest(event.position);
        if (hit != hoverHandle) { hoverHandle = hit; repaint(); }
        return;
    }
    const auto delta = event.position - getLocalBounds().getCentre().toFloat();
    if (delta.getDistanceFromOrigin() < 0.5f) { return; }
    camera.look(delta.x * 0.003, -delta.y * 0.003);
    centreCursor();
    repaint();
}

bool MotionCompositionView::keyPressed(const juce::KeyPress& key) {
    if (key == juce::KeyPress::escapeKey) {
        if (navigating) { setNavigating(false); return true; }
        if (validGesture()) { cancelGesture(); return true; }
        if (navigationDrag) {
            // The camera's pose goes back with the view, unwritten.
            if (lockedCamera != 0) {
                processor.document.cancelHeld();
                lastPose.reset();
            }
            camera = cameraAtDown;
            navigationDrag = false;
            repaint();
            return true;
        }
        if (marqueeArmed) { resetMarquee(); repaint(); return true; }
        // Escape clears the picked parts, then leaves part mode.
        if (partMode && !picked.empty()) { picked.clear(); refreshPartBar(); return true; }
        if (partMode) { setPartMode(false); return true; }
    }
    const auto code = key.getKeyCode();
    const auto letter = osci::shortcutKey(key);
    // Tab switches between objects and their parts, as Blender's edit mode.
    if (key == juce::KeyPress::tabKey && !navigating) { setPartMode(!partMode); return true; }
    if (partMode && key == juce::KeyPress('a', juce::ModifierKeys::commandModifier, 0)) {
        pickAllParts();
        return true;
    }
    if (partMode && !key.getModifiers().isAnyModifierKeyDown() && letter == 'E') { extractPicked(); return true; }
    // Views along an axis: numpad 1, 3, 7 (Ctrl for the opposite side), or
    // 1, 3, 7 on the number row as with Blender's emulated numpad.
    const auto opposite = key.getModifiers().isCtrlDown();
    const auto plain = !key.getModifiers().isAnyModifierKeyDown();
    if (!navigating && (code == juce::KeyPress::numberPad1 || (plain && code == '1'))) { setViewPreset(opposite ? ViewPreset::back : ViewPreset::front); return true; }
    if (!navigating && (code == juce::KeyPress::numberPad3 || (plain && code == '3'))) { setViewPreset(opposite ? ViewPreset::left : ViewPreset::right); return true; }
    if (!navigating && (code == juce::KeyPress::numberPad7 || (plain && code == '7'))) { setViewPreset(opposite ? ViewPreset::bottom : ViewPreset::top); return true; }
    if (key.getModifiers().isCommandDown() || key.getModifiers().isCtrlDown()) {
        // Undo mid-drag takes back the drag alone, as the Graph's does.
        const auto undoing = letter == 'Z' && !key.getModifiers().isShiftDown() && validGesture();
        cancelGesture();
        setNavigating(false);
        return undoing;
    }
    if (navigating) { return true; }
    if (key.getModifiers().isAltDown()) { return false; }
    // Single-letter tools take plain keys only, so Shift+R and friends
    // reach the editor's commands.
    if (key.getModifiers().isShiftDown()) { return false; }
    if (letter == 'P') { setMotionPathVisible(!showMotionPath); return true; }
    if (letter == 'G') { setTool(MotionTransformTool::move); return true; }
    if (letter == 'Y') { setTool(MotionTransformTool::anchor); return true; }
    if (letter == 'R') { setTool(MotionTransformTool::rotate); return true; }
    if (letter == 'S') { setTool(MotionTransformTool::scale); return true; }
    if (letter == 'F') { frameSelection(); return true; }
    if (letter == 'N') { setNavigating(true); return true; }
    if (key.getKeyCode() == '0') { resetView(); return true; }

    return false;
}

juce::String MotionCompositionView::emptyMessage() const {
    const auto& project = processor.document.project();
    const auto now = processor.position.load();
    std::optional<double> next;
    bool any = false, hidden = false;
    const auto soloing = std::any_of(project.tracks.begin(), project.tracks.end(), [](const auto& track) { return track.solo; });
    for (const auto& track : project.tracks) {
        if (track.kind == motion::TrackKind::audio) { continue; }
        const auto silenced = track.muted || (soloing && !track.solo);
        for (const auto& clip : track.clips) {
            any = true;
            const auto timing = clip.timing(project.tempo());
            if (silenced && timing.start <= now && now < timing.end()) { hidden = true; }
            if (!silenced && timing.start > now && (!next.has_value() || timing.start < *next)) { next = timing.start; }
        }
    }
    if (!any) { return "Drop files here, or choose Add source"; }
    if (hidden) { return "Clips here are on muted or un-soloed tracks"; }
    const auto grid = project.timeGrid();
    const auto at = "Nothing on screen at " + juce::String(grid.positionLabel(now));
    return next.has_value() ? at + motion::style::dot() + "next clip at " + juce::String(grid.positionLabel(*next)) : at;
}

double MotionCompositionView::editingTime() const {
    if (pathKey.has_value() && (pathKey->selection != selected || pathKey->generation != processor.document.generation()
        || pathKey->scope != processor.document.editingComposition() || pathKey->seekSerial != processor.seekRevision() || pathKey->revision != processor.document.revision() || processor.playing.load())) {
        pathKey.reset();
    }
    return pathKey.has_value() ? pathKey->time : processor.position.load();
}

bool MotionCompositionView::atSelectedPathEnd(double time) const {
    if (!pathKey.has_value() || pathKey->selection != selected || pathKey->time != time) { return false; }
    for (const auto& track : processor.document.project().tracks) {
        for (const auto& clip : track.clips) {
            if (clip.id == selected) { return clip.timing(processor.document.project().tempo()).end() == time; }
        }
    }
    return false;
}

void MotionCompositionView::updateMotionPath() {
    if (!pathDirty && pathSelection == selected && pathRevision == processor.document.revision()) { return; }
    motionPath = motion::editor::buildMotionPath(processor.document.project(), selected);
    pathDirty = false;
    pathSelection = selected;
    pathRevision = processor.document.revision();
}

void MotionCompositionView::paintMotionPath(juce::Graphics& g) {
    if (!showMotionPath || navigating || selected == 0) { return; }
    updateMotionPath();
    const auto colour = motion::style::motionPath();
    const auto view = camera.view();
    for (std::size_t index = 0; index < motionPath.points.size(); ++index) {
        const auto& point = motionPath.points[index];
        if (index > 0 && !point.breakBefore) {
            const auto line = screenLine(view, motionPath.points[index - 1].position, point.position);
            if (line.has_value()) {
                g.setColour(colour.withAlpha(0.35f));
                g.drawLine(*line, 1);
            }
        }
        const auto screen = screenPoint(point.position);
        if (screen.has_value() && point.dot) {
            g.setColour(colour.withAlpha(0.65f));
            g.fillEllipse(screen->x - 1.5f, screen->y - 1.5f, 3, 3);
        }
    }
    for (const auto& point : motionPath.points) {
        if (!point.key) { continue; }
        const auto screen = screenPoint(point.position);
        if (!screen.has_value()) { continue; }
        const juce::Rectangle<float> bounds(screen->x - 4, screen->y - 4, 8, 8);
        g.setColour(osci::Colours::veryDark());
        g.fillRect(bounds);
        g.setColour(colour);
        g.drawRect(bounds, 1.5f);
    }
    g.setFont(motion::style::caption());
    g.setColour(osci::Colours::textMuted());
    g.drawFittedText(motionPath.tooComplex ? "Path hidden: more than 2,048 transform keys"
        : "Position path (before effects)" + motion::style::dot() + "Click a key to seek", getLocalBounds().removeFromTop(26).withTrimmedLeft(48).withTrimmedRight(96).reduced(10, 0), juce::Justification::centredLeft, 2);
}

bool MotionCompositionView::seekMotionKey(juce::Point<float> position) {
    updateMotionPath();
    const motion::editor::MotionPathPoint* closest = nullptr;
    float distance = 9;
    for (const auto& point : motionPath.points) {
        if (!point.key || point.time == editingTime()) { continue; }
        const auto screen = screenPoint(point.position);
        if (screen.has_value() && screen->getDistanceFrom(position) < distance) {
            closest = &point;
            distance = screen->getDistanceFrom(position);
        }
    }
    if (closest == nullptr) { return false; }
    const auto time = closest->time;
    const auto rate = std::max(1.0, processor.getSampleRate());
    const auto atEnd = !motionPath.points.empty() && time == motionPath.points.back().time;
    processor.playing.store(false);
    processor.seek(atEnd ? std::max(motionPath.points.front().time, time - 1.0 / rate) : time);
    pathKey = PathKey {selected, processor.document.generation(), processor.document.editingComposition(), processor.seekRevision(), processor.document.revision(), time, closest->contentTime};
    repaint();
    return true;
}

bool MotionCompositionView::editable(double time) const {
    const auto& project = processor.document.project();
    if (motion::findGroup(project, selected) != nullptr) {
        // A group moves while anything it holds is shown and unlocked.
        return std::any_of(project.tracks.begin(), project.tracks.end(), [&](const auto& track) {
            return track.kind == motion::TrackKind::visual && !track.locked && motion::groupHolds(project, selected, track.group) && motion::trackIsAudible(project, track);
        });
    }
    const auto* track = motion::findClipTrack(project, selected);
    if (track == nullptr || track->locked || !motion::trackIsAudible(project, *track)) { return false; }
    return motion::findClip(project, selected)->contains(time, project.tempo()) || atSelectedPathEnd(time);
}

MotionCompositionGizmo MotionCompositionView::currentGizmo() const {
    const auto time = editingTime();
    gizmoFrame = motion::editor::gizmoFrameFor(processor.document.project(), selected, time);
    // Effects after the transform (track, group or composition) can move what
    // is drawn away from the handles; as in After Effects, the handles still
    // show and edit the object's own transform.
    if (navigating || !editable(time) || !gizmoFrame.has_value()) { return {}; }
    return MotionCompositionGizmo::layout(*gizmoFrame, camera, tool, std::clamp(getWidth() * 0.2, 35.0, 72.0), outputFrame().getHeight(),
        [this](motion::Vec3 point) { return screenPoint(point); });
}

bool MotionCompositionView::beginGesture(double time) {
    for (const auto& track : processor.document.project().tracks) {
        if (track.locked && std::any_of(track.clips.begin(), track.clips.end(), [this](const auto& clip) { return clip.id == selected; })) {
            dragHint = "This object's track is locked";
            return false;
        }
    }
    gizmoAtDown = motion::editor::gizmoFrameFor(processor.document.project(), selected, time);
    if (!editable(time) || !gizmoAtDown.has_value()) {
        dragHint = "Edit this object's transform in Properties";
        return false;
    }
    // A plain click only selects: the document is touched only once the
    // pointer actually drags.
    editTime = time;
    editSelection = selected;
    companions.clear();
    if (tool != MotionTransformTool::anchor && selectedObjects) {
        const auto& project = processor.document.project();
        for (const auto id : selectedObjects()) {
            if (id == selected) { continue; }
            const auto* track = motion::findClipTrack(project, id);
            const auto* clip = motion::findClip(project, id);
            if (track == nullptr || clip == nullptr || track->locked || !clip->contains(time, project.tempo())) { continue; }
            const auto frame = motion::editor::gizmoFrameFor(project, id, time);
            if (frame.has_value()) { companions.emplace_back(id, *frame); }
        }
    }
    edit.begin();
    dragStarted = false;
    return true;
}

void MotionCompositionView::timerCallback() {
    if (!navigating || !hasKeyboardFocus(true) || !isShowing() || !juce::Process::isForegroundProcess()) {
        setNavigating(false);
        return;
    }
    const auto now = juce::Time::getMillisecondCounterHiRes();
    const auto seconds = std::clamp((now - lastTick) / 1000.0, 0.0, 0.05);
    lastTick = now;
    // By position, as games do: W A S D (Z Q S D on AZERTY), Q and E; or the arrows.
    using Key = motion::ui::KeyPosition;
    const auto down = [](Key key, int arrow = 0) { return motion::ui::isKeyPositionDown(key) || (arrow != 0 && juce::KeyPress::isKeyCurrentlyDown(arrow)); };
    const motion::Vec3 direction { static_cast<double>(down(Key::d, juce::KeyPress::rightKey) - down(Key::a, juce::KeyPress::leftKey)),
        static_cast<double>(down(Key::e) - down(Key::q)), static_cast<double>(down(Key::w, juce::KeyPress::upKey) - down(Key::s, juce::KeyPress::downKey)) };
    const auto modifiers = juce::ModifierKeys::getCurrentModifiersRealtime();
    if (modifiers.isCommandDown() || modifiers.isCtrlDown() || modifiers.isAltDown()) { return; }
    const auto fast = modifiers.isShiftDown();
    camera.fly(direction, std::clamp(camera.distance() * 0.5, 0.01, 1e6) * (fast ? 3 : 1), seconds);
    repaint();
}

void MotionCompositionView::releaseCursor() {
    setMouseCursor(juce::MouseCursor::NormalCursor);
    juce::Desktop::getInstance().getMainMouseSource().setScreenPosition(savedCursor);
}

void MotionCompositionView::cancelGesture() {
    handleDrag.reset();
    if (validGesture()) {
        edit.cancel();
        if (pathKey.has_value()) { pathKey->revision = processor.document.revision(); }
    }
    if (navigationDrag && lockedCamera != 0) { processor.document.endHeld(); }
    navigationDrag = false;
}

bool MotionCompositionView::validGesture() {
    if (edit.stale()) { edit.reset(); }
    return edit.active();
}

juce::Rectangle<float> MotionCompositionView::outputFrame() const {
    const auto size = std::max(10, std::min(getWidth(), getHeight()) - 48);
    return getLocalBounds().toFloat().withSizeKeepingCentre(size, size);
}

motion::editor::Vec2 MotionCompositionView::normalized(juce::Point<float> point) const {
    const auto frame = outputFrame();
    return { (point.x - frame.getCentreX()) * 2 / frame.getWidth(), (frame.getCentreY() - point.y) * 2 / frame.getHeight() };
}

motion::Vec3 MotionCompositionView::worldPoint(osci::Point point, double time) const {
    if (prepared != nullptr) { point = prepared->applyCompositionEffects(point, time); }
    return { point.x, point.y, point.z };
}

std::optional<juce::Point<float>> MotionCompositionView::screenPoint(const CameraView& view, motion::Vec3 point) const {
    if (!view.has_value()) { return std::nullopt; }
    const auto projected = view->project(point);
    if (!projected.has_value() || std::abs(projected->x) > 1000 || std::abs(projected->y) > 1000) { return std::nullopt; }
    const auto frame = outputFrame();
    return juce::Point<float> { static_cast<float>(frame.getCentreX() + projected->x * frame.getWidth() / 2),
        static_cast<float>(frame.getCentreY() - projected->y * frame.getHeight() / 2) };
}

void MotionCompositionView::drawWorldLine(juce::Graphics& g, motion::Vec3 start, motion::Vec3 end) const {
    const auto line = screenLine(camera.view(), start, end);
    if (line.has_value()) { g.drawLine(*line, 1); }
}

std::optional<juce::Line<float>> MotionCompositionView::screenLine(const CameraView& view, motion::Vec3 start, motion::Vec3 end) const {
    if (!view.has_value()) { return std::nullopt; }
    const auto segment = view->projectSegment(view->toEye(start), view->toEye(end));
    if (!segment.has_value()) { return std::nullopt; }
    const auto frame = outputFrame();
    const auto toScreen = [&frame](motion::editor::Vec2 point) {
        return juce::Point<float>(static_cast<float>(frame.getCentreX() + point.x * frame.getWidth() / 2), static_cast<float>(frame.getCentreY() - point.y * frame.getHeight() / 2));
    };
    return juce::Line<float>(toScreen(segment->first), toScreen(segment->second));
}

bool MotionCompositionView::level(const motion::Curve& curve) {
    const auto& keys = curve.keyframes();
    return std::abs(curve.base) < 1.0e-9 && std::all_of(keys.begin(), keys.end(), [](const auto& key) {
        const auto flat = key.interpolation != motion::Interpolation::cubic || (std::abs(key.incomingSlope) < 1.0e-9 && std::abs(key.outgoingSlope) < 1.0e-9);
        return std::abs(key.value) < 1.0e-9 && flat;
    });
}

std::optional<MotionCompositionView::Pose> MotionCompositionView::documentPose(motion::Id id) const {
    for (const auto& item : processor.document.project().cameras) {
        if (item.id != id) { continue; }
        // Rigged, rolled, linked or modulated cameras keep their own aim.
        // Keys on a level Z rotation (keying the whole camera) are fine.
        if (driveBlocker(id).isNotEmpty()) { return std::nullopt; }
        const auto value = [&](const char* name, double fallback) {
            const auto found = item.properties.find(name);
            return found != item.properties.end() ? found->second.evaluateBase(cameraTime()) : fallback;
        };
        return Pose {value("position.x", 0), value("position.y", 0), value("position.z", 4), value("rotation.x", 0), -value("rotation.y", 0), value("fov", motion::defaultCameraFieldOfView)};
    }
    return std::nullopt;
}

void MotionCompositionView::syncCamera() {
    if (lockedCamera == 0) { return; }
    const auto view = viewPose();
    // The view drives the camera only while the transport is stopped.
    if (lastPose.has_value() && !(view == *lastPose) && !processor.playing.load() && documentPose(lockedCamera).has_value()) {
        const auto id = lockedCamera;
        const auto time = cameraTime();
        processor.document.editCoalesced("Move camera", "camera-view:" + juce::String(static_cast<juce::int64>(id)), [id, time, view](motion::Project& project) {
            for (auto& item : project.cameras) {
                if (item.id != id) { continue; }
                // A camera with keys is keyed as a whole at the playhead, so
                // its position, rotation and lens keys stay together. One
                // without keys just takes the new pose.
                const auto keyed = std::any_of(item.properties.begin(), item.properties.end(), [](const auto& entry) { return entry.second.animated(); });
                // The view wraps its yaw; keep the camera's turn continuous.
                const auto turn = item.properties["rotation.y"].evaluateBase(time);
                const auto yaw = turn + std::remainder(-view.yaw - turn, 360.0);
                for (const auto& [name, value] : std::initializer_list<std::pair<const char*, double>> {{"position.x", view.x}, {"position.y", view.y}, {"position.z", view.z}, {"rotation.x", view.pitch}, {"rotation.y", yaw}}) {
                    auto& curve = item.properties[name];
                    if (keyed) { curve.setKeyValue(time, value); } else { curve.base = value; }
                }
                if (!keyed) { continue; }
                // The view has no roll; the lens stays as it is.
                item.properties["rotation.z"].setKeyValue(time, 0.0);
                auto& lens = item.properties["fov"];
                lens.setKeyValue(time, lens.evaluateBase(time));
            }
        });
        lastPose = view;
        return;
    }
    const auto pose = documentPose(lockedCamera);
    if (!pose.has_value()) {
        setDrivenCamera(0);
        return;
    }
    if (lastPose.has_value() && *pose == *lastPose) { return; }
    const auto distance = std::clamp(camera.distance() > 0 ? camera.distance() : 4.0, motion::editor::Camera::minimumDistance, motion::editor::Camera::maximumDistance);
    camera.position = {pose->x, pose->y, pose->z};
    camera.pitch = std::clamp(pose->pitch * std::numbers::pi / 180, -motion::editor::Camera::pitchLimit, motion::editor::Camera::pitchLimit);
    camera.yaw = pose->yaw * std::numbers::pi / 180;
    camera.fovDegrees = std::clamp(pose->fov, 1.0, 150.0);
    camera.pivot = camera.position + camera.forward() * distance;
    lastPose = viewPose();
    repaint();
}

std::optional<motion::ui::generator_handles::Context> MotionCompositionView::handleContext() const {
    if (navigating || partMode || tool == MotionTransformTool::anchor || selected == 0) { return std::nullopt; }
    const auto time = editingTime();
    if (!editable(time)) { return std::nullopt; }
    return motion::ui::generator_handles::context(processor.document.project(), selected, time);
}

std::vector<MotionCompositionView::ShownHandle> MotionCompositionView::shownHandles(const motion::ui::generator_handles::Context& context) const {
    std::vector<ShownHandle> result;
    const auto view = camera.view();
    const auto list = motion::generator::handles(*context.definition, context.values);
    for (std::size_t index = 0; index < list.size(); ++index) {
        const auto at = screenPoint(view, context.world(list[index].at));
        if (at.has_value() && getLocalBounds().toFloat().expanded(8).contains(*at)) { result.push_back({index, list[index], *at}); }
    }
    return result;
}

int MotionCompositionView::handleAt(juce::Point<float> position) const {
    const auto context = handleContext();
    if (!context.has_value()) { return -1; }
    // The gizmo's middle stays the gizmo's, whatever handle is under it.
    const auto gizmo = currentGizmo();
    if (gizmo.visible && gizmo.origin.getDistanceFrom(position) <= 10) { return -1; }
    int nearest = -1;
    auto distance = 8.0f;
    for (const auto& shown : shownHandles(*context)) {
        const auto away = shown.at.getDistanceFrom(position);
        if (away <= distance) {
            distance = away;
            nearest = static_cast<int>(shown.index);
        }
    }
    return nearest;
}

void MotionCompositionView::paintGeneratorHandles(juce::Graphics& g) const {
    auto context = handleContext();
    if (!context.has_value()) { return; }
    // Mid-drag, where the drag has them.
    if (handleDrag.has_value()) { context->values = handleDrag->values; }
    const auto shown = shownHandles(*context);
    const auto lit = handleDrag.has_value() ? static_cast<int>(handleDrag->index) : hoverGeneratorHandle;
    for (const auto& item : shown) { motion::ui::generator_handles::paint(g, item.at, item.handle, static_cast<int>(item.index) == lit); }
    for (const auto& item : shown) {
        if (static_cast<int>(item.index) == lit) { motion::ui::generator_handles::paintHint(g, item.at, motion::ui::generator_handles::hint(*context, item.handle), getLocalBounds().toFloat(), getLocalBounds().toFloat().removeFromTop(96).removeFromRight(96)); }
    }
}

void MotionCompositionView::dragHandle(const juce::MouseEvent& event) {
    auto& drag = *handleDrag;
    const auto& definition = *drag.context.definition;
    std::vector<double> next;
    if (drag.handle.kind == motion::generator::Handle::Kind::count) {
        // A step for every dozen pixels sideways.
        next = motion::generator::step(definition, *drag.handle.count, drag.context.values, juce::roundToInt((event.position.x - down.x) / 12.0f));
    } else {
        std::optional<motion::Vec3> to;
        if (drag.handle.axis.has_value()) {
            // Along its axis, as a gizmo's axis handle drags.
            const auto direction = drag.context.worldDirection(*drag.handle.axis);
            const auto length = direction.length();
            if (!(length > 0)) { return; }
            const auto distance = motion::editor::gizmo::axisDragDistance(cameraAtDown, normalized(down), normalized(event.position), drag.context.world(drag.handle.at), direction * (1 / length));
            if (distance.has_value()) { to = drag.handle.at + *drag.handle.axis * (*distance / length); }
        } else {
            // Across the drawing's own plane, where the pointer meets it.
            const auto ray = cameraAtDown.ray(normalized(event.position));
            const auto origin = ray.has_value() ? drag.context.local(ray->origin) : std::nullopt;
            const auto direction = ray.has_value() ? drag.context.localDirection(ray->direction) : std::nullopt;
            if (origin.has_value() && direction.has_value() && std::abs(direction->z) > 1.0e-9) {
                const auto t = -origin->z / direction->z;
                if (t > 0) { to = *origin + *direction * t; }
            }
        }
        if (!to.has_value()) { return; }
        next = motion::generator::drag(definition, drag.index, drag.values, drag.previous, *to, event.mods.isShiftDown());
        drag.previous = *to;
    }
    drag.values = next;
    auto project = edit.start();
    const auto changed = motion::ui::generator_handles::write(project, drag.clip, drag.context, next);
    edit.show(std::move(project), changed);
    repaint();
}

bool MotionCompositionView::stepHandleCount(float delta) {
    const auto context = handleContext();
    if (!context.has_value()) { return false; }
    const auto shown = shownHandles(*context);
    const auto found = std::find_if(shown.begin(), shown.end(), [this](const auto& item) { return static_cast<int>(item.index) == hoverGeneratorHandle; });
    if (found == shown.end() || !found->handle.count.has_value()) { return false; }
    // A notch of a wheel is a step; a trackpad's glide adds up to one.
    wheelSteps += delta * 8;
    const auto steps = static_cast<int>(wheelSteps);
    if (steps == 0) { return true; }
    wheelSteps -= static_cast<float>(steps);
    const auto parameter = *found->handle.count;
    const auto next = motion::generator::step(*context->definition, parameter, context->values, steps);
    const auto& spec = context->definition->parameters[parameter];
    const auto id = selected;
    // Notches in a row are one undo step.
    const auto changed = processor.document.tryEditCoalesced("Change " + juce::String(spec.label.data(), spec.label.size()), "handle:" + juce::String(id) + ":" + juce::String(spec.id.data(), spec.id.size()),
        [&](motion::Project& project) { return motion::ui::generator_handles::write(project, id, *context, next); });
    if (changed && onPropertyEdited) { onPropertyEdited(id, std::string(spec.id)); }
    repaint();
    return true;
}
