#pragma once

#include "MotionStyle.h"

#include "../MotionProcessor.h"

// A camera's rig, shown above its transform and lens in Properties: what it
// aims at, what carries it, and a cut to it at the playhead. Cuts themselves
// live in the timeline's Cameras band.
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
        showing.setFont(motion::style::body());
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
        cutButton.setButtonText("Cut to it at the playhead");
        addChildComponent(cutButton);
    }

    void setCamera(motion::Id id) {
        camera = id;
        refresh();
    }
    int preferredHeight() const { return camera == 0 ? 0 : motion::style::inspector::headingHeight + 3 * motion::style::controlHeight + 3 * motion::style::gap; }

    void refresh() {
        const auto& project = processor.document.project();
        const auto* found = findCamera(project, camera);
        refreshRig(project, found);
        // On screen already, or a cut away to it.
        const auto onScreen = activeCamera() == camera;
        showing.setVisible(onScreen);
        cutButton.setVisible(!onScreen);
        cutButton.setEnabled(found != nullptr && frameTime() < project.duration);
    }
    void resized() override {
        auto area = getLocalBounds();
        title.setBounds(area.removeFromTop(motion::style::inspector::headingHeight));
        area.removeFromTop(motion::style::gap);
        // Choices end where Properties' value columns do.
        const motion::style::PropertyGrid grid(area.getWidth());
        const auto right = grid.value + grid.column;
        for (auto [label, box] : {std::pair<juce::Label*, juce::Component*> {&lookAtLabel, &lookAt}, {&parentLabel, &parent}}) {
            auto row = area.removeFromTop(motion::style::controlHeight).withWidth(right);
            area.removeFromTop(motion::style::gap);
            label->setBounds(row.removeFromLeft(56));
            box->setBounds(row);
        }
        auto row = area.removeFromTop(motion::style::controlHeight).withWidth(right);
        shotLabel.setBounds(row.removeFromLeft(56));
        showing.setBounds(row);
        cutButton.setBounds(row.withWidth(cutButton.getBestWidthForHeight(row.getHeight()) + 20));
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
            for (const auto& clip : track.clips) { targets.emplace_back(clip.id, juce::String(clip.name)); }
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

    MotionProcessor& processor;
    motion::Id camera = 0;
    juce::ComboBox lookAt, parent;
    juce::Label title, lookAtLabel, parentLabel, shotLabel, showing;
    std::vector<std::pair<motion::Id, juce::String>> listedTargets, listedParents;
    juce::TextButton cutButton;
};
