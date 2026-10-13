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
    g.drawText("Cameras", cardInset + 8, top, namesWidth - 40, cameraBandHeight, juce::Justification::centredLeft);
    // The plus adds a camera at the playhead, drawn like an icon button.
    const auto add = addCameraBounds().toFloat();
    if (addHover) {
        g.setColour(juce::Colours::white.withAlpha(.08f));
        g.fillRoundedRectangle(add, motion::style::radius + 1);
    }
    motion::icons::draw(g, motion::icons::Icon::add, add, addHover ? osci::Colours::text() : osci::Colours::text().withAlpha(.72f), 16.0f);
    // Between cuts the first camera shows; label each visible gap.
    const bool defaultSelected = !project.cameras.empty() && selected == project.cameras.front().id;
    const auto label = project.cameras.empty() ? juce::String("Default view") : juce::String(project.cameras.front().name);
    double from = 0;
    const auto gap = [&](double end) {
        const auto left = std::max(namesWidth, timeX(from)), right = std::min(getWidth(), timeX(end));
        if (right <= left) { return; }
        if (defaultSelected) {
            g.setColour(juce::Colours::white.withAlpha(.07f));
            g.fillRect(left, top + 2, right - left, cameraBandHeight - 4);
        }
        g.setColour(osci::Colours::textMuted().withAlpha(defaultSelected ? .9f : .55f));
        if (right - left > 60) { g.drawText(label, left + 6, top, right - left - 10, cameraBandHeight, juce::Justification::centredLeft, true); }
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
        g.fillRoundedRectangle(bounds.toFloat(), 3.0f);
        if (cut.id == selectedCut || cut.camera == selected) {
            g.setColour(osci::Colours::accentColor());
            g.drawRoundedRectangle(bounds.toFloat().reduced(.5f), 3.0f, 1.2f);
        }
        g.setColour(osci::Colours::text());
        g.drawText(cameraName(cut.camera), bounds.reduced(6, 0), juce::Justification::centredLeft, true);
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
        const auto centre = juce::Point<float>(static_cast<float>(timeX(time)) + .5f, static_cast<float>(top) + cameraBandHeight * .5f);
        const auto chosen = selectedCameraKey.has_value() && selectedCameraKey->first == camera->id && std::abs(selectedCameraKey->second - time) < 1.0e-6;
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
        cameraKeyDrag = CameraKeyDrag {selected, *key, *key, event.x, processor.document.project(), processor.document.revision(), false};
        repaint();
        return;
    }
    selectedCameraKey.reset();
    const auto time = std::clamp(snapTime(scrollTime + (event.x - namesWidth) / pixelsPerSecond, event.mods), 0.0, processor.document.project().duration);
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
    drag.before = processor.document.project();
    drag.revision = processor.document.revision();
    cutDrag = std::move(drag);
    repaint();
}

void MotionTimelineView::cameraBandDrag(const juce::MouseEvent& event) {
    auto& drag = *cutDrag;
    if (processor.document.revision() != drag.revision) { cutDrag.reset(); return; }
    const auto& project = drag.before;
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
    auto candidate = drag.before;
    if (!motion::Document::applyCutRange(candidate, drag.cut, start, end).wasOk()) { return; }
    // Back where it began is no change to record.
    drag.changed = start != drag.start || end != drag.end;
    processor.document.preview(std::move(candidate));
    drag.revision = processor.document.revision();
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
    menu.addItem(3, "Add camera here");
    menu.addSubMenu("Cut to camera here", cutTo, !cameras.empty());
    if (cut != 0) {
        menu.addSubMenu("Show camera", show);
        menu.addItem(1, "Delete cut");
    }
    const auto* cutValue = findCut(cut);
    const auto camera = cutValue != nullptr ? cutValue->camera : cameras.empty() ? motion::Id(0) : cameras.front();
    if (camera != 0) {
        menu.addSeparator();
        menu.addItem(2, "Delete " + cameraName(camera));
    }
    motion::ui::showDocumentMenu(menu, *this, processor.document, juce::PopupMenu::Options().withTargetComponent(this).withMousePosition(), [this, cut, time, camera, cameras](int result) {
        auto& document = processor.document;
        const auto chosen = [&](int base) { return cameras[static_cast<std::size_t>(result - base)]; };
        if (result == 3) {
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
            const auto offset = std::abs(timeX(key.time) - x);
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
        const auto& keys = curve.keyframes();
        const auto found = std::find_if(keys.begin(), keys.end(), [from](const auto& key) { return std::abs(key.time - from) < 1.0e-6; });
        if (found == keys.end()) { continue; }
        auto moved = *found;
        curve.removeKey(moved.time);
        moved.time = to;
        curve.setKey(moved);
    }
}

void MotionTimelineView::dragCameraKey(const juce::MouseEvent& event) {
    auto& drag = *cameraKeyDrag;
    // An undo or load under the drag ends it where it is.
    if (processor.document.revision() != drag.revision) {
        cameraKeyDrag.reset();
        return;
    }
    const auto time = processor.document.project().frameTime(snapTime(drag.from + (event.x - drag.downX) / pixelsPerSecond, event.mods));
    if (std::abs(time - drag.to) < 1.0e-9) { return; }
    auto updated = drag.before;
    const auto camera = std::find_if(updated.cameras.begin(), updated.cameras.end(), [&drag](const auto& item) { return item.id == drag.camera; });
    if (camera == updated.cameras.end()) { return; }
    // A key never lands on another key of the same camera.
    const auto occupied = std::abs(time - drag.from) > 1.0e-6 && std::any_of(camera->properties.begin(), camera->properties.end(), [time](const auto& entry) { return entry.second.hasKeyAt(time); });
    if (occupied) { return; }
    // Back where it started is no move at all.
    drag.moved = std::abs(time - drag.from) > 1.0e-9;
    if (drag.moved) { moveCameraKeys(*camera, drag.from, time); }
    processor.document.preview(std::move(updated));
    drag.revision = processor.document.revision();
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
        selectMarker(id); processor.seek(time); revealTime(time); repaint();
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
        if (result == 2) { const auto removed = processor.document.removeMarker(id); if (removed.failed() && onError) { onError(removed.getErrorMessage()); } }
        if (result == 4 || result == 5) { jumpMarker(result == 5); }
        if (result == 6 || result == 7) { editTempoChange(time); }
        if (result == 9) {
            const auto* existing = tempoChangeNear(time);
            if (existing != nullptr) {
                const auto changed = processor.document.setTempoChange(existing->beat, existing->bpm, existing->beat, !existing->ramp);
                if (changed.failed() && onError) { onError(changed.getErrorMessage()); }
            }
        }
        if (result == 8) {
            const auto* existing = tempoChangeNear(time);
            if (existing != nullptr) {
                const auto removed = processor.document.removeTempoChange(existing->beat);
                if (removed.failed() && onError) { onError(removed.getErrorMessage()); }
            }
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
