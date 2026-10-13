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
        hiddenLines.addItem("Drawn", 1);
        hiddenLines.addItem("Removed", 2);
        hiddenLines.setName("Camera hidden lines");
        hiddenLines.setTitle("Camera hidden lines");
        hiddenLines.setTooltip("Hide the lines this camera can't see, behind the faces of 3D meshes, as a solid object would. Baked in the background as you edit.");
        hiddenLines.onChange = [this] { applyHiddenLines(); };
        addAndMakeVisible(hiddenLines);
        motion::style::inspector::styleHeading(title, "Rig");
        addAndMakeVisible(title);
        for (auto [label, text] : {std::pair {&lookAtLabel, "Look at"}, std::pair {&parentLabel, "Parent"}, std::pair {&hiddenLabel, "Hidden lines"}, std::pair {&shotLabel, "Shot"}, std::pair {&keysLabel, "Keys"}}) {
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
        keyButton.setName("Key camera");
        keyButton.onClick = [this] { toggleKey(); };
        addAndMakeVisible(keyButton);
    }

    void setCamera(motion::Id id) {
        camera = id;
        refresh();
    }
    // Rows, then the same room under them as a section of properties leaves.
    int preferredHeight() const { return camera == 0 ? 0 : motion::style::inspector::headingHeight + 5 * motion::style::controlHeight + 5 * motion::style::gap + motion::style::padding; }

    void refresh() {
        const auto& project = processor.document.project();
        const auto* found = findCamera(project, camera);
        refreshRig(project, found);
        hiddenLines.setSelectedId(found != nullptr && found->hideHiddenLines ? 2 : 1, juce::dontSendNotification);
        // On screen already, or a cut away to it.
        const auto onScreen = activeCamera() == camera;
        showing.setVisible(onScreen);
        cutButton.setVisible(!onScreen);
        cutButton.setEnabled(found != nullptr && frameTime() < project.duration);
        // One click keys position, rotation and lens together; on a key, it
        // takes it away again.
        const auto keyed = isKeyed(found);
        keyButton.setEnabled(found != nullptr);
        keyButton.setButtonText(keyed ? "Remove keys" : "Key all");
        keyButton.setTooltip(keyed ? "Remove the position, rotation and field of view keys at the playhead" : "Key position, rotation and field of view at the playhead");
    }
    void resized() override {
        auto area = getLocalBounds();
        title.setBounds(area.removeFromTop(motion::style::inspector::headingHeight));
        area.removeFromTop(motion::style::gap);
        // Choices span the Y and Z columns, like the Scope's overlay.
        const motion::style::PropertyGrid grid(area.getWidth());
        const auto right = grid.value + grid.column, left = grid.column + motion::style::gap;
        for (auto [label, box] : {std::pair<juce::Label*, juce::Component*> {&lookAtLabel, &lookAt}, {&parentLabel, &parent}, {&hiddenLabel, &hiddenLines}}) {
            auto row = area.removeFromTop(motion::style::controlHeight).withWidth(right);
            area.removeFromTop(motion::style::gap);
            label->setBounds(row.removeFromLeft(left));
            box->setBounds(row);
        }
        auto row = area.removeFromTop(motion::style::controlHeight).withWidth(right);
        area.removeFromTop(motion::style::gap);
        shotLabel.setBounds(row.removeFromLeft(left));
        showing.setBounds(row);
        cutButton.setBounds(row);
        row = area.removeFromTop(motion::style::controlHeight).withWidth(right);
        keysLabel.setBounds(row.removeFromLeft(left));
        keyButton.setBounds(row);
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
    bool isKeyed(const motion::Camera* found) const {
        if (found == nullptr) { return false; }
        const auto time = frameTime();
        return std::all_of(motion::cameraPropertySpecs.begin(), motion::cameraPropertySpecs.end(), [&](const auto& spec) {
            const auto curve = found->properties.find(spec.id);
            return curve != found->properties.end() && (curve->second.link.has_value() || curve->second.hasKeyAt(time));
        });
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
                    auto& curve = item.properties[std::string(spec.id)];
                    // Without keys the camera keeps the pose it had here.
                    if (!curve.link.has_value()) { motion::keyedit::setKeyed(curve, time, !keyed); }
                }
            }
        });
        refresh();
    }

    void applyHiddenLines() {
        const auto hide = hiddenLines.getSelectedId() == 2;
        const auto id = camera;
        processor.document.edit(hide ? "Remove hidden lines" : "Draw hidden lines", [id, hide](motion::Project& project) {
            for (auto& item : project.cameras) {
                if (item.id == id) { item.hideHiddenLines = hide; }
            }
        });
        refresh();
    }

    MotionProcessor& processor;
    motion::Id camera = 0;
    juce::ComboBox lookAt, parent, hiddenLines;
    juce::Label title, lookAtLabel, parentLabel, hiddenLabel, shotLabel, keysLabel, showing;
    std::vector<std::pair<motion::Id, juce::String>> listedTargets, listedParents;
    juce::TextButton cutButton, keyButton;
};
