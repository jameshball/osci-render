#include "ModulatorPanels.h"

namespace motion::ui {
void LabelledScrub::setup(juce::Component& owner, const juce::String& text, const juce::String& name, const PropertySpec& spec) {
    label.setText(text, juce::dontSendNotification);
    label.setFont(style::caption());
    label.setColour(juce::Label::textColourId, osci::Colours::textMuted());
    field.setName(name);
    field.setTitle(name);
    field.setSpec(spec);
    owner.addAndMakeVisible(label);
    owner.addAndMakeVisible(field);
}

void LabelledScrub::layout(juce::Rectangle<int>& area) {
    auto row = area.removeFromTop(style::controlHeight);
    area.removeFromTop(style::gap);
    label.setBounds(row.removeFromLeft(62));
    field.setBounds(row);
}

RouteRow::RouteRow(Document& document, std::function<void(const juce::Result&)> report) : document(document), report(std::move(report)) {
    label.setFont(style::caption());
    label.setColour(juce::Label::textColourId, osci::Colours::text());
    amount.setSpec(amountSpec);
    amount.onChange = [this](double value) { apply(value, route.mode); };
    amount.onCommit = [this](double value) { apply(value, route.mode); };
    mode.setTooltip("Add to the value, or multiply it by 1 + amount x modulator");
    mode.onClick = [this] { apply(amount.getValue(), route.mode == ModulationMode::add ? ModulationMode::multiply : ModulationMode::add); };
    remove.setTooltip("Stop driving this property");
    remove.setIconPadding(5);
    remove.onClick = [this] { this->report(this->document.removeRoute(route.id)); };
    for (auto* component : std::initializer_list<juce::Component*> {&label, &amount, &mode, &remove}) { addAndMakeVisible(component); }
}

void RouteRow::show(const ModulationRoute& value, const juce::String& text) {
    route = value;
    if (label.getText() != text) {
        label.setText(text, juce::dontSendNotification);
        for (auto [component, name] : {std::pair<juce::Component*, const char*> {&amount, "Route amount "}, {&mode, "Route mode "}, {&remove, "Remove route "}}) {
            component->setName(name + text);
            component->setTitle(name + text);
        }
    }
    amount.setValue(route.amount);
    mode.setButtonText(route.mode == ModulationMode::multiply ? juce::String::fromUTF8("\xc3\x97") : juce::String("+"));
}

void RouteRow::resized() {
    auto area = getLocalBounds();
    label.setBounds(area.removeFromTop(area.getHeight() / 2));
    remove.setBounds(area.removeFromRight(24));
    mode.setBounds(area.removeFromRight(24).reduced(1));
    amount.setBounds(area.reduced(0, 1));
}

void RouteRow::showAll(std::vector<std::unique_ptr<RouteRow>>& rows, juce::Component& parent, const std::vector<std::pair<ModulationRoute, juce::String>>& routes, Document& document, const std::function<void(const juce::Result&)>& report) {
    rows.resize(std::min(rows.size(), routes.size()));
    while (rows.size() < routes.size()) {
        rows.push_back(std::make_unique<RouteRow>(document, report));
        parent.addAndMakeVisible(*rows.back());
    }
    for (std::size_t index = 0; index < routes.size(); ++index) { rows[index]->show(routes[index].first, routes[index].second); }
}

void RouteRow::apply(double value, ModulationMode next) {
    auto edited = route;
    edited.amount = value;
    edited.mode = next;
    report(document.setRoute(edited));
}
}

