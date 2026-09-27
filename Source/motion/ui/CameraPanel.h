#pragma once

#include "../MotionProcessor.h"
#include <array>
#include <cstdlib>
#include <optional>

class MotionCameraPanel : public juce::Component {
public:
    explicit MotionCameraPanel(MotionProcessor& owner) : processor(owner) {
        setName("Camera inspector");
        cameraChoice.setName("Camera to edit");
        cameraChoice.setTextWhenNothingSelected("No cameras");
        cameraChoice.onChange = [this] {
            const auto index = cameraChoice.getSelectedItemIndex();
            if (index >= 0 && index < static_cast<int>(listedCameras.size())) {
                finishEditors();
                selected = listedCameras[static_cast<std::size_t>(index)].first;
                refresh();
            }
        };
        addAndMakeVisible(cameraChoice);
        addButton.setButtonText("Add");
        addButton.setTooltip("Add a camera for editing; use Cut here to put it on output");
        addButton.onClick = [this] { addCamera(); };
        addAndMakeVisible(addButton);
        name.setName("Camera name");
        name.setEditable(false, true);
        name.setColour(juce::Label::backgroundColourId, osci::Colours::veryDark());
        name.onEditorShow = [this] { editingName = selected; };
        name.onTextChange = [this] {
            const auto id = editingName;
            const auto text = name.getText().trim();
            const auto* camera = findCamera(processor.document.project(), id);
            if (camera != nullptr && text.isNotEmpty() && camera->name != text.toStdString()) {
                processor.document.edit("Rename camera", [id, text](motion::Project& project) {
                    auto* target = findCamera(project, id);
                    if (target != nullptr) {
                        target->name = text.toStdString();
                    }
                });
            }
            refresh();
        };
        addAndMakeVisible(name);
        const std::array<const char*, 7> labels { "X", "Y", "Z", "X", "Y", "Z", "Field of view" };
        for (std::size_t i = 0; i < values.size(); ++i) {
            auto& label = captions[i];
            label.setText(labels[i], juce::dontSendNotification);
            label.setFont(12.0f);
            addAndMakeVisible(label);
            auto& value = values[i];
            value.setName("Camera " + juce::String(motion::cameraPropertyNames[i]));
            value.setComponentID("motion.camera." + juce::String(motion::cameraPropertyNames[i]));
            value.setEditable(false, true);
            value.setJustificationType(juce::Justification::centredRight);
            value.setColour(juce::Label::backgroundColourId, osci::Colours::veryDark());
            value.setFont(juce::FontOptions(13.0f));
            value.onEditorShow = [this, i] {
                editing[i] = EditTarget { selected, frameTime() };
                selectProperty(i);
            };
            value.onTextChange = [this, i] { setValue(i); };
            addAndMakeVisible(value);
            auto& key = keys[i];
            key.setName("Key camera " + juce::String(motion::cameraPropertyNames[i]));
            key.setButtonText(key.getName());
            key.setTooltip("Add or update a keyframe on the current project frame");
            key.onClick = [this, i] { addKey(i); };
            addAndMakeVisible(key);
        }
        cutButton.setButtonText("Cut here");
        cutButton.setTooltip("Use this camera from the current frame until the next camera cut");
        cutButton.onClick = [this] { cutHere(); };
        addAndMakeVisible(cutButton);
        status.setFont(11.0f);
        status.setJustificationType(juce::Justification::centredLeft);
        addAndMakeVisible(status);
        frameLabel.setFont(11.0f);
        frameLabel.setJustificationType(juce::Justification::centredRight);
        addAndMakeVisible(frameLabel);
        refresh();
    }

    std::function<void(motion::Id, std::string)> onPropertySelected;
    motion::Id selectedCameraId() const { return selected; }
    void restoreSelection(motion::Id id) { selected = id; refresh(); }

