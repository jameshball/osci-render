#include "TimelineView.h"
#include "MenuIcons.h"

// The bands over the tracks: cameras and their cuts, markers, and tempo
// changes in the ruler.

juce::Rectangle<int> MotionTimelineView::cutBounds(const motion::CameraCut& cut) const {
    const auto left = std::max(namesWidth, timeX(cut.start));
    const auto right = std::min(getWidth(), timeX(cut.end()));
    if (right <= left) { return {}; }
    return {left, cameraBandTop() + 2, right - left, cameraBandHeight - 4};
}

juce::Colour MotionTimelineView::cameraColour(const motion::Project& project, motion::Id camera) {
    const auto found = std::find_if(project.cameras.begin(), project.cameras.end(), [camera](const auto& item) { return item.id == camera; });
    const auto index = found == project.cameras.end() ? 0 : static_cast<int>(found - project.cameras.begin());
    // Cool and warm greys, never lilac (modulation) or green (selection).
    static const std::array<juce::uint32, 4> palette {0xff3d4f63, 0xff3f5a56, 0xff5a533f, 0xff4b505c};
    return juce::Colour(palette[static_cast<std::size_t>(index) % palette.size()]);
}

juce::String MotionTimelineView::cameraName(motion::Id camera) const {
    for (const auto& item : processor.document.project().cameras) {
        if (item.id == camera) { return juce::String(item.name); }
    }
    return "Camera";
}

void MotionTimelineView::paintCameraBand(juce::Graphics& g) const {
    const auto& project = processor.document.project();
    const auto top = cameraBandTop();
    fillCard(g, top, cameraBandHeight, osci::Colours::surfaceRaised(), osci::Colours::surfaceRaised().darker(.15f));
    g.setColour(osci::Colours::textMuted());
    g.setFont(motion::style::caption());
    g.drawText("Cameras", nameX, top, addCameraBounds().getX() - 2 - nameX, cameraBandHeight, juce::Justification::centredLeft);
    // The plus adds a camera at the playhead, drawn like an icon button.
    const auto add = addCameraBounds().toFloat();
    if (addHover) {
        g.setColour(motion::style::hoverFill());
        g.fillRoundedRectangle(add, motion::style::radiusControl);
    }
    motion::icons::draw(g, motion::icons::Icon::add, add, addHover ? osci::Colours::text() : osci::Colours::text().withAlpha(.72f), motion::style::iconMedium);
    // The selected camera's keys show in the band; names start clear of a
    // key at their start.
    const auto keysShown = std::any_of(project.cameras.begin(), project.cameras.end(), [this](const auto& item) { return item.id == selected; });
    const auto nameInset = keysShown ? 14 : 6;
    // Between cuts the first camera shows; label each visible gap.
    const bool defaultSelected = !project.cameras.empty() && selected == project.cameras.front().id;
    const auto label = project.cameras.empty() ? juce::String("Default view") : juce::String(project.cameras.front().name);
    double from = 0;
    const auto gap = [&](double end) {
        const auto left = std::max(namesWidth, timeX(from)), right = std::min(getWidth(), timeX(end));
        if (right <= left) { return; }
        if (defaultSelected) {
            g.setColour(motion::style::restFill());
            g.fillRect(left, top + 2, right - left, cameraBandHeight - 4);
        }
        g.setColour(osci::Colours::textMuted().withAlpha(defaultSelected ? .9f : .55f));
        if (right - left > 60) { g.drawText(label, left + nameInset, top, right - left - nameInset - 4, cameraBandHeight, juce::Justification::centredLeft, true); }
    };
    for (const auto& cut : project.cameraCuts) {
        if (cut.start > from) { gap(cut.start); }
        from = std::max(from, cut.end());
    }
    if (from < project.duration) { gap(project.duration); }
    for (const auto& cut : project.cameraCuts) {
        const auto bounds = cutBounds(cut);
        if (bounds.isEmpty()) { continue; }
        g.setColour(cameraColour(project, cut.camera).withAlpha(cut.id == selectedCut ? 1.0f : .85f));
        g.fillRoundedRectangle(bounds.toFloat(), motion::style::radiusControl);
        if (cut.id == selectedCut || cut.camera == selected) {
            g.setColour(motion::style::accent());
            g.drawRoundedRectangle(bounds.toFloat().reduced(.5f), motion::style::radiusControl, 1.2f);
        }
        g.setColour(osci::Colours::text());
        g.drawText(cameraName(cut.camera), bounds.withTrimmedLeft(nameInset).withTrimmedRight(6), juce::Justification::centredLeft, true);
    }
    // The selected camera's keys, so its moves read on the timeline.
    const auto camera = std::find_if(project.cameras.begin(), project.cameras.end(), [this](const auto& item) { return item.id == selected; });
    if (camera == project.cameras.end()) { return; }
    std::set<double> times;
    for (const auto& [name, curve] : camera->properties) {
        for (const auto& key : curve.keyframes()) { times.insert(key.time); }
    }
    juce::Graphics::ScopedSaveState band(g);
    g.reduceClipRegion(namesWidth, top, getWidth() - namesWidth, cameraBandHeight);
    for (const auto time : times) {
        const auto centre = juce::Point<float>(static_cast<float>(cameraKeyX(time)) + .5f, static_cast<float>(top) + cameraBandHeight * .5f);
        const auto chosen = selectedCameraKey.has_value() && selectedCameraKey->first == camera->id && motion::sameTime(selectedCameraKey->second, time);
        g.setColour(osci::Colours::veryDark());
        motion::style::drawDiamond(g, centre, chosen ? 6.0f : 5.0f, true);
        g.setColour(chosen ? motion::style::accent() : osci::Colours::text().withAlpha(.8f));
        motion::style::drawDiamond(g, centre, chosen ? 4.5f : 3.5f, true);
    }
}

