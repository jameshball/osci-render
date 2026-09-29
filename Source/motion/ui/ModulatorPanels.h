#pragma once

#include "../MotionProcessor.h"
#include "../model/ModulationGraph.h"
#include "MotionStyle.h"
#include "ScrubField.h"

namespace motion::ui {
inline const EffectInstance* findEffect(const Project& project, Id id) {
    const EffectInstance* found = nullptr;
    const auto scan = [&](const std::vector<EffectInstance>& list) {
        for (const auto& effect : list) { if (effect.id == id) { found = &effect; } }
    };
    scan(project.effects);
    for (const auto& group : project.groups) { scan(group.effects); }
    for (const auto& track : project.tracks) {
        scan(track.effects);
        for (const auto& clip : track.clips) { scan(clip.effects); }
    }
    return found;
}

inline juce::String propertyLabel(const Project& project, Id id, const std::string& property) {
    const auto* effect = findEffect(project, id);
    const auto* definition = effect != nullptr ? effectDefinition(effect->type) : nullptr;
    if (definition != nullptr) {
        for (const auto& parameter : definition->parameters) {
            if (parameter.id == property) { return juce::String(parameter.name); }
        }
    }
    for (const auto specs : {objectPropertySpecs(), cameraPropertySpecs(), audioPropertySpecs()}) {
        const auto* spec = findPropertySpec(specs, property);
        if (spec != nullptr) { return juce::String(spec->label.data(), spec->label.size()); }
    }
    return juce::String(property);
}

inline juce::String describeProperty(const Project& project, Id id, const std::string& property) {
    const auto target = findPropertyTarget(project, id);
    const auto owner = target.has_value() ? juce::String(target->name.data(), target->name.size()) : juce::String("Missing");
    return owner + juce::String(juce::CharPointer_UTF8(" \xc2\xb7 ")) + propertyLabel(project, id, property);
}

inline juce::String describeModulator(const Modulator& modulator) {
    if (modulator.kind == ModulatorKind::envelope) { return "Envelope"; }
    if (modulator.kind == ModulatorKind::controller) { return modulator.controller == MidiControl::pitchBend ? "Pitch bend" : "CC " + juce::String(modulator.controller); }
    static const char* shapes[] { "Sine", "Triangle", "Saw", "Square", "Smooth random", "Random steps", "Loudness" };
    const auto index = static_cast<int>(modulator.shape.waveform);
    return index >= 0 && index < 7 ? juce::String(shapes[index]) : juce::String("Oscillator");
}

inline constexpr PropertySpec rateHzSpec {"rate", "Rate", "", "", 0.001, 1000, 1, .01, 3, " Hz"};
inline constexpr PropertySpec beatsSpec {"beats", "Beats", "", "", 0.0625, 64, 1, .25, 3, " beats"};
inline constexpr PropertySpec phaseSpec {"phase", "Phase", "", "", 0, 360, 0, 1, 1, "\xc2\xb0"};
inline constexpr PropertySpec seedSpec {"seed", "Seed", "", "", 0, 4294967295.0, 0, 1, 0, ""};
inline constexpr PropertySpec secondsSpec {"seconds", "Seconds", "", "", 0, 60, 0, .01, 3, " s"};
inline constexpr PropertySpec unitSpec {"unit", "Level", "", "", 0, 1, 0, .01, 2, ""};
inline constexpr PropertySpec pitchSpec {"pitch", "Pitch", "", "", 0, 127, 0, 1, 0, ""};
inline constexpr PropertySpec amountSpec {"amount", "Amount", "", "", -unbounded, unbounded, 1, .01, 3, ""};
inline constexpr PropertySpec delaySpec {"delay", "Delay", "", "", -3600, 3600, 0, .01, 3, " s"};

// A labelled scrub row; owners set the spec and callbacks.
struct LabelledScrub {
    juce::Label label;
    MotionScrubField field;
    void setup(juce::Component& owner, const juce::String& text, const juce::String& name, const PropertySpec& spec) {
        label.setText(text, juce::dontSendNotification);
        label.setFont(style::small());
        label.setColour(juce::Label::textColourId, style::muted());
        field.setName(name);
        field.setTitle(name);
        field.setSpec(spec);
        owner.addAndMakeVisible(label);
        owner.addAndMakeVisible(field);
    }
    void setVisible(bool visible) { label.setVisible(visible); field.setVisible(visible); }
    void layout(juce::Rectangle<int>& area) {
        auto row = area.removeFromTop(style::controlHeight);
        area.removeFromTop(style::gap);
        label.setBounds(row.removeFromLeft(62));
        field.setBounds(row);
    }
};
}

