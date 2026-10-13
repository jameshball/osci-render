#include "TimelineView.h"
#include "MenuIcons.h"
#include "../model/KeyEdit.h"

// Key lanes: the keys a clip's properties have, selected, dragged, eased,
// added and deleted in place.

namespace {
template <typename Keys>
bool holdsKey(const motion::Clip& clip, const Keys& keys) {
    return std::any_of(keys.begin(), keys.end(), [&clip](const auto& key) { return key.clip == clip.id; });
}
template <typename Keys>
bool holdsKey(const motion::Track& track, const Keys& keys) {
    return std::any_of(track.clips.begin(), track.clips.end(), [&keys](const auto& clip) { return holdsKey(clip, keys); });
}
// True when one of `keys` is on a locked track.
template <typename Keys>
bool keysLocked(const motion::Project& project, const Keys& keys) {
    return std::any_of(project.tracks.begin(), project.tracks.end(), [&keys](const auto& track) { return track.locked && holdsKey(track, keys); });
}
// Calls change(clip) for each clip on an unlocked track that holds one of
// `keys`; tracks without them stay shared.
template <typename Keys, typename Change>
void changeKeyedClips(motion::Project& project, const Keys& keys, Change&& change) {
    project.tracks.changeEach([&keys](const motion::Track& track) { return !track.locked && holdsKey(track, keys); }, [&](motion::Track& track) {
        for (auto& clip : track.clips) {
            if (holdsKey(clip, keys)) { change(clip); }
        }
    });
}
}

std::optional<MotionTimelineView::KeyRef> MotionTimelineView::keyAt(juce::Point<int> point) const {
    const auto* lane = laneAtY(point.y);
    if (lane == nullptr || point.x < namesWidth) { return std::nullopt; }
    const auto& project = processor.document.project();
    const auto& track = project.tracks[static_cast<std::size_t>(lane->track)];
    std::optional<KeyRef> best;
    auto bestDistance = 7.0;
    for (const auto& clip : track.clips) {
        const auto found = clip.properties.find(lane->lane);
        const auto timing = clip.timing(project.tempo());
        if (found == clip.properties.end() || timing.rate == 0) { continue; }
        for (const auto& key : found->second.keyframes()) {
            const auto distance = std::abs(timeX(timing.projectTime(key.time)) - point.x);
            if (distance < bestDistance) { bestDistance = distance; best = KeyRef{clip.id, lane->lane, key.time}; }
        }
    }
    return best;
}

// Every key on every lane shown, scrolled into view or not.
std::vector<MotionTimelineView::KeyRef> MotionTimelineView::allLaneKeys() const {
    std::vector<KeyRef> result;
    const auto& project = processor.document.project();
    for (const auto& row : rows) {
        if (!row.isLane()) { continue; }
        for (const auto& clip : project.tracks[static_cast<std::size_t>(row.track)].clips) {
            const auto found = clip.properties.find(row.lane);
            if (found == clip.properties.end() || clip.timing(project.tempo()).rate == 0) { continue; }
            for (const auto& key : found->second.keyframes()) { result.push_back({clip.id, row.lane, key.time}); }
        }
    }
    return result;
}

std::vector<MotionTimelineView::KeyRef> MotionTimelineView::keysInside(juce::Rectangle<int> area) const {
    std::vector<KeyRef> result;
    const auto& project = processor.document.project();
    for (int visible = firstVisibleRow(); visible < static_cast<int>(rows.size()); ++visible) {
        const auto& row = rows[static_cast<std::size_t>(visible)];
        const auto y = rowY(visible);
        if (!row.isLane() || y + laneHeight / 2 < area.getY() || y + laneHeight / 2 > area.getBottom()) { continue; }
        for (const auto& clip : project.tracks[static_cast<std::size_t>(row.track)].clips) {
            const auto found = clip.properties.find(row.lane);
            const auto timing = clip.timing(project.tempo());
            if (found == clip.properties.end() || timing.rate == 0) { continue; }
            for (const auto& key : found->second.keyframes()) {
                const auto x = timeX(timing.projectTime(key.time));
                if (x >= area.getX() && x <= area.getRight()) { result.push_back({clip.id, row.lane, key.time}); }
            }
        }
    }
    return result;
}

// Where a key at content time `time` goes when moved `delta` seconds on the
// timeline: through the clip's own timing, which a tempo warp bends.
static double movedKeyTime(const motion::ClipTiming& timing, double time, double delta) { return timing.localTime(timing.projectTime(time) + delta); }

