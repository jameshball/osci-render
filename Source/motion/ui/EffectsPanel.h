#pragma once

#include "MotionStyle.h"
#include "MotionIcons.h"

#include "../MotionProcessor.h"
#include "../model/PropertyTarget.h"

// Effects in the order the selected clip passes through them: its own, its
// track's, each enclosing group's, then the whole composition's. Each stage
// adds its own effects, so there is no separate "apply to" choice. The
// selected effect's controls sit below. Property edits use the same owner
// clock and undo model as the graph editor.
class MotionEffectsPanel : public juce::Component, public juce::DragAndDropTarget {
public:
    explicit MotionEffectsPanel(MotionProcessor& ownerProcessor) : processor(ownerProcessor), chain(*this) {
        setName("Effects inspector");
        chainView.setViewedComponent(&chain, false);
        chainView.setScrollBarsShown(true, false);
        chainView.setScrollBarThickness(6);
        hint.setFont(motion::style::caption());
        hint.setColour(juce::Label::textColourId, motion::style::muted());
        hint.setJustificationType(juce::Justification::topLeft);
        title.setFont(motion::style::title());
        title.setJustificationType(juce::Justification::centredLeft);
        for (auto* component : std::initializer_list<juce::Component*> {&chainView, &title, &viewport, &hint}) { addAndMakeVisible(component); }
        viewport.setViewedComponent(&controls, false);
        viewport.setScrollBarsShown(true, false);
        refresh();
    }
    ~MotionEffectsPanel() override { cancelGesture(); }
    std::function<void(motion::Id, std::string)> onPropertySelected;

    // The clip (or group) whose chain is shown; a track header shows the
    // track's chain through showOwner.
    void setSelectedClip(motion::Id id) {
        if (clipId == id && trackId == 0) { return; }
        cancelGesture();
        clipId = id;
        trackId = 0;
        selected = 0;
        refresh();
    }
    void showOwner(motion::Id id, motion::Id effectId) {
        cancelGesture();
        const auto& project = processor.document.project();
        const auto isTrack = std::any_of(project.tracks.begin(), project.tracks.end(), [id](const auto& track) { return track.id == id; });
        if (isTrack) {
            trackId = id;
            clipId = 0;
        } else if (id != 0) {
            clipId = id;
            trackId = 0;
        }
        selected = effectId;
        refresh();
        notifySelection();
    }
    struct ViewState {
        motion::Id clip = 0, track = 0, effect = 0;
        int scroll = 0;
    };
    ViewState viewState() const { return {clipId, trackId, selected, viewport.getViewPositionY()}; }
    void restoreView(const ViewState& state) {
        cancelGesture();
        clipId = state.clip;
        trackId = state.track;
        selected = state.effect;
        refresh();
        viewport.setViewPosition(0, state.scroll);
    }
    void activate() { refresh(); notifySelection(); }
    // From the library: the stage of the selected effect, else the closest
    // stage to the selection.
    void addEffect(const std::string& type) {
        if (sections.empty()) { return; }
        auto owner = sections.front().owner;
        for (const auto& section : sections) {
            if (std::find(section.effects.begin(), section.effects.end(), selected) != section.effects.end()) { owner = section.owner; }
        }
        addEffect(type, owner, -1);
    }
    void addEffect(const std::string& type, motion::Id owner, int index) {
        const auto* existing = motion::findEffectOwner(processor.document.project(), owner);
        const auto* definition = motion::effectDefinition(type);
        if (existing == nullptr || definition == nullptr || existing->size() >= motion::maximumEffectsPerOwner) { return; }
        auto effect = motion::makeEffect(processor.document.newId(), *definition);
        const auto id = effect.id;
        processor.document.edit("Add " + juce::String(definition->name), [owner, effect, index](motion::Project& project) {
            auto* effects = motion::findEffectOwner(project, owner);
            if (effects == nullptr) { return; }
            const auto at = index < 0 ? effects->size() : std::min(static_cast<std::size_t>(index), effects->size());
            effects->insert(effects->begin() + static_cast<std::ptrdiff_t>(at), effect);
        });
        selected = id;
        refresh();
        notifySelection();
    }