const motion::CameraCut* MotionTimelineView::findCut(motion::Id id) const {
    for (const auto& cut : processor.document.project().cameraCuts) {
        if (cut.id == id) { return &cut; }
    }
    return nullptr;
}

const motion::CameraCut* MotionTimelineView::cutAt(juce::Point<int> point) const {
    for (const auto& cut : processor.document.project().cameraCuts) {
        if (cutBounds(cut).expanded(3, 0).contains(point)) { return &cut; }
    }
    return nullptr;
}

void MotionTimelineView::cameraBandDown(const juce::MouseEvent& event) {
    if (addCameraBounds().contains(event.getPosition())) {
        if (onAddCamera) { onAddCamera(processor.position.load()); }
        return;
    }
    if (event.x < namesWidth) { return; }
    // The selected camera's keys come first: select one, or drag it.
    const auto key = event.mods.isPopupMenu() ? std::nullopt : cameraKeyAt(event.x);
    if (key.has_value()) {
        selectedCut = 0;
        selectedCameraKey = std::make_pair(selected, *key);
        edit.begin();
        gesture = CameraKeyDrag {selected, *key, *key, event.x};
        repaint();
        return;
    }
    selectedCameraKey.reset();
    const auto time = std::clamp(snapTime(timeAt(event.x), event.mods), 0.0, processor.document.project().duration);
    const auto* cut = cutAt(event.getPosition());
    selectedCut = cut != nullptr ? cut->id : 0;
    // A cut, or the gap where the first camera shows, selects that camera.
    const auto& cameras = processor.document.project().cameras;
    const auto camera = cut != nullptr ? cut->camera : cameras.empty() ? motion::Id(0) : cameras.front().id;
    if (camera != 0 && onSelection) { onSelection(camera); }
    if (event.mods.isPopupMenu()) {
        showCutMenu(selectedCut, time);
        repaint();
        return;
    }
    if (cut == nullptr) {
        if (camera == 0) { processor.seek(time); }
        repaint();
        return;
    }
    const auto bounds = cutBounds(*cut);
    CutDrag drag;
    drag.cut = cut->id;
    drag.start = cut->start;
    drag.end = cut->end();
    drag.downX = event.x;
    drag.mode = std::abs(event.x - bounds.getX()) <= 5 ? CutDrag::Mode::left : (std::abs(event.x - bounds.getRight()) <= 5 ? CutDrag::Mode::right : CutDrag::Mode::move);
    edit.begin();
    gesture = std::move(drag);
    repaint();
}