    void refresh() {
        const auto& project = processor.document.project();
        const auto* camera = findCamera(project, selected);
        if (camera == nullptr) {
            finishEditors();
            selected = project.cameras.empty() ? 0 : project.cameras.front().id;
            camera = findCamera(project, selected);
        }
        std::vector<std::pair<motion::Id, std::string>> choices;
        for (const auto& item : project.cameras) {
            choices.emplace_back(item.id, item.name);
        }
        if (choices != listedCameras) {
            listedCameras = std::move(choices);
            cameraChoice.clear(juce::dontSendNotification);
            for (std::size_t i = 0; i < listedCameras.size(); ++i) {
                cameraChoice.addItem(juce::String(listedCameras[i].second), static_cast<int>(i) + 1);
            }
        }
        const auto found = std::find_if(listedCameras.begin(), listedCameras.end(), [this](const auto& item) { return item.first == selected; });
        cameraChoice.setSelectedId(found == listedCameras.end() ? 0 : static_cast<int>(found - listedCameras.begin()) + 1, juce::dontSendNotification);
        name.setEnabled(camera != nullptr);
        if (!name.isBeingEdited()) {
            name.setText(camera != nullptr ? juce::String(camera->name) : "Add a camera to begin", juce::dontSendNotification);
        }
        const auto time = frameTime();
        for (std::size_t i = 0; i < values.size(); ++i) {
            const auto property = camera != nullptr ? camera->properties.find(motion::cameraPropertyNames[i]) : std::map<std::string, motion::Curve>::const_iterator {};
            const auto available = camera != nullptr && property != camera->properties.end();
            values[i].setEnabled(available);
            keys[i].setEnabled(available);
            if (!values[i].isBeingEdited()) {
                values[i].setText(available ? juce::String(property->second.evaluateBase(time), 3) : juce::String::charToString(0x2014), juce::dontSendNotification);
            }
            auto state = osci::KeyframeButton::State::unanimated;
            if (available && property->second.animated()) {
                const auto keyed = std::any_of(property->second.keyframes().begin(), property->second.keyframes().end(), [time](const auto& key) { return std::abs(key.time - time) < 1.0e-7; });
                state = keyed ? osci::KeyframeButton::State::keyed : osci::KeyframeButton::State::animated;
            }
            keys[i].setState(state);
        }
        auto active = project.cameras.empty() ? 0 : project.cameras.front().id;
        for (const auto& cut : project.cameraCuts) {
            if (cut.contains(processor.position.load())) {
                active = cut.camera;
                break;
            }
        }
        const auto* output = findCamera(project, active);
        const auto prefix = processor.document.editingComposition() == 0 ? "Output: " : "Preview only: ";
        status.setText(juce::String(prefix) + (output == nullptr ? "default view" : juce::String(output->name) + (active == selected ? " | editing" : "")), juce::dontSendNotification);
        cutButton.setTooltip(processor.document.editingComposition() == 0 ? "Use this camera from the current frame until the next camera cut"
            : "Camera cuts here affect this composition's preview. Main's camera controls final output.");
        status.setTooltip(output == nullptr ? "The composition uses its default view" : "Active output camera: " + juce::String(output->name));
        frameLabel.setText("Frame " + juce::String(static_cast<juce::int64>(std::llround(time * project.frameRate))), juce::dontSendNotification);
        cutButton.setEnabled(camera != nullptr && time < project.duration);
        repaint();
    }

    void resized() override {
        auto area = getLocalBounds().reduced(10, 6);
        auto chooser = area.removeFromTop(26);
        addButton.setBounds(chooser.removeFromRight(44));
        chooser.removeFromRight(5);
        cameraChoice.setBounds(chooser);
        area.removeFromTop(5);
        name.setBounds(area.removeFromTop(23));
        area.removeFromTop(21);
        for (std::size_t i = 0; i < values.size(); ++i) {
            if (i == 3 || i == 6) {
                area.removeFromTop(21);
            }
            auto row = area.removeFromTop(26);
            captions[i].setBounds(row.removeFromLeft(i == 6 ? 91 : 23));
            keys[i].setBounds(row.removeFromRight(23));
            row.removeFromRight(4);
            values[i].setBounds(row.reduced(0, 1));
        }
        area.removeFromTop(7);
        auto footer = area.removeFromTop(24);
        cutButton.setBounds(footer.removeFromLeft(88));
        frameLabel.setBounds(footer);
        status.setBounds(area.removeFromTop(20));
    }