MotionModulatorLibrary::MotionModulatorLibrary(MotionProcessor& owner) : processor(owner) {
    setName("Modulator library");
    viewport.setViewedComponent(&content, false);
    viewport.setScrollBarsShown(true, false);
    viewport.setScrollBarThickness(6);
    addAndMakeVisible(viewport);
    addButton.setButtonText("Add modulator");
    addButton.setName("Add modulator");
    addButton.setTitle("Add modulator");
    addButton.onClick = [this] { add(); };
    content.addAndMakeVisible(addButton);
    name.setName("Modulator name");
    name.setTitle("Modulator name");
    name.setFont(motion::style::title());
    name.setEditable(false, true);
    name.setTooltip("Double-click to rename");
    name.onTextChange = [this] {
        const auto text = name.getText().trim();
        if (text.isEmpty() || text.length() > 120) { refresh(); return; }
        change([text](auto& modulator) { modulator.name = text.toStdString(); });
    };
    content.addAndMakeVisible(name);
    remove.setName("Delete modulator");
    remove.setTooltip("Delete this modulator and its routes");
    remove.iconSize = 16.0f;
    remove.onClick = [this] {
        if (selected == 0) { return; }
        report(processor.document.removeModulator(selected));
        selected = 0;
        refresh();
    };
    content.addAndMakeVisible(remove);
    // What kind of modulator, as pictures: wave shapes, random, the
    // soundtrack's loudness, envelopes on MIDI notes and MIDI controllers.
    for (std::size_t index = 0; index < kinds().size(); ++index) {
        const auto& kind = kinds()[index];
        auto& button = shapes[index];
        button = std::make_unique<ShapeButton>(kind.name, kind.kind, kind.waveform);
        button->onClick = [this, index] { setKind(index); };
        content.addAndMakeVisible(*button);
    }
    more.onClick = [this] {
        showMore = more.getToggleState();
        refresh();
    };
    content.addAndMakeVisible(more);
    for (auto [chip, beats] : {std::pair {&hertz, false}, std::pair {&beatsChip, true}}) {
        chip->setClickingTogglesState(false);
        chip->onClick = [this, beats = beats] { change([beats](auto& modulator) { modulator.shape.tempoSync = beats; }); };
        content.addAndMakeVisible(chip);
    }
    hertz.setTooltip("Cycles per second");
    beatsChip.setTooltip("Beats per cycle, following the tempo");
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
    for (auto* box : {&source, &controller, &channel}) { content.addAndMakeVisible(box); }
    rate.setup(content, "Rate", "Modulator rate", motion::ui::rateHzSpec);
    phase.setup(content, "Phase", "Modulator phase", motion::ui::phaseSpec);
    seed.setup(content, "Seed", "Modulator seed", motion::ui::seedSpec);
    attack.setup(content, "Attack", "Envelope attack", motion::ui::secondsSpec);
    decay.setup(content, "Decay", "Envelope decay", motion::ui::secondsSpec);
    sustain.setup(content, "Sustain", "Envelope sustain", motion::ui::unitSpec);
    release.setup(content, "Release", "Envelope release", motion::ui::secondsSpec);
    velocity.setup(content, "Velocity", "Envelope velocity", motion::ui::unitSpec);
    lowest.setup(content, "Low note", "Envelope lowest pitch", motion::ui::pitchSpec);
    highest.setup(content, "High note", "Envelope highest pitch", motion::ui::pitchSpec);
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
    routesTitle.setFont(motion::style::caption());
    routesTitle.setColour(juce::Label::textColourId, osci::Colours::textMuted());
    routesTitle.setText("Drives", juce::dontSendNotification);
    content.addAndMakeVisible(routesTitle);
    routesHint.setText("Drag the card onto a property.", juce::dontSendNotification);
    routesHint.setFont(motion::style::caption());
    routesHint.setColour(juce::Label::textColourId, osci::Colours::textMuted());
    routesHint.setJustificationType(juce::Justification::topLeft);
    content.addAndMakeVisible(routesHint);
    processor.document.addChangeListener(this);
    refresh();
}