// Library tab: the composition's shared modulators, one selected for editing
// with the list of properties it drives.
class MotionModulatorLibrary final : public juce::Component, private juce::ChangeListener {
public:
    explicit MotionModulatorLibrary(MotionProcessor& owner) : processor(owner) {
        setName("Modulator library");
        addOscillator.setButtonText("+ LFO");
        addOscillator.setName("Add LFO");
        addOscillator.setTitle("Add LFO");
        addOscillator.setTooltip("Add a shared oscillator, random or soundtrack follower");
        addEnvelope.setButtonText("+ Env");
        addEnvelope.setName("Add envelope");
        addEnvelope.setTitle("Add envelope");
        addEnvelope.setTooltip("Add an envelope that fires on the notes of a MIDI clip");
        addOscillator.onClick = [this] { add(motion::ModulatorKind::oscillator); };
        addEnvelope.onClick = [this] { add(motion::ModulatorKind::envelope); };
        addController.setButtonText("+ MIDI CC");
        addController.setName("Add MIDI controller");
        addController.setTitle("Add MIDI controller");
        addController.setTooltip("Add a modulator that follows a MIDI clip's controller or pitch bend");
        addController.onClick = [this] { add(motion::ModulatorKind::controller); };
        for (auto* button : {&addOscillator, &addEnvelope, &addController}) {
            button->setColour(juce::TextButton::buttonColourId, motion::style::raised());
            addAndMakeVisible(button);
        }
        list.setName("Modulators");
        list.setTitle("Modulators");
        list.setTextWhenNoChoicesAvailable("No modulators");
        list.setTextWhenNothingSelected("Choose a modulator");
        list.onChange = [this] {
            const auto index = list.getSelectedItemIndex();
            const auto& modulators = processor.document.project().modulators;
            selected = index >= 0 && index < static_cast<int>(modulators.size()) ? modulators[static_cast<std::size_t>(index)].id : 0;
            refresh();
        };
        addAndMakeVisible(list);
        name.setName("Modulator name");
        name.setTitle("Modulator name");
        name.setEditable(false, true);
        name.setColour(juce::Label::backgroundColourId, motion::style::field());
        name.onTextChange = [this] {
            const auto text = name.getText().trim();
            if (text.isEmpty() || text.length() > 120) { refresh(); return; }
            change([text](auto& modulator) { modulator.name = text.toStdString(); });
        };
        addAndMakeVisible(name);
        remove.setButtonText("Delete");
        remove.setName("Delete modulator");
        remove.setTitle("Delete modulator");
        remove.setColour(juce::TextButton::buttonColourId, motion::style::raised());
        remove.onClick = [this] {
            if (selected == 0) { return; }
            report(processor.document.removeModulator(selected));
            selected = 0;
            refresh();
        };
        addAndMakeVisible(remove);
        waveform.setName("Modulator waveform");
        waveform.addItemList({ "Sine", "Triangle", "Saw", "Square", "Smooth random", "Random steps", "Soundtrack loudness" }, 1);
        waveform.onChange = [this] { change([this](auto& modulator) { modulator.shape.waveform = static_cast<motion::ModulationWaveform>(waveform.getSelectedId() - 1); }); };
        clock.setName("Modulator clock");
        clock.addItemList({ "Hz", "Beats" }, 1);
        clock.onChange = [this] { change([this](auto& modulator) { modulator.shape.tempoSync = clock.getSelectedId() == 2; }); };
        source.setName("Envelope source");
        source.setTextWhenNothingSelected("Choose a MIDI clip");
        source.onChange = [this] {
            const auto index = source.getSelectedItemIndex();
            if (index < 0 || index >= static_cast<int>(sourceIds.size())) { return; }
            const auto id = sourceIds[static_cast<std::size_t>(index)];
            change([id](auto& modulator) { modulator.source = id; });
        };
        controller.setName("Modulator controller");
        controller.setTitle("Modulator controller");
        controller.addItem("Pitch bend", 129);
        for (const auto [number, label] : std::initializer_list<std::pair<int, const char*>> {{1, "Mod wheel (CC 1)"}, {2, "Breath (CC 2)"}, {7, "Volume (CC 7)"}, {10, "Pan (CC 10)"}, {11, "Expression (CC 11)"}}) {
            controller.addItem(label, number + 1);
        }
        for (int number = 0; number < 128; ++number) {
            if (number != 1 && number != 2 && number != 7 && number != 10 && number != 11) { controller.addItem("CC " + juce::String(number), number + 1); }
        }
        controller.onChange = [this] { const auto number = controller.getSelectedId() - 1; change([number](auto& modulator) { modulator.controller = number; }); };
        channel.setName("Modulator channel");
        channel.setTitle("Modulator channel");
        channel.addItem("Any channel", 1);
        for (int index = 1; index <= 16; ++index) { channel.addItem("Channel " + juce::String(index), index + 1); }
        channel.onChange = [this] { const auto value = channel.getSelectedId() - 1; change([value](auto& modulator) { modulator.controllerChannel = value; }); };
        for (auto* box : {&waveform, &clock, &source, &controller, &channel}) { addAndMakeVisible(box); }
        rate.setup(*this, "Rate", "Modulator rate", motion::ui::rateHzSpec);
        phase.setup(*this, "Phase", "Modulator phase", motion::ui::phaseSpec);
        seed.setup(*this, "Seed", "Modulator seed", motion::ui::seedSpec);
        attack.setup(*this, "Attack", "Envelope attack", motion::ui::secondsSpec);
        decay.setup(*this, "Decay", "Envelope decay", motion::ui::secondsSpec);
        sustain.setup(*this, "Sustain", "Envelope sustain", motion::ui::unitSpec);
        release.setup(*this, "Release", "Envelope release", motion::ui::secondsSpec);
        velocity.setup(*this, "Velocity", "Envelope velocity", motion::ui::unitSpec);
        lowest.setup(*this, "Low note", "Envelope lowest pitch", motion::ui::pitchSpec);
        highest.setup(*this, "High note", "Envelope highest pitch", motion::ui::pitchSpec);
        bind(rate, [](auto& modulator, double value) {
            if (modulator.shape.tempoSync) { modulator.shape.beatsPerCycle = value; } else { modulator.shape.rateHz = value; }
        });
        bind(phase, [](auto& modulator, double value) { modulator.shape.phase = value / 360.0; });
        bind(seed, [](auto& modulator, double value) { modulator.shape.seed = static_cast<std::uint32_t>(value); });
        bind(attack, [](auto& modulator, double value) { modulator.attack = value; });
        bind(decay, [](auto& modulator, double value) { modulator.decay = value; });
        bind(sustain, [](auto& modulator, double value) { modulator.sustain = value; });
        bind(release, [](auto& modulator, double value) { modulator.release = value; });
        bind(velocity, [](auto& modulator, double value) { modulator.velocity = value; });
        bind(lowest, [](auto& modulator, double value) { modulator.lowestPitch = static_cast<int>(value); });
        bind(highest, [](auto& modulator, double value) { modulator.highestPitch = static_cast<int>(value); });
        routesTitle.setText("Drives", juce::dontSendNotification);
        routesTitle.setFont(motion::style::strong());
        addAndMakeVisible(routesTitle);
        hint.setText("No modulators. Add an LFO, random, envelope or controller, then route it from the Graph.", juce::dontSendNotification);
        hint.setTooltip("One modulator drives many properties from a single clock: an LFO, a random walk, the soundtrack's loudness, or a MIDI clip's notes (envelope) and controllers.");
        hint.setFont(motion::style::small());
        hint.setColour(juce::Label::textColourId, motion::style::muted());
        hint.setJustificationType(juce::Justification::topLeft);
        addAndMakeVisible(hint);
        processor.document.addChangeListener(this);
        refresh();
    }
    ~MotionModulatorLibrary() override { processor.document.removeChangeListener(this); }

