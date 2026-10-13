#pragma once

#include "MotionStyle.h"

#include "../MotionProcessor.h"
#include "../model/KeyEdit.h"

// A camera's rig, shown above its transform and lens in Properties: what it
// aims at, what carries it, a cut to it and a key of all of it at the
// playhead. Cuts themselves live in the timeline's Cameras band.
class MotionCameraRig final : public juce::Component {
public:
    explicit MotionCameraRig(MotionProcessor& owner) : processor(owner) {
        setName("Camera rig");
        for (auto* box : {&lookAt, &parent}) {
            box->setTextWhenNothingSelected("None");
            addAndMakeVisible(box);
        }
        lookAt.setName("Camera look at");
        lookAt.setTitle("Camera look at");
        lookAt.setTooltip("Aim this camera at an object or group; its Z rotation becomes roll");
        parent.setName("Camera parent");
        parent.setTitle("Camera parent");
        parent.setTooltip("Carry this camera with a group's transform");
        lookAt.onChange = [this] { applyRig(); };
        parent.onChange = [this] { applyRig(); };
        motion::style::inspector::styleHeading(title, "Rig");
        addAndMakeVisible(title);
        for (auto [label, text] : {std::pair {&lookAtLabel, "Look at"}, std::pair {&parentLabel, "Parent"}, std::pair {&shotLabel, "Shot"}}) {
            motion::style::inspector::styleCaption(*label, text);
            addAndMakeVisible(label);
        }
        // A status, not a value: caption-sized and muted.
        showing.setFont(motion::style::caption());
        showing.setColour(juce::Label::textColourId, osci::Colours::textMuted());
        showing.setBorderSize({});
        showing.setText("On screen at the playhead", juce::dontSendNotification);
        addChildComponent(showing);
        cutButton.setName("Cut to camera here");
        cutButton.setTooltip("Show this camera from the playhead until the next cut");
        cutButton.onClick = [this] {
            motion::Id cut = 0;
            processor.document.cutToCamera(camera, frameTime(), cut);
            refresh();
        };
        cutButton.setButtonText("Cut to camera here");
        addChildComponent(cutButton);
        // The shot's keys as any property's: a diamond between the previous
        // and next key, in the column the property rows keep theirs.
        keyButton.setName("Key camera shot");
        keyButton.setTitle("Key camera shot");
        keyButton.setColour(osci::KeyframeButton::keyColourId, osci::Colours::text());
        keyButton.setWantsKeyboardFocus(false);
        keyButton.onClick = [this] { toggleKey(); };
        previousKey.setTooltip("Go to the camera's previous key");
        nextKey.setTooltip("Go to the camera's next key");
        previousKey.onClick = [this] { jumpToKey(false); };
        nextKey.onClick = [this] { jumpToKey(true); };
        for (auto* button : std::initializer_list<juce::Component*> {&keyButton, &previousKey, &nextKey}) { addAndMakeVisible(button); }
    }

    void setCamera(motion::Id id) {
        camera = id;
        refresh();
    }
    // Rows, then the same room under them as a section of properties leaves.
    int preferredHeight() const { return camera == 0 ? 0 : motion::style::inspector::headingHeight + 3 * motion::style::controlHeight + 3 * motion::style::gap + motion::style::padding; }