void MotionModulatorLibrary::refresh() {
    const auto& project = processor.document.project();
    const auto& modulators = project.modulators;
    if (selected != 0 && std::none_of(modulators.begin(), modulators.end(), [this](const auto& item) { return item.id == selected; })) { selected = 0; }
    if (selected == 0 && !modulators.empty()) { selected = modulators.front().id; }
    // Cards are rebuilt only when the list changes, so a card being
    // clicked or dragged survives selection.
    std::vector<std::pair<motion::Modulator, int>> listing;
    for (const auto& modulator : modulators) {
        int routeCount = 0;
        for (const auto& route : project.routes) { routeCount += route.modulator == modulator.id ? 1 : 0; }
        listing.emplace_back(modulator, routeCount);
    }
    if (listing != listedCards) {
        listedCards = listing;
        cards.clear();
        for (const auto& [modulator, routeCount] : listing) {
            auto card = std::make_unique<Card>(*this, modulator, routeCount);
            content.addAndMakeVisible(*card);
            cards.push_back(std::move(card));
        }
    }
    for (auto& card : cards) { card->repaint(); }
    const auto* modulator = current();
    const auto oscillator = modulator != nullptr && modulator->kind == motion::ModulatorKind::oscillator;
    const auto envelope = modulator != nullptr && modulator->kind == motion::ModulatorKind::envelope;
    const auto midiControl = modulator != nullptr && modulator->kind == motion::ModulatorKind::controller;
    const auto loudness = oscillator && modulator->shape.waveform == motion::ModulationWaveform::soundtrack;
    const auto noise = oscillator && (modulator->shape.waveform == motion::ModulationWaveform::noiseHold || modulator->shape.waveform == motion::ModulationWaveform::noiseSmooth);
    name.setVisible(modulator != nullptr);
    remove.setVisible(modulator != nullptr);
    for (auto& button : shapes) { button->setVisible(modulator != nullptr); }
    // The essentials show; phase, seed, velocity, note range and channel
    // wait behind More.
    const auto hasMore = (oscillator && !loudness) || envelope || midiControl;
    more.setVisible(hasMore);
    more.setToggleState(showMore, juce::dontSendNotification);
    hertz.setVisible(oscillator && !loudness);
    beatsChip.setVisible(oscillator && !loudness);
    rate.setVisible(oscillator && !loudness);
    phase.setVisible(oscillator && !loudness && showMore);
    seed.setVisible(noise && showMore);
    source.setVisible(envelope || midiControl);
    controller.setVisible(midiControl);
    channel.setVisible(midiControl && showMore);
    for (auto* row : {&attack, &decay, &sustain, &release}) { row->setVisible(envelope); }
    for (auto* row : {&velocity, &lowest, &highest}) { row->setVisible(envelope && showMore); }
    if (modulator != nullptr) {
        if (!name.isBeingEdited()) { name.setText(juce::String(modulator->name), juce::dontSendNotification); }
        for (std::size_t index = 0; index < shapes.size(); ++index) { shapes[index]->setToggleState(kindIndex(*modulator) == index, juce::dontSendNotification); }
        hertz.setToggleState(!modulator->shape.tempoSync, juce::dontSendNotification);
        beatsChip.setToggleState(modulator->shape.tempoSync, juce::dontSendNotification);
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
        source.setTextWhenNoChoicesAvailable("No MIDI clips yet");
    }
    showRoutes();
    resized();
    repaint();
}

void MotionModulatorLibrary::resized() {
    viewport.setBounds(getLocalBounds());
    const auto width = viewport.getMaximumVisibleWidth();
    // The add button sits exactly where Assets' Add source does.
    auto area = juce::Rectangle<int>(0, 0, width, 100000).reduced(motion::style::padding, 0).withTrimmedTop(6);
    addButton.setBounds(area.removeFromTop(30));
    area.removeFromTop(cards.empty() ? 0 : 6);
    for (auto& card : cards) {
        card->setBounds(area.removeFromTop(cardHeight));
        area.removeFromTop(motion::style::gap);
    }
    const auto* modulator = current();
    if (modulator != nullptr) {
        area.removeFromTop(motion::style::padding * 2);
        separatorY = area.getY() - motion::style::padding;
        auto header = area.removeFromTop(motion::style::controlHeight);
        remove.setBounds(header.removeFromRight(motion::style::controlHeight));
        name.setBounds(header);
        area.removeFromTop(motion::style::gap);
        {
            // Two rows of five kinds.
            const auto each = (area.getWidth() + 2) / 5;
            for (std::size_t index = 0; index < shapes.size(); ++index) {
                const auto column = static_cast<int>(index % 5), line = static_cast<int>(index / 5);
                shapes[index]->setBounds(area.getX() + column * each, area.getY() + line * 30, each - 2, 28);
            }
            area.removeFromTop(60);
            area.removeFromTop(motion::style::gap);
        }
        previewArea = area.removeFromTop(44);
        area.removeFromTop(motion::style::padding);
        for (auto* box : {&source, &controller, &channel}) {
            if (!box->isVisible()) { continue; }
            box->setBounds(area.removeFromTop(motion::style::controlHeight));
            area.removeFromTop(motion::style::gap);
        }
        if (rate.field.isVisible()) {
            // The clock's unit sits on the label line; the value gets the width.
            auto heading = area.removeFromTop(18);
            beatsChip.setBounds(heading.removeFromRight(46).reduced(0, 1));
            heading.removeFromRight(motion::style::gap);
            hertz.setBounds(heading.removeFromRight(32).reduced(0, 1));
            rate.label.setBounds(heading);
            area.removeFromTop(2);
            rate.field.setBounds(area.removeFromTop(motion::style::controlHeight));
            area.removeFromTop(motion::style::gap);
        }
        for (auto* row : {&attack, &decay, &sustain, &release}) {
            if (row->field.isVisible()) { row->layout(area); }
        }
        if (more.isVisible()) {
            more.setBounds(area.removeFromTop(20).removeFromLeft(56));
            area.removeFromTop(motion::style::gap);
        }
        for (auto* row : {&phase, &seed, &velocity, &lowest, &highest}) {
            if (row->field.isVisible()) { row->layout(area); }
        }
        area.removeFromTop(motion::style::padding);
        routesTitle.setBounds(area.removeFromTop(18));
        if (routesHint.isVisible()) { routesHint.setBounds(area.removeFromTop(36)); }
        for (auto& route : routes) {
            route->setBounds(area.removeFromTop(motion::ui::RouteRow::height));
            area.removeFromTop(motion::style::gap);
        }
    }
    content.setSize(width, area.getY() + motion::style::padding);
}