    void paint(juce::Graphics& g) override {
        g.setColour(osci::Colours::text().withAlpha(0.65f));
        g.setFont(11.0f);
        g.drawText("POSITION", 14, 66, getWidth() - 28, 19, juce::Justification::centredLeft);
        g.drawText("ROTATION | degrees", 14, 165, getWidth() - 28, 19, juce::Justification::centredLeft);
        g.drawText("LENS", 14, 264, getWidth() - 28, 19, juce::Justification::centredLeft);
    }

private:
    struct EditTarget { motion::Id id; double time; };

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
    void finishEditors() {
        name.hideEditor(true);
        for (auto& value : values) {
            value.hideEditor(true);
        }
        editing.fill(std::nullopt);
    }
    void selectProperty(std::size_t index) {
        if (onPropertySelected) {
            onPropertySelected(selected, motion::cameraPropertyNames[index]);
        }
    }
    void setValue(std::size_t index) {
        const auto target = editing[index].value_or(EditTarget { selected, frameTime() });
        editing[index].reset();
        const auto text = values[index].getText().trim().toStdString();
        char* end = nullptr;
        const auto value = std::strtod(text.c_str(), &end);
        if (text.empty() || end == text.c_str() || *end != '\0' || !std::isfinite(value)
            || (index == 6 && (value <= 0.0 || value >= 180.0))) {
            refresh();
            return;
        }
        const auto* camera = findCamera(processor.document.project(), target.id);
        const auto property = std::string(motion::cameraPropertyNames[index]);
        if (camera == nullptr || !camera->properties.contains(property) || camera->properties.at(property).evaluateBase(target.time) == value) {
            refresh();
            return;
        }
        processor.document.edit("Change camera property", [target, property, value](motion::Project& project) {
            auto* camera = findCamera(project, target.id);
            if (camera != nullptr) {
                auto& curve = camera->properties.at(property);
                if (curve.animated()) {
                    curve.setKeyValue(target.time, value);
                } else {
                    curve.base = value;
                }
            }
        });
        refresh();
    }
    void addKey(std::size_t index) {
        selectProperty(index);
        const auto id = selected;
        const auto time = frameTime();
        const auto property = std::string(motion::cameraPropertyNames[index]);
        const auto* camera = findCamera(processor.document.project(), id);
        if (camera == nullptr || !camera->properties.contains(property)) {
            return;
        }
        processor.document.edit("Key camera property", [id, time, property](motion::Project& project) {
            auto* camera = findCamera(project, id);
            if (camera != nullptr) {
                auto& curve = camera->properties.at(property);
                curve.setKeyValue(time, curve.evaluateBase(time));
            }
        });
        refresh();
    }
    void addCamera() {
        finishEditors();
        motion::Camera camera;
        camera.id = processor.document.newId();
        camera.name = "Camera " + std::to_string(processor.document.project().cameras.size() + 1);
        selected = camera.id;
        processor.document.edit("Add camera", [camera](motion::Project& project) { project.cameras.push_back(camera); });
        refresh();
    }
    void cutHere() {
        const auto& project = processor.document.project();
        const auto time = frameTime();
        if (findCamera(project, selected) == nullptr || time >= project.duration) {
            return;
        }
        const auto cameraId = selected;
        const auto cutId = processor.document.newId();
        processor.document.edit("Cut to camera", [time, cameraId, cutId](motion::Project& updated) {
            if (findCamera(updated, cameraId) == nullptr) {
                return;
            }
            auto end = updated.duration;
            for (const auto& cut : updated.cameraCuts) {
                if (cut.start > time) {
                    end = std::min(end, cut.start);
                }
            }
            std::erase_if(updated.cameraCuts, [time](const auto& cut) { return cut.start == time; });
            for (auto& cut : updated.cameraCuts) {
                if (cut.start < time && cut.end() > time) {
                    cut.duration = time - cut.start;
                }
            }
            updated.cameraCuts.push_back({ cutId, cameraId, time, end - time });
            std::sort(updated.cameraCuts.begin(), updated.cameraCuts.end(), [](const auto& left, const auto& right) { return left.start < right.start; });
        });
        refresh();
    }

    MotionProcessor& processor;
    motion::Id selected = 0;
    motion::Id editingName = 0;
    std::vector<std::pair<motion::Id, std::string>> listedCameras;
    juce::ComboBox cameraChoice;
    juce::TextButton addButton, cutButton;
    juce::Label name, status, frameLabel;
    std::array<juce::Label, 7> captions, values;
    std::array<osci::KeyframeButton, 7> keys;
    std::array<std::optional<EditTarget>, 7> editing;
};