    void refresh() {
        const auto& project = processor.document.project();
        const auto* found = findCamera(project, camera);
        refreshRig(project, found);
        // On screen already, or a cut away to it.
        const auto onScreen = activeCamera() == camera;
        showing.setVisible(onScreen);
        cutButton.setVisible(!onScreen);
        cutButton.setEnabled(found != nullptr && frameTime() < project.duration);
        // One click keys position, rotation and lens together; on a key, it
        // takes it away again.
        const auto keyed = isKeyed(found);
        bool animated = false, earlier = false, later = false;
        const auto now = frameTime();
        forEachShotCurve(found, [&](const motion::Curve& curve) {
            animated = animated || curve.animated();
            for (const auto& key : curve.keyframes()) {
                earlier = earlier || key.time < now - 1.0e-6;
                later = later || key.time > now + 1.0e-6;
            }
        });
        keyButton.setEnabled(found != nullptr);
        keyButton.setState(keyed ? osci::KeyframeButton::State::keyed : animated ? osci::KeyframeButton::State::animated : osci::KeyframeButton::State::unanimated);
        keyButton.setTooltip(keyed ? "Remove the shot's keys at the playhead: position, rotation and field of view" : "Key the shot at the playhead: position, rotation and field of view together");
        previousKey.setVisible(animated);
        nextKey.setVisible(animated);
        previousKey.setEnabled(earlier);
        nextKey.setEnabled(later);
    }
    void resized() override {
        auto area = getLocalBounds();
        title.setBounds(area.removeFromTop(motion::style::inspector::headingHeight));
        area.removeFromTop(motion::style::gap);
        // Choices span the Y and Z columns, like the Scope's overlay.
        const motion::style::PropertyGrid grid(area.getWidth());
        const auto right = grid.value + grid.column, left = grid.column + motion::style::gap;
        for (auto [label, box] : {std::pair<juce::Label*, juce::Component*> {&lookAtLabel, &lookAt}, {&parentLabel, &parent}}) {
            auto row = area.removeFromTop(motion::style::controlHeight).withWidth(right);
            area.removeFromTop(motion::style::gap);
            label->setBounds(row.removeFromLeft(left));
            box->setBounds(row);
        }
        auto row = area.removeFromTop(motion::style::controlHeight);
        area.removeFromTop(motion::style::gap);
        nextKey.setBounds(row.removeFromRight(12));
        keyButton.setBounds(row.removeFromRight(18));
        previousKey.setBounds(row.removeFromRight(12));
        row = row.withWidth(right);
        shotLabel.setBounds(row.removeFromLeft(left));
        showing.setBounds(row);
        cutButton.setBounds(row);
    }

private:
    static const motion::Camera* findCamera(const motion::Project& project, motion::Id id) {
        const auto found = std::find_if(project.cameras.begin(), project.cameras.end(), [id](const auto& item) { return item.id == id; });
        return found == project.cameras.end() ? nullptr : &*found;
    }
    double frameTime() const { return processor.document.project().frameTime(processor.position.load()); }
    motion::Id activeCamera() const {
        const auto& project = processor.document.project();
        for (const auto& cut : project.cameraCuts) {
            if (cut.contains(processor.position.load())) { return cut.camera; }
        }
        return project.cameras.empty() ? 0 : project.cameras.front().id;
    }
    // Look-at choices: visual clips and groups; parents: groups only.
    void refreshRig(const motion::Project& project, const motion::Camera* found) {
        std::vector<std::pair<motion::Id, juce::String>> targets, parents;
        for (const auto& group : project.groups) {
            targets.emplace_back(group.id, "Group: " + juce::String(group.name));
            parents.emplace_back(group.id, juce::String(group.name));
        }
        for (const auto& track : project.tracks) {
            if (track.kind != motion::TrackKind::visual) { continue; }
            // A composition's instance has no one place to aim at.
            for (const auto& clip : track.clips) {
                if (clip.composition == 0) { targets.emplace_back(clip.id, juce::String(clip.name)); }
            }
        }
        const auto fill = [](juce::ComboBox& box, std::vector<std::pair<motion::Id, juce::String>>& listed, std::vector<std::pair<motion::Id, juce::String>> items, motion::Id current) {
            if (items != listed) {
                listed = std::move(items);
                box.clear(juce::dontSendNotification);
                box.addItem("None", 1);
                for (std::size_t index = 0; index < listed.size(); ++index) { box.addItem(listed[index].second, static_cast<int>(index) + 2); }
            }
            const auto at = std::find_if(listed.begin(), listed.end(), [current](const auto& item) { return item.first == current; });
            box.setSelectedId(at == listed.end() ? 1 : static_cast<int>(at - listed.begin()) + 2, juce::dontSendNotification);
        };
        fill(lookAt, listedTargets, std::move(targets), found != nullptr ? found->target : 0);
        fill(parent, listedParents, std::move(parents), found != nullptr ? found->parent : 0);
        parent.setEnabled(!listedParents.empty());
    }
    void applyRig() {
        const auto choice = [](const juce::ComboBox& box, const std::vector<std::pair<motion::Id, juce::String>>& listed) {
            const auto index = box.getSelectedId() - 2;
            return index >= 0 && index < static_cast<int>(listed.size()) ? listed[static_cast<std::size_t>(index)].first : motion::Id(0);
        };
        processor.document.setCameraRig(camera, choice(lookAt, listedTargets), choice(parent, listedParents));
        refresh();
    }