MotionModulatorLibrary::Card::Card(MotionModulatorLibrary& library, const motion::Modulator& value, int routeCount) : owner(library), modulator(value), routes(routeCount) {
    setName(juce::String(modulator.name));
    setTitle(juce::String(modulator.name) + " modulator");
}

void MotionModulatorLibrary::Card::paint(juce::Graphics& g) {
    const auto active = modulator.id == owner.selected;
    const auto hover = fade.value();
    const auto bounds = getLocalBounds().toFloat();
    g.setColour(active ? osci::Colours::surfaceRaised().interpolatedWith(osci::Colours::accentColor(), .1f) : osci::Colours::veryDark().interpolatedWith(osci::Colours::surfaceRaised(), hover));
    g.fillRoundedRectangle(bounds, motion::style::radius + 1);
    if (active) {
        g.setColour(osci::Colours::accentColor().withAlpha(.7f));
        g.drawRoundedRectangle(bounds.reduced(.5f), motion::style::radius + 1, 1.0f);
    }
    auto area = getLocalBounds().reduced(motion::style::gap + 2, motion::style::gap);
    const auto picture = area.removeFromLeft(44).toFloat();
    g.setColour(juce::Colours::black.withAlpha(.35f));
    g.fillRoundedRectangle(picture, motion::style::radius);
    motion::ui::paintModulatorShape(g, modulator, picture.reduced(5, 7), motion::style::key().withAlpha(.6f + .4f * (active ? 1.0f : hover)), 1.2f);
    area.removeFromLeft(motion::style::padding);
    g.setFont(motion::style::caption());
    g.setColour(routes == 0 ? osci::Colours::textMuted().withAlpha(.7f) : osci::Colours::accentColor().brighter(.3f));
    g.drawText(routes == 0 ? juce::String("Not routed") : routes == 1 ? juce::String("Drives 1") : "Drives " + juce::String(routes), area.removeFromBottom(area.getHeight() / 2), juce::Justification::topLeft, true);
    g.setFont(motion::style::body());
    g.setColour(osci::Colours::text());
    g.drawText(juce::String(modulator.name), area, juce::Justification::bottomLeft, true);
}

void MotionModulatorLibrary::Card::mouseDrag(const juce::MouseEvent& event) {
    if (event.getDistanceFromDragStart() < 4) { return; }
    auto* container = juce::DragAndDropContainer::findParentDragContainerFor(this);
    if (container != nullptr && !container->isDragAndDropActive()) { container->startDragging("motion-modulator:" + juce::String(static_cast<juce::int64>(modulator.id)), this); }
}