void MotionTimelineView::cameraBandDrag(const juce::MouseEvent& event) {
    const auto& drag = std::get<CutDrag>(gesture);
    if (edit.stale()) { gesture = {}; edit.reset(); return; }
    const auto& project = edit.start();
    const auto delta = (event.x - drag.downX) / pixelsPerSecond;
    auto start = drag.start, end = drag.end;
    // Neighbouring cuts bound every gesture; nothing overlaps.
    double lower = 0, upper = project.duration;
    for (const auto& other : project.cameraCuts) {
        if (other.id == drag.cut) { continue; }
        if (other.end() <= drag.start + 1.0e-9) { lower = std::max(lower, other.end()); }
        if (other.start >= drag.end - 1.0e-9) { upper = std::min(upper, other.start); }
    }
    const auto frame = project.frameRate > 0 ? 1.0 / project.frameRate : 1.0 / 30;
    // Each bound no further than the other, however short the cut or tight
    // its neighbours.
    const auto bounded = [](double value, double low, double high) { return std::clamp(value, low, std::max(low, high)); };
    if (drag.mode == CutDrag::Mode::move) {
        start = bounded(snapEdge(drag.start + delta, event.mods, project, {}), lower, upper - (drag.end - drag.start));
        end = start + (drag.end - drag.start);
    } else if (drag.mode == CutDrag::Mode::left) {
        start = bounded(snapEdge(drag.start + delta, event.mods, project, {}), lower, drag.end - frame);
    } else {
        end = std::max(drag.start + frame, std::min(snapEdge(drag.end + delta, event.mods, project, {}), upper));
    }
    // Shown as it goes, one undo step when let go; a place it can't go is
    // simply not taken.
    auto candidate = project;
    if (!motion::Document::applyCutRange(candidate, drag.cut, start, end).wasOk()) { return; }
    // Back where it began is no change to record.
    edit.show(std::move(candidate), start != drag.start || end != drag.end);
    repaint();
}

void MotionTimelineView::showCutMenu(motion::Id cut, double time) {
    const auto& project = processor.document.project();
    juce::PopupMenu menu, cutTo, show;
    std::vector<motion::Id> cameras;
    for (const auto& camera : project.cameras) {
        cameras.push_back(camera.id);
        cutTo.addItem(100 + static_cast<int>(cameras.size()) - 1, juce::String(camera.name));
        show.addItem(200 + static_cast<int>(cameras.size()) - 1, juce::String(camera.name));
    }
    const auto camera = cameraAt(cut);
    if (camera != 0) {
        menu.addItem(4, "Look through " + cameraName(camera));
        menu.addSeparator();
    }
    menu.addItem(3, "Add camera here");
    menu.addSubMenu("Cut to camera here", cutTo, !cameras.empty());
    if (cut != 0) {
        menu.addSubMenu("Show camera", show);
        menu.addItem(1, "Delete cut");
    }
    if (camera != 0) {
        menu.addSeparator();
        // The last camera stays (Document::removeCamera).
        menu.addItem(2, "Delete " + cameraName(camera), processor.document.project().cameras.size() > 1);
    }
    motion::ui::showDocumentMenu(menu, *this, processor.document, juce::PopupMenu::Options().withTargetComponent(this).withMousePosition(), [this, cut, time, camera, cameras](int result) {
        auto& document = processor.document;
        const auto chosen = [&](int base) { return cameras[static_cast<std::size_t>(result - base)]; };
        if (result == 4) {
            if (onLookThrough) { onLookThrough(camera); }
        } else if (result == 3) {
            if (onAddCamera) { onAddCamera(time); }
        } else if (result == 2) {
            report(document.removeCamera(camera));
            selectedCut = 0;
            if (onSelection) { onSelection(0); }
        } else if (result == 1) {
            report(document.removeCut(cut));
            selectedCut = 0;
        } else if (result >= 200) {
            report(document.setCutCamera(cut, chosen(200)));
        } else if (result >= 100) {
            motion::Id created = 0;
            report(document.cutToCamera(chosen(100), time, created));
            selectedCut = created;
        }
        repaint();
    });
}

// The camera a cut shows, or with none (a gap) the first camera, which the
// output shows there.
motion::Id MotionTimelineView::cameraAt(motion::Id cut) const {
    const auto* cutValue = findCut(cut);
    if (cutValue != nullptr) { return cutValue->camera; }
    const auto& cameras = processor.document.project().cameras;
    return cameras.empty() ? motion::Id(0) : cameras.front().id;
}

const motion::Camera* MotionTimelineView::selectedCamera() const {
    const auto& cameras = processor.document.project().cameras;
    const auto found = std::find_if(cameras.begin(), cameras.end(), [this](const auto& item) { return item.id == selected; });
    return found == cameras.end() ? nullptr : &*found;
}

