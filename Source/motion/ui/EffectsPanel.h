#pragma once

#include "../MotionProcessor.h"
#include "../model/PropertyTarget.h"

// The stack stays visible while the selected effect exposes its controls below.
// Property edits use the same owner clock and undo model as the graph editor.
class MotionEffectsPanel : public juce::Component, private juce::ListBoxModel, public juce::DragAndDropTarget {
public:
    explicit MotionEffectsPanel(MotionProcessor& ownerProcessor) : processor(ownerProcessor), stack("Effect stack", this) {
        setName("Effects inspector");
        scope.setName("Effect scope");
        scope.addItem("Clip", 1);
        scope.addItem("Track", 2);
        scope.addItem("Composition", 3);
        scope.addItem("Group", 4);
        scope.setSelectedId(1, juce::dontSendNotification);
        scope.onChange = [this] { cancelGesture(); selected = 0; refresh(); notifySelection(); };
        stack.setRowHeight(28);
        stack.setColour(juce::ListBox::backgroundColourId, osci::Colours::veryDark());
        stack.setOutlineThickness(0);
        stack.setTooltip("Click the box to switch an effect on or off; drag to reorder; right-click for more");
        addButton.setButtonText("Add effect...");
        addButton.onClick = [this] { showAddMenu(); };
        scopeLabel.setText("Apply to", juce::dontSendNotification);
        scopeLabel.setFont(motion::style::small());
        scopeLabel.setColour(juce::Label::textColourId, motion::style::muted());
        hint.setFont(motion::style::small());
        hint.setColour(juce::Label::textColourId, motion::style::muted());
        hint.setJustificationType(juce::Justification::centredTop);
        for (auto* component : std::initializer_list<juce::Component*> { &scopeLabel, &scope, &stack, &addButton, &title, &viewport }) {
            addAndMakeVisible(component);
        }
        addChildComponent(hint);
        title.setFont(motion::style::strong());
        title.setJustificationType(juce::Justification::centredLeft);
        viewport.setViewedComponent(&controls, false);
        viewport.setScrollBarsShown(true, false);
        refresh();
    }
    ~MotionEffectsPanel() override { cancelGesture(); }
    std::function<void(motion::Id, std::string)> onPropertySelected;

    void setSelectedClip(motion::Id id) {
        if (clipId == id && !explicitTrack) { return; }
        cancelGesture();
        explicitTrack = false;
        clipId = id;
        trackId = 0;
        const auto* group = motion::findGroup(processor.document.project(), id);
        if (group != nullptr) { scope.setSelectedId(4, juce::dontSendNotification); }
        for (const auto& track : processor.document.project().tracks) {
            for (const auto& clip : track.clips) { if (clip.id == id) { trackId = track.id; } }
        }
        selected = 0;
        refresh();
    }
    motion::Id ownerId() const {
        if (scope.getSelectedId() == 3) { return 0; }
        if (scope.getSelectedId() == 1) { return motion::findGroup(processor.document.project(), clipId) == nullptr ? clipId : 0; }
        if (scope.getSelectedId() == 4) {
            if (explicitTrack) {
                for (const auto& track : processor.document.project().tracks) { if (track.id == trackId) { return track.group; } }
                return 0;
            }
            if (motion::findGroup(processor.document.project(), clipId) != nullptr) { return clipId; }
            for (const auto& track : processor.document.project().tracks) {
                for (const auto& clip : track.clips) { if (clip.id == clipId) { return track.group; } }
            }
            return 0;
        }
        if (explicitTrack) { return trackId; }
        for (const auto& track : processor.document.project().tracks) {
            for (const auto& clip : track.clips) { if (clip.id == clipId) { return track.id; } }
        }
        return 0;
    }
    bool validOwner() const { return scope.getSelectedId() == 3 || (ownerId() != 0 && motion::findEffectOwner(processor.document.project(), ownerId()) != nullptr); }
    void showOwner(motion::Id id, motion::Id effectId) {
        cancelGesture();
        const auto& project = processor.document.project();
        int choice = 3;
        explicitTrack = false;
        for (const auto& track : project.tracks) {
            if (track.id == id) {
                choice = 2;
                explicitTrack = true;
                trackId = id;
                clipId = track.clips.empty() ? 0 : track.clips.front().id;
            }
            for (const auto& clip : track.clips) {
                if (clip.id == id) { choice = 1; clipId = id; trackId = track.id; }
            }
        }
        if (motion::findGroup(project, id) != nullptr) { choice = 4; clipId = id; }
        scope.setSelectedId(choice, juce::dontSendNotification);
        selected = effectId;
        refresh();
        notifySelection();
    }
    struct ViewState {
        motion::Id clip = 0, track = 0, effect = 0;
        int scope = 1, scroll = 0;
        bool explicitTrack = false;
    };
    ViewState viewState() const { return {clipId, trackId, selected, scope.getSelectedId(), viewport.getViewPositionY(), explicitTrack}; }
    void restoreView(const ViewState& state) {
        cancelGesture();
        clipId = state.clip; trackId = state.track; selected = state.effect; explicitTrack = state.explicitTrack;
        scope.setSelectedId(state.scope, juce::dontSendNotification);
        refresh();
        viewport.setViewPosition(0, state.scroll);
    }
    void activate() { refresh(); notifySelection(); }
    void addEffect(const std::string& type) {
        if (!validOwner()) { return; }
        const auto* existing = motion::findEffectOwner(processor.document.project(), ownerId());
        if (existing == nullptr || existing->size() >= motion::maximumEffectsPerOwner) { return; }
        const auto* definition = motion::effectDefinition(type);
        if (definition == nullptr) { return; }
        auto effect = motion::makeEffect(processor.document.newId(), *definition);
        const auto id = effect.id;
        const auto owner = ownerId();
        processor.document.edit("Add " + juce::String(definition->name), [owner, effect](motion::Project& project) {
            auto* effects = motion::findEffectOwner(project, owner);
            if (effects != nullptr) { effects->push_back(effect); }
        });
        selected = id;
        refresh();
        notifySelection();
    }