    // A key on every camera property at the playhead (linked ones follow
    // their source and are left alone).
    // The shot: where the camera is, which way it looks and its lens; not
    // what it hides or how it shades, which are keyed on their own.
    static bool isShot(std::string_view property) { return property.starts_with("position.") || property.starts_with("rotation.") || property == "fov"; }
    template <typename Visit>
    static void forEachShotCurve(const motion::Camera* found, Visit&& visit) {
        if (found == nullptr) { return; }
        for (const auto& [property, curve] : found->properties) {
            if (isShot(property)) { visit(curve); }
        }
    }
    bool isKeyed(const motion::Camera* found) const {
        if (found == nullptr) { return false; }
        const auto time = frameTime();
        return std::all_of(motion::cameraPropertySpecs.begin(), motion::cameraPropertySpecs.end(), [&](const auto& spec) {
            if (!isShot(spec.id)) { return true; }
            const auto curve = found->properties.find(spec.id);
            return curve != found->properties.end() && (curve->second.link.has_value() || curve->second.hasKeyAt(time));
        });
    }
    void jumpToKey(bool forward) {
        const auto now = frameTime();
        std::optional<double> best;
        forEachShotCurve(findCamera(processor.document.project(), camera), [&](const motion::Curve& curve) {
            for (const auto& key : curve.keyframes()) {
                const bool candidate = forward ? key.time > now + 1.0e-6 : key.time < now - 1.0e-6;
                if (candidate && (!best.has_value() || (forward ? key.time < *best : key.time > *best))) { best = key.time; }
            }
        });
        if (best.has_value()) { processor.seek(std::clamp(*best, 0.0, processor.document.project().duration)); }
    }
    void toggleKey() {
        const auto* found = findCamera(processor.document.project(), camera);
        if (found == nullptr) { return; }
        const auto time = frameTime();
        const auto keyed = isKeyed(found);
        const auto id = camera;
        processor.document.edit(keyed ? "Remove camera keys" : "Key camera", [id, time, keyed](motion::Project& project) {
            for (auto& item : project.cameras) {
                if (item.id != id) { continue; }
                for (const auto& spec : motion::cameraPropertySpecs) {
                    if (!isShot(spec.id)) { continue; }
                    auto& curve = item.properties[std::string(spec.id)];
                    // Without keys the camera keeps the pose it had here.
                    if (!curve.link.has_value()) { motion::keyedit::setKeyed(curve, time, !keyed); }
                }
            }
        });
        refresh();
    }

    MotionProcessor& processor;
    motion::Id camera = 0;
    juce::ComboBox lookAt, parent;
    juce::Label title, lookAtLabel, parentLabel, shotLabel, showing;
    std::vector<std::pair<motion::Id, juce::String>> listedTargets, listedParents;
    juce::TextButton cutButton;
    osci::KeyframeButton keyButton;
    motion::style::ChevronButton previousKey {"Previous camera key", false}, nextKey {"Next camera key", true};
};