std::optional<double> MotionTimelineView::cameraKeyAt(int x) const {
    const auto* camera = selectedCamera();
    if (camera == nullptr) { return std::nullopt; }
    std::optional<double> nearest;
    int distance = 6;
    for (const auto& [name, curve] : camera->properties) {
        for (const auto& key : curve.keyframes()) {
            const auto offset = std::abs(cameraKeyX(key.time) - x);
            if (offset < distance) {
                distance = offset;
                nearest = key.time;
            }
        }
    }
    return nearest;
}

void MotionTimelineView::moveCameraKeys(motion::Camera& camera, double from, double to) {
    for (auto& [name, curve] : camera.properties) {
        const auto* found = curve.findKey(from);
        if (found == nullptr) { continue; }
        auto moved = *found;
        curve.removeKey(moved.time);
        moved.time = to;
        curve.setKey(moved);
    }
}

void MotionTimelineView::dragCameraKey(const juce::MouseEvent& event) {
    auto& drag = std::get<CameraKeyDrag>(gesture);
    // An undo or load under the drag ends it where it is.
    if (edit.stale()) {
        gesture = {};
        edit.reset();
        return;
    }
    const auto time = processor.document.project().frameTime(snapTime(drag.from + (event.x - drag.downX) / pixelsPerSecond, event.mods));
    if (motion::sameTime(time, drag.to)) { return; }
    auto updated = edit.start();
    const auto camera = std::find_if(updated.cameras.begin(), updated.cameras.end(), [&drag](const auto& item) { return item.id == drag.camera; });
    if (camera == updated.cameras.end()) { return; }
    // A key never lands on another key of the same camera.
    const auto occupied = !motion::sameTime(time, drag.from) && std::any_of(camera->properties.begin(), camera->properties.end(), [time](const auto& entry) { return entry.second.hasKeyAt(time); });
    if (occupied) { return; }
    // Back where it started is no move at all.
    const auto moved = !motion::sameTime(time, drag.from);
    if (moved) { moveCameraKeys(*camera, drag.from, time); }
    edit.show(std::move(updated), moved);
    drag.to = time;
    selectedCameraKey = std::make_pair(drag.camera, time);
    repaint();
}

void MotionTimelineView::deleteCameraKey(motion::Id id, double time) {
    processor.document.tryEdit("Delete camera key", [id, time](motion::Project& project) {
        bool removed = false;
        for (auto& camera : project.cameras) {
            if (camera.id != id) { continue; }
            // Without keys the camera keeps the pose it had here.
            for (auto& [name, curve] : camera.properties) { removed = motion::keyedit::setKeyed(curve, time, false) || removed; }
        }
        return removed;
    });
}

juce::Rectangle<int> MotionTimelineView::markerBounds(const motion::Marker& marker) const {
    const auto x = timeX(marker.time);
    if (x < namesWidth || x >= getWidth()) { return {}; }
    // A flag as wide as its name, not a block that reads as a clip.
    const auto label = juce::roundToInt(juce::TextLayout::getStringWidth(motion::style::caption(), marker.name));
    auto right = std::min(getWidth(), x + std::min(140, label + 16));
    for (const auto& next : processor.document.project().markers) {
        if (next.time > marker.time) { right = std::min(right, timeX(next.time) - 2); break; }
    }
    return {x, markerBandTop() + 1, std::max(3, right - x), markerBandHeight - 2};
}

const motion::Marker* MotionTimelineView::markerAt(juce::Point<int> point) const {
    if (point.y < markerBandTop() || point.y >= markerBandTop() + markerBandHeight) { return nullptr; }
    for (const auto& marker : processor.document.project().markers) {
        if (markerBounds(marker).contains(point)) { return &marker; }
    }
    return nullptr;
}

void MotionTimelineView::clearCameraSelection() {
    selectedCut = 0;
    selectedCameraKey.reset();
}

void MotionTimelineView::selectMarker(motion::Id id) {
    clearCameraSelection();
    selected = 0;
    selectedClips.clear();
    if (onSelection) { onSelection(0); }
    selectedMarker = id;
}

