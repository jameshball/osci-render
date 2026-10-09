#include "CompositionView.h"
#include "../model/KeyEdit.h"
#include "../model/SourceParts.h"
#include "../model/AnchorEdit.h"

MotionCompositionView::MotionCompositionView(MotionProcessor& processor) : processor(processor) {
    setName("Composition preview");
    setWantsKeyboardFocus(true);
    cameraSync.tick = [this] { syncCamera(); };
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
}

MotionCompositionView::~MotionCompositionView() {
    cameraSync.stopTimer();
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
    if (found) { camera.frame((minimum + maximum) * 0.5, std::max(0.05, (maximum - minimum).length() * 0.5)); }
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

bool MotionCompositionView::cameraKeyed(motion::Id id) const {
    const auto& cameras = processor.document.project().cameras;
    const auto found = std::find_if(cameras.begin(), cameras.end(), [id](const auto& item) { return item.id == id; });
    if (found == cameras.end()) { return false; }
    const auto time = cameraTime();
    return std::all_of(motion::cameraPropertySpecs.begin(), motion::cameraPropertySpecs.end(), [&](const auto& spec) {
        const auto curve = found->properties.find(spec.id);
        return curve != found->properties.end() && (curve->second.link.has_value() || curve->second.hasKeyAt(time));
    });
}

void MotionCompositionView::toggleCameraKey(motion::Id id) {
    const auto& cameras = processor.document.project().cameras;
    if (std::none_of(cameras.begin(), cameras.end(), [id](const auto& item) { return item.id == id; })) { return; }
    const auto time = cameraTime();
    const auto keyed = cameraKeyed(id);
    processor.document.edit(keyed ? "Remove camera key" : "Key camera", [id, time, keyed](motion::Project& project) {
        for (auto& item : project.cameras) {
            if (item.id != id) { continue; }
            for (const auto& spec : motion::cameraPropertySpecs) {
                auto& curve = item.properties[std::string(spec.id)];
                // Without keys the camera keeps the pose it had here.
                if (!curve.link.has_value()) { motion::keyedit::setKeyed(curve, time, !keyed); }
            }
        }
    });
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
    pathDirty = true;
    prepared = std::make_unique<motion::PreparedComposition>(project, 48000, nullptr, motion::CompositionPurpose::editorGeometry);
    ++preparedSerial;
    prunePicked();
    repaint();
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
        g.setColour(osci::Colours::text().withAlpha(0.5f));
        g.setFont(motion::style::body());
        // Clear of the tool strip on the left; wraps in a narrow Scene.
        g.drawFittedText(prepared == nullptr ? juce::String("Preparing...") : emptyMessage(), getLocalBounds().withTrimmedLeft(48).reduced(12, 0), juce::Justification::centred, 3);
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
            const auto current = id == selected || (isSelected && isSelected(id));
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
            lines.add(piece.line, pickable ? osci::Colours::text().withAlpha(.38f * depthFade(piece.depth)) : piece.colour.withAlpha(alpha));
        }
    }
    lines.stroke(g, 1.0f);
    highlightedLines.stroke(g, 1.4f);
    hoveredLines.stroke(g, 2.0f);
    paintCameras(g, time);
    paintMotionPath(g);
    if (!partMode) { currentGizmo().paint(g, edit.active() ? dragAxis : hoverHandle); }
    if (marquee.has_value()) {
        const auto region = marqueeRegion();
        g.setColour(osci::Colours::accentColor().withAlpha(.08f));
        g.fillPath(region);
        g.setColour(osci::Colours::accentColor().withAlpha(.7f));
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

const std::vector<MotionCompositionView::ScreenPiece>& MotionCompositionView::screenPieces() const {
    auto live = processor.liveSourcePreview();
    const auto time = editingTime();
    const PiecesKey key {preparedSerial, live.get(), time, camera.position.x, camera.position.y, camera.position.z, camera.yaw, camera.pitch, camera.fovDegrees, selected, getWidth(), getHeight()};
    if (pieceKey == key) { return pieceCache; }
    pieceKey = key;
    // Held so a later set of live frames cannot reuse this one's address.
    pieceLive = std::move(live);
    pieceCache.clear();
    if (prepared == nullptr) { return pieceCache; }
    const auto view = camera.view();
    for (const auto& clip : prepared->clips) {
        const auto sampleTime = clipTime(clip, time);
        if (!clip.active(sampleTime)) { continue; }
        forEachPiece(clip, sampleTime, view, pieceLive.get(), [&](const Piece& piece) {
            const auto line = screenLine(view, piece.a, piece.b);
            if (line.has_value()) { pieceCache.push_back({*line, piece.a, piece.b, piece.colour, &clip, piece.shape, static_cast<float>(view->toEye((piece.a + piece.b) * .5).z)}); }
        });
    }
    return pieceCache;
}

float MotionCompositionView::depthFade(double depth) const {
    const auto pivot = std::max(1.0e-6, camera.distance());
    return static_cast<float>(std::clamp(1.0 - (depth - pivot) / pivot, .4, 1.0));
}

double MotionCompositionView::clipTime(const motion::PreparedClip& clip, double time) const {
    return clip.editorId() == selected && atSelectedPathEnd(time) ? std::nextafter(time, 0.0) : time;
}

bool MotionCompositionView::warps(const motion::PreparedClip& clip) const {
    const auto stage = [](const motion::PreparedClipStage& item) {
        return !item.effects.empty() || !item.trackEffects.empty() || !item.compositionEffects.empty()
            || std::any_of(item.groups.begin(), item.groups.end(), [](const auto& group) { return !group.effects.empty(); });
    };
    return (prepared != nullptr && prepared->hasCompositionEffects()) || stage(clip) || std::any_of(clip.ancestors.begin(), clip.ancestors.end(), stage);
}

void MotionCompositionView::forEachPiece(const motion::PreparedClip& clip, double time, const CameraView& view, const motion::LiveSourceFrames* live, const std::function<void(const Piece&)>& visit) const {
    const auto* source = clip.resolveSource(live);
    if (source == nullptr || !view.has_value()) { return; }
    const auto lit = [](const osci::Point& point) { return point.r != 0 || point.g != 0 || point.b != 0; };
    const auto colourOf = [](const osci::Point& point) { return juce::Colour::fromFloatRGBA(point.r, point.g, point.b, 1.0f); };
    const auto* drawing = source->drawingAt(source->frameIndex(clip.localTime(time)));
    if (drawing == nullptr) {
        // Traced sources walk their stored points at native density, so
        // short lit runs stay visible. Output uses its audio rate.
        const auto count = source->previewSampleCount();
        const auto span = source->previewPhaseSpan();
        auto previous = clip.sample(time, 0, span, 0, live);
        for (std::size_t index = 1; index <= count; ++index) {
            const auto next = clip.sample(time, static_cast<double>(index) / count, span, 0, live);
            if (lit(previous) && lit(next)) { visit({worldPoint(previous, time), worldPoint(next, time), colourOf(next), -1}); }
            previous = next;
        }
        return;
    }
    const auto count = drawing->shapeCount();
    const auto bent = warps(clip);
    // Big sources take fewer steps per curve, so the Scene stays quick.
    const auto mostSteps = static_cast<int>(std::clamp<std::size_t>(60000 / std::max<std::size_t>(1, count), 1, 48));
    const auto pixels = outputFrame().getHeight() / 2.0;
    const auto place = [&](osci::Shape& shape, float progress) {
        const auto point = clip.processPoint(shape.nextVector(progress), time);
        return std::pair {point, worldPoint(point, time)};
    };
    for (std::size_t index = 0; index < count; ++index) {
        auto* shape = drawing->shape(index);
        // Shapes of no length never reach the output, so they are not drawn.
        if (shape == nullptr || !(shape->length() > 0)) { continue; }
        const auto straight = dynamic_cast<osci::Line*>(shape) != nullptr;
        const auto start = place(*shape, 0), end = place(*shape, 1);
        int steps = 1;
        // About one step per 4 px of curve on screen; effects can bend
        // straight lines too.
        if (bent || !straight) {
            const auto least = straight ? 1 : std::min(4, mostSteps);
            steps = mostSteps;
            if (mostSteps > least) {
                const auto first = view->project(start.second), middle = view->project(place(*shape, .5f).second), last = view->project(end.second);
                if (first.has_value() && middle.has_value() && last.has_value()) {
                    const auto span = (std::hypot(middle->x - first->x, middle->y - first->y) + std::hypot(last->x - middle->x, last->y - middle->y)) * pixels;
                    if (std::isfinite(span)) { steps = static_cast<int>(std::clamp(std::ceil(span / 4), static_cast<double>(least), static_cast<double>(mostSteps))); }
                }
            }
        }
        auto previous = start;
        for (int step = 1; step <= steps; ++step) {
            const auto next = step == steps ? end : place(*shape, static_cast<float>(step) / steps);
            if (lit(previous.first) && lit(next.first)) { visit({previous.second, next.second, colourOf(next.first), static_cast<int>(index)}); }
            previous = next;
        }
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
        g.setColour(juce::Colours::white.withAlpha(.05f));
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
    if (navigationDrag) { return; }
    if (event.mods.isPopupMenu()) {
        // Like a left click, the menu acts on what is under the pointer.
        if (prepared != nullptr) {
            const auto hit = pickAt(event.position, nullptr);
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
    const auto hit = pickAt(event.position, &dragAnchor);
    // Empty space starts a marquee; Shift adds to the selection.
    if (hit == 0) {
        marqueeArmed = true;
        marqueeAdds = adding;
        if (adding) { return; }
    } else if (adding) {
        if (onSelectClips) { onSelectClips({hit}, SelectionChange::toggle); }
        return;
    }
    selected = hit;
    if (onSelection) {
        onSelection(hit);
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
    if (!validGesture() || !gizmoAtDown.has_value()) { return; }
    if (!dragStarted) {
        if (event.getDistanceFromDragStart() < 3) { return; }
        // Playback carries on: the drag edits at the time it began.
        dragStarted = true;
    }
    std::array<double, 3> offsets { 0, 0, 0 };
    double scaleFactor = 1;
    if (gesture == Gesture::plane || gesture == Gesture::moveAxis) {
        std::optional<motion::Vec3> worldDelta;
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
    const auto target = motion::findPropertyTarget(project, editSelection);
    if (!target.has_value()) { return; }
    bool anyChange = false;
    for (int axis = 0; axis < 3; ++axis) {
        const auto scaling = gesture == Gesture::uniformScale || gesture == Gesture::scaleAxis;
        if (scaling && gesture == Gesture::scaleAxis && axis != dragAxis) { continue; }
        const auto prefix = scaling ? "scale." : gesture == Gesture::rotateAxis ? "rotation." : "position.";
        const auto property = std::string(prefix) + "xyz"[axis];
        auto* curve = target->curve(property);
        if (curve == nullptr) { continue; }
        const auto localTime = pathKey.has_value() && pathKey->selection == editSelection ? pathKey->contentTime : target->localTime(editTime);
        const auto base = curve->evaluateBase(localTime);
        // A zero scale can be recovered by dragging; negative scales keep their sign.
        const auto value = scaling ? (base == 0 ? scaleFactor - 1 : base * scaleFactor) : base + offsets[axis];
        if (!std::isfinite(value)) { return; }
        if (std::abs(value - base) <= 1e-10) { continue; }
        if (curve->animated()) {
            curve->setKeyValue(localTime, value);
        } else {
            curve->base = value;
        }
        if (!anyChange) { editedProperty = property; }
        anyChange = true;
    }
    // Position, rotation and scale key as one property, as in After
    // Effects: when any axis is animated, every axis gets this key.
    if (anyChange) {
        const auto scaling = gesture == Gesture::uniformScale || gesture == Gesture::scaleAxis;
        const std::string prefix = scaling ? "scale." : gesture == Gesture::rotateAxis ? "rotation." : "position.";
        const auto localTime = pathKey.has_value() && pathKey->selection == editSelection ? pathKey->contentTime : target->localTime(editTime);
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
    edit.show(std::move(project), anyChange);
    if (pathKey.has_value()) { pathKey->revision = processor.document.revision(); }
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
    if (!pose.has_value()) { return "This object's transform cannot be edited here"; }
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

void MotionCompositionView::mouseUp(const juce::MouseEvent&) {
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
    if (validGesture()) {
        const auto label = tool == MotionTransformTool::move ? "Move object" : tool == MotionTransformTool::rotate ? "Rotate object" : tool == MotionTransformTool::anchor ? "Move anchor" : "Scale object";
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

std::vector<motion::Id> MotionCompositionView::clipsIn(juce::Rectangle<float> area) const {
    std::vector<motion::Id> ids;
    for (const auto& piece : screenPieces()) {
        const auto id = piece.clip->editorId();
        if (std::find(ids.begin(), ids.end(), id) == ids.end() && area.intersects(piece.line)) { ids.push_back(id); }
    }
    return ids;
}

const motion::PreparedDrawing* MotionCompositionView::partDrawing(const motion::PreparedClip& clip) const {
    if (!clip.ancestors.empty() || clip.liveIdentity != nullptr || clip.source == nullptr || clip.source->frameCount() != 1) { return nullptr; }
    return clip.source->drawingAt(0);
}

juce::String MotionCompositionView::partBlocker(motion::Id id) const {
    const auto& project = processor.document.project();
    const auto* clip = motion::findClip(project, id);
    if (clip == nullptr || clip->composition != 0) { return "Compositions are picked whole"; }
    const auto asset = motion::findAsset(project.assets, clip->asset);
    return asset != nullptr ? motion::parts::unavailableReason(*asset) : juce::String();
}

std::optional<MotionCompositionView::PartHit> MotionCompositionView::pickPart(juce::Point<float> position) const {
    std::optional<PartHit> hit;
    float nearest = 8;
    for (const auto& piece : screenPieces()) {
        if (piece.shape < 0 || partDrawing(*piece.clip) == nullptr) { continue; }
        juce::Point<float> closest;
        const auto distance = piece.line.getDistanceFromPoint(position, closest);
        if (distance < nearest) {
            nearest = distance;
            hit = PartHit {piece.clip->editorId(), static_cast<std::size_t>(piece.shape)};
        }
    }
    return hit;
}

// A shape is picked when the area crosses any of it on screen; picking
// pieces takes every piece the area crosses.
std::map<motion::Id, MotionCompositionView::Picked> MotionCompositionView::partsIn(const juce::Path& area, bool touching) const {
    std::map<motion::Id, Picked> found;
    if (prepared == nullptr) { return found; }
    const auto bounds = area.getBounds();
    juce::Path box;
    box.addRectangle(bounds);
    const auto rectangular = area == box;
    std::map<const motion::PreparedClip*, std::vector<signed char>> inside;
    for (const auto& piece : screenPieces()) {
        const auto* drawing = piece.shape >= 0 ? partDrawing(*piece.clip) : nullptr;
        if (drawing == nullptr) { continue; }
        auto& states = inside[piece.clip];
        states.resize(drawing->shapeCount(), -1);
        auto& state = states[static_cast<std::size_t>(piece.shape)];
        // Anything the area crosses is picked, as the object marquee picks.
        const auto crossed = rectangular ? bounds.intersects(piece.line)
            : area.contains(piece.line.getStart()) || area.contains(piece.line.getEnd()) || area.contains(piece.line.getPointAlongLineProportionally(.5f));
        state = static_cast<signed char>(state == 1 || crossed ? 1 : 0);
    }
    for (const auto& [clip, states] : inside) {
        const auto paths = touching ? motion::parts::pathIndices(*partDrawing(*clip)) : std::vector<std::size_t>();
        std::set<std::size_t> touchedPaths;
        for (std::size_t shape = 0; shape < states.size(); ++shape) {
            if (states[shape] == 1 && touching) { touchedPaths.insert(paths[shape]); }
        }
        for (std::size_t shape = 0; shape < states.size(); ++shape) {
            if (touching ? !touchedPaths.contains(paths[shape]) : states[shape] != 1) { continue; }
            auto& entry = found[clip->editorId()];
            entry.source = clip->source;
            entry.shapes.insert(shape);
        }
    }
    return found;
}

void MotionCompositionView::pickParts(const juce::Path& area, bool add, bool touching) {
    if (prepared == nullptr) { return; }
    if (!add) { picked.clear(); }
    for (const auto& [id, entry] : partsIn(area, touching)) {
        auto& merged = picked[id];
        merged.source = entry.source;
        merged.shapes.insert(entry.shapes.begin(), entry.shapes.end());
    }
    refreshPartBar();
    repaint();
}

// The area being dragged, as it would pick: a box, or the lasso closed.
juce::Path MotionCompositionView::marqueeRegion() const {
    juce::Path region;
    if (partMode && partPick == PartPick::lasso) {
        region = lasso;
        region.closeSubPath();
    } else if (marquee.has_value()) {
        region.addRectangle(*marquee);
    }
    return region;
}

// Every shape of every object whose parts can be picked, on screen or not.
void MotionCompositionView::pickAllParts() {
    if (prepared == nullptr) { return; }
    picked.clear();
    const auto time = editingTime();
    for (const auto& clip : prepared->clips) {
        const auto* drawing = partDrawing(clip);
        if (drawing == nullptr || !clip.active(clipTime(clip, time))) { continue; }
        for (std::size_t shape = 0; shape < drawing->shapeCount(); ++shape) {
            auto* item = drawing->shape(shape);
            if (item == nullptr || !(item->length() > 0)) { continue; }
            auto& entry = picked[clip.editorId()];
            entry.source = clip.source;
            entry.shapes.insert(shape);
        }
    }
    refreshPartBar();
    repaint();
}

// What a click on `part` picks: the shape, or with pieces its whole piece.
std::set<std::size_t> MotionCompositionView::pickedBy(PartHit part) const {
    std::set<std::size_t> shapes {part.shape};
    if (partPick != PartPick::pieces || prepared == nullptr) { return shapes; }
    const auto clip = std::find_if(prepared->clips.begin(), prepared->clips.end(), [&](const auto& item) { return item.editorId() == part.clip && partDrawing(item) != nullptr; });
    if (clip == prepared->clips.end()) { return shapes; }
    const auto paths = motion::parts::pathIndices(*partDrawing(*clip));
    for (std::size_t shape = 0; shape < paths.size(); ++shape) {
        if (part.shape < paths.size() && paths[shape] == paths[part.shape]) { shapes.insert(shape); }
    }
    return shapes;
}

// A click picks one shape, or with pieces its whole piece; Shift toggles.
void MotionCompositionView::clickPart(PartHit part, bool adding) {
    const auto clip = std::find_if(prepared->clips.begin(), prepared->clips.end(), [&](const auto& item) { return item.editorId() == part.clip && partDrawing(item) != nullptr; });
    if (clip == prepared->clips.end()) { return; }
    if (part.shape >= partDrawing(*clip)->shapeCount()) { return; }
    const auto shapes = pickedBy(part);
    const auto existing = picked.find(part.clip);
    const auto already = existing != picked.end() && existing->second.shapes.contains(part.shape);
    if (adding && already) {
        for (const auto shape : shapes) { existing->second.shapes.erase(shape); }
        if (existing->second.shapes.empty()) { picked.erase(existing); }
    } else {
        if (!adding && !already) { picked.clear(); }
        auto& entry = picked[part.clip];
        entry.source = clip->source;
        entry.shapes.insert(shapes.begin(), shapes.end());
    }
    refreshPartBar();
}

// The whole path the shape is part of: a letter's outline, a polyline.
void MotionCompositionView::pickPath(PartHit hit, bool add) {
    const auto found = std::find_if(prepared->clips.begin(), prepared->clips.end(), [&](const auto& clip) { return clip.editorId() == hit.clip && partDrawing(clip) != nullptr; });
    if (found == prepared->clips.end()) { return; }
    const auto paths = motion::parts::pathIndices(*partDrawing(*found));
    if (hit.shape >= paths.size()) { return; }
    if (!add) { picked.clear(); }
    auto& entry = picked[hit.clip];
    entry.source = found->source;
    for (std::size_t shape = 0; shape < paths.size(); ++shape) {
        if (paths[shape] == paths[hit.shape]) { entry.shapes.insert(shape); }
    }
    refreshPartBar();
    repaint();
}

// Picks follow their source: a changed or removed source drops them.
void MotionCompositionView::prunePicked() {
    for (auto entry = picked.begin(); entry != picked.end();) {
        const auto still = prepared != nullptr && std::any_of(prepared->clips.begin(), prepared->clips.end(), [&](const auto& clip) {
            return clip.editorId() == entry->first && clip.source == entry->second.source && partDrawing(clip) != nullptr;
        });
        entry = still ? std::next(entry) : picked.erase(entry);
    }
    hoverPart.reset();
    refreshPartBar();
}

std::size_t MotionCompositionView::pickedPieceCount() const {
    std::size_t count = 0;
    if (prepared == nullptr) { return count; }
    for (const auto& [id, entry] : picked) {
        const auto clip = std::find_if(prepared->clips.begin(), prepared->clips.end(), [id = id](const auto& item) { return item.editorId() == id && item.ancestors.empty(); });
        const auto* drawing = clip != prepared->clips.end() ? partDrawing(*clip) : nullptr;
        if (drawing == nullptr) { continue; }
        const auto paths = motion::parts::pathIndices(*drawing);
        std::set<std::size_t> touched;
        for (const auto shape : entry.shapes) {
            if (shape < paths.size()) { touched.insert(paths[shape]); }
        }
        count += touched.size();
    }
    return count;
}

std::size_t MotionCompositionView::pickedPartCount() const {
    std::size_t count = 0;
    for (const auto& [id, entry] : picked) { count += entry.shapes.size(); }
    return count;
}

void MotionCompositionView::setPartMode(bool enabled) {
    if (partMode == enabled) { return; }
    if (enabled && !anythingPickable()) {
        if (onStatus) { onStatus("Text, SVG, OBJ and drawings at the playhead have parts to pick"); }
        // The tool strip shows the mode as it is.
        if (onPartModeChanged) { onPartModeChanged(false); }
        return;
    }
    setNavigating(false);
    cancelGesture();
    partMode = enabled;
    resetMarquee();
    setMouseCursor(enabled ? juce::MouseCursor::CrosshairCursor : juce::MouseCursor::NormalCursor);
    hoverPart.reset();
    hoverShapes.clear();
    hoverHandle = -1;
    if (!enabled) { picked.clear(); }
    refreshPartBar();
    if (onPartModeChanged) { onPartModeChanged(enabled); }
    repaint();
}

void MotionCompositionView::extractPicked() {
    if (picked.empty()) {
        if (onStatus) { onStatus("Pick some parts first"); }
        return;
    }
    if (onExtractParts) { onExtractParts(picked); }
}

bool MotionCompositionView::anythingPickable() const {
    if (prepared == nullptr) { return false; }
    const auto time = editingTime();
    return std::any_of(prepared->clips.begin(), prepared->clips.end(), [&](const auto& clip) { return clip.active(clipTime(clip, time)) && partDrawing(clip) != nullptr; });
}

void MotionCompositionView::resetMarquee() {
    marqueeArmed = false;
    marquee.reset();
    marqueeParts.clear();
    marqueeClips.clear();
    lasso.clear();
    pressedPart.reset();
}

void MotionCompositionView::refreshPartBar() {
    const auto count = pickedPartCount();
    const auto shown = partMode && count > 0;
    // Picking pieces counts pieces, as they were clicked.
    const auto pieces = partPick == PartPick::pieces;
    const auto shownCount = pieces ? pickedPieceCount() : count;
    partCount.setText(juce::String(static_cast<juce::uint64>(shownCount)) + (pieces ? (shownCount == 1 ? " piece" : " pieces") : (shownCount == 1 ? " part" : " parts")), juce::dontSendNotification);
    partCount.setVisible(shown);
    extractButton.setVisible(shown);
    resized();
    repaint();
}

// The part bar: the count, then Extract, 4 px inside a floating panel
// 12 px above the Scene's foot.
juce::Rectangle<int> MotionCompositionView::partBar() const {
    constexpr auto inset = motion::icons::ToolStrip::inset, cell = motion::icons::ToolStrip::cell;
    const auto labelWidth = juce::roundToInt(std::ceil(juce::TextLayout::getStringWidth(motion::style::body(), partCount.getText())));
    const auto width = inset + barTextInset + labelWidth + barTextInset + extractWidth + inset;
    return {(getWidth() - width) / 2, getHeight() - 8 - (cell + 2 * inset), width, cell + 2 * inset};
}

// Laid out as the tool strips are: 3 px in, a 28 px tall button, 8 px from
// the Scene's edge.
void MotionCompositionView::resized() {
    auto bar = partBar().reduced(motion::icons::ToolStrip::inset);
    extractButton.setBounds(bar.removeFromRight(extractWidth));
    partCount.setBounds(bar);
}

void MotionCompositionView::itemDragMove(const SourceDetails& details) {
    const auto type = details.description.toString().fromFirstOccurrenceOf(":", false, false).toStdString();
    const auto hit = prepared != nullptr ? pickAt(details.localPosition.toFloat(), nullptr) : motion::Id(0);
    if (dropHover == hit) { return; }
    dropHover = hit;
    if (onEffectPreview) { onEffectPreview(type, hit); }
    repaint();
}

void MotionCompositionView::itemDragExit(const SourceDetails& details) {
    dropHover.reset();
    if (onEffectPreview) { onEffectPreview(details.description.toString().fromFirstOccurrenceOf(":", false, false).toStdString(), std::nullopt); }
    repaint();
}

void MotionCompositionView::itemDropped(const SourceDetails& details) {
    const auto hit = dropHover.value_or(0);
    dropHover.reset();
    const auto type = details.description.toString().fromFirstOccurrenceOf(":", false, false).toStdString();
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
        g.setColour(osci::Colours::accentColor().withAlpha(everything ? .9f : .3f));
        g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(2.0f), motion::style::panelRadius, everything ? 2.0f : 1.0f);
        if (everything) {
            auto label = getLocalBounds().removeFromBottom(30).withSizeKeepingCentre(110, 22);
            g.setColour(osci::Colours::accentColor().withAlpha(.85f));
            g.fillRoundedRectangle(label.toFloat(), motion::style::radius + 2);
            g.setColour(juce::Colours::white);
            g.setFont(motion::style::body());
            g.drawText("Everything", label, juce::Justification::centred, false);
        }
    }
    const juce::String help = navigating ? "WASD / arrows move" + motion::style::dot() + "Q E down / up" + motion::style::dot() + "Shift faster" + motion::style::dot() + "Esc finishes"
        : (validGesture() || navigationDrag) && dragHint.isNotEmpty() ? dragHint : juce::String();
    if (help.isNotEmpty()) {
        auto strip = getLocalBounds().removeFromBottom(26).reduced(8, 3);
        strip = strip.withSizeKeepingCentre(std::min(strip.getWidth(), juce::GlyphArrangement::getStringWidthInt(motion::style::body(), help) + 24), strip.getHeight());
        g.setColour(osci::Colours::veryDark().withAlpha(.85f));
        g.fillRoundedRectangle(strip.toFloat(), 4);
        g.setColour(osci::Colours::textMuted());
        g.setFont(motion::style::body());
        g.drawFittedText(help, strip.reduced(8, 0), juce::Justification::centred, 1);
    }
}

void MotionCompositionView::mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) {
    if (navigating || edit.active()) { return; }
    const auto pixels = juce::Point<double>(wheel.deltaX, wheel.deltaY) * 256.0;
    if (!wheel.isSmooth || event.mods.isCommandDown() || event.mods.isCtrlDown()) {
        if (wheel.isInertial) { return; }
        camera.dolly(-(std::abs(wheel.deltaY) >= std::abs(wheel.deltaX) ? wheel.deltaY : wheel.deltaX) * 2.0);
    } else if (wheel.isInertial) {
        // Momentum after the fingers lift would keep the view drifting.
        return;
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
        const auto hit = currentGizmo().hitTest(event.position);
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
        if (navigationDrag) { camera = cameraAtDown; navigationDrag = false; repaint(); return true; }
        if (marqueeArmed) { resetMarquee(); repaint(); return true; }
        // Escape clears the picked parts, then leaves part mode.
        if (partMode && !picked.empty()) { picked.clear(); refreshPartBar(); return true; }
        if (partMode) { setPartMode(false); return true; }
    }
    // Tab switches between objects and their parts, as Blender's edit mode.
    if (key == juce::KeyPress::tabKey && !navigating) { setPartMode(!partMode); return true; }
    if (partMode && key == juce::KeyPress('a', juce::ModifierKeys::commandModifier, 0)) {
        pickAllParts();
        return true;
    }
    if (partMode && !key.getModifiers().isAnyModifierKeyDown() && key.getKeyCode() == 'E') { extractPicked(); return true; }
    // Views along an axis: numpad 1, 3, 7 (Ctrl for the opposite side), or
    // 1, 3, 7 on the number row as with Blender's emulated numpad.
    const auto code = key.getKeyCode();
    const auto opposite = key.getModifiers().isCtrlDown();
    const auto plain = !key.getModifiers().isAnyModifierKeyDown();
    if (!navigating && (code == juce::KeyPress::numberPad1 || (plain && code == '1'))) { setViewPreset(opposite ? ViewPreset::back : ViewPreset::front); return true; }
    if (!navigating && (code == juce::KeyPress::numberPad3 || (plain && code == '3'))) { setViewPreset(opposite ? ViewPreset::left : ViewPreset::right); return true; }
    if (!navigating && (code == juce::KeyPress::numberPad7 || (plain && code == '7'))) { setViewPreset(opposite ? ViewPreset::bottom : ViewPreset::top); return true; }
    if (key.getModifiers().isCommandDown() || key.getModifiers().isCtrlDown()) {
        cancelGesture();
        setNavigating(false);
        return false;
    }
    if (navigating) { return true; }
    if (key.getModifiers().isAltDown()) { return false; }
    // Single-letter tools take plain keys only, so Shift+R and friends
    // reach the editor's commands.
    if (key.getModifiers().isShiftDown()) { return false; }
    if (key.getKeyCode() == 'P') { setMotionPathVisible(!showMotionPath); return true; }
    if (key.getKeyCode() == 'G') { setTool(MotionTransformTool::move); return true; }
    if (key.getKeyCode() == 'Y') { setTool(MotionTransformTool::anchor); return true; }
    if (key.getKeyCode() == 'R') { setTool(MotionTransformTool::rotate); return true; }
    if (key.getKeyCode() == 'S') { setTool(MotionTransformTool::scale); return true; }
    if (key.getKeyCode() == 'F') { frameSelection(); return true; }
    if (key.getKeyCode() == 'N') { setNavigating(true); return true; }
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
    if (!any) { return "Drop files here to start, or Add source (" + motion::style::shortcutText("Cmd+I") + ")"; }
    if (hidden) { return "Clips here are on muted or un-soloed tracks"; }
    const auto grid = project.timeGrid();
    const auto at = "Nothing on screen at " + juce::String(grid.positionLabel(now));
    return next.has_value() ? at + "  -  next clip at " + juce::String(grid.positionLabel(*next)) : at;
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
        : "Position path (before effects)" + motion::style::dot() + "Click a key to seek", getLocalBounds().removeFromTop(26).withTrimmedLeft(48).reduced(10, 0), juce::Justification::centredLeft, 2);
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
    const auto* track = motion::findClipTrack(project, selected);
    if (track == nullptr || track->locked || !motion::trackIsAudible(project, *track)) { return false; }
    return motion::findClip(project, selected)->contains(time, project.tempo()) || atSelectedPathEnd(time);
}

MotionCompositionGizmo MotionCompositionView::currentGizmo() const {
    const auto time = editingTime();
    gizmoFrame = motion::editor::gizmoFrameForClip(processor.document.project(), selected, time);
    if (navigating || !editable(time) || !gizmoFrame.has_value() || gizmoFrame->parent.hasPostTransformEffects) { return {}; }
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
    gizmoAtDown = motion::editor::gizmoFrameForClip(processor.document.project(), selected, time);
    if (!editable(time) || !gizmoAtDown.has_value() || gizmoAtDown->parent.hasPostTransformEffects) {
        dragHint = "Use the inspector for modulated or effected results";
        return false;
    }
    // A plain click only selects: the document is touched only once the
    // pointer actually drags.
    editTime = time;
    editSelection = selected;
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
    const auto down = [](int letter, int arrow = 0) { return juce::KeyPress::isKeyCurrentlyDown(letter) || (arrow != 0 && juce::KeyPress::isKeyCurrentlyDown(arrow)); };
    const motion::Vec3 direction { static_cast<double>(down('D', juce::KeyPress::rightKey) - down('A', juce::KeyPress::leftKey)),
        static_cast<double>(down('E') - down('Q')), static_cast<double>(down('W', juce::KeyPress::upKey) - down('S', juce::KeyPress::downKey)) };
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
    if (validGesture()) {
        edit.cancel();
        if (pathKey.has_value()) { pathKey->revision = processor.document.revision(); }
    }
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
