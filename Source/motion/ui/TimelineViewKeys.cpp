#include "TimelineView.h"
#include "MenuIcons.h"
#include "../model/KeyEdit.h"

// Key lanes: the keys a clip's properties have, selected, dragged, eased,
// added and deleted in place.

namespace {
// Each key's owner (a clip, an effect...) and its clock, through which its
// content time maps to the timeline's.
template <typename ProjectType>
auto ownerOf(ProjectType& project, motion::Id owner) { return motion::findPropertyTarget(project, owner); }
// True when one of `keys` belongs to something on a locked track.
template <typename Keys>
bool keysLocked(const motion::Project& project, const Keys& keys) {
    return std::any_of(keys.begin(), keys.end(), [&project](const auto& key) {
        const auto target = ownerOf(project, key.owner);
        return target.has_value() && target->locked;
    });
}
// Calls change(curve, key) for each of `keys` whose curve exists; true if any did.
template <typename Keys, typename Change>
bool changeKeys(motion::Project& project, const Keys& keys, Change&& change) {
    bool any = false;
    for (const auto& key : keys) {
        auto* curve = motion::findPropertyCurve(project, key.owner, key.property);
        if (curve != nullptr) { any = change(*curve, key) || any; }
    }
    return any;
}
}

std::optional<MotionTimelineView::KeyRef> MotionTimelineView::keyAt(juce::Point<int> point) const {
    const auto* lane = laneAtY(point.y);
    if (lane == nullptr || point.x < namesWidth) { return std::nullopt; }
    const auto& project = processor.document.project();
    std::optional<KeyRef> best;
    auto bestDistance = 7.0;
    for (const auto owner : lane->owners) {
        const auto target = ownerOf(project, owner);
        const auto* curve = target.has_value() ? target->curve(lane->property) : nullptr;
        if (curve == nullptr || target->clock().rate == 0) { continue; }
        for (const auto& key : curve->keyframes()) {
            const auto distance = std::abs(timeX(target->projectTime(key.time)) - point.x);
            if (distance < bestDistance) { bestDistance = distance; best = KeyRef{owner, lane->property, key.time}; }
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
        for (const auto owner : row.owners) {
            const auto target = ownerOf(project, owner);
            const auto* curve = target.has_value() ? target->curve(row.property) : nullptr;
            if (curve == nullptr || target->clock().rate == 0) { continue; }
            for (const auto& key : curve->keyframes()) { result.push_back({owner, row.property, key.time}); }
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
        for (const auto owner : row.owners) {
            const auto target = ownerOf(project, owner);
            const auto* curve = target.has_value() ? target->curve(row.property) : nullptr;
            if (curve == nullptr || target->clock().rate == 0) { continue; }
            for (const auto& key : curve->keyframes()) {
                const auto x = timeX(target->projectTime(key.time));
                if (x >= area.getX() && x <= area.getRight()) { result.push_back({owner, row.property, key.time}); }
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
    const KeySet chosen(keys);
    std::set<motion::Id> owners;
    for (const auto& key : keys) { owners.insert(key.owner); }
    for (const auto owner : owners) {
        auto target = ownerOf(project, owner);
        if (!target.has_value() || target->properties == nullptr) { continue; }
        const auto timing = target->clock();
        for (auto& [name, curve] : *target->properties) {
            std::vector<motion::Keyframe> moving;
            for (const auto& key : curve.keyframes()) {
                if (chosen.contains(owner, name, key.time)) { moving.push_back(key); }
            }
            if (moving.empty()) { continue; }
            for (const auto& key : moving) { curve.removeKey(key.time); }
            for (auto key : moving) {
                key.time = movedKeyTime(timing, key.time, delta);
                if (curve.hasKeyAt(key.time)) {
                    collided = true;
                    continue;
                }
                curve.setKey(key);
            }
        }
    }
    return !collided;
}

void MotionTimelineView::dragKeys(int x, juce::ModifierKeys modifiers) {
    auto* keyDrag = active<KeyDrag>();
    if (keyDrag == nullptr) { return; }
    if (edit.stale()) { gesture = {}; edit.reset(); return; }
    const auto& before = edit.start();
    const auto grabbed = ownerOf(before, keyDrag->grabbed.owner);
    if (!grabbed.has_value()) { return; }
    const auto timing = grabbed->clock();
    if (timing.rate == 0) { return; }
    const auto grabbedTime = timing.projectTime(keyDrag->grabbed.time);
    auto delta = (x - keyDrag->downX) / pixelsPerSecond;
    if (delta != 0) { delta = snapEdge(grabbedTime + delta, modifiers, before, {}, &keyDrag->keys) - grabbedTime; }
    auto updated = before;
    if (delta != 0 && !moveKeys(updated, keyDrag->keys, delta)) { return; }
    selectedKeys = keyDrag->keys;
    for (auto& key : selectedKeys) {
        const auto owner = ownerOf(before, key.owner);
        if (owner.has_value()) { key.time = movedKeyTime(owner->clock(), key.time, delta); }
    }
    edit.show(std::move(updated), delta != 0);
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
        const auto owner = ownerOf(project, key.owner);
        if (owner.has_value()) { key.time = movedKeyTime(owner->clock(), key.time, delta); }
    }
    // Nothing moves onto another key, nor when a key's track is locked.
    const auto nudged = processor.document.tryEditCoalesced(keys.size() > 1 ? "Nudge keys" : "Nudge key", "timeline.nudge.keys", [this, &keys, delta](motion::Project& updated) { return moveKeys(updated, keys, delta); });
    if (nudged) { selectedKeys = std::move(moved); }
    repaint();
    return true;
}

bool MotionTimelineView::nudgeSelectedClips(int frames) {
    if (gesture.index() != 0) { return true; }
    const auto& project = processor.document.project();
    const auto delta = frames / (project.frameRate > 0 ? project.frameRate : 30.0);
    report(processor.document.nudgeClips({selectedClips.begin(), selectedClips.end()}, delta));
    repaint();
    return true;
}

bool MotionTimelineView::easeSelectedKeys(bool in, bool out) {
    if (selectedKeys.empty()) { return false; }
    const auto keys = selectedKeys;
    const auto eased = processor.document.tryEdit(in && out ? "Easy ease" : (in ? "Easy ease in" : "Easy ease out"), [&keys, in, out](motion::Project& project) {
        if (keysLocked(project, keys)) { return false; }
        return changeKeys(project, keys, [in, out](motion::Curve& curve, const KeyRef& key) { return motion::easeKey(curve, key.time, in, out); });
    });
    repaint();
    return eased;
}

void MotionTimelineView::deleteSelectedKeys() {
    const auto keys = selectedKeys;
    const auto removed = processor.document.tryEdit(keys.size() > 1 ? "Delete keys" : "Delete key", [&keys](motion::Project& project) {
        if (keysLocked(project, keys)) { return false; }
        return changeKeys(project, keys, [](motion::Curve& curve, const KeyRef& key) { return curve.removeKey(key.time); });
    });
    if (removed) { selectedKeys.clear(); }
    repaint();
}

void MotionTimelineView::addKeyAt(const Row& lane, int x, juce::ModifierKeys modifiers) {
    const auto& project = processor.document.project();
    const auto time = snapTime(timeAt(x), modifiers);
    // The owner whose span holds the time (a later one wins, as it draws on top).
    std::optional<motion::Id> owner;
    double local = 0;
    for (const auto candidate : lane.owners) {
        const auto target = ownerOf(project, candidate);
        if (!target.has_value() || target->locked || time < target->start || time > target->end()) { continue; }
        owner = candidate;
        local = target->localTime(time);
    }
    if (!owner.has_value()) { return; }
    const auto id = *owner;
    const auto property = lane.property;
    const auto added = processor.document.tryEdit("Add key", [&](motion::Project& updated) {
        auto* curve = motion::findPropertyCurve(updated, id, property);
        if (curve == nullptr) { return false; }
        curve->setKeyValue(local, curve->evaluateBase(local));
        return true;
    });
    if (added) { selectedKeys = {{id, property, local}}; }
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
            // Keys on locked tracks keep theirs; the others change.
            std::vector<KeyRef> open;
            std::copy_if(keys.begin(), keys.end(), std::back_inserter(open), [&project](const auto& key) { return !keysLocked(project, std::array {key}); });
            return changeKeys(project, open, [shape](motion::Curve& curve, const KeyRef& key) { return motion::keyedit::setInterpolation(curve, key.time, shape); });
        });
    });
}