MotionModulatorLibrary::ShapeButton::ShapeButton(const juce::String& text, motion::ModulatorKind kind, motion::ModulationWaveform waveform) : juce::Button("Shape " + text) {
    setTitle(getName());
    setTooltip(text);
    setWantsKeyboardFocus(false);
    shape.kind = kind;
    shape.shape.waveform = waveform;
    shape.shape.seed = 3;
    shape.sustain = .5;
    if (text == "Pitch bend") { shape.controller = motion::MidiControl::pitchBend; }
}

void MotionModulatorLibrary::ShapeButton::paintButton(juce::Graphics& g, bool, bool down) {
    const auto bounds = getLocalBounds().toFloat();
    const auto on = getToggleState();
    g.setColour(on ? osci::Colours::accentColor().withAlpha(down ? .45f : .35f) : juce::Colours::white.withAlpha(down ? .12f : .04f + .05f * fade.value()));
    g.fillRoundedRectangle(bounds, motion::style::radius);
    motion::ui::paintModulatorShape(g, shape, bounds.reduced(5, 8), on ? juce::Colours::white : osci::Colours::text().withAlpha(.6f + .4f * fade.value()), 1.3f);
}

const motion::Modulator* MotionModulatorLibrary::current() const {
    const auto& modulators = processor.document.project().modulators;
    const auto found = std::find_if(modulators.begin(), modulators.end(), [this](const auto& item) { return item.id == selected; });
    return found == modulators.end() ? nullptr : &*found;
}

const std::array<MotionModulatorLibrary::Kind, 10>& MotionModulatorLibrary::kinds() {
    using motion::ModulatorKind, motion::ModulationWaveform;
    static const std::array<Kind, 10> list {{
        {"Sine", ModulatorKind::oscillator, ModulationWaveform::sine}, {"Triangle", ModulatorKind::oscillator, ModulationWaveform::triangle},
        {"Saw", ModulatorKind::oscillator, ModulationWaveform::saw}, {"Square", ModulatorKind::oscillator, ModulationWaveform::square},
        {"Smooth random", ModulatorKind::oscillator, ModulationWaveform::noiseSmooth}, {"Random steps", ModulatorKind::oscillator, ModulationWaveform::noiseHold},
        {"Soundtrack loudness", ModulatorKind::oscillator, ModulationWaveform::soundtrack}, {"Envelope on MIDI notes", ModulatorKind::envelope, ModulationWaveform::sine},
        {"MIDI controller", ModulatorKind::controller, ModulationWaveform::sine}, {"Pitch bend", ModulatorKind::controller, ModulationWaveform::sine}}};
    return list;
}

std::size_t MotionModulatorLibrary::kindIndex(const motion::Modulator& modulator) {
    if (modulator.kind == motion::ModulatorKind::envelope) { return 7; }
    if (modulator.kind == motion::ModulatorKind::controller) { return modulator.controller == motion::MidiControl::pitchBend ? 9 : 8; }
    return std::min<std::size_t>(static_cast<std::size_t>(modulator.shape.waveform), 6);
}

void MotionModulatorLibrary::setKind(std::size_t index) {
    const auto* modulator = current();
    if (modulator == nullptr) { return; }
    const auto& kind = kinds()[index];
    const auto oldBase = motion::ui::modulatorKindName(*modulator).toStdString();
    change([&](auto& edited) {
        edited.kind = kind.kind;
        if (kind.kind == motion::ModulatorKind::oscillator) { edited.shape.waveform = kind.waveform; }
        if (index == 9) { edited.controller = motion::MidiControl::pitchBend; }
        if (index == 8 && edited.controller == motion::MidiControl::pitchBend) { edited.controller = 1; }
        if (edited.name.rfind(oldBase + " ", 0) == 0) { edited.name = motion::ui::modulatorKindName(edited).toStdString() + edited.name.substr(oldBase.size()); }
    });
}

void MotionModulatorLibrary::add(motion::ModulatorKind kind, motion::ModulationWaveform waveform) {
    motion::Modulator modulator;
    modulator.kind = kind;
    modulator.shape.waveform = waveform;
    const auto base = motion::ui::modulatorKindName(modulator).toStdString();
    const auto& existing = processor.document.project().modulators;
    for (int number = 1;; ++number) {
        modulator.name = base + " " + std::to_string(number);
        if (std::none_of(existing.begin(), existing.end(), [&modulator](const auto& item) { return item.name == modulator.name; })) { break; }
    }
    motion::Id id = 0;
    const auto result = processor.document.addModulator(modulator, id);
    report(result);
    if (result.wasOk()) { selected = id; }
    refresh();
}