void MotionTimelineView::dragMarker(const juce::MouseEvent& event) {
    // One the document changed under ends.
    if (edit.stale()) { gesture = {}; edit.reset(); repaint(); return; }
    const auto& drag = std::get<MarkerDrag>(gesture);
    const auto id = drag.marker;
    auto updated = edit.start();
    const auto delta = (event.x - drag.downX) / pixelsPerSecond;
    const auto time = delta == 0 ? drag.originalTime : std::clamp(snapEdge(drag.originalTime + delta, event.mods, edit.start(), {}, nullptr, id), 0.0, updated.duration);
    if (std::any_of(updated.markers.begin(), updated.markers.end(), [id, time](const auto& marker) { return marker.id != id && motion::sameTime(marker.time, time); })) { return; }
    for (auto& marker : updated.markers) {
        if (marker.id == id) { marker.time = time; }
    }
    std::sort(updated.markers.begin(), updated.markers.end(), [](const auto& a, const auto& b) { return a.time != b.time ? a.time < b.time : a.id < b.id; });
    edit.show(std::move(updated), time != drag.originalTime);
    repaint();
}

void MotionTimelineView::jumpMarker(bool forward) {
    const auto now = processor.position.load();
    const motion::Marker* target = nullptr;
    for (const auto& marker : processor.document.project().markers) {
        if (forward && marker.time > now + 0.000001) { target = &marker; break; }
        if (!forward && marker.time < now - 0.000001) { target = &marker; }
    }
    if (target != nullptr) {
        const auto id = target->id;
        const auto time = target->time;
        selectMarker(id);
        processor.seek(time);
        revealTime(time);
        repaint();
    }
}

void MotionTimelineView::showMarkerMenu(motion::Id id, double time) {
    juce::PopupMenu menu;
    if (id != 0) {
        menu.addItem(1, "Edit marker...");
        menu.addItem(2, "Delete marker");
        menu.addSeparator();
    }
    menu.addItem(3, "Add marker here...");
    menu.addItem(4, "Previous marker", !processor.document.project().markers.empty());
    menu.addItem(5, "Next marker", !processor.document.project().markers.empty());
    menu.addSeparator();
    const auto tempo = processor.document.project().tempo();
    const auto beat = std::round(tempo.beats(time) * 4) / 4;
    const auto* change = tempoChangeNear(time);
    if (change != nullptr) {
        menu.addItem(7, "Edit tempo change...");
        menu.addItem(9, "Glide into this tempo", true, change->ramp);
        menu.addItem(8, "Delete tempo change");
    } else {
        menu.addItem(6, "Add tempo change here...", beat > 0);
    }
    motion::ui::showDocumentMenu(menu, *this, processor.document, juce::PopupMenu::Options().withTargetComponent(this).withMousePosition(), [this, id, time](int result) {
        if ((result == 1 || result == 3) && onEditMarker) { onEditMarker(result == 1 ? id : 0, time); }
        if (result == 2) { report(processor.document.removeMarker(id)); }
        if (result == 4 || result == 5) { jumpMarker(result == 5); }
        if (result == 6 || result == 7) { editTempoChange(time); }
        if (result == 9) {
            const auto* existing = tempoChangeNear(time);
            if (existing != nullptr) { report(processor.document.setTempoChange(existing->beat, existing->bpm, existing->beat, !existing->ramp)); }
        }
        if (result == 8) {
            const auto* existing = tempoChangeNear(time);
            if (existing != nullptr) { report(processor.document.removeTempoChange(existing->beat)); }
        }
    });
}

const motion::TempoChange* MotionTimelineView::tempoChangeNear(double time) const {
    const auto& project = processor.document.project();
    if (project.tempoChanges == nullptr) { return nullptr; }
    const auto tempo = project.tempo();
    for (const auto& change : *project.tempoChanges) {
        if (std::abs(timeX(tempo.seconds(change.beat)) - timeX(time)) <= 6) { return &change; }
    }
    return nullptr;
}

void MotionTimelineView::editTempoChange(double time) {
    const auto tempo = processor.document.project().tempo();
    const auto* existing = tempoChangeNear(time);
    const auto beat = existing != nullptr ? existing->beat : std::round(tempo.beats(time) * 4) / 4;
    const auto current = existing != nullptr ? existing->bpm : tempo.bpmAt(time);
    if (onEditTempo) { onEditTempo(beat, current, existing != nullptr ? std::optional<double>(existing->beat) : std::nullopt); }
}

// The loop brace and the bake strip along the time ruler.

