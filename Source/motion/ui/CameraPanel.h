#pragma once

#include "../MotionProcessor.h"
#include "PropertyInspector.h"

// Output cameras: choose, add, rename and delete cameras, aim or parent them,
// cut to them at the playhead (cuts are edited in the timeline's Cameras
// band), and edit the chosen camera's lens and transform.
class MotionCameraPanel : public juce::Component {
public:
    explicit MotionCameraPanel(MotionProcessor& owner) : processor(owner), inspector(owner) {
        setName("Camera inspector");
        cameraChoice.setName("Camera to edit");
        cameraChoice.setTextWhenNothingSelected("No cameras");
        cameraChoice.onChange = [this] {
            const auto index = cameraChoice.getSelectedItemIndex();
            if (index >= 0 && index < static_cast<int>(listedCameras.size())) {
                name.hideEditor(true);
                selected = listedCameras[static_cast<std::size_t>(index)].first;
                refresh();
            }
        };
        addAndMakeVisible(cameraChoice);
        addButton.setButtonText("Add");
        addButton.setTooltip("Add a camera; use Cut here to put it on the output");
        addButton.onClick = [this] { addCamera(); };
        addAndMakeVisible(addButton);
        removeButton.setButtonText("Delete");
        removeButton.setTooltip("Delete this camera and its cuts");
        removeButton.onClick = [this] { removeCamera(); };
        addAndMakeVisible(removeButton);
        name.setName("Camera name");
        name.setEditable(false, true);
        name.setFont(motion::style::title());
        name.setColour(juce::Label::backgroundColourId, motion::style::field());
        name.onEditorShow = [this] { editingName = selected; };
        name.onTextChange = [this] {
            const auto id = editingName;
            const auto text = name.getText().trim();
            const auto* camera = findCamera(processor.document.project(), id);
            if (camera != nullptr && text.isNotEmpty() && camera->name != text.toStdString()) {
                processor.document.edit("Rename camera", [id, text](motion::Project& project) {
                    auto* target = findCamera(project, id);
                    if (target != nullptr) { target->name = text.toStdString(); }
                });
            }
            refresh();
        };
        addAndMakeVisible(name);
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
        for (auto* label : {&lookAtLabel, &parentLabel}) {
            label->setFont(motion::style::small());
            label->setColour(juce::Label::textColourId, motion::style::muted());
            addAndMakeVisible(label);
        }
        lookAtLabel.setText("Look at", juce::dontSendNotification);
        parentLabel.setText("Parent", juce::dontSendNotification);
        cutButton.setButtonText("Cut here");
        cutButton.setTooltip("Use this camera from the current frame until the next cut. Cuts appear in the timeline's Cameras band, where they move, trim and switch camera.");
        cutButton.onClick = [this] { cutHere(); };
        addAndMakeVisible(cutButton);
        status.setFont(motion::style::small());
        status.setColour(juce::Label::textColourId, motion::style::muted());
        addAndMakeVisible(status);
        inspector.setShowsHeader(false);
        inspector.setNamePrefix("camera ");
        inspector.onPropertySelected = [this](motion::Id id, const std::string& property) { if (onPropertySelected) { onPropertySelected(id, property); } };
        addAndMakeVisible(inspector);
        refresh();
    }

    std::function<void(motion::Id, std::string)> onPropertySelected;
    motion::Id selectedCameraId() const { return selected; }
    void restoreSelection(motion::Id id) { selected = id; refresh(); }

    void refresh() {
        const auto& project = processor.document.project();
        const auto* camera = findCamera(project, selected);
        if (camera == nullptr) {
            name.hideEditor(true);
            selected = project.cameras.empty() ? 0 : project.cameras.front().id;
            camera = findCamera(project, selected);
        }
        std::vector<std::pair<motion::Id, std::string>> choices;
        for (const auto& item : project.cameras) { choices.emplace_back(item.id, item.name); }
        if (choices != listedCameras) {
            listedCameras = std::move(choices);
            cameraChoice.clear(juce::dontSendNotification);
            for (std::size_t i = 0; i < listedCameras.size(); ++i) { cameraChoice.addItem(juce::String(listedCameras[i].second), static_cast<int>(i) + 1); }
        }
        const auto found = std::find_if(listedCameras.begin(), listedCameras.end(), [this](const auto& item) { return item.first == selected; });
        cameraChoice.setSelectedId(found == listedCameras.end() ? 0 : static_cast<int>(found - listedCameras.begin()) + 1, juce::dontSendNotification);
        name.setEnabled(camera != nullptr);
        removeButton.setEnabled(camera != nullptr);
        if (!name.isBeingEdited()) { name.setText(camera != nullptr ? juce::String(camera->name) : "Add a camera to begin", juce::dontSendNotification); }
        inspector.setTarget(selected);
        refreshRig(project, camera);
        auto active = project.cameras.empty() ? 0 : project.cameras.front().id;
        for (const auto& cut : project.cameraCuts) {
            if (cut.contains(processor.position.load())) { active = cut.camera; break; }
        }
        const auto* output = findCamera(project, active);
        const auto prefix = processor.document.editingComposition() == 0 ? "On output: " : "Preview only: ";
        status.setText(juce::String(prefix) + (output == nullptr ? "default view" : juce::String(output->name) + (active == selected ? " (editing)" : "")), juce::dontSendNotification);
        cutButton.setEnabled(camera != nullptr && frameTime() < project.duration);
        repaint();
    }
    void resized() override {
        auto area = getLocalBounds().reduced(motion::style::padding + 2, 6);
        auto chooser = area.removeFromTop(motion::style::controlHeight);
        removeButton.setBounds(chooser.removeFromRight(54));
        chooser.removeFromRight(motion::style::gap);
        addButton.setBounds(chooser.removeFromRight(44));
        chooser.removeFromRight(motion::style::gap);
        cameraChoice.setBounds(chooser);
        area.removeFromTop(motion::style::padding);
        name.setBounds(area.removeFromTop(motion::style::controlHeight));
        area.removeFromTop(motion::style::padding);
        for (auto [label, box] : {std::pair {&lookAtLabel, &lookAt}, std::pair {&parentLabel, &parent}}) {
            auto row = area.removeFromTop(motion::style::controlHeight);
            area.removeFromTop(motion::style::gap);
            label->setBounds(row.removeFromLeft(56));
            box->setBounds(row);
        }
        area.removeFromTop(motion::style::padding - motion::style::gap);
        auto output = area.removeFromTop(motion::style::controlHeight);
        cutButton.setBounds(output.removeFromRight(76));
        status.setBounds(output);
        area.removeFromTop(motion::style::padding);
        inspector.setBounds(area.withTrimmedLeft(-motion::style::padding - 2).withTrimmedRight(-motion::style::padding - 2));
    }

private:

    static const motion::Camera* findCamera(const motion::Project& project, motion::Id id) {
        const auto found = std::find_if(project.cameras.begin(), project.cameras.end(), [id](const auto& camera) { return camera.id == id; });
        return found == project.cameras.end() ? nullptr : &*found;
    }
    static motion::Camera* findCamera(motion::Project& project, motion::Id id) {
        const auto found = std::find_if(project.cameras.begin(), project.cameras.end(), [id](const auto& camera) { return camera.id == id; });
        return found == project.cameras.end() ? nullptr : &*found;
    }
    double frameTime() const {
        const auto& project = processor.document.project();
        const auto time = std::clamp(processor.position.load(), 0.0, project.duration);
        return project.frameRate > 0.0 ? std::clamp(std::round(time * project.frameRate) / project.frameRate, 0.0, project.duration) : time;
    }
    void addCamera() {
        name.hideEditor(true);
        motion::Camera camera;
        camera.id = processor.document.newId();
        camera.name = "Camera " + std::to_string(processor.document.project().cameras.size() + 1);
        selected = camera.id;
        processor.document.edit("Add camera", [camera](motion::Project& project) { project.cameras.push_back(camera); });
        refresh();
    }
    void removeCamera() {
        const auto id = selected;
        processor.document.tryEdit("Delete camera", [id](motion::Project& project) {
            const auto before = project.cameras.size();
            std::erase_if(project.cameras, [id](const auto& camera) { return camera.id == id; });
            std::erase_if(project.cameraCuts, [id](const auto& cut) { return cut.camera == id; });
            return project.cameras.size() != before;
        });
        refresh();
    }
    void cutHere() {
        motion::Id cut = 0;
        processor.document.cutToCamera(selected, frameTime(), cut);
        refresh();
    }
    // Look-at choices: visual clips and groups; parents: groups only.
    void refreshRig(const motion::Project& project, const motion::Camera* camera) {
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
            const auto found = std::find_if(listed.begin(), listed.end(), [current](const auto& item) { return item.first == current; });
            box.setSelectedId(found == listed.end() ? 1 : static_cast<int>(found - listed.begin()) + 2, juce::dontSendNotification);
        };
        fill(lookAt, listedTargets, std::move(targets), camera != nullptr ? camera->target : 0);
        fill(parent, listedParents, std::move(parents), camera != nullptr ? camera->parent : 0);
        lookAt.setEnabled(camera != nullptr);
        parent.setEnabled(camera != nullptr);
    }
    void applyRig() {
        const auto choice = [](const juce::ComboBox& box, const std::vector<std::pair<motion::Id, juce::String>>& listed) {
            const auto index = box.getSelectedId() - 2;
            return index >= 0 && index < static_cast<int>(listed.size()) ? listed[static_cast<std::size_t>(index)].first : motion::Id(0);
        };
        processor.document.setCameraRig(selected, choice(lookAt, listedTargets), choice(parent, listedParents));
        refresh();
    }

    MotionProcessor& processor;
    motion::Id selected = 0, editingName = 0;
    std::vector<std::pair<motion::Id, std::string>> listedCameras;
    juce::ComboBox cameraChoice, lookAt, parent;
    juce::Label lookAtLabel, parentLabel;
    std::vector<std::pair<motion::Id, juce::String>> listedTargets, listedParents;
    juce::TextButton addButton, removeButton, cutButton;
    juce::Label name, status;
    MotionPropertyInspector inspector;
};
