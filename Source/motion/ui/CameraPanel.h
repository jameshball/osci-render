#pragma once

#include "../MotionProcessor.h"
#include "PropertyInspector.h"

// Output cameras: choose, add, rename and delete cameras, place cuts on the
// output, review the cut list, and edit the chosen camera's animated lens and
// transform through the shared property inspector.
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
        cutButton.setButtonText("Cut here");
        cutButton.setTooltip("Use this camera from the current frame until the next camera cut");
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
        auto active = project.cameras.empty() ? 0 : project.cameras.front().id;
        for (const auto& cut : project.cameraCuts) {
            if (cut.contains(processor.position.load())) { active = cut.camera; break; }
        }
        const auto* output = findCamera(project, active);
        const auto prefix = processor.document.editingComposition() == 0 ? "On output: " : "Preview only: ";
        status.setText(juce::String(prefix) + (output == nullptr ? "default view" : juce::String(output->name) + (active == selected ? " (editing)" : "")), juce::dontSendNotification);
        cutButton.setEnabled(camera != nullptr && frameTime() < project.duration);
        if (cuts != project.cameraCuts) { cuts = project.cameraCuts; rebuildCuts(); }
        repaint();
    }

    void paint(juce::Graphics& g) override {
        g.setFont(motion::style::small());
        g.setColour(motion::style::muted());
        g.drawText("Cuts", cutsArea.withHeight(16), juce::Justification::centredLeft);
        if (cutRows.empty()) {
            g.setColour(motion::style::subtle());
            g.drawText("No cuts: the first camera is used throughout.", cutsArea.withTrimmedTop(16).withHeight(20), juce::Justification::centredLeft);
        }
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
        auto output = area.removeFromTop(motion::style::controlHeight);
        cutButton.setBounds(output.removeFromRight(76));
        status.setBounds(output);
        area.removeFromTop(motion::style::padding);
        const auto listHeight = 18 + std::max(20, std::min(5, static_cast<int>(cutRows.size())) * 22);
        cutsArea = area.removeFromTop(listHeight);
        auto rows = cutsArea.withTrimmedTop(18);
        for (std::size_t index = 0; index < cutRows.size(); ++index) {
            cutRows[index]->setVisible(index < 5);
            if (index < 5) { cutRows[index]->setBounds(rows.removeFromTop(22)); }
        }
        area.removeFromTop(motion::style::padding);
        inspector.setBounds(area.withTrimmedLeft(-motion::style::padding - 2).withTrimmedRight(-motion::style::padding - 2));
    }

private:
    // One cut: click to seek to it; the cross removes it.
    struct CutRow final : juce::Component {
        juce::String text;
        std::function<void()> onSeek, onRemove;
        juce::TextButton remove {"x"};
        CutRow() {
            remove.setTooltip("Remove this cut");
            remove.onClick = [this] { if (onRemove) { onRemove(); } };
            addAndMakeVisible(remove);
        }
        void paint(juce::Graphics& g) override {
            g.setColour(isMouseOver() ? motion::style::raised() : juce::Colours::transparentBlack);
            g.fillRoundedRectangle(getLocalBounds().toFloat(), motion::style::radius);
            g.setColour(motion::style::text());
            g.setFont(motion::style::body());
            g.drawText(text, getLocalBounds().reduced(6, 0).withTrimmedRight(22), juce::Justification::centredLeft);
        }
        void resized() override { remove.setBounds(getLocalBounds().removeFromRight(20).reduced(1, 2)); }
        void mouseUp(const juce::MouseEvent& event) override { if (event.eventComponent == this && onSeek) { onSeek(); } }
        void mouseEnter(const juce::MouseEvent&) override { repaint(); }
        void mouseExit(const juce::MouseEvent&) override { repaint(); }
    };

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
    void rebuildCuts() {
        cutRows.clear();
        const auto& project = processor.document.project();
        const auto grid = project.timeGrid();
        for (const auto& cut : cuts) {
            auto row = std::make_unique<CutRow>();
            const auto* camera = findCamera(project, cut.camera);
            row->text = juce::String(grid.positionLabel(cut.start)) + "   " + (camera != nullptr ? juce::String(camera->name) : juce::String("?"));
            row->setName("Camera cut " + juce::String(cut.id));
            row->remove.setName("Remove camera cut " + juce::String(cut.id));
            const auto id = cut.id;
            const auto start = cut.start;
            row->onSeek = [this, start] { processor.seek(start); };
            row->onRemove = [this, id] { removeCut(id); };
            addAndMakeVisible(*row);
            cutRows.push_back(std::move(row));
        }
        resized();
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
    void removeCut(motion::Id id) {
        processor.document.tryEdit("Remove camera cut", [id](motion::Project& project) {
            const auto before = project.cameraCuts.size();
            std::erase_if(project.cameraCuts, [id](const auto& cut) { return cut.id == id; });
            return project.cameraCuts.size() != before;
        });
        refresh();
    }
    void cutHere() {
        const auto& project = processor.document.project();
        const auto time = frameTime();
        if (findCamera(project, selected) == nullptr || time >= project.duration) { return; }
        const auto cameraId = selected;
        const auto cutId = processor.document.newId();
        processor.document.tryEdit("Cut to camera", [time, cameraId, cutId](motion::Project& updated) {
            if (findCamera(updated, cameraId) == nullptr) { return false; }
            auto end = updated.duration;
            for (const auto& cut : updated.cameraCuts) {
                if (cut.start > time) { end = std::min(end, cut.start); }
            }
            std::erase_if(updated.cameraCuts, [time](const auto& cut) { return cut.start == time; });
            for (auto& cut : updated.cameraCuts) {
                if (cut.start < time && cut.end() > time) { cut.duration = motion::spanUntil(cut.start, time); }
            }
            updated.cameraCuts.push_back({ cutId, cameraId, time, motion::spanUntil(time, end) });
            std::sort(updated.cameraCuts.begin(), updated.cameraCuts.end(), [](const auto& left, const auto& right) { return left.start < right.start; });
            return true;
        });
        refresh();
    }

    MotionProcessor& processor;
    motion::Id selected = 0, editingName = 0;
    std::vector<std::pair<motion::Id, std::string>> listedCameras;
    std::vector<motion::CameraCut> cuts;
    std::vector<std::unique_ptr<CutRow>> cutRows;
    juce::Rectangle<int> cutsArea;
    juce::ComboBox cameraChoice;
    juce::TextButton addButton, removeButton, cutButton;
    juce::Label name, status;
    MotionPropertyInspector inspector;
};