    std::function<void(const juce::String&)> onError;
    motion::Id selectedModulator() const { return selected; }
    void select(motion::Id id) { selected = id; refresh(); }

    void refresh() {
        const auto& project = processor.document.project();
        const auto& modulators = project.modulators;
        if (selected != 0 && std::none_of(modulators.begin(), modulators.end(), [this](const auto& item) { return item.id == selected; })) { selected = 0; }
        if (selected == 0 && !modulators.empty()) { selected = modulators.front().id; }
        list.clear(juce::dontSendNotification);
        list.setVisible(!modulators.empty());
        hint.setVisible(modulators.empty());
        for (std::size_t index = 0; index < modulators.size(); ++index) {
            list.addItem(juce::String(modulators[index].name) + "  (" + motion::ui::describeModulator(modulators[index]) + ")", static_cast<int>(index) + 1);
            if (modulators[index].id == selected) { list.setSelectedItemIndex(static_cast<int>(index), juce::dontSendNotification); }
        }
        const auto* modulator = current();
        const auto oscillator = modulator != nullptr && modulator->kind == motion::ModulatorKind::oscillator;
        const auto envelope = modulator != nullptr && modulator->kind == motion::ModulatorKind::envelope;
        const auto midiControl = modulator != nullptr && modulator->kind == motion::ModulatorKind::controller;
        const auto loudness = oscillator && modulator->shape.waveform == motion::ModulationWaveform::soundtrack;
        const auto noise = oscillator && (modulator->shape.waveform == motion::ModulationWaveform::noiseHold || modulator->shape.waveform == motion::ModulationWaveform::noiseSmooth);
        name.setVisible(modulator != nullptr);
        remove.setVisible(modulator != nullptr);
        waveform.setVisible(oscillator);
        clock.setVisible(oscillator && !loudness);
        rate.setVisible(oscillator && !loudness);
        phase.setVisible(oscillator && !loudness);
        seed.setVisible(noise);
        source.setVisible(envelope || midiControl);
        controller.setVisible(midiControl);
        channel.setVisible(midiControl);
        for (auto* row : {&attack, &decay, &sustain, &release, &velocity, &lowest, &highest}) { row->setVisible(envelope); }
        if (modulator != nullptr) {
            if (!name.isBeingEdited()) { name.setText(juce::String(modulator->name), juce::dontSendNotification); }
            waveform.setSelectedId(static_cast<int>(modulator->shape.waveform) + 1, juce::dontSendNotification);
            clock.setSelectedId(modulator->shape.tempoSync ? 2 : 1, juce::dontSendNotification);
            controller.setSelectedId(modulator->controller + 1, juce::dontSendNotification);
            channel.setSelectedId(modulator->controllerChannel + 1, juce::dontSendNotification);
            rate.field.setSpec(modulator->shape.tempoSync ? motion::ui::beatsSpec : motion::ui::rateHzSpec);
            rate.field.setValue(modulator->shape.tempoSync ? modulator->shape.beatsPerCycle : modulator->shape.rateHz);
            phase.field.setValue(modulator->shape.phase * 360.0);
            seed.field.setValue(modulator->shape.seed);
            attack.field.setValue(modulator->attack);
            decay.field.setValue(modulator->decay);
            sustain.field.setValue(modulator->sustain);
            release.field.setValue(modulator->release);
            velocity.field.setValue(modulator->velocity);
            lowest.field.setValue(modulator->lowestPitch);
            highest.field.setValue(modulator->highestPitch);
            source.clear(juce::dontSendNotification);
            sourceIds.clear();
            for (const auto& track : project.tracks) {
                for (const auto& clip : track.clips) {
                    if (track.kind != motion::TrackKind::visual || clip.midi == nullptr) { continue; }
                    sourceIds.push_back(clip.id);
                    source.addItem(juce::String(clip.name), static_cast<int>(sourceIds.size()));
                    if (clip.id == modulator->source) { source.setSelectedItemIndex(static_cast<int>(sourceIds.size()) - 1, juce::dontSendNotification); }
                }
            }
        }
        rebuildRoutes();
        resized();
        repaint();
    }