    void refresh() {
        const auto previousSelection = selected;
        buildSections();
        std::vector<motion::Id> all;
        for (const auto& section : sections) { all.insert(all.end(), section.effects.begin(), section.effects.end()); }
        if (std::find(all.begin(), all.end(), selected) == all.end()) { selected = all.empty() ? 0 : all.front(); }
        const auto* effect = motion::findEffect(processor.document.project(), selected);
        title.setText(effect == nullptr ? juce::String() : juce::String(effect->name) + (effect->enabled ? "" : "  (off)"), juce::dontSendNotification);
        hint.setText(clipId == 0 && trackId == 0 ? "Select a clip to see every effect it passes through. Composition effects apply to everything."
            : all.empty() ? "Add an effect with +, or drag one from the Effects library onto a stage." : juce::String(), juce::dontSendNotification);
        hint.setVisible(hint.getText().isNotEmpty());
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
                    row->label.setFont(motion::style::caption());
                    row->label.setColour(juce::Label::textColourId, motion::style::muted());
                    row->value.setName("Effect " + juce::String(parameter.id));
                    row->value.setSliderStyle(juce::Slider::LinearHorizontal);
                    row->value.setTextBoxStyle(juce::Slider::TextBoxRight, false, 62, 22);
                    row->value.setRange(parameter.min, parameter.max, 0.0001);
                    row->value.setNumDecimalPlacesToDisplay(2);
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
        chain.refresh();
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
        auto area = getLocalBounds().reduced(motion::style::padding, motion::style::gap);
        const auto chainHeight = std::min(chain.preferredHeight(), std::max(area.getHeight() / 2, 120));
        chainView.setBounds(area.removeFromTop(chainHeight));
        chain.setSize(chainView.getMaximumVisibleWidth(), chain.preferredHeight());
        if (hint.isVisible()) { hint.setBounds(area.removeFromTop(44).reduced(0, motion::style::gap)); }
        area.removeFromTop(motion::style::padding);
        title.setBounds(area.removeFromTop(title.getText().isEmpty() ? 0 : motion::style::controlHeight));
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
        if ((key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey) && selected != 0) { remove(selected); return true; }
        return false;
    }
    // Drops anywhere in the panel add to (or move into) the stage under them.
    bool isInterestedInDragSource(const SourceDetails& details) override {
        const auto value = details.description.toString();
        return value.startsWith("motion-effect:") || value.startsWith("motion-effect-instance:");
    }
    void itemDragMove(const SourceDetails& details) override { chain.setDropPoint(chain.getLocalPoint(this, details.localPosition)); }
    void itemDragExit(const SourceDetails&) override { chain.setDropPoint(std::nullopt); }
    void itemDropped(const SourceDetails& details) override {
        const auto point = chain.getLocalPoint(this, details.localPosition);
        chain.setDropPoint(std::nullopt);
        const auto [owner, index] = chain.insertionAt(point);
        if (owner < 0) { return; }
        const auto value = details.description.toString();
        if (value.startsWith("motion-effect:")) {
            addEffect(value.fromFirstOccurrenceOf(":", false, false).toStdString(), static_cast<motion::Id>(owner), index);
        } else {
            move(static_cast<motion::Id>(value.fromFirstOccurrenceOf(":", false, false).getLargeIntValue()), static_cast<motion::Id>(owner), index);
        }
    }

private:
    struct Row { std::string id; juce::Label label; juce::Slider value; osci::KeyframeButton key; };
    struct Gesture { motion::Project before; std::uint64_t revision; motion::Id effect; std::string property; double time; bool changed = false; };
    struct Section {
        juce::String kind, name;
        motion::Id owner = 0;
        std::vector<motion::Id> effects;
    };