    void refresh() {
        const auto previousSelection = selected;
        const auto* effects = validOwner() ? motion::findEffectOwner(processor.document.project(), ownerId()) : nullptr;
        std::vector<motion::Id> next;
        if (effects != nullptr) { for (const auto& effect : *effects) { next.push_back(effect.id); } }
        ids = std::move(next);
        if (std::find(ids.begin(), ids.end(), selected) == ids.end()) { selected = ids.empty() ? 0 : ids.front(); }
        updating = true;
        stack.updateContent();
        const auto found = std::find(ids.begin(), ids.end(), selected);
        if (found == ids.end()) { stack.deselectAllRows(); } else { stack.selectRow(static_cast<int>(found - ids.begin())); }
        updating = false;
        addButton.setEnabled(validOwner() && ids.size() < motion::maximumEffectsPerOwner);
        const auto* effect = motion::findEffect(processor.document.project(), selected);
        title.setText(effect == nullptr ? juce::String() : juce::String(effect->name) + (effect->enabled ? "" : "  (off)"), juce::dontSendNotification);
        // Say what to do when there is nothing to show.
        hint.setText(!validOwner() ? "Select a clip to give it effects, or apply them to the whole composition."
            : ids.empty() ? "No effects yet. Add one, or drag one from the Effects library." : juce::String(), juce::dontSendNotification);
        hint.setVisible(hint.getText().isNotEmpty());
        stack.setVisible(!ids.empty());
        const auto type = effect == nullptr ? std::string() : effect->type;
        if (builtFor != selected || builtType != type) {
            cancelGesture();
            rows.clear();
            cancelledGesture = false;
            builtFor = selected;
            builtType = type;
            const auto* definition = motion::effectDefinition(type);
            if (definition != nullptr) {
                for (const auto& parameter : definition->parameters) {
                    auto row = std::make_unique<Row>();
                    row->id = parameter.id;
                    row->label.setText(juce::String(parameter.name), juce::dontSendNotification);
                    row->label.setFont(12);
                    row->value.setName("Effect " + juce::String(parameter.id));
                    row->value.setSliderStyle(juce::Slider::LinearHorizontal);
                    row->value.setTextBoxStyle(juce::Slider::TextBoxRight, false, 62, 22);
                    row->value.setRange(parameter.min, parameter.max, 0.0001);
                    row->key.setButtonText("Key effect " + juce::String(parameter.id));
                    row->value.onDragStart = [this, name = row->id] { beginGesture(name); };
                    row->value.onValueChange = [this, pointer = row.get()] { setValue(pointer->id, pointer->value.getValue(), false); };
                    row->value.onDragEnd = [this] { finishGesture(); };
                    row->key.onClick = [this, name = row->id] { setValue(name, 0, true); };
                    controls.addAndMakeVisible(row->label);
                    controls.addAndMakeVisible(row->value);
                    controls.addAndMakeVisible(row->key);
                    rows.push_back(std::move(row));
                }
            }
        }
        updateValues();
        resized();
        repaint();
        if (previousSelection != selected && isShowing()) { notifySelection(); }
    }
    void updateValues() {
        const auto target = motion::findPropertyTarget(processor.document.project(), selected);
        if (!target.has_value()) { return; }
        const auto local = target->localTime(frameTime());
        for (const auto& row : rows) {
            const auto* curve = target->curve(row->id);
            if (curve == nullptr) { continue; }
            bool editing = false;
            for (auto* child : row->value.getChildren()) {
                const auto* label = dynamic_cast<juce::Label*>(child);
                editing = editing || (label != nullptr && label->isBeingEdited());
            }
            if (!gesture.has_value() && !editing) { row->value.setValue(curve->evaluateBase(local), juce::dontSendNotification); }
            const auto& keys = curve->keyframes();
            const bool keyed = std::any_of(keys.begin(), keys.end(), [local](const auto& key) { return std::abs(key.time - local) < 1.0e-6; });
            row->key.setState(keyed ? osci::KeyframeButton::State::keyed : (curve->animated() ? osci::KeyframeButton::State::animated : osci::KeyframeButton::State::unanimated));
        }
    }
    void resized() override {
        auto area = getLocalBounds().reduced(6, 3);
        // One line: what the effects apply to, and adding one.
        auto top = area.removeFromTop(28);
        scopeLabel.setBounds(top.removeFromLeft(54));
        addButton.setBounds(top.removeFromRight(std::min(100, top.getWidth() / 2)));
        top.removeFromRight(4);
        scope.setBounds(top);
        area.removeFromTop(6);
        if (hint.isVisible()) { hint.setBounds(area.removeFromTop(48).reduced(4, 6)); }
        if (stack.isVisible()) {
            stack.setBounds(area.removeFromTop(std::min(140, 28 * static_cast<int>(ids.size()))));
            area.removeFromTop(6);
        }
        title.setBounds(area.removeFromTop(title.getText().isEmpty() ? 0 : 24));
        viewport.setBounds(area);
        // One compact line per parameter: name, slider, key.
        controls.setSize(std::max(1, viewport.getMaximumVisibleWidth()), static_cast<int>(rows.size()) * 30);
        int y = 0;
        for (const auto& row : rows) {
            auto line = juce::Rectangle<int>(0, y, controls.getWidth(), 28);
            row->label.setBounds(line.removeFromLeft(std::min(96, line.getWidth() / 3)));
            row->key.setBounds(line.removeFromRight(22));
            row->value.setBounds(line.reduced(0, 2));
            y += 30;
        }
    }
    bool keyPressed(const juce::KeyPress& key) override {
        if (key == juce::KeyPress::escapeKey && gesture.has_value()) { cancelGesture(); updateValues(); return true; }
        return false;
    }
    bool isInterestedInDragSource(const SourceDetails& details) override {
        const auto value = details.description.toString();
        return value.startsWith("motion-effect:") || (value.startsWith("motion-effect-instance:") && std::find(ids.begin(), ids.end(), instanceId(value)) != ids.end());
    }
    void itemDropped(const SourceDetails& details) override {
        const auto value = details.description.toString();
        if (value.startsWith("motion-effect:")) { addEffect(value.fromFirstOccurrenceOf(":", false, false).toStdString()); return; }
        const auto id = instanceId(value);
        const auto target = std::clamp((details.localPosition.y - stack.getY()) / 28, 0, std::max(0, static_cast<int>(ids.size()) - 1));
        reorder(id, target);
    }

private:
    struct Row { std::string id; juce::Label label; juce::Slider value; osci::KeyframeButton key; };
    struct Gesture { motion::Project before; std::uint64_t revision; motion::Id effect; std::string property; double time; bool changed = false; };
    int getNumRows() override { return static_cast<int>(ids.size()); }
    juce::String getNameForRow(int row) override {
        const auto* effect = row >= 0 && row < getNumRows() ? motion::findEffect(processor.document.project(), ids[static_cast<std::size_t>(row)]) : nullptr;
        return effect == nullptr ? juce::String() : juce::String(effect->name);
    }
    void paintListBoxItem(int row, juce::Graphics& g, int width, int height, bool active) override {
        if (row < 0 || row >= getNumRows()) { return; }
        const auto* effect = motion::findEffect(processor.document.project(), ids[static_cast<std::size_t>(row)]);
        if (effect == nullptr) { return; }
        if (active) {
            g.setColour(osci::Colours::surfaceRaised().interpolatedWith(osci::Colours::accentColor(), 0.08f));
            g.fillRoundedRectangle(juce::Rectangle<float>(1, 1, width - 2, height - 2), 3);
            g.setColour(osci::Colours::accentColor().withAlpha(0.65f));
            g.fillRect(1, 5, 2, height - 10);
        }
        // The on/off box, then the name (dimmed when bypassed).
        const auto box = juce::Rectangle<float>(10, height * .5f - 6, 12, 12);
        g.setColour(osci::Colours::text().withAlpha(.6f));
        g.drawRoundedRectangle(box, 2, 1.2f);
        if (effect->enabled) {
            g.setColour(motion::style::accent());
            g.fillRoundedRectangle(box.reduced(2.5f), 1.5f);
        }
        g.setColour(osci::Colours::text().withAlpha(effect->enabled ? 1.0f : 0.4f));
        g.setFont(13);
        g.drawText(juce::String(effect->name), 30, 0, width - 38, height, juce::Justification::centredLeft);
    }
    void listBoxItemClicked(int row, const juce::MouseEvent& event) override {
        if (row < 0 || row >= getNumRows()) { return; }
        const auto id = ids[static_cast<std::size_t>(row)];
        if (event.mods.isPopupMenu()) { showStackMenu(id, row); return; }
        if (event.x < 28) { setEnabled(id, !isEnabled(id)); }
    }
    bool isEnabled(motion::Id id) const {
        const auto* effect = motion::findEffect(processor.document.project(), id);
        return effect != nullptr && effect->enabled;
    }
    void setEnabled(motion::Id id, bool value) {
        processor.document.edit(value ? "Enable effect" : "Bypass effect", [id, value](motion::Project& project) {
            auto* effect = motion::findEffect(project, id);
            if (effect != nullptr) { effect->enabled = value; }
        });
        refresh();
    }
    void showStackMenu(motion::Id id, int row) {
        juce::PopupMenu menu;
        menu.addItem(1, isEnabled(id) ? "Switch off" : "Switch on");
        menu.addItem(2, "Move up", row > 0);
        menu.addItem(3, "Move down", row + 1 < getNumRows());
        menu.addSeparator();
        menu.addItem(4, "Remove");
        const juce::Component::SafePointer<MotionEffectsPanel> owner(this);
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&stack).withMousePosition(), [owner, id, row](int result) {
            if (owner == nullptr || result == 0) { return; }
            if (result == 1) { owner->setEnabled(id, !owner->isEnabled(id)); }
            if (result == 2 || result == 3) { owner->reorder(id, row + (result == 2 ? -1 : 1)); }
            if (result == 4) { owner->selected = id; owner->removeSelected(); }
        });
    }
    void selectedRowsChanged(int row) override {
        if (updating || row < 0 || row >= getNumRows()) { return; }
        cancelGesture(); selected = ids[static_cast<std::size_t>(row)]; refresh(); notifySelection();
    }
    void deleteKeyPressed(int) override { removeSelected(); }
    juce::var getDragSourceDescription(const juce::SparseSet<int>& selection) override {
        if (selection.size() == 0 || selection[0] < 0 || selection[0] >= getNumRows()) { return {}; }
        return "motion-effect-instance:" + juce::String(ids[static_cast<std::size_t>(selection[0])]);
    }
    static motion::Id instanceId(const juce::String& value) { return static_cast<motion::Id>(value.fromFirstOccurrenceOf(":", false, false).getLargeIntValue()); }
    void notifySelection() { if (onPropertySelected) { onPropertySelected(selected, "strength"); } }
    void showAddMenu() {
        juce::PopupMenu menu;
        int id = 1;
        for (const auto& definition : motion::effectCatalog()) { menu.addItem(id++, juce::String(definition.name)); }
        const juce::Component::SafePointer<MotionEffectsPanel> owner(this);
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&addButton), [owner](int result) {
            if (owner != nullptr && result > 0 && result <= static_cast<int>(motion::effectCatalog().size())) { owner->addEffect(motion::effectCatalog()[static_cast<std::size_t>(result - 1)].id); }
        });
    }
    void removeSelected() {
        if (selected == 0) { return; }
        cancelGesture();
        const auto id = selected, owner = ownerId();
        processor.document.edit("Remove effect", [id, owner](motion::Project& project) {
            auto* effects = motion::findEffectOwner(project, owner);
            if (effects != nullptr) { std::erase_if(*effects, [id](const auto& effect) { return effect.id == id; }); }
        });
        selected = 0; refresh(); notifySelection();
    }
    void reorder(motion::Id id, int destination) {
        const auto found = std::find(ids.begin(), ids.end(), id);
        if (found == ids.end() || found - ids.begin() == destination) { return; }
        const auto owner = ownerId();
        processor.document.edit("Reorder effects", [owner, id, destination](motion::Project& project) {
            auto* effects = motion::findEffectOwner(project, owner);
            if (effects == nullptr) { return; }
            const auto source = std::find_if(effects->begin(), effects->end(), [id](const auto& effect) { return effect.id == id; });
            if (source == effects->end()) { return; }
            auto effect = std::move(*source);
            effects->erase(source);
            effects->insert(effects->begin() + std::min(destination, static_cast<int>(effects->size())), std::move(effect));
        });
        refresh();
    }
    double frameTime() const {
        const auto& project = processor.document.project();
        return std::clamp(std::round(processor.position.load() * project.frameRate) / project.frameRate, 0.0, project.duration);
    }
    void beginGesture(const std::string& property) {
        cancelGesture();
        cancelledGesture = false;
        gesture = Gesture { processor.document.project(), processor.document.revision(), selected, property, frameTime() };
        if (onPropertySelected) { onPropertySelected(selected, property); }
    }
    void setValue(const std::string& property, double value, bool key) {
        if (cancelledGesture) { return; }
        if (gesture.has_value() && gesture->revision != processor.document.revision()) { gesture.reset(); cancelledGesture = true; return; }
        const auto id = selected;
        const auto time = gesture.has_value() ? gesture->time : frameTime();
        const auto target = motion::findPropertyTarget(processor.document.project(), id);
        const auto* curve = target.has_value() ? target->curve(property) : nullptr;
        if (curve == nullptr) { return; }
        const auto local = target->localTime(time);
        if (key) { value = curve->evaluateBase(local); }
        const auto* effect = motion::findEffect(processor.document.project(), id);
        const auto* definition = effect == nullptr ? nullptr : motion::effectDefinition(effect->type);
        if (definition == nullptr || !std::isfinite(value)) { return; }
        for (const auto& parameter : definition->parameters) {
            if (parameter.id == property) { value = std::clamp(value, parameter.min, parameter.max); }
        }
        if (!key && !gesture.has_value() && value == curve->evaluateBase(local)) { return; }
        const auto operation = [id, property, value, local, key](motion::Project& project) {
            auto* changed = motion::findPropertyCurve(project, id, property);
            if (changed == nullptr) { return; }
            if (key || changed->animated()) { changed->setKeyValue(local, value); } else { changed->base = value; }
        };
        if (gesture.has_value()) {
            auto project = gesture->before;
            operation(project);
            const auto previous = motion::findPropertyTarget(gesture->before, id);
            const auto* original = previous.has_value() ? previous->curve(property) : nullptr;
            gesture->changed = original != nullptr && original->evaluateBase(local) != value;
            if (!gesture->changed) { project = gesture->before; }
            processor.document.preview(std::move(project));
            gesture->revision = processor.document.revision();
        } else {
            if (key) { processor.document.edit("Key effect parameter", operation); }
            else { processor.document.editCoalesced("Change effect parameter", "effect:" + juce::String(id) + ":" + juce::String(property), operation); }
        }
        if (onPropertySelected) { onPropertySelected(id, property); }
    }
    void finishGesture() {
        cancelledGesture = false;
        if (!gesture.has_value()) { return; }
        auto done = std::move(*gesture); gesture.reset();
        if (done.changed && done.revision == processor.document.revision()) { processor.document.commit("Change effect parameter", std::move(done.before)); }
    }
    void cancelGesture() {
        if (!gesture.has_value()) { return; }
        cancelledGesture = true;
        auto done = std::move(*gesture); gesture.reset();
        if (done.changed && done.revision == processor.document.revision()) { processor.document.preview(std::move(done.before)); }
    }
    MotionProcessor& processor;
    motion::Id clipId = 0, trackId = 0, selected = 0, builtFor = 0;
    std::string builtType;
    std::vector<motion::Id> ids;
    juce::ComboBox scope;
    juce::ListBox stack;
    juce::TextButton addButton;
    juce::Label title, scopeLabel, hint;
    juce::Viewport viewport;
    juce::Component controls;
    std::vector<std::unique_ptr<Row>> rows;
    std::optional<Gesture> gesture;
    bool updating = false;
    bool cancelledGesture = false;
    bool explicitTrack = false;
};