    void resized() override {
        auto area = getLocalBounds().reduced(motion::style::gap, motion::style::padding);
        auto buttons = area.removeFromTop(motion::style::controlHeight);
        const auto third = buttons.getWidth() / 3;
        addOscillator.setBounds(buttons.removeFromLeft(third).reduced(1, 0));
        addEnvelope.setBounds(buttons.removeFromLeft(third).reduced(1, 0));
        addController.setBounds(buttons.reduced(1, 0));
        area.removeFromTop(motion::style::padding);
        if (hint.isVisible()) { hint.setBounds(area.removeFromTop(48)); }
        list.setBounds(area.removeFromTop(motion::style::controlHeight));
        area.removeFromTop(motion::style::gap);
        if (current() == nullptr) { return; }
        auto header = area.removeFromTop(motion::style::controlHeight);
        remove.setBounds(header.removeFromRight(56));
        header.removeFromRight(motion::style::gap);
        name.setBounds(header);
        area.removeFromTop(motion::style::gap);
        const auto place = [&](juce::Component& component) {
            if (!component.isVisible()) { return; }
            component.setBounds(area.removeFromTop(motion::style::controlHeight));
            area.removeFromTop(motion::style::gap);
        };
        place(waveform);
        place(source);
        place(controller);
        place(channel);
        if (clock.isVisible()) {
            auto row = area.removeFromTop(motion::style::controlHeight);
            area.removeFromTop(motion::style::gap);
            clock.setBounds(row.removeFromRight(64));
            row.removeFromRight(motion::style::gap);
            rate.label.setBounds(row.removeFromLeft(34));
            rate.field.setBounds(row);
        }
        for (auto* row : {&phase, &seed, &attack, &decay, &sustain, &release, &velocity, &lowest, &highest}) {
            if (row->field.isVisible()) { row->layout(area); }
        }
        area.removeFromTop(motion::style::padding);
        routesTitle.setBounds(area.removeFromTop(20));
        for (auto& route : routes) {
            if (area.getHeight() < motion::style::controlHeight) { route->setVisible(false); continue; }
            route->setVisible(true);
            route->setBounds(area.removeFromTop(motion::style::controlHeight * 2));
            area.removeFromTop(motion::style::gap);
        }
    }
    void paint(juce::Graphics& g) override { g.fillAll(motion::style::panel()); }

private:
    // One route: what it drives, its depth, mode and removal.
    struct RouteRow final : juce::Component {
        RouteRow(MotionModulatorLibrary& owner, const motion::ModulationRoute& route, const juce::String& text) : owner(owner), route(route) {
            target.setText(text, juce::dontSendNotification);
            target.setFont(motion::style::small());
            target.setColour(juce::Label::textColourId, motion::style::text());
            amount.setName("Route amount " + text);
            amount.setTitle("Route amount " + text);
            amount.setSpec(motion::ui::amountSpec);
            amount.setValue(route.amount);
            amount.onChange = [this](double value) { apply(value, this->route.mode); };
            amount.onCommit = [this](double value) { apply(value, this->route.mode); };
            mode.setButtonText(route.mode == motion::ModulationMode::multiply ? "x" : "+");
            mode.setName("Route mode " + text);
            mode.setTitle("Route mode " + text);
            mode.setTooltip("Add to the value, or multiply it by 1 + amount x modulator");
            mode.onClick = [this] { apply(amount.getValue(), this->route.mode == motion::ModulationMode::add ? motion::ModulationMode::multiply : motion::ModulationMode::add); };
            remove.setButtonText("x");
            remove.setName("Remove route " + text);
            remove.setTitle("Remove route " + text);
            remove.onClick = [this] { this->owner.report(this->owner.processor.document.removeRoute(this->route.id)); };
            for (auto* component : std::initializer_list<juce::Component*> {&target, &amount, &mode, &remove}) { addAndMakeVisible(component); }
        }
        void apply(double value, motion::ModulationMode next) {
            auto edited = route;
            edited.amount = value;
            edited.mode = next;
            owner.report(owner.processor.document.setRoute(edited));
        }
        void resized() override {
            auto area = getLocalBounds();
            target.setBounds(area.removeFromTop(area.getHeight() / 2));
            remove.setBounds(area.removeFromRight(24));
            mode.setBounds(area.removeFromRight(24));
            amount.setBounds(area.reduced(0, 1));
        }
        MotionModulatorLibrary& owner;
        motion::ModulationRoute route;
        juce::Label target;
        MotionScrubField amount;
        juce::TextButton mode, remove;
    };