    // The stages the shown clip's signal passes through, first to last.
    void buildSections() {
        const auto& project = processor.document.project();
        sections.clear();
        const auto addSection = [&](const juce::String& kind, const juce::String& name, motion::Id owner) {
            const auto* effects = motion::findEffectOwner(project, owner);
            if (effects == nullptr) { return; }
            Section section {kind, name, owner, {}};
            for (const auto& effect : *effects) { section.effects.push_back(effect.id); }
            sections.push_back(std::move(section));
        };
        motion::Id group = 0;
        const auto* selectedGroup = motion::findGroup(project, clipId);
        if (selectedGroup != nullptr) {
            group = clipId;
        } else {
            for (const auto& track : project.tracks) {
                const auto holds = trackId != 0 ? track.id == trackId : std::any_of(track.clips.begin(), track.clips.end(), [this](const auto& clip) { return clip.id == clipId; });
                if (!holds || track.kind != motion::TrackKind::visual) { continue; }
                for (const auto& clip : track.clips) {
                    if (clip.id == clipId) { addSection("Clip", juce::String(clip.name), clip.id); }
                }
                addSection("Track", juce::String(track.name), track.id);
                group = track.group;
            }
        }
        for (std::size_t depth = 0; group != 0 && depth < motion::maximumGroupDepth; ++depth) {
            const auto* value = motion::findGroup(project, group);
            if (value == nullptr) { break; }
            addSection("Group", juce::String(value->name), value->id);
            group = value->parent;
        }
        addSection("Composition", "everything", 0);
    }
    const Section* sectionOf(motion::Id effect) const {
        for (const auto& section : sections) {
            if (std::find(section.effects.begin(), section.effects.end(), effect) != section.effects.end()) { return &section; }
        }
        return nullptr;
    }