bool MotionTimelineView::moveKeys(motion::Project& project, const std::vector<KeyRef>& keys, double delta) {
    if (keysLocked(project, keys)) { return false; }
    bool collided = false;
    const auto tempo = project.tempo();
    changeKeyedClips(project, keys, [&](motion::Clip& clip) {
        const auto timing = clip.timing(tempo);
        for (auto& [name, curve] : clip.properties) {
            std::vector<motion::Keyframe> moving;
            for (const auto& key : curve.keyframes()) {
                const bool chosen = std::any_of(keys.begin(), keys.end(), [&](const auto& item) { return item.clip == clip.id && item.property == name && motion::sameTime(item.time, key.time); });
                if (chosen) { moving.push_back(key); }
            }
            if (moving.empty()) { continue; }
            for (const auto& key : moving) { curve.removeKey(key.time); }
            for (auto key : moving) {
                key.time = movedKeyTime(timing, key.time, delta);
                const auto& remaining = curve.keyframes();
                if (std::any_of(remaining.begin(), remaining.end(), [&](const auto& other) { return motion::sameTime(other.time, key.time); })) {
                    collided = true;
                    continue;
                }
                curve.setKey(key);
            }
        }
    });
    return !collided;
}

void MotionTimelineView::dragKeys(int x, juce::ModifierKeys modifiers) {
    auto* keyDrag = active<KeyDrag>();
    if (keyDrag == nullptr) { return; }
    if (processor.document.revision() != keyDrag->revision) { gesture = {}; return; }
    const auto* clip = motion::findClip(keyDrag->before, keyDrag->grabbed.clip);
    if (clip == nullptr) { return; }
    const auto timing = clip->timing(keyDrag->before.tempo());
    if (timing.rate == 0) { return; }
    const auto grabbedTime = timing.projectTime(keyDrag->grabbed.time);
    auto delta = (x - keyDrag->downX) / pixelsPerSecond;
    if (delta != 0) { delta = snapEdge(grabbedTime + delta, modifiers, keyDrag->before, {}, &keyDrag->keys) - grabbedTime; }
    auto updated = keyDrag->before;
    if (delta != 0 && !moveKeys(updated, keyDrag->keys, delta)) { return; }
    keyDrag->changed = delta != 0;
    selectedKeys = keyDrag->keys;
    for (auto& key : selectedKeys) {
        const auto* owner = motion::findClip(keyDrag->before, key.clip);
        if (owner != nullptr) { key.time = movedKeyTime(owner->timing(keyDrag->before.tempo()), key.time, delta); }
    }
    processor.document.preview(std::move(updated));
    keyDrag->revision = processor.document.revision();
    repaint();
}

bool MotionTimelineView::nudgeSelectedKeys(int frames) {
    if (active<KeyDrag>() != nullptr) { return true; }
    const auto& project = processor.document.project();
    const auto delta = frames / (project.frameRate > 0 ? project.frameRate : 30.0);
    const auto keys = selectedKeys;
    // Where each selected key lands, through its clip's own timing.
    auto moved = keys;
    for (auto& key : moved) {
        const auto* owner = motion::findClip(project, key.clip);
        if (owner != nullptr) { key.time = movedKeyTime(owner->timing(project.tempo()), key.time, delta); }
    }
    // Nothing moves onto another key, nor when a key's track is locked.
    const auto nudged = processor.document.tryEditCoalesced(keys.size() > 1 ? "Nudge keys" : "Nudge key", "timeline.nudge.keys", [this, &keys, delta](motion::Project& updated) { return moveKeys(updated, keys, delta); });
    if (nudged) { selectedKeys = std::move(moved); }
    repaint();
    return true;
}

void MotionTimelineView::endKeyDrag() {
    auto* keyDrag = active<KeyDrag>();
    if (keyDrag == nullptr) { return; }
    if (keyDrag->changed && processor.document.revision() == keyDrag->revision) {
        processor.document.commit(keyDrag->keys.size() > 1 ? "Move keys" : "Move key", std::move(keyDrag->before));
    }
    gesture = {};
}

void MotionTimelineView::cancelKeyDrag() {
    auto* keyDrag = active<KeyDrag>();
    if (keyDrag != nullptr && keyDrag->changed && processor.document.revision() == keyDrag->revision) {
        selectedKeys = keyDrag->keys;
        processor.document.preview(std::move(keyDrag->before));
    }
    // A marquee given up leaves the keys selected as they were.
    const auto* marquee = active<KeyMarquee>();
    if (marquee != nullptr) { selectedKeys = marquee->before; }
    if (keyDrag != nullptr || marquee != nullptr) { gesture = {}; }
    repaint();
}