    const motion::Modulator* current() const {
        const auto& modulators = processor.document.project().modulators;
        const auto found = std::find_if(modulators.begin(), modulators.end(), [this](const auto& item) { return item.id == selected; });
        return found == modulators.end() ? nullptr : &*found;
    }
    void add(motion::ModulatorKind kind) {
        motion::Modulator modulator;
        modulator.kind = kind;
        const auto count = std::count_if(processor.document.project().modulators.begin(), processor.document.project().modulators.end(), [kind](const auto& item) { return item.kind == kind; });
        modulator.name = (kind == motion::ModulatorKind::envelope ? "Envelope " : kind == motion::ModulatorKind::controller ? "MIDI CC " : "LFO ") + std::to_string(count + 1);
        motion::Id id = 0;
        const auto result = processor.document.addModulator(modulator, id);
        report(result);
        if (result.wasOk()) { selected = id; }
        refresh();
    }
    void change(const std::function<void(motion::Modulator&)>& update) {
        const auto* modulator = current();
        if (modulator == nullptr) { return; }
        auto edited = *modulator;
        update(edited);
        report(processor.document.setModulator(edited));
    }
    void bind(motion::ui::LabelledScrub& row, std::function<void(motion::Modulator&, double)> apply) {
        row.field.onChange = [this, apply](double value) { change([&](auto& modulator) { apply(modulator, value); }); };
        row.field.onCommit = row.field.onChange;
    }
    void report(const juce::Result& result) {
        if (result.failed() && onError) { onError(result.getErrorMessage()); }
    }
    void rebuildRoutes() {
        routes.clear();
        const auto& project = processor.document.project();
        for (const auto& route : project.routes) {
            if (route.modulator != selected) { continue; }
            auto row = std::make_unique<RouteRow>(*this, route, motion::ui::describeProperty(project, route.target, route.property));
            addAndMakeVisible(*row);
            routes.push_back(std::move(row));
        }
        routesTitle.setVisible(current() != nullptr);
        routesTitle.setText(routes.empty() ? "Drives nothing yet: route it from a property's Graph panel" : "Drives", juce::dontSendNotification);
    }
    void changeListenerCallback(juce::ChangeBroadcaster*) override {
        // A scrub in progress keeps its own value until release.
        for (auto* row : {&rate, &phase, &seed, &attack, &decay, &sustain, &release, &velocity, &lowest, &highest}) {
            if (row->field.isEditing()) { return; }
        }
        for (const auto& route : routes) { if (route->amount.isEditing()) { return; } }
        refresh();
    }