void MotionTimelineView::updateBake() {
    const auto bake = processor.hiddenLineBake();
    const auto now = juce::Time::getMillisecondCounterHiRes();
    const auto finished = bake == nullptr || bake->complete();
    const auto wasShown = bakeShown;
    if (bake.get() != bakeSeen) {
        // A new bake goes on from what the strip showed of the last one.
        bakeShown = bakeShown && !finished && bakeFinishedAt == 0;
        bakeSeen = bake.get();
        bakeSeenAt = now;
        bakeFinishedAt = 0;
    }
    if (!finished && now - bakeSeenAt >= bakeShowAfterMs) { bakeShown = true; }
    if (finished && bakeFinishedAt == 0) { bakeFinishedAt = now; }
    // Faded out: nothing more to paint.
    if (bakeShown && bakeFinishedAt > 0 && now - bakeFinishedAt > bakeFadeMs + 100) { bakeShown = false; }
    if (bakeShown || wasShown) { repaint(bakeStrip().expanded(0, 1)); }
}

void MotionTimelineView::paintBake(juce::Graphics& g) const {
    const auto bake = processor.hiddenLineBake();
    // Its frames are the main composition's, on its time.
    if (bake == nullptr || processor.document.editingComposition() != 0) { return; }
    if (!bakeShown || bake.get() != bakeSeen) { return; }
    const auto opacity = bakeFinishedAt > 0 ? 1.0 - (juce::Time::getMillisecondCounterHiRes() - bakeFinishedAt) / bakeFadeMs : 1.0;
    if (opacity <= 0) { return; }
    const auto strip = bakeStrip();
    // Over the loop's brace, the strip cuts through it, so the progress
    // reads the same along the whole ruler.
    const auto loop = loopBounds();
    if (!loop.isEmpty()) {
        g.setColour(osci::Colours::surfaceRaised().withMultipliedAlpha(static_cast<float>(opacity)));
        g.fillRect(juce::Rectangle<int>(loop.getX(), strip.getY() - 1, loop.getWidth(), strip.getHeight() + 2).getIntersection(strip.expanded(0, 1)));
    }
    const auto first = std::max(strip.getX(), timeX(0));
    const auto last = std::min(strip.getRight(), timeX(bake->timeOf(bake->frameCount())));
    using State = motion::hidden::Bake::State;
    // Neutral, so it never reads as a loop or a selection: the frames being
    // baked brightest, those baked dimmer, those waiting faintest.
    const auto colour = [opacity](State state) {
        const auto alpha = state == State::done ? .35f : state == State::baking ? .6f : .12f;
        return osci::Colours::text().withAlpha(alpha * static_cast<float>(opacity));
    };
    // Each column shows the least done of its frames; runs of one state are
    // one rectangle.
    std::optional<State> run;
    auto runStart = first;
    for (auto x = first; x <= last; ++x) {
        std::optional<State> here;
        if (x < last) {
            const auto from = bake->frameOf(timeAt(x));
            const auto next = bake->frameOf(timeAt(x + 1));
            const auto to = next > from ? next - 1 : from;
            here = State::done;
            for (auto frame = from; frame <= to; ++frame) {
                const auto state = bake->state(frame);
                if (state == State::baking) { here = state; break; }
                if (state == State::waiting) { here = state; }
            }
        }
        if (here != run) {
            if (run.has_value()) {
                g.setColour(colour(*run));
                g.fillRect(runStart, strip.getY(), x - runStart, strip.getHeight());
            }
            run = here;
            runStart = x;
        }
    }
}

juce::Rectangle<int> MotionTimelineView::loopBounds() const {
    const auto& project = processor.document.project();
    if (!project.hasLoop()) { return {}; }
    const auto left = std::max(namesWidth, timeX(project.loopStart)), right = std::min(getWidth(), timeX(project.loopEnd));
    return right > left ? juce::Rectangle<int>(left, loopTop, right - left, loopHeight) : juce::Rectangle<int>();
}