    // The stages and their effects, painted as one list.
    class Chain final : public juce::Component, public juce::SettableTooltipClient {
    public:
        explicit Chain(MotionEffectsPanel& owner) : panel(owner) {
            setName("Effect chain");
            setWantsKeyboardFocus(true);
        }
        static constexpr int headerHeight = 26, rowHeight = 26, sectionGap = 6;
        int preferredHeight() const {
            int height = 0;
            for (const auto& section : panel.sections) { height += headerHeight + rowHeight * static_cast<int>(section.effects.size()) + sectionGap; }
            return height;
        }
        void refresh() {
            // One add button per stage.
            while (addButtons.size() < panel.sections.size()) {
                auto button = std::make_unique<motion::icons::Button>("Add effect", motion::icons::Icon::add);
                button->iconSize = 16.0f;
                addAndMakeVisible(*button);
                addButtons.push_back(std::move(button));
            }
            addButtons.resize(panel.sections.size());
            for (std::size_t index = 0; index < addButtons.size(); ++index) {
                const auto& section = panel.sections[index];
                addButtons[index]->setName("Add effect to " + section.kind.toLowerCase());
                addButtons[index]->setTitle(addButtons[index]->getName());
                addButtons[index]->setTooltip("Add an effect to " + (section.owner == 0 ? juce::String("the whole composition") : "this " + section.kind.toLowerCase()));
                addButtons[index]->onClick = [this, owner = section.owner, button = addButtons[index].get()] { panel.showAddMenu(owner, button); };
            }
            resized();
            repaint();
        }
        void resized() override {
            int y = 0;
            for (std::size_t index = 0; index < panel.sections.size() && index < addButtons.size(); ++index) {
                addButtons[index]->setBounds(getWidth() - 24, y + 2, 22, 22);
                y += headerHeight + rowHeight * static_cast<int>(panel.sections[index].effects.size()) + sectionGap;
            }
        }
        void paint(juce::Graphics& g) override {
            const auto& project = panel.processor.document.project();
            int y = 0;
            for (const auto& section : panel.sections) {
                auto header = juce::Rectangle<int>(0, y, getWidth() - 28, headerHeight);
                g.setFont(motion::style::caption());
                g.setColour(motion::style::muted());
                const auto kindWidth = juce::GlyphArrangement::getStringWidthInt(motion::style::caption(), section.kind) + 6;
                g.drawText(section.kind, header.removeFromLeft(kindWidth), juce::Justification::centredLeft, false);
                g.setFont(motion::style::body());
                g.setColour(motion::style::text());
                g.drawText(section.name, header, juce::Justification::centredLeft, true);
                y += headerHeight;
                for (const auto id : section.effects) {
                    const auto* effect = motion::findEffect(project, id);
                    if (effect == nullptr) { continue; }
                    paintEffect(g, *effect, juce::Rectangle<int>(0, y, getWidth(), rowHeight));
                    y += rowHeight;
                }
                g.setColour(motion::style::outline().withAlpha(.5f));
                g.drawHorizontalLine(y + sectionGap / 2, 0.0f, static_cast<float>(getWidth()));
                y += sectionGap;
            }
            if (dropPoint.has_value()) {
                const auto [owner, index] = insertionAt(*dropPoint);
                if (owner >= 0) {
                    g.setColour(motion::style::accent());
                    g.fillRect(0, insertionY(static_cast<motion::Id>(owner), index) - 1, getWidth(), 2);
                }
            }
        }
        void setDropPoint(std::optional<juce::Point<int>> point) {
            dropPoint = point;
            repaint();
        }
        // The stage owner (-1 for none) and index an item dropped at `point` joins.
        std::pair<std::int64_t, int> insertionAt(juce::Point<int> point) const {
            int y = 0;
            for (const auto& section : panel.sections) {
                const auto height = headerHeight + rowHeight * static_cast<int>(section.effects.size()) + sectionGap;
                if (point.y < y + height || &section == &panel.sections.back()) {
                    const auto index = std::clamp((point.y - y - headerHeight + rowHeight / 2) / rowHeight, 0, static_cast<int>(section.effects.size()));
                    return {static_cast<std::int64_t>(section.owner), index};
                }
                y += height;
            }
            return {-1, 0};
        }
        void mouseMove(const juce::MouseEvent& event) override {
            const auto id = effectAt(event.position.toInt());
            if (id != hovered) {
                hovered = id;
                repaint();
            }
            setTooltip(id != 0 ? "Click the box to switch it on or off. Drag to reorder or move to another stage. Right-click for more." : juce::String());
        }
        void mouseExit(const juce::MouseEvent&) override {
            hovered = 0;
            repaint();
        }
        void mouseDown(const juce::MouseEvent& event) override {
            const auto id = effectAt(event.position.toInt());
            if (id == 0) { return; }
            if (event.mods.isPopupMenu()) {
                panel.select(id);
                panel.showEffectMenu(id);
                return;
            }
            if (event.x < 26) {
                panel.setEffectEnabled(id, !panel.isEffectEnabled(id));
                return;
            }
            pendingRemove = event.x >= getWidth() - 26 ? id : 0;
            panel.select(id);
        }
        void mouseDrag(const juce::MouseEvent& event) override {
            const auto id = effectAt(event.getMouseDownPosition());
            if (id == 0 || event.getDistanceFromDragStart() < 4) { return; }
            pendingRemove = 0;
            auto* container = juce::DragAndDropContainer::findParentDragContainerFor(this);
            if (container != nullptr && !container->isDragAndDropActive()) { container->startDragging("motion-effect-instance:" + juce::String(id), this); }
        }
        void mouseUp(const juce::MouseEvent& event) override {
            const auto id = std::exchange(pendingRemove, motion::Id(0));
            if (id != 0 && !event.mouseWasDraggedSinceMouseDown() && effectAt(event.position.toInt()) == id && event.x >= getWidth() - 26) { panel.remove(id); }
        }
    private:
        void paintEffect(juce::Graphics& g, const motion::EffectInstance& effect, juce::Rectangle<int> row) const {
            const auto active = effect.id == panel.selected;
            const auto bounds = row.toFloat().reduced(0, 1);
            if (active || effect.id == hovered) {
                g.setColour(active ? motion::style::raised().interpolatedWith(motion::style::accent(), .1f) : juce::Colours::white.withAlpha(.04f));
                g.fillRoundedRectangle(bounds, motion::style::radius);
            }
            if (active) {
                g.setColour(motion::style::accent().withAlpha(.7f));
                g.fillRect(bounds.withWidth(2).reduced(0, 5));
            }
            // The on/off box, then the name (dimmed when bypassed).
            const auto box = juce::Rectangle<float>(8, bounds.getCentreY() - 6, 12, 12);
            g.setColour(motion::style::text().withAlpha(.6f));
            g.drawRoundedRectangle(box, 2, 1.2f);
            if (effect.enabled) {
                g.setColour(motion::style::accent());
                g.fillRoundedRectangle(box.reduced(2.5f), 1.5f);
            }
            g.setColour(motion::style::text().withAlpha(effect.enabled ? 1.0f : .4f));
            g.setFont(motion::style::body());
            g.drawText(juce::String(effect.name), row.withTrimmedLeft(28).withTrimmedRight(28), juce::Justification::centredLeft, true);
            if (active || effect.id == hovered) {
                motion::icons::draw(g, motion::icons::Icon::close, row.removeFromRight(26).toFloat(), motion::style::text().withAlpha(.6f), 14.0f);
            }
        }
        motion::Id effectAt(juce::Point<int> point) const {
            int y = 0;
            for (const auto& section : panel.sections) {
                y += headerHeight;
                for (const auto id : section.effects) {
                    if (point.y >= y && point.y < y + rowHeight) { return id; }
                    y += rowHeight;
                }
                y += sectionGap;
            }
            return 0;
        }
        int insertionY(motion::Id owner, int index) const {
            int y = 0;
            for (const auto& section : panel.sections) {
                if (section.owner == owner) { return y + headerHeight + rowHeight * index; }
                y += headerHeight + rowHeight * static_cast<int>(section.effects.size()) + sectionGap;
            }
            return 0;
        }
        MotionEffectsPanel& panel;
        std::vector<std::unique_ptr<motion::icons::Button>> addButtons;
        std::optional<juce::Point<int>> dropPoint;
        motion::Id hovered = 0, pendingRemove = 0;
    };