    MotionProcessor& processor;
    motion::Id selected = 0;
    juce::TextButton addOscillator, addEnvelope, addController, remove;
    juce::ComboBox list, waveform, clock, source, controller, channel;
    std::vector<motion::Id> sourceIds;
    juce::Label name, routesTitle, hint;
    motion::ui::LabelledScrub rate, phase, seed, attack, decay, sustain, release, velocity, lowest, highest;
    std::vector<std::unique_ptr<RouteRow>> routes;
};

// Graph column: modulators routed into the selected property, and its link.
class MotionRoutingPanel final : public juce::Component, private juce::ChangeListener {
public:
    explicit MotionRoutingPanel(MotionProcessor& owner) : processor(owner) {
        setName("Property routing");
        route.setButtonText("Route modulator...");
        route.setName("Route modulator");
        route.setTitle("Route modulator");
        route.setColour(juce::TextButton::buttonColourId, motion::style::raised());
        route.onClick = [this] { showRouteMenu(); };
        link.setButtonText("Link to...");
        link.setName("Link property");
        link.setTitle("Link property");
        link.setColour(juce::TextButton::buttonColourId, motion::style::raised());
        link.onClick = [this] { showLinkMenu(); };
        unlink.setButtonText("Unlink");
        unlink.setName("Unlink property");
        unlink.setTitle("Unlink property");
        unlink.onClick = [this] { report(processor.document.setLink(targetId, propertyName, std::nullopt)); };
        linkSource.setFont(motion::style::small());
        linkSource.setName("Link source");
        scale.setup(*this, "Scale", "Link scale", motion::ui::amountSpec);
        offset.setup(*this, "Offset", "Link offset", motion::ui::amountSpec);
        delay.setup(*this, "Delay", "Link delay", motion::ui::delaySpec);
        const auto bindLink = [this](motion::ui::LabelledScrub& row, std::function<void(motion::PropertyLink&, double)> apply) {
            row.field.onChange = [this, apply](double value) {
                const auto* curve = motion::findPropertyCurve(processor.document.project(), targetId, propertyName);
                if (curve == nullptr || !curve->link.has_value()) { return; }
                auto edited = *curve->link;
                apply(edited, value);
                report(processor.document.setLink(targetId, propertyName, edited));
            };
            row.field.onCommit = row.field.onChange;
        };
        bindLink(scale, [](auto& value, double number) { value.scale = number; });
        bindLink(offset, [](auto& value, double number) { value.offset = number; });
        bindLink(delay, [](auto& value, double number) { value.delay = number; });
        for (auto* component : std::initializer_list<juce::Component*> {&route, &link, &unlink, &linkSource}) { addAndMakeVisible(component); }
        processor.document.addChangeListener(this);
        refresh();
    }
    ~MotionRoutingPanel() override { processor.document.removeChangeListener(this); }

    std::function<void(const juce::String&)> onError;
    // Opens the library on a modulator (after creating one from the menu).
    std::function<void(motion::Id)> onShowModulator;

