#pragma once

#include "MotionIcons.h"

#include "../MotionProcessor.h"
#include "ScrubField.h"
#include "ColourPicker.h"
#include "../model/PropertySchema.h"
#include "../model/SpatialMotion.h"
#include "../model/Drawing.h"

// Scrolling transform/appearance inspector for one property target (object,
// group, audio clip or camera). Rows group related axes; each row keys all of
// its axes at once and navigates between its keys. Values edit at the key
// under the playhead (frame-aligned) or, for animated curves, create one.
class MotionPropertyInspector final : public juce::Component, public juce::DragAndDropTarget {
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
        title.setBorderSize({});
        kind.setBorderSize({});
        title.onTextChange = [this] {
            const auto text = title.getText().trim();
            if (onRename && text.isNotEmpty()) { onRename(target, text); }
            refresh();
        };
        kind.setFont(motion::style::caption());
        kind.setColour(juce::Label::textColourId, motion::style::muted());
        kind.setJustificationType(juce::Justification::centredRight);
        addAndMakeVisible(title);
        addAndMakeVisible(kind);
    }

    std::function<void(motion::Id, const std::string&)> onPropertySelected;
    // Content time of a key selected elsewhere (e.g. a motion-path key), if any.
    std::function<std::optional<double>(motion::Id)> selectedKeyTime;
    std::function<void()> onKeyTimeEdited;
    // The row's modulation chip: open the graph (oscillator, routes, link) for this property.
    std::function<void(motion::Id, const std::string&)> onModulate;
    // Opens a popover pointing at `anchor` (the colour picker).
    std::function<void(std::unique_ptr<juce::Component>, juce::Component& anchor)> onShowPopover;
    // Double-clicking a camera's name renames it.
    std::function<void(motion::Id, const juce::String&)> onRename;

    // Embedded inspectors (e.g. in the camera panel) supply their own title.
    // Distinguishes controls of several inspectors for automation and access.
    void setNamePrefix(juce::String prefix) { namePrefix = std::move(prefix); layoutSignature = "none"; refresh(); }
    // A section above the property rows (the clip's timing), sized by
    // `height`; 0 hides it.
    void setLead(juce::Component* component, std::function<int()> height) {
        if (lead != nullptr && lead != component) { content.removeChildComponent(lead); }
        lead = component;
        leadHeight = std::move(height);
        if (lead != nullptr) { content.addChildComponent(lead); }
        layoutContent();
    }
    // A section below the property rows (the owner's effects).
    void setTrail(juce::Component* component, std::function<int()> height) {
        if (trail != nullptr && trail != component) { content.removeChildComponent(trail); }
        trail = component;
        trailHeight = std::move(height);
        if (trail != nullptr) { content.addChildComponent(trail); }
        layoutContent();
    }
    // What the header says when the target has no properties of its own (a
    // track or the composition, shown for their effects).
    void setHeading(std::optional<std::pair<juce::String, juce::String>> value) {
        heading = std::move(value);
        refresh();
    }
    void relayout() { layoutContent(); }
    // Scrolls so `component` (inside the inspector) is fully in view.
    void reveal(juce::Component& component) {
        layoutContent();
        const auto area = content.getLocalArea(&component, component.getLocalBounds());
        auto position = viewport.getViewPosition();
        if (area.getBottom() > position.y + viewport.getMaximumVisibleHeight()) { position.y = area.getBottom() - viewport.getMaximumVisibleHeight() + motion::style::padding; }
        if (area.getY() < position.y) { position.y = area.getY() - motion::style::padding; }
        viewport.setViewPosition(position);
    }
    void setShowsHeader(bool shows) { showsHeader = shows; title.setVisible(shows); kind.setVisible(shows); resized(); }
    void setTarget(motion::Id id) {
        if (id != target) {
            title.hideEditor(true);
            target = id;
            cancelGesture();
        }
        refresh();
    }
    motion::Id getTarget() const { return target; }
    void setSelectionCount(std::size_t count) {
        if (count == selectionCount) { return; }
        selectionCount = count;
        resized();
        refresh();
    }

    // Rebuilds rows only when the target's property set changes; otherwise
    // updates values and key states in place (cheap enough for 30 Hz).
    void refresh() {
        const auto found = motion::findPropertyTarget(processor.document.project(), target);
        const bool editable = found.has_value() && !found->isEffect;
        if (gesture.has_value() && processor.document.revision() != gesture->revision) { cancelGesture(); }
        specList.clear();
        if (editable) {
            const auto base = motion::propertySpecs(*found);
            specList.assign(base.begin(), base.end());
            for (const auto& slider : luaSliders()) { specList.push_back(slider); }
        }
        const auto specs = std::span<const motion::PropertySpec>(specList);
        const auto modes = currentModes();
        motionModes = modes.has_value();
        auto signature = editable ? juce::String(static_cast<int>(found->camera)) + juce::String(static_cast<int>(found->beam)) + juce::String(static_cast<int>(found->isAudio)) + juce::String(static_cast<int>(motionModes)) : juce::String();
        signature << ":" << static_cast<int>(specList.size());
        if (signature != layoutSignature) {
            layoutSignature = signature;
            build(specs);
        }
        for (auto& row : rows) {
            if (row->mode == nullptr || !modes.has_value()) { continue; }
            const bool path = row->group == "Position";
            const bool on = path ? modes->first : modes->second;
            const std::string prefix = path ? "position." : "rotation.";
            const auto* x = found->curve(prefix + "x");
            const auto* y = found->curve(prefix + "y");
            const auto* z = found->curve(prefix + "z");
            const bool keyed = x != nullptr && y != nullptr && z != nullptr && (x->animated() || y->animated() || z->animated());
            const auto times = [](const motion::Curve& curve) {
                std::vector<double> result;
                for (const auto& key : curve.keyframes()) { result.push_back(key.time); }
                return result;
            };
            row->misaligned = on && keyed && (times(*x) != times(*y) || times(*x) != times(*z));
            row->mode->setToggleState(on, juce::dontSendNotification);
            row->mode->setOnColour(row->misaligned ? motion::style::warning().withAlpha(.6f) : motion::style::accent().withAlpha(.45f));
            const juce::String base = path ? "Travel one smooth path through the keyed positions at constant speed (Bezier keys ease in and out)."
                                           : "Interpolate keyed rotations as orientations along the shortest arc, free of gimbal lock.";
            row->mode->setTooltip(row->misaligned ? "X, Y and Z no longer share key times, so this is paused. Click to key every axis at each key time." : base + " Keys all three axes together.");
        }
        title.setEditable(false, editable && found->camera);
        title.setTooltip(editable && found->camera ? "Double-click to rename" : juce::String());
        if (!title.isBeingEdited()) { title.setText(editable ? juce::String(found->name.data(), found->name.size()) : heading.has_value() ? heading->first : juce::String(), juce::dontSendNotification); }
        // With several clips selected the header says so; edits apply to the
        // one named.
        const juce::String kindText = !editable ? (heading.has_value() ? heading->second : juce::String()) : found->beam ? juce::String() : found->camera ? "Camera" : found->isGroup ? "Group" : found->isAudio ? "Audio" : sourceKind(target);
        kind.setText(editable && selectionCount > 1 ? "Editing 1 of " + juce::String(selectionCount) : kindText, juce::dontSendNotification);
        kind.setTooltip(editable && selectionCount > 1 ? juce::String(selectionCount) + " clips are selected; these fields edit only " + juce::String(found->name.data(), found->name.size()) + "." : juce::String());
        // Children repaint their own changes; only the empty message is painted here.
        const auto wasEmpty = std::exchange(empty, !editable && !heading.has_value());
        if (empty != wasEmpty) { repaint(); }
        if (!editable) { return; }
        const auto time = keyTime(*found);
        for (auto& row : rows) {
            bool allKeyed = true, anyAnimated = false, modulated = false;
            for (auto& field : row->fields) {
                const auto property = std::string(field->spec.id);
                const auto* curve = found->curve(property);
                modulated = modulated || isModulated(property);
                if (curve == nullptr) { continue; }
                field->editor.setValue(curve->evaluateBase(time));
                anyAnimated = anyAnimated || curve->animated();
                allKeyed = allKeyed && hasKey(*curve, time);
            }
            // A locked track's values show but do not edit.
            for (auto& field : row->fields) { field->editor.setEnabled(!found->locked); }
            row->key.setEnabled(!found->locked);
            if (row->swatch != nullptr) { row->swatch->setEnabled(!found->locked); }
            row->key.setState(allKeyed && anyAnimated ? osci::KeyframeButton::State::keyed
                : (anyAnimated ? osci::KeyframeButton::State::animated : osci::KeyframeButton::State::unanimated));
            row->previous.setEnabled(anyAnimated);
            row->next.setEnabled(anyAnimated);
            row->modulate.setToggleState(modulated, juce::dontSendNotification);
            if (row->swatch != nullptr) {
                const auto rgb = colourOf(*row);
                row->swatch->setColour(juce::Colour::fromFloatRGBA(static_cast<float>(rgb[0]), static_cast<float>(rgb[1]), static_cast<float>(rgb[2]), 1.0f));
            }
        }
    }

    // Modulators dragged from the library route to the field or row dropped on.
    std::function<void(motion::Id modulator, motion::Id target, std::vector<std::string> properties)> onRouteModulator;
    void setModulatorDrag(bool active) {
        modulatorDrag = active;
        if (!active) { dropTarget = nullptr; }
        repaint();
    }
    bool isInterestedInDragSource(const SourceDetails& details) override { return details.description.toString().startsWith("motion-modulator:"); }
    void itemDragMove(const SourceDetails& details) override {
        auto* found = routeTargetAt(details.localPosition);
        if (found != dropTarget.getComponent()) {
            dropTarget = found;
            repaint();
        }
    }
    void itemDragExit(const SourceDetails&) override {
        dropTarget = nullptr;
        repaint();
    }
    void itemDropped(const SourceDetails& details) override {
        auto* found = routeTargetAt(details.localPosition);
        dropTarget = nullptr;
        repaint();
        if (found == nullptr || !onRouteModulator) { return; }
        const auto modulator = static_cast<motion::Id>(details.description.toString().fromFirstOccurrenceOf(":", false, false).getLargeIntValue());
        const auto& properties = found->getProperties();
        const auto owner = properties.contains("routeTarget") ? static_cast<motion::Id>(properties["routeTarget"].toString().getLargeIntValue()) : target;
        std::vector<std::string> names;
        for (const auto& name : juce::StringArray::fromTokens(properties["routeProperties"].toString(), ",", "")) { names.push_back(name.toStdString()); }
        onRouteModulator(modulator, owner, names);
    }
    void paintOverChildren(juce::Graphics& g) override {
        if (!modulatorDrag) { return; }
        g.reduceClipRegion(viewport.getBounds());
        std::function<void(juce::Component&)> outline = [&](juce::Component& parent) {
            for (auto* child : parent.getChildren()) {
                if (!child->isVisible()) { continue; }
                if (child->getProperties().contains("routeProperties") && dynamic_cast<MotionScrubField*>(child) != nullptr) {
                    const auto area = getLocalArea(child, child->getLocalBounds()).toFloat();
                    g.setColour(motion::style::accent().withAlpha(child == dropTarget.getComponent() ? .95f : .35f));
                    g.drawRoundedRectangle(area.reduced(.5f), motion::style::radius, child == dropTarget.getComponent() ? 2.0f : 1.0f);
                }
                outline(*child);
            }
        };
        outline(content);
        auto* row = dropTarget.getComponent();
        if (row != nullptr && dynamic_cast<MotionScrubField*>(row) == nullptr) {
            g.setColour(motion::style::accent().withAlpha(.9f));
            g.drawRoundedRectangle(getLocalArea(row, row->getLocalBounds()).toFloat().expanded(2), motion::style::radius + 1, 2.0f);
        }
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
            auto header = area.removeFromTop(34).reduced(motion::style::padding, 0);
            kind.setBounds(header.removeFromRight(selectionCount > 1 ? 110 : 60));
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
    // The Colour row's swatch: the colour at the playhead; a click opens the picker.
    struct Swatch : juce::Button {
        Swatch() : juce::Button("Colour swatch") {
            setTitle("Colour swatch");
            setTooltip("Pick a colour");
            setWantsKeyboardFocus(false);
            setMouseCursor(juce::MouseCursor::PointingHandCursor);
        }
        void paintButton(juce::Graphics& g, bool highlighted, bool) override {
            const auto bounds = getLocalBounds().toFloat().reduced(.5f);
            g.setColour(colour);
            g.fillRoundedRectangle(bounds, motion::style::radius);
            g.setColour(juce::Colours::white.withAlpha(highlighted ? .5f : .22f));
            g.drawRoundedRectangle(bounds, motion::style::radius, 1.0f);
        }
        void setColour(juce::Colour value) {
            if (value != colour) {
                colour = value;
                repaint();
            }
        }
        juce::Colour colour = juce::Colours::white;
    };
    struct Row : juce::Component {
        juce::String group;
        std::vector<std::unique_ptr<Field>> fields;
        std::unique_ptr<Swatch> swatch;
        osci::KeyframeButton key;
        motion::icons::Chip modulate {"Modulate", motion::icons::Icon::wave};
        motion::style::ChevronButton previous {"Previous key", false}, next {"Next key", true};
        // Position: one spatial path; Rotation: quaternion orientation.
        std::unique_ptr<motion::icons::Chip> mode;
        bool misaligned = false; // mode on, but axes no longer share key times
        // A single value sits on one line: its name, then the value in the
        // grid's last column, beside its keys.
        bool compact() const { return fields.size() == 1 && mode == nullptr && swatch == nullptr; }
        int preferredHeight() const { return compact() ? motion::style::controlHeight : 17 + motion::style::controlHeight; }
        void paint(juce::Graphics& g) override {
            g.setFont(motion::style::caption());
            g.setColour(motion::style::muted());
            g.drawText(group, compact() ? getLocalBounds().withRight(captionRight) : getLocalBounds().removeFromTop(16), juce::Justification::centredLeft);
        }
        void resized() override {
            auto area = getLocalBounds();
            if (compact()) {
                auto line = area;
                next.setBounds(line.removeFromRight(12));
                key.setBounds(line.removeFromRight(18));
                previous.setBounds(line.removeFromRight(12));
                line.removeFromRight(motion::style::gap);
                const auto width = std::min(110, (line.getWidth() - motion::style::gap * 2) / 3);
                auto value = line.withLeft(line.getX() + 2 * (width + motion::style::gap)).withWidth(width);
                fields.front()->editor.setBounds(value);
                modulate.setBounds(juce::Rectangle<int>(20, 18).withCentre({value.getX() - motion::style::gap - 10, line.getCentreY()}));
                captionRight = modulate.getX() - motion::style::gap;
                return;
            }
            auto heading = area.removeFromTop(16);
            if (swatch != nullptr) {
                // Beside the heading, as After Effects places a colour's swatch.
                const auto label = juce::roundToInt(juce::TextLayout::getStringWidth(motion::style::caption(), group));
                swatch->setBounds(heading.getX() + label + 6, heading.getY() + 2, 24, 12);
            }
            modulate.setBounds(heading.removeFromRight(20).reduced(0, 1));
            if (mode != nullptr) {
                heading.removeFromRight(motion::style::gap);
                mode->setBounds(heading.removeFromRight(20).reduced(0, 1));
            }
            area.removeFromTop(1);
            auto line = area.removeFromTop(motion::style::controlHeight);
            next.setBounds(line.removeFromRight(12));
            key.setBounds(line.removeFromRight(18));
            previous.setBounds(line.removeFromRight(12));
            line.removeFromRight(motion::style::gap);
            const auto count = static_cast<int>(fields.size());
            // Capped, so an axis label stays beside its value in a wide inspector.
            // Single values take one column of the three-axis grid so every row lines up.
            const auto columns = std::max(3, count);
            const auto width = std::min(110, (line.getWidth() - motion::style::gap * (columns - 1)) / columns);
            for (int index = 0; index < count; ++index) {
                fields[static_cast<std::size_t>(index)]->editor.setBounds(line.removeFromLeft(width));
                line.removeFromLeft(motion::style::gap);
            }
        }
        int captionRight = 0;
    };
    struct Gesture {
        motion::Project before;
        std::uint64_t revision;
        motion::Id target;
    };

    // Driven by a link or a shared modulator route.
    bool isModulated(const std::string& property) const {
        const auto& project = processor.document.project();
        const auto routed = std::any_of(project.routes.begin(), project.routes.end(), [&](const auto& route) { return route.target == target && route.property == property; });
        const auto found = motion::findPropertyTarget(project, target);
        const auto* curve = found.has_value() ? found->curve(property) : nullptr;
        return routed || (curve != nullptr && curve->link.has_value());
    }
    void build(std::span<const motion::PropertySpec> specs) {
        rows.clear();
        content.removeAllChildren();
        if (lead != nullptr) { content.addChildComponent(lead); }
        if (trail != nullptr) { content.addChildComponent(trail); }
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
                row->modulate.setClickingTogglesState(false);
                row->modulate.quiet = true;
                row->modulate.setName("Modulate " + namePrefix + row->group.toLowerCase());
                row->modulate.setTitle(row->modulate.getName());
                row->modulate.setTooltip("Modulate " + row->group.toLowerCase() + ": open it in the Graph with its oscillator, modulator routes and link. Lit when something drives it.");
                row->addAndMakeVisible(row->modulate);
                auto* raw = row.get();
                row->modulate.onClick = [this, raw] {
                    if (raw->fields.empty() || !onModulate) { return; }
                    // Open the first axis that is actually driven.
                    auto property = std::string(raw->fields.front()->spec.id);
                    for (const auto& field : raw->fields) {
                        const auto id = std::string(field->spec.id);
                        if (isModulated(id)) { property = id; break; }
                    }
                    onModulate(target, property);
                };
                row->key.onClick = [this, raw] { toggleKeys(*raw); };
                // Like Motion's other icon buttons: no focus ring after a click.
                row->key.setWantsKeyboardFocus(false);
                if (motionModes && (row->group == "Position" || row->group == "Rotation")) {
                    const bool path = row->group == "Position";
                    row->mode = std::make_unique<motion::icons::Chip>(path ? "Spatial path" : "Quaternion rotation", path ? motion::icons::Icon::path : motion::icons::Icon::rotate);
                    row->mode->setName(namePrefix + (path ? "Spatial path" : "Quaternion rotation"));
                    row->mode->setTitle(row->mode->getName());
                    row->mode->setTooltip(path ? "Travel one smooth path through the keyed positions at constant speed (Bezier keys ease in and out). Keys all three axes together."
                                               : "Interpolate keyed rotations as orientations along the shortest arc, free of gimbal lock. Keys all three axes together.");
                    // A misaligned mode stays on and realigns its keys.
                    row->mode->onClick = [this, raw, path] { setMotionMode(path, raw->mode->getToggleState() || raw->misaligned); };
                    row->addAndMakeVisible(*row->mode);
                }
                if (row->group == "Colour") {
                    row->swatch = std::make_unique<Swatch>();
                    row->swatch->onClick = [this, raw] { openColourPicker(*raw); };
                    row->addAndMakeVisible(*row->swatch);
                }
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
            editor.onCancel = [this] { cancelGesture(); refresh(); };
            editor.onCommit = [this, property](double value) { commitValue(property, value); };
            // A modulator dropped here drives this axis; on the row, all of them.
            editor.getProperties().set("routeProperties", juce::String(property));
            auto& routes = rows.back()->getProperties();
            routes.set("routeProperties", routes["routeProperties"].toString() + (routes["routeProperties"].toString().isEmpty() ? "" : ",") + juce::String(property));
            rows.back()->addAndMakeVisible(editor);
            rows.back()->fields.push_back(std::move(field));
        }
        layoutContent();
    }
    void layoutContent() {
        const auto width = viewport.getWidth() - (viewport.isVerticalScrollBarShown() ? 8 : 0);
        int y = 0;
        const auto leading = lead != nullptr && leadHeight ? leadHeight() : 0;
        if (lead != nullptr) {
            lead->setBounds(motion::style::padding, y, width - motion::style::padding * 2, leading);
            lead->setVisible(leading > 0);
            y += leading > 0 ? leading + motion::style::padding : 0;
        }
        for (std::size_t index = 0; index < rows.size(); ++index) {
            auto& row = rows[index];
            row->setBounds(motion::style::padding, y, width - motion::style::padding * 2, row->preferredHeight());
            // One-line values stack closer, as a list.
            const auto nextCompact = index + 1 < rows.size() && rows[index + 1]->compact();
            y += row->preferredHeight() + (row->compact() && nextCompact ? motion::style::gap : motion::style::padding);
        }
        const auto trailing = trail != nullptr && trailHeight ? trailHeight() : 0;
        if (trail != nullptr) {
            trail->setBounds(motion::style::padding, y, width - motion::style::padding * 2, trailing);
            trail->setVisible(trailing > 0);
            y += trailing > 0 ? trailing + motion::style::padding : 0;
        }
        content.setSize(std::max(0, width), y + motion::style::padding);
    }

    // Keys land on frames so they align with the timeline grid and exports.
    double keyTime(const motion::PropertyTarget& found) const {
        if (selectedKeyTime) {
            const auto selected = selectedKeyTime(found.id);
            if (selected.has_value()) { return *selected; }
        }
        return found.localTime(processor.document.project().frameTime(processor.position.load()));
    }
    static bool hasKey(const motion::Curve& curve, double time) {
        const auto& keys = curve.keyframes();
        return std::any_of(keys.begin(), keys.end(), [time](const auto& key) { return std::abs(key.time - time) < 1.0e-6; });
    }
    void apply(motion::Project& project, const std::string& property, double value, double time) const {
        auto* curve = motion::findPropertyCurve(project, target, property);
        if (curve == nullptr) { curve = createSlider(project, property); }
        if (curve == nullptr) { return; }
        if (curve->animated()) { curve->setKeyValue(time, value); } else { curve->base = value; }
    }
    void beginGesture(const std::string& property) {
        const auto found = motion::findPropertyTarget(processor.document.project(), target);
        if (!found.has_value() || found->locked) { return; }
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
    void endGesture(const juce::String& name = "Change property") {
        if (gesture.has_value() && changed && processor.document.revision() == gesture->revision) {
            processor.document.commit(name.toStdString(), std::move(gesture->before));
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
    // The row's red, green and blue as shown (0..1).
    static MotionColourPicker::Rgb colourOf(const Row& row) {
        MotionColourPicker::Rgb rgb {1.0, 1.0, 1.0};
        for (const auto& field : row.fields) {
            const auto id = std::string(field->spec.id);
            const auto index = id == "red" ? 0 : id == "green" ? 1 : id == "blue" ? 2 : -1;
            if (index >= 0) { rgb[static_cast<std::size_t>(index)] = std::clamp(field->editor.getValue(), 0.0, 1.0); }
        }
        return rgb;
    }
    // The picker edits all three channels as one gesture and one undo step.
    void openColourPicker(Row& row) {
        if (row.swatch == nullptr || !onShowPopover) { return; }
        auto picker = std::make_unique<MotionColourPicker>(colourOf(row));
        picker->onBegin = [this] { beginGesture("red"); };
        picker->onChange = [this](MotionColourPicker::Rgb rgb) {
            if (!gesture.has_value() || gesture->target != target) { return; }
            auto updated = gesture->before;
            for (const auto& [property, value] : std::initializer_list<std::pair<const char*, double>> {{"red", rgb[0]}, {"green", rgb[1]}, {"blue", rgb[2]}}) {
                apply(updated, property, value, gestureTime);
            }
            processor.document.preview(std::move(updated));
            gesture->revision = processor.document.revision();
            changed = true;
        };
        picker->onEnd = [this] { endGesture("Change colour"); };
        onShowPopover(std::move(picker), *row.swatch);
    }
    void commitValue(const std::string& property, double value) {
        const auto found = motion::findPropertyTarget(processor.document.project(), target);
        if (!found.has_value() || found->locked) {
            refresh();
            return;
        }
        const auto time = keyTime(*found);
        processor.document.edit("Change property", [&](motion::Project& project) { apply(project, property, value, time); });
        if (onPropertySelected) { onPropertySelected(target, property); }
        if (onKeyTimeEdited) { onKeyTimeEdited(); }
    }
public:
    // Alt+Shift+P/R/S/T, as in After Effects: key a group at the playhead.
    bool toggleGroupKeys(const juce::String& group) {
        for (auto& row : rows) {
            if (row->group == group && row->isVisible()) {
                toggleKeys(*row);
                return true;
            }
        }
        return false;
    }
private:
    void toggleKeys(Row& row) {
        const auto found = motion::findPropertyTarget(processor.document.project(), target);
        if (!found.has_value() || found->locked) { return; }
        const auto time = keyTime(*found);
        bool allKeyed = true;
        for (auto& field : row.fields) {
            const auto* curve = found->curve(std::string(field->spec.id));
            allKeyed = allKeyed && curve != nullptr && hasKey(*curve, time);
        }
        processor.document.edit(allKeyed ? "Remove keyframe" : "Set keyframe", [&](motion::Project& project) {
            for (auto& field : row.fields) {
                auto* curve = motion::findPropertyCurve(project, target, std::string(field->spec.id));
                if (curve == nullptr) { curve = createSlider(project, std::string(field->spec.id)); }
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
        const auto projectTime = found->projectTime(*best);
        processor.seek(std::clamp(projectTime, 0.0, processor.document.project().duration));
    }

    // What a clip shows, named by its source rather than "Object".
    juce::String sourceKind(motion::Id clip) const {
        const auto& project = processor.document.project();
        for (const auto& track : project.tracks) {
            for (const auto& item : track.clips) {
                if (item.id != clip) { continue; }
                if (item.composition != 0) { return "Composition"; }
                const auto found = std::find_if(project.assets.begin(), project.assets.end(), [&](const auto& asset) { return asset->id == item.asset; });
                if (found == project.assets.end()) { return "Object"; }
                const auto& asset = **found;
                const auto extension = asset.extension.toLowerCase();
                if (asset.liveIdentity != nullptr) { return "Blender"; }
                if (extension == ".txt") { return "Text"; }
                if (extension == ".lua") { return "Lua"; }
                if (extension == ".lsystem") { return "Fractal"; }
                if (motion::Document::isVideoSource(extension)) { return "Video"; }
                if (motion::Document::isRasterSource(extension)) { return "Image"; }
                if (extension == ".svg") { return motion::drawing::isDrawing(juce::String::fromUTF8(static_cast<const char*>(asset.data.getData()), static_cast<int>(asset.data.getSize()))) ? "Drawing" : "Vector"; }
                if (extension == ".json" || extension == ".lottie") { return "Lottie"; }
                if (extension == ".obj") { return "3D object"; }
                return "Object";
            }
        }
        return "Object";
    }
    // Sliders a Lua clip's script reads (slider_a ...), plus any it already
    // animates. Their curves are created on first edit.
    std::vector<motion::PropertySpec> luaSliders() const {
        std::vector<motion::PropertySpec> result;
        const auto& project = processor.document.project();
        for (const auto& track : project.tracks) {
            for (const auto& clip : track.clips) {
                if (clip.id != target || clip.composition != 0) { continue; }
                const auto asset = std::find_if(project.assets.begin(), project.assets.end(), [&](const auto& item) { return item != nullptr && item->id == clip.asset; });
                if (asset == project.assets.end() || !(*asset)->extension.equalsIgnoreCase(".lua")) { return result; }
                const auto script = juce::String::fromUTF8(static_cast<const char*>((*asset)->data.getData()), static_cast<int>((*asset)->data.getSize()));
                for (const auto& spec : motion::luaSliderSpecs()) {
                    const auto name = juce::String("slider_") + juce::String::charToString(static_cast<juce::juce_wchar>(spec.id.back()));
                    if (script.contains(name) || clip.properties.contains(std::string(spec.id))) { result.push_back(spec); }
                }
                return result;
            }
        }
        return result;
    }
    motion::Curve* createSlider(motion::Project& project, const std::string& property) const {
        if (!property.starts_with("slider.")) { return nullptr; }
        auto found = motion::findPropertyTarget(project, target);
        if (!found.has_value() || found->properties == nullptr) { return nullptr; }
        return &(*found->properties)[property];
    }
    // (spatial path, quaternion rotation) of the target clip or group.
    std::optional<std::pair<bool, bool>> currentModes() const {
        const auto& project = processor.document.project();
        for (const auto& group : project.groups) {
            if (group.id == target) { return std::make_pair(group.spatialPath, group.quaternionRotation); }
        }
        for (const auto& track : project.tracks) {
            if (track.kind != motion::TrackKind::visual) { continue; }
            for (const auto& clip : track.clips) {
                if (clip.id == target) { return std::make_pair(clip.spatialPath, clip.quaternionRotation); }
            }
        }
        return std::nullopt;
    }
    // Turning a mode on keys every axis wherever any axis has a key, so the
    // three curves share key times and the path or orientation applies.
    void setMotionMode(bool path, bool enabled) {
        const auto id = target;
        const std::string prefix = path ? "position." : "rotation.";
        processor.document.edit(path ? (enabled ? "Use spatial path" : "Use separate position axes") : (enabled ? "Use quaternion rotation" : "Use Euler rotation"),
            [id, path, enabled, prefix](motion::Project& project) {
            const auto apply = [&](auto& owner) {
                (path ? owner.spatialPath : owner.quaternionRotation) = enabled;
                if (!enabled) { return; }
                std::set<double> times;
                for (const auto axis : {"x", "y", "z"}) {
                    for (const auto& key : owner.properties[prefix + axis].keyframes()) { times.insert(key.time); }
                }
                for (const auto axis : {"x", "y", "z"}) {
                    auto& curve = owner.properties[prefix + axis];
                    const auto copy = curve;
                    for (const auto time : times) {
                        if (std::none_of(copy.keyframes().begin(), copy.keyframes().end(), [time](const auto& key) { return key.time == time; })) {
                            curve.setKeyValue(time, copy.evaluateBase(time));
                        }
                    }
                }
            };
            for (auto& group : project.groups) { if (group.id == id) { apply(group); } }
            for (auto& track : project.tracks) {
                for (auto& clip : track.clips) { if (clip.id == id) { apply(clip); } }
            }
        });
    }

    MotionProcessor& processor;
    juce::Viewport viewport;
    juce::Component content;
    juce::Label title, kind;
    std::vector<std::unique_ptr<Row>> rows;
    // The innermost field or row under `point` that a modulator can drive.
    juce::Component* routeTargetAt(juce::Point<int> point) {
        auto* component = getComponentAt(point);
        while (component != nullptr && component != this) {
            if (component->getProperties().contains("routeProperties")) { return component; }
            component = component->getParentComponent();
        }
        return nullptr;
    }
    bool modulatorDrag = false;
    juce::Component::SafePointer<juce::Component> dropTarget;
    juce::Component* lead = nullptr;
    juce::Component* trail = nullptr;
    std::function<int()> trailHeight;
    std::optional<std::pair<juce::String, juce::String>> heading;
    std::size_t selectionCount = 1;
    std::function<int()> leadHeight;
    std::vector<motion::PropertySpec> specList;
    motion::Id target = 0;
    juce::String layoutSignature = "none", namePrefix;
    std::optional<Gesture> gesture;
    double gestureTime = 0;
    bool changed = false, empty = true, showsHeader = true, motionModes = false;
};