    void select(motion::Id id) {
        if (id == selected) { return; }
        cancelGesture();
        selected = id;
        refresh();
        notifySelection();
    }
    bool isEffectEnabled(motion::Id id) const {
        const auto* effect = motion::findEffect(processor.document.project(), id);
        return effect != nullptr && effect->enabled;
    }
    void setEffectEnabled(motion::Id id, bool value) {
        processor.document.edit(value ? "Enable effect" : "Bypass effect", [id, value](motion::Project& project) {
            auto* effect = motion::findEffect(project, id);
            if (effect != nullptr) { effect->enabled = value; }
        });
        refresh();
    }
    void showEffectMenu(motion::Id id) {
        const auto* section = sectionOf(id);
        if (section == nullptr) { return; }
        const auto index = static_cast<int>(std::find(section->effects.begin(), section->effects.end(), id) - section->effects.begin());
        const auto owner = section->owner;
        const auto count = static_cast<int>(section->effects.size());
        juce::PopupMenu menu;
        menu.addItem(1, isEffectEnabled(id) ? "Switch off" : "Switch on");
        menu.addItem(2, "Move up", index > 0);
        menu.addItem(3, "Move down", index + 1 < count);
        menu.addSeparator();
        menu.addItem(4, "Remove");
        const juce::Component::SafePointer<MotionEffectsPanel> safe(this);
        menu.setLookAndFeel(&getLookAndFeel());
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&chain).withMousePosition(), [safe, id, index, owner](int result) {
            if (safe == nullptr || result == 0) { return; }
            if (result == 1) { safe->setEffectEnabled(id, !safe->isEffectEnabled(id)); }
            if (result == 2) { safe->move(id, owner, index - 1); }
            if (result == 3) { safe->move(id, owner, index + 2); }
            if (result == 4) { safe->remove(id); }
        });
    }
    void showAddMenu(motion::Id owner, juce::Component* target) {
        juce::PopupMenu menu;
        int id = 1;
        for (const auto& definition : motion::effectCatalog()) { menu.addItem(id++, juce::String(definition.name)); }
        const juce::Component::SafePointer<MotionEffectsPanel> safe(this);
        menu.setLookAndFeel(&getLookAndFeel());
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(target), [safe, owner](int result) {
            if (safe != nullptr && result > 0 && result <= static_cast<int>(motion::effectCatalog().size())) { safe->addEffect(motion::effectCatalog()[static_cast<std::size_t>(result - 1)].id, owner, -1); }
        });
    }
    void notifySelection() { if (onPropertySelected) { onPropertySelected(selected, "strength"); } }
    void remove(motion::Id id) {
        cancelGesture();
        processor.document.tryEdit("Remove effect", [id](motion::Project& project) {
            for (auto* effects : allOwners(project)) {
                const auto before = effects->size();
                std::erase_if(*effects, [id](const auto& effect) { return effect.id == id; });
                if (effects->size() != before) { return true; }
            }
            return false;
        });
        if (selected == id) { selected = 0; }
        refresh();
        notifySelection();
    }
    // Moves an effect to `index` in `owner`'s stage (possibly another stage).
    void move(motion::Id id, motion::Id owner, int index) {
        const auto* section = sectionOf(id);
        if (section == nullptr) { return; }
        const auto source = static_cast<int>(std::find(section->effects.begin(), section->effects.end(), id) - section->effects.begin());
        if (section->owner == owner && (index == source || index == source + 1)) { return; }
        const auto* destination = motion::findEffectOwner(processor.document.project(), owner);
        if (destination == nullptr || (section->owner != owner && destination->size() >= motion::maximumEffectsPerOwner)) { return; }
        const auto from = section->owner;
        processor.document.tryEdit(from == owner ? "Reorder effects" : "Move effect", [id, from, owner, index, source](motion::Project& project) {
            auto* origin = motion::findEffectOwner(project, from);
            if (origin == nullptr) { return false; }
            const auto found = std::find_if(origin->begin(), origin->end(), [id](const auto& effect) { return effect.id == id; });
            if (found == origin->end()) { return false; }
            auto effect = std::move(*found);
            origin->erase(found);
            auto* target = motion::findEffectOwner(project, owner);
            if (target == nullptr) { return false; }
            // Removing the effect first shifts later positions in its own stage.
            const auto at = std::clamp(from == owner && index > source ? index - 1 : index, 0, static_cast<int>(target->size()));
            target->insert(target->begin() + at, std::move(effect));
            return true;
        });
        selected = id;
        refresh();
    }
    static std::vector<std::vector<motion::EffectInstance>*> allOwners(motion::Project& project) {
        std::vector<std::vector<motion::EffectInstance>*> owners {&project.effects};
        for (auto& group : project.groups) { owners.push_back(&group.effects); }
        for (auto& track : project.tracks) {
            owners.push_back(&track.effects);
            for (auto& clip : track.clips) { owners.push_back(&clip.effects); }
        }
        return owners;
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
    std::vector<Section> sections;
    Chain chain;
    juce::Viewport chainView;
    juce::Label title, hint;
    juce::Viewport viewport;
    juce::Component controls;
    std::vector<std::unique_ptr<Row>> rows;
    std::optional<Gesture> gesture;
    bool cancelledGesture = false;
};