    void setTarget(motion::Id id, std::string property) {
        targetId = id;
        propertyName = std::move(property);
        refresh();
    }
    void refresh() {
        const auto& project = processor.document.project();
        const auto* curve = motion::findPropertyCurve(project, targetId, propertyName);
        rows.clear();
        for (const auto& item : project.routes) {
            if (item.target != targetId || item.property != propertyName) { continue; }
            const auto modulator = std::find_if(project.modulators.begin(), project.modulators.end(), [&](const auto& value) { return value.id == item.modulator; });
            if (modulator == project.modulators.end()) { continue; }
            auto row = std::make_unique<Row>(*this, item, juce::String(modulator->name));
            addAndMakeVisible(*row);
            rows.push_back(std::move(row));
        }
        const auto drivable = motion::drivableProperty(project, targetId, propertyName);
        route.setEnabled(drivable);
        link.setEnabled(drivable);
        const auto linked = curve != nullptr && curve->link.has_value();
        unlink.setVisible(linked);
        linkSource.setVisible(linked);
        scale.setVisible(linked);
        offset.setVisible(linked);
        delay.setVisible(linked);
        if (linked) {
            linkSource.setText(juce::String(juce::CharPointer_UTF8("\xe2\x86\x90 ")) + motion::ui::describeProperty(project, curve->link->source, curve->link->property), juce::dontSendNotification);
            scale.field.setValue(curve->link->scale);
            offset.field.setValue(curve->link->offset);
            delay.field.setValue(curve->link->delay);
        }
        resized();
        repaint();
    }
    int preferredHeight() const {
        auto height = 30 + motion::style::controlHeight + motion::style::gap + static_cast<int>(rows.size()) * (motion::style::controlHeight + motion::style::gap);
        height += motion::style::padding + motion::style::controlHeight + motion::style::gap;
        if (linkSource.isVisible()) { height += 20 + 3 * (motion::style::controlHeight + motion::style::gap); }
        return height + motion::style::padding;
    }
    void resized() override {
        auto area = getLocalBounds().reduced(7, 0);
        area.removeFromTop(30);
        for (auto& row : rows) {
            row->setBounds(area.removeFromTop(motion::style::controlHeight));
            area.removeFromTop(motion::style::gap);
        }
        route.setBounds(area.removeFromTop(motion::style::controlHeight));
        area.removeFromTop(motion::style::padding);
        auto header = area.removeFromTop(motion::style::controlHeight);
        area.removeFromTop(motion::style::gap);
        if (unlink.isVisible()) { unlink.setBounds(header.removeFromRight(64)); header.removeFromRight(motion::style::gap); }
        link.setBounds(header);
        if (linkSource.isVisible()) {
            linkSource.setBounds(area.removeFromTop(20));
            scale.layout(area);
            offset.layout(area);
            delay.layout(area);
        }
    }
    void paint(juce::Graphics& g) override {
        g.setColour(osci::Colours::surface());
        g.fillRoundedRectangle(getLocalBounds().toFloat(), 5);
        osci::PanelHeader::paintBackground(g, getLocalBounds().removeFromTop(30).toFloat(), osci::Colours::veryDark());
        g.setColour(osci::Colours::text());
        g.setFont(14);
        g.drawText("Routing", 10, 0, getWidth() - 20, 30, juce::Justification::centredLeft);
    }

private:
    struct Row final : juce::Component {
        Row(MotionRoutingPanel& owner, const motion::ModulationRoute& route, const juce::String& text) : owner(owner), route(route) {
            name.setText(text, juce::dontSendNotification);
            name.setFont(motion::style::small());
            amount.setName("Routed amount " + text);
            amount.setTitle("Routed amount " + text);
            amount.setSpec(motion::ui::amountSpec);
            amount.setValue(route.amount);
            amount.onChange = [this](double value) { edit(value); };
            amount.onCommit = [this](double value) { edit(value); };
            remove.setButtonText("x");
            remove.setName("Unroute " + text);
            remove.setTitle("Unroute " + text);
            remove.onClick = [this] { this->owner.report(this->owner.processor.document.removeRoute(this->route.id)); };
            for (auto* component : std::initializer_list<juce::Component*> {&name, &amount, &remove}) { addAndMakeVisible(component); }
        }
        void edit(double value) {
            auto edited = route;
            edited.amount = value;
            owner.report(owner.processor.document.setRoute(edited));
        }
        void resized() override {
            auto area = getLocalBounds();
            remove.setBounds(area.removeFromRight(24));
            name.setBounds(area.removeFromLeft(area.getWidth() / 2));
            amount.setBounds(area.reduced(2, 1));
        }
        MotionRoutingPanel& owner;
        motion::ModulationRoute route;
        juce::Label name;
        MotionScrubField amount;
        juce::TextButton remove;
    };