void MotionModulatorLibrary::change(const std::function<void(motion::Modulator&)>& update) {
    const auto* modulator = current();
    if (modulator == nullptr) { return; }
    auto edited = *modulator;
    update(edited);
    report(processor.document.setModulator(edited));
}

void MotionModulatorLibrary::bind(motion::ui::LabelledScrub& row, std::function<void(motion::Modulator&, double)> apply) {
    row.field.onChange = [this, apply](double value) { change([&](auto& modulator) { apply(modulator, value); }); };
    row.field.onCommit = row.field.onChange;
}

void MotionModulatorLibrary::showRoutes() {
    const auto& project = processor.document.project();
    std::vector<std::pair<motion::ModulationRoute, juce::String>> shown;
    for (const auto& route : project.routes) {
        if (route.modulator == selected) { shown.emplace_back(route, motion::describeProperty(project, route.target, route.property)); }
    }
    motion::ui::RouteRow::showAll(routes, content, shown, processor.document, [this](const juce::Result& result) { report(result); });
    routesTitle.setVisible(current() != nullptr);
    routesHint.setVisible(current() != nullptr && routes.empty());
}

void MotionModulatorLibrary::changeListenerCallback(juce::ChangeBroadcaster*) {
    // Hidden, it refreshes when its tab is shown.
    if (!isShowing()) { return; }
    // A scrub in progress keeps its own value until release.
    for (auto* row : {&rate, &phase, &seed, &attack, &decay, &sustain, &release, &velocity, &lowest, &highest}) {
        if (row->field.isEditing()) { return; }
    }
    for (const auto& route : routes) { if (route->isEditing()) { return; } }
    refresh();
}

void MotionModulatorLibrary::Content::paint(juce::Graphics& g) {
    const auto* modulator = owner.current();
    if (modulator == nullptr) { return; }
    g.setColour(juce::Colours::black.withAlpha(.45f));
    g.drawHorizontalLine(owner.separatorY, static_cast<float>(motion::style::padding), static_cast<float>(getWidth() - motion::style::padding));
    const auto area = owner.previewArea.toFloat();
    g.setColour(juce::Colours::black.withAlpha(.35f));
    g.fillRoundedRectangle(area, motion::style::radius);
    g.setColour(osci::Colours::outlineSubtle().withAlpha(.4f));
    g.drawHorizontalLine(juce::roundToInt(area.getCentreY()), area.getX() + 4, area.getRight() - 4);
    motion::ui::paintModulatorShape(g, *modulator, area.reduced(8, 6), motion::style::key(), 1.6f);
}

MotionRoutingPanel::MotionRoutingPanel(MotionProcessor& owner) : processor(owner) {
    setName("Property routing");
    link.setButtonText("Link to...");
    link.setName("Link property");
    link.setTitle("Link property");
    link.setColour(juce::TextButton::buttonColourId, osci::Colours::surfaceRaised());
    link.onClick = [this] { showLinkMenu(); };
    unlink.setButtonText("Unlink");
    unlink.setName("Unlink property");
    unlink.setTitle("Unlink property");
    unlink.onClick = [this] { report(processor.document.setLink(targetId, propertyName, std::nullopt)); };
    linkSource.setFont(motion::style::caption());
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
    for (auto* component : std::initializer_list<juce::Component*> {&link, &unlink, &linkSource}) { addAndMakeVisible(component); }
    processor.document.addChangeListener(this);
    refresh();
}

void MotionRoutingPanel::setTarget(motion::Id id, std::string property) {
    targetId = id;
    propertyName = std::move(property);
    refresh();
}

