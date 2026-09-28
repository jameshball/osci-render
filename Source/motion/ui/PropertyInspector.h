#pragma once

#include "../MotionProcessor.h"
#include "ScrubField.h"
#include "../model/PropertySchema.h"

// Scrolling transform/appearance inspector for one property target (object,
// group, audio clip or camera). Rows group related axes; each row keys all of
// its axes at once and navigates between its keys. Values edit at the key
// under the playhead (frame-aligned) or, for animated curves, create one.
class MotionPropertyInspector final : public juce::Component {
public:
    explicit MotionPropertyInspector(MotionProcessor& owner) : processor(owner) {
        setName("Property inspector");
        viewport.setViewedComponent(&content, false);
        viewport.setScrollBarsShown(true, false);
        viewport.setScrollBarThickness(6);
        addAndMakeVisible(viewport);
        title.setFont(motion::style::title());
        title.setColour(juce::Label::textColourId, motion::style::text());
        title.setName("Inspector title");
        kind.setFont(motion::style::small());
        kind.setColour(juce::Label::textColourId, motion::style::muted());
        kind.setJustificationType(juce::Justification::centredRight);
        addAndMakeVisible(title);
        addAndMakeVisible(kind);
    }

    std::function<void(motion::Id, const std::string&)> onPropertySelected;
    // Content time of a key selected elsewhere (e.g. a motion-path key), if any.
    std::function<std::optional<double>(motion::Id)> selectedKeyTime;
    std::function<void()> onKeyTimeEdited;

    // Embedded inspectors (e.g. in the camera panel) supply their own title.
    // Distinguishes controls of several inspectors for automation and access.
    void setNamePrefix(juce::String prefix) { namePrefix = std::move(prefix); layoutSignature = "none"; refresh(); }
    void setShowsHeader(bool shows) { showsHeader = shows; title.setVisible(shows); kind.setVisible(shows); resized(); }
    void setTarget(motion::Id id) {
        if (id != target) { target = id; cancelGesture(); }
        refresh();
    }
    motion::Id getTarget() const { return target; }

    // Rebuilds rows only when the target's property set changes; otherwise
    // updates values and key states in place (cheap enough for 30 Hz).
    void refresh() {
        const auto found = motion::findPropertyTarget(processor.document.project(), target);
        const bool editable = found.has_value() && !found->isEffect;
        if (gesture.has_value() && processor.document.revision() != gesture->revision) { cancelGesture(); }
        const auto specs = editable ? motion::propertySpecs(*found) : std::span<const motion::PropertySpec>{};
        const auto signature = editable ? juce::String(static_cast<int>(found->camera)) + juce::String(static_cast<int>(found->isAudio)) : juce::String();
        if (signature != layoutSignature) {
            layoutSignature = signature;
            build(specs);
        }
        title.setText(editable ? juce::String(found->name.data(), found->name.size()) : "Nothing selected", juce::dontSendNotification);
        kind.setText(!editable ? juce::String() : found->camera ? "Camera" : found->isGroup ? "Group" : found->isAudio ? "Audio" : "Object", juce::dontSendNotification);
        empty = !editable;
        if (!editable) { repaint(); return; }
        const auto time = keyTime(*found);
        for (auto& row : rows) {
            bool allKeyed = true, anyAnimated = false;
            for (auto& field : row->fields) {
                const auto* curve = found->curve(std::string(field->spec.id));
                if (curve == nullptr) { continue; }
                field->editor.setValue(curve->evaluateBase(time));
                anyAnimated = anyAnimated || curve->animated();
                allKeyed = allKeyed && hasKey(*curve, time);
            }
            row->key.setState(allKeyed && anyAnimated ? osci::KeyframeButton::State::keyed
                : (anyAnimated ? osci::KeyframeButton::State::animated : osci::KeyframeButton::State::unanimated));
            row->previous.setEnabled(anyAnimated);
            row->next.setEnabled(anyAnimated);
        }
        repaint();
    }