bool MotionTimelineView::easeSelectedKeys(bool in, bool out) {
    if (selectedKeys.empty()) { return false; }
    const auto keys = selectedKeys;
    const auto eased = processor.document.tryEdit(in && out ? "Easy ease" : (in ? "Easy ease in" : "Easy ease out"), [&keys, in, out](motion::Project& project) {
        if (keysLocked(project, keys)) { return false; }
        bool any = false;
        changeKeyedClips(project, keys, [&](motion::Clip& clip) {
            for (const auto& key : keys) {
                if (key.clip != clip.id) { continue; }
                const auto found = clip.properties.find(key.property);
                any = (found != clip.properties.end() && motion::easeKey(found->second, key.time, in, out)) || any;
            }
        });
        return any;
    });
    repaint();
    return eased;
}

void MotionTimelineView::deleteSelectedKeys() {
    const auto keys = selectedKeys;
    const auto removed = processor.document.tryEdit(keys.size() > 1 ? "Delete keys" : "Delete key", [&keys](motion::Project& project) {
        if (keysLocked(project, keys)) { return false; }
        bool any = false;
        changeKeyedClips(project, keys, [&](motion::Clip& clip) {
            for (const auto& key : keys) {
                if (key.clip != clip.id) { continue; }
                const auto found = clip.properties.find(key.property);
                if (found != clip.properties.end()) { any = found->second.removeKey(key.time) || any; }
            }
        });
        return any;
    });
    if (removed) { selectedKeys.clear(); }
    repaint();
}

void MotionTimelineView::addKeyAt(const Row& lane, int x, juce::ModifierKeys modifiers) {
    const auto& project = processor.document.project();
    const auto time = snapTime(scrollTime + (x - namesWidth) / pixelsPerSecond, modifiers);
    const auto& track = project.tracks[static_cast<std::size_t>(lane.track)];
    const motion::Clip* target = nullptr;
    for (const auto& clip : track.clips) {
        const auto timing = clip.timing(project.tempo());
        if (time >= timing.start && time <= timing.end()) { target = &clip; }
    }
    if (target == nullptr || track.locked) { return; }
    const auto clipId = target->id;
    const auto local = target->timing(project.tempo()).localTime(time);
    const auto property = lane.lane;
    const auto added = processor.document.tryEdit("Add key", [&](motion::Project& updated) {
        auto* curve = motion::findPropertyCurve(updated, clipId, property);
        if (curve == nullptr) { return false; }
        curve->setKeyValue(local, curve->evaluateBase(local));
        return true;
    });
    if (added) { selectedKeys = {{clipId, property, local}}; }
    repaint();
}

void MotionTimelineView::showKeyMenu() {
    if (selectedKeys.empty()) { return; }
    juce::PopupMenu menu;
    const char* names[] = {"Hold", "Linear", "Auto", "Bezier"};
    for (int index = 0; index < 4; ++index) {
        juce::PopupMenu::Item item(names[index]);
        item.itemID = index + 1;
        item.image = motion::ui::menu_icons::interpolation(static_cast<motion::Interpolation>(index));
        menu.addItem(std::move(item));
    }
    menu.addSeparator();
    // In line with the shapes' names.
    juce::PopupMenu::Item remove("Delete");
    remove.itemID = 10;
    remove.image = motion::ui::menu_icons::blank();
    menu.addItem(std::move(remove));
    motion::ui::showDocumentMenu(menu, *this, processor.document, juce::PopupMenu::Options().withTargetComponent(this).withMousePosition(), [this](int result) {
        if (result == 10) { deleteSelectedKeys(); return; }
        const auto shape = static_cast<motion::Interpolation>(result - 1);
        const auto keys = selectedKeys;
        processor.document.tryEdit("Change key interpolation", [&](motion::Project& project) {
            bool any = false;
            changeKeyedClips(project, keys, [&](motion::Clip& clip) {
                for (const auto& key : keys) {
                    if (key.clip != clip.id) { continue; }
                    const auto found = clip.properties.find(key.property);
                    if (found != clip.properties.end()) { any = motion::keyedit::setInterpolation(found->second, key.time, shape) || any; }
                }
            });
            return any;
        });
    });
}