    void showRouteMenu() {
        const auto& modulators = processor.document.project().modulators;
        juce::PopupMenu menu;
        menu.setLookAndFeel(&getLookAndFeel());
        menu.addItem(1, "New LFO");
        menu.addItem(2, "New envelope");
        if (!modulators.empty()) { menu.addSeparator(); }
        for (std::size_t index = 0; index < modulators.size(); ++index) { menu.addItem(100 + static_cast<int>(index), juce::String(modulators[index].name)); }
        juce::Component::SafePointer<MotionRoutingPanel> safe(this);
        const auto id = targetId;
        const auto property = propertyName;
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&route), [safe, id, property](int result) {
            if (safe == nullptr || result == 0) { return; }
            auto& document = safe->processor.document;
            // Rotations get a visible default depth; everything else one unit.
            const auto amount = property.starts_with("rotation.") ? 45.0 : (property.starts_with("red") || property.starts_with("green") || property.starts_with("blue") ? 0.5 : 1.0);
            const motion::ModulationRoute route {0, 0, id, property, amount, motion::ModulationMode::add};
            if (result == 1 || result == 2) {
                motion::Modulator created;
                created.kind = result == 2 ? motion::ModulatorKind::envelope : motion::ModulatorKind::oscillator;
                created.name = result == 2 ? "Envelope" : "LFO";
                motion::Id modulator = 0;
                const auto added = document.addRoutedModulator(created, route, modulator);
                safe->report(added);
                if (added.wasOk() && safe->onShowModulator) { safe->onShowModulator(modulator); }
                return;
            }
            const auto index = static_cast<std::size_t>(result - 100);
            if (index >= document.project().modulators.size()) { return; }
            auto existing = route;
            existing.modulator = document.project().modulators[index].id;
            motion::Id routeId = 0;
            safe->report(document.addRoute(existing, routeId));
        });
    }
    void showLinkMenu() {
        const auto& project = processor.document.project();
        juce::PopupMenu menu;
        menu.setLookAndFeel(&getLookAndFeel());
        choices.clear();
        const auto addOwner = [&](motion::Id owner, const juce::String& title, const motion::PropertyMap& properties) {
            juce::PopupMenu sub;
            for (const auto& [name, curve] : properties) {
                if (owner == targetId && name == propertyName) { continue; }
                choices.push_back({owner, name});
                sub.addItem(static_cast<int>(choices.size()), motion::ui::propertyLabel(project, owner, name));
            }
            if (sub.getNumItems() > 0) { menu.addSubMenu(title, sub); }
        };
        for (const auto& track : project.tracks) {
            for (const auto& clip : track.clips) { addOwner(clip.id, juce::String(clip.name), clip.properties); }
        }
        for (const auto& group : project.groups) { addOwner(group.id, "Group: " + juce::String(group.name), group.properties); }
        for (const auto& camera : project.cameras) { addOwner(camera.id, "Camera: " + juce::String(camera.name), camera.properties); }
        juce::Component::SafePointer<MotionRoutingPanel> safe(this);
        const auto id = targetId;
        const auto property = propertyName;
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&link), [safe, id, property](int result) {
            if (safe == nullptr || result <= 0 || result > static_cast<int>(safe->choices.size())) { return; }
            const auto& choice = safe->choices[static_cast<std::size_t>(result - 1)];
            safe->report(safe->processor.document.setLink(id, property, motion::PropertyLink {choice.first, choice.second, 1, 0, 0}));
        });
    }
    void report(const juce::Result& result) {
        if (result.failed() && onError) { onError(result.getErrorMessage()); }
    }
    void changeListenerCallback(juce::ChangeBroadcaster*) override {
        for (auto* row : {&scale, &offset, &delay}) { if (row->field.isEditing()) { return; } }
        for (const auto& row : rows) { if (row->amount.isEditing()) { return; } }
        refresh();
        if (onLayoutChanged) { onLayoutChanged(); }
    }

public:
    std::function<void()> onLayoutChanged;

private:
    MotionProcessor& processor;
    motion::Id targetId = 0;
    std::string propertyName;
    juce::TextButton route, link, unlink;
    juce::Label linkSource;
    motion::ui::LabelledScrub scale, offset, delay;
    std::vector<std::unique_ptr<Row>> rows;
    std::vector<std::pair<motion::Id, std::string>> choices;
};