    void paint(juce::Graphics& g) override {
        if (!empty) { return; }
        g.setColour(motion::style::muted());
        g.setFont(motion::style::body());
        g.drawFittedText("Select an object, group, audio clip or camera to edit its properties.",
            getLocalBounds().withTrimmedTop(40).reduced(motion::style::padding * 2, 0).removeFromTop(60), juce::Justification::topLeft, 3);
    }
    void resized() override {
        auto area = getLocalBounds();
        if (showsHeader) {
            auto header = area.removeFromTop(34).reduced(motion::style::padding + 2, 0);
            kind.setBounds(header.removeFromRight(60));
            title.setBounds(header);
        }
        viewport.setBounds(area);
        layoutContent();
    }

private:
    struct Field {
        explicit Field(const motion::PropertySpec& value) : spec(value) {}
        motion::PropertySpec spec;
        MotionScrubField editor;
    };
    struct Row : juce::Component {
        juce::String group;
        std::vector<std::unique_ptr<Field>> fields;
        osci::KeyframeButton key;
        motion::style::ChevronButton previous {"Previous key", false}, next {"Next key", true};
        void paint(juce::Graphics& g) override {
            g.setFont(motion::style::small());
            g.setColour(motion::style::muted());
            g.drawText(group, getLocalBounds().removeFromTop(16).withTrimmedLeft(2), juce::Justification::centredLeft);
        }
        void resized() override {
            auto area = getLocalBounds();
            area.removeFromTop(17);
            auto line = area.removeFromTop(motion::style::controlHeight);
            next.setBounds(line.removeFromRight(12));
            key.setBounds(line.removeFromRight(18));
            previous.setBounds(line.removeFromRight(12));
            line.removeFromRight(motion::style::gap);
            const auto count = static_cast<int>(fields.size());
            const auto width = (line.getWidth() - motion::style::gap * (count - 1)) / std::max(1, count);
            for (int index = 0; index < count; ++index) {
                fields[static_cast<std::size_t>(index)]->editor.setBounds(line.removeFromLeft(width));
                line.removeFromLeft(motion::style::gap);
            }
        }
    };
    struct Gesture {
        motion::Project before;
        std::uint64_t revision;
        motion::Id target;
    };

    void build(std::span<const motion::PropertySpec> specs) {
        rows.clear();
        content.removeAllChildren();
        for (const auto& spec : specs) {
            if (rows.empty() || rows.back()->group != juce::String(spec.group.data(), spec.group.size())) {
                auto row = std::make_unique<Row>();
                row->group = juce::String(spec.group.data(), spec.group.size());
                row->key.setName("Key " + namePrefix + row->group.toLowerCase());
                row->key.setTitle(row->key.getName());
                row->key.setTooltip("Add or remove keys for " + row->group.toLowerCase() + " at the playhead");
                row->previous.setName("Previous " + row->group.toLowerCase() + " key");
                row->next.setName("Next " + row->group.toLowerCase() + " key");
                row->previous.setTooltip("Go to the previous key");
                row->next.setTooltip("Go to the next key");
                row->addAndMakeVisible(row->previous);
                row->addAndMakeVisible(row->next);
                auto* raw = row.get();
                row->key.onClick = [this, raw] { toggleKeys(*raw); };
                row->previous.onClick = [this, raw] { jumpToKey(*raw, false); };
                row->next.onClick = [this, raw] { jumpToKey(*raw, true); };
                row->addAndMakeVisible(row->key);
                content.addAndMakeVisible(*row);
                rows.push_back(std::move(row));
            }
            auto field = std::make_unique<Field>(spec);
            auto& editor = field->editor;
            editor.setSpec(spec);
            editor.setName(namePrefix + juce::String(spec.id.data(), spec.id.size()));
            editor.setTitle(juce::String(spec.label.data(), spec.label.size()));
            editor.setComponentID("motion." + namePrefix.replace(" ", ".") + juce::String(spec.id.data(), spec.id.size()));
            editor.setTooltip(juce::String(spec.label.data(), spec.label.size()) + ": drag to scrub (Shift fine, Cmd coarse), double-click to type.");
            if (spec.axis.size() == 1) {
                editor.setPrefix(juce::String(spec.axis.data(), spec.axis.size()));
                const auto axis = spec.axis[0];
                editor.setAxisColour(axis == 'X' || axis == 'R' ? motion::style::axisX() : axis == 'Y' || axis == 'G' ? motion::style::axisY() : motion::style::axisZ());
            }
            const std::string property(spec.id);
            editor.onBegin = [this, property] { beginGesture(property); };
            editor.onChange = [this, property](double value) { previewValue(property, value); };
            editor.onEnd = [this] { endGesture(); };
            editor.onCancel = [this] { refresh(); };
            editor.onCommit = [this, property](double value) { commitValue(property, value); };
            rows.back()->addAndMakeVisible(editor);
            rows.back()->fields.push_back(std::move(field));
        }
        layoutContent();
    }
    void layoutContent() {
        const auto width = viewport.getWidth() - (viewport.isVerticalScrollBarShown() ? 8 : 0);
        int y = 0;
        for (auto& row : rows) {
            row->setBounds(motion::style::padding, y, width - motion::style::padding * 2, 17 + motion::style::controlHeight);
            y += 17 + motion::style::controlHeight + motion::style::padding;
        }
        content.setSize(std::max(0, width), y + motion::style::padding);
    }