void MotionTimelineView::paintLoop(juce::Graphics& g) const {
    const auto bounds = loopBounds();
    if (bounds.isEmpty()) { return; }
    const auto on = processor.document.project().looping;
    const auto colour = on ? motion::style::accent() : osci::Colours::textMuted();
    if (on) {
        g.setColour(colour.withAlpha(.05f));
        g.fillRect(bounds.getX(), rulerHeight, bounds.getWidth(), getHeight() - rulerHeight);
    }
    // One opaque shape (bar and end brackets), so nothing overlaps.
    juce::Path brace;
    brace.addRoundedRectangle(bounds.toFloat(), motion::style::radiusSmall);
    brace.addRectangle(static_cast<float>(bounds.getX()), static_cast<float>(loopTop - 4), 2.0f, static_cast<float>(loopHeight + 4));
    brace.addRectangle(static_cast<float>(bounds.getRight() - 2), static_cast<float>(loopTop - 4), 2.0f, static_cast<float>(loopHeight + 4));
    brace.setUsingNonZeroWinding(true);
    g.setColour(osci::Colours::surfaceRaised().interpolatedWith(colour, on ? .8f : .55f));
    g.fillPath(brace);
}

bool MotionTimelineView::loopDown(const juce::MouseEvent& event) {
    const auto bounds = loopBounds();
    if (bounds.isEmpty() || event.y < loopTop - 4 || event.y >= loopTop + loopHeight || event.x < bounds.getX() - 5 || event.x > bounds.getRight() + 5) { return false; }
    const auto& project = processor.document.project();
    if (event.getNumberOfClicks() > 1) {
        processor.document.changeView([](motion::Composition& state) { state.looping = !state.looping; });
        return true;
    }
    const auto mode = std::abs(event.x - bounds.getX()) <= 5 ? LoopDrag::Mode::left : std::abs(event.x - bounds.getRight()) <= 5 ? LoopDrag::Mode::right : LoopDrag::Mode::move;
    edit.begin();
    gesture = LoopDrag {mode, project.loopStart, project.loopEnd, event.x};
    return true;
}

bool MotionTimelineView::moveNearestLoopEdge(const juce::MouseEvent& event) {
    const auto& project = processor.document.project();
    if (project.duration <= 0) { return false; }
    const auto frame = project.frameRate > 0 ? 1 / project.frameRate : 1.0 / 30;
    const auto time = std::clamp(snapTime(timeAt(event.x), event.mods), 0.0, project.duration);
    auto start = project.loopStart, end = project.loopEnd;
    juce::String label;
    if (!project.hasLoop()) {
        const auto playhead = std::clamp(processor.position.load(), 0.0, project.duration);
        start = std::min(time, playhead);
        end = std::abs(time - playhead) >= frame ? std::max(time, playhead) : project.duration;
        label = "Set loop";
    } else if (std::abs(time - start) <= std::abs(time - end)) {
        start = std::min(time, end - frame);
        label = "Move loop start";
    } else {
        end = std::max(time, start + frame);
        label = "Move loop end";
    }
    if (end - start < frame || (start == project.loopStart && end == project.loopEnd)) { return true; }
    const motion::Document::ViewChange view(processor.document);
    processor.document.edit(label, [start, end](motion::Project& state) {
        state.loopStart = start;
        state.loopEnd = end;
    });
    repaint();
    return true;
}

void MotionTimelineView::loopDragged(const juce::MouseEvent& event) {
    const auto& drag = std::get<LoopDrag>(gesture);
    if (edit.stale()) { gesture = {}; edit.reset(); return; }
    const auto& before = edit.start();
    const auto delta = (event.x - drag.downX) / pixelsPerSecond;
    auto updated = before;
    const auto length = drag.end - drag.start;
    const auto frame = updated.frameRate > 0 ? 1 / updated.frameRate : 1.0 / 30;
    if (drag.mode == LoopDrag::Mode::move) {
        updated.loopStart = std::clamp(snapEdge(drag.start + delta, event.mods, before, {}), 0.0, std::max(0.0, updated.duration - length));
        updated.loopEnd = updated.loopStart + length;
    } else if (drag.mode == LoopDrag::Mode::left) {
        updated.loopStart = std::clamp(snapEdge(drag.start + delta, event.mods, before, {}), 0.0, std::max(0.0, drag.end - frame));
    } else {
        updated.loopEnd = std::clamp(snapEdge(drag.end + delta, event.mods, before, {}), std::min(drag.start + frame, updated.duration), updated.duration);
    }
    const auto changed = updated.loopStart != before.loopStart || updated.loopEnd != before.loopEnd;
    const motion::Document::ViewChange view(processor.document);
    edit.show(std::move(updated), changed);
    repaint();
}