void MotionRoutingPanel::refresh() {
    const auto& project = processor.document.project();
    const auto* curve = motion::findPropertyCurve(project, targetId, propertyName);
    std::vector<std::pair<motion::ModulationRoute, juce::String>> shown;
    for (const auto& item : project.routes) {
        if (item.target != targetId || item.property != propertyName) { continue; }
        const auto modulator = std::find_if(project.modulators.begin(), project.modulators.end(), [&](const auto& value) { return value.id == item.modulator; });
        if (modulator != project.modulators.end()) { shown.emplace_back(item, juce::String(modulator->name)); }
    }
    motion::ui::RouteRow::showAll(rows, *this, shown, processor.document, [this](const juce::Result& result) { report(result); });
    const auto drivable = motion::drivableProperty(project, targetId, propertyName);
    link.setEnabled(drivable);
    const auto linked = curve != nullptr && curve->link.has_value();
    unlink.setVisible(linked);
    linkSource.setVisible(linked);
    scale.setVisible(linked);
    offset.setVisible(linked);
    delay.setVisible(linked);
    if (linked) {
        linkSource.setText(juce::String(juce::CharPointer_UTF8("\xe2\x86\x90 ")) + motion::describeProperty(project, curve->link->source, curve->link->property), juce::dontSendNotification);
        scale.field.setValue(curve->link->scale);
        offset.field.setValue(curve->link->offset);
        delay.field.setValue(curve->link->delay);
    }
    resized();
    repaint();
}

int MotionRoutingPanel::preferredHeight() const {
    auto height = 30 + static_cast<int>(rows.size()) * (motion::ui::RouteRow::height + motion::style::gap);
    height += motion::style::controlHeight + motion::style::gap;
    if (linkSource.isVisible()) { height += 20 + 3 * (motion::style::controlHeight + motion::style::gap); }
    return height + motion::style::padding;
}

void MotionRoutingPanel::resized() {
    auto area = getLocalBounds().reduced(7, 0);
    area.removeFromTop(30);
    for (auto& row : rows) {
        row->setBounds(area.removeFromTop(motion::ui::RouteRow::height));
        area.removeFromTop(motion::style::gap);
    }
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

void MotionRoutingPanel::paint(juce::Graphics& g) {
    g.setColour(osci::Colours::surface());
    g.fillRoundedRectangle(getLocalBounds().toFloat(), 5);
    osci::PanelHeader::paintBackground(g, getLocalBounds().removeFromTop(30).toFloat(), osci::Colours::veryDark());
    g.setColour(osci::Colours::text());
    g.setFont(motion::style::body());
    g.drawText("Routing", 10, 0, getWidth() - 20, 30, juce::Justification::centredLeft);
}

void MotionRoutingPanel::showLinkMenu() {
    const auto& project = processor.document.project();
    juce::PopupMenu menu;
    choices.clear();
    const auto addOwner = [&](motion::Id owner, const juce::String& title, const motion::PropertyMap& properties) {
        juce::PopupMenu sub;
        for (const auto& [name, curve] : properties) {
            if (owner == targetId && name == propertyName) { continue; }
            choices.push_back({owner, name});
            sub.addItem(static_cast<int>(choices.size()), motion::propertyLabel(project, owner, name));
        }
        if (sub.getNumItems() > 0) { menu.addSubMenu(title, sub); }
    };
    for (const auto& track : project.tracks) {
        for (const auto& clip : track.clips) { addOwner(clip.id, juce::String(clip.name), clip.properties); }
    }
    for (const auto& group : project.groups) { addOwner(group.id, "Group: " + juce::String(group.name), group.properties); }
    for (const auto& camera : project.cameras) { addOwner(camera.id, "Camera: " + juce::String(camera.name), camera.properties); }
    const auto id = targetId;
    const auto property = propertyName;
    motion::ui::showDocumentMenu(menu, *this, processor.document, juce::PopupMenu::Options().withTargetComponent(&link), [this, id, property](int result) {
        if (result <= 0 || result > static_cast<int>(choices.size())) { return; }
        const auto& choice = choices[static_cast<std::size_t>(result - 1)];
        report(processor.document.setLink(id, property, motion::PropertyLink {choice.first, choice.second, 1, 0, 0}));
    });
}

void MotionRoutingPanel::changeListenerCallback(juce::ChangeBroadcaster*) {
    for (auto* row : {&scale, &offset, &delay}) { if (row->field.isEditing()) { return; } }
    for (const auto& row : rows) { if (row->isEditing()) { return; } }
    refresh();
    if (onLayoutChanged) { onLayoutChanged(); }
}