    // Keys land on frames so they align with the timeline grid and exports.
    double keyTime(const motion::PropertyTarget& found) const {
        if (selectedKeyTime) {
            const auto selected = selectedKeyTime(found.id);
            if (selected.has_value()) { return *selected; }
        }
        const auto frameRate = processor.document.project().frameRate;
        auto time = processor.position.load();
        if (std::isfinite(frameRate) && frameRate > 0) { time = std::round(time * frameRate) / frameRate; }
        return found.localTime(time);
    }
    static bool hasKey(const motion::Curve& curve, double time) {
        const auto& keys = curve.keyframes();
        return std::any_of(keys.begin(), keys.end(), [time](const auto& key) { return std::abs(key.time - time) < 1.0e-6; });
    }
    void apply(motion::Project& project, const std::string& property, double value, double time) const {
        auto* curve = motion::findPropertyCurve(project, target, property);
        if (curve == nullptr) { return; }
        if (curve->animated()) { curve->setKeyValue(time, value); } else { curve->base = value; }
    }
    void beginGesture(const std::string& property) {
        const auto found = motion::findPropertyTarget(processor.document.project(), target);
        if (!found.has_value()) { return; }
        gesture = Gesture{processor.document.project(), processor.document.revision(), target};
        gestureTime = keyTime(*found);
        if (onPropertySelected) { onPropertySelected(target, property); }
    }
    void previewValue(const std::string& property, double value) {
        if (!gesture.has_value() || gesture->target != target) { return; }
        auto updated = gesture->before;
        apply(updated, property, value, gestureTime);
        processor.document.preview(std::move(updated));
        gesture->revision = processor.document.revision();
        changed = true;
    }
    void endGesture() {
        if (gesture.has_value() && changed && processor.document.revision() == gesture->revision) {
            processor.document.commit("Change property", std::move(gesture->before));
            if (onKeyTimeEdited) { onKeyTimeEdited(); }
        }
        gesture.reset();
        changed = false;
        refresh();
    }
    void cancelGesture() {
        if (gesture.has_value() && changed && processor.document.revision() == gesture->revision) {
            processor.document.preview(gesture->before);
        }
        gesture.reset();
        changed = false;
    }
    void commitValue(const std::string& property, double value) {
        const auto found = motion::findPropertyTarget(processor.document.project(), target);
        if (!found.has_value()) { return; }
        const auto time = keyTime(*found);
        processor.document.edit("Change property", [&](motion::Project& project) { apply(project, property, value, time); });
        if (onPropertySelected) { onPropertySelected(target, property); }
        if (onKeyTimeEdited) { onKeyTimeEdited(); }
    }
    void toggleKeys(Row& row) {
        const auto found = motion::findPropertyTarget(processor.document.project(), target);
        if (!found.has_value()) { return; }
        const auto time = keyTime(*found);
        bool allKeyed = true;
        for (auto& field : row.fields) {
            const auto* curve = found->curve(std::string(field->spec.id));
            allKeyed = allKeyed && curve != nullptr && hasKey(*curve, time);
        }
        processor.document.edit(allKeyed ? "Remove keyframe" : "Set keyframe", [&](motion::Project& project) {
            for (auto& field : row.fields) {
                auto* curve = motion::findPropertyCurve(project, target, std::string(field->spec.id));
                if (curve == nullptr) { continue; }
                if (allKeyed) { curve->removeKey(time); } else { curve->setKeyValue(time, curve->evaluateBase(time)); }
            }
        });
        if (!row.fields.empty() && onPropertySelected) { onPropertySelected(target, std::string(row.fields.front()->spec.id)); }
        if (onKeyTimeEdited) { onKeyTimeEdited(); }
    }
    void jumpToKey(Row& row, bool forward) {
        const auto found = motion::findPropertyTarget(processor.document.project(), target);
        if (!found.has_value() || found->rate == 0) { return; }
        const auto now = keyTime(*found);
        std::optional<double> best;
        for (auto& field : row.fields) {
            const auto* curve = found->curve(std::string(field->spec.id));
            if (curve == nullptr) { continue; }
            for (const auto& key : curve->keyframes()) {
                const bool candidate = forward ? key.time > now + 1.0e-6 : key.time < now - 1.0e-6;
                if (candidate && (!best.has_value() || (forward ? key.time < *best : key.time > *best))) { best = key.time; }
            }
        }
        if (!best.has_value()) { return; }
        const auto projectTime = found->start + (*best - found->offset) / found->rate;
        processor.seek(std::clamp(projectTime, 0.0, processor.document.project().duration));
    }

    MotionProcessor& processor;
    juce::Viewport viewport;
    juce::Component content;
    juce::Label title, kind;
    std::vector<std::unique_ptr<Row>> rows;
    motion::Id target = 0;
    juce::String layoutSignature = "none", namePrefix;
    std::optional<Gesture> gesture;
    double gestureTime = 0;
    bool changed = false, empty = true, showsHeader = true;
};
