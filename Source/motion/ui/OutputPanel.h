#pragma once

#include "../BeamOutput.h"
#include "MotionStyle.h"
#include "Sheet.h"
#include <functional>

namespace motion::ui::output {
// Outputs as the interface numbers them, from 1: "3" or "3–4".
inline juce::String range(int first, int count) {
    return count <= 1 ? juce::String(first + 1) : juce::String(first + 1) + juce::String::fromUTF8("\xe2\x80\x93") + juce::String(first + count);
}
// The beam's forms as the panel and the button name them.
inline juce::String formName(OutputMode mode) {
    switch (mode) {
        case OutputMode::xy: return "XY";
        case OutputMode::xyz: return "XYZ";
        case OutputMode::xyzInverted: return "XYZ, inverted Z";
        case OutputMode::xyrgb: return "XYRGB";
        case OutputMode::soundtrack: break;
    }
    return "Off";
}
// Short, for the top bar.
inline juce::String shortFormName(OutputMode mode) { return mode == OutputMode::xyzInverted ? juce::String("XYZ inv.") : formName(mode); }
// What each of a form's outputs carries, one letter each.
inline juce::StringArray roles(OutputMode mode) {
    switch (mode) {
        case OutputMode::xy: return {"X", "Y"};
        case OutputMode::xyz:
        case OutputMode::xyzInverted: return {"X", "Y", "Z"};
        case OutputMode::xyrgb: return {"X", "Y", "R", "G", "B"};
        case OutputMode::soundtrack: break;
    }
    return {};
}
inline juce::Colour soundColour() { return style::waveform(); }
inline juce::Colour beamColour() { return osci::Colours::accentColor(); }
// A beam output's letter in what it carries: red, green and blue in their
// own colours, so the colour's R never reads as the sound's right.
inline juce::Colour roleColour(const juce::String& role, bool beam) {
    if (!beam) { return osci::Colours::text(); }
    if (role == "R") { return juce::Colour(0xffff7a7a); }
    if (role == "G") { return juce::Colour(0xff7dea8c); }
    if (role == "B") { return juce::Colour(0xff7ab4ff); }
    return osci::Colours::text();
}
// The routing in a few words: "Sound 1–2 · Beam XY 3–4".
inline juce::String summary(const OutputRouting& routing) {
    juce::StringArray parts;
    if (routing.soundOn()) { parts.add("Sound " + range(routing.sound, 2)); }
    if (routing.beamOn()) { parts.add("Beam " + formName(routing.beam) + " " + range(routing.beamFirst, routing.beamChannels())); }
    return parts.isEmpty() ? juce::String("Nothing") : parts.joinIntoString(style::dot());
}
}

// The top bar's Output: what plays where on the interface, as a field that
// opens the Output panel.
class MotionOutputButton final : public juce::Button {
public:
    MotionOutputButton() : juce::Button("Output") {
        setTitle("Output");
        setTooltip("Where the project plays on your audio interface: its sound, and the beam for a real oscilloscope");
    }
    // With no device open nothing plays, whatever the routing.
    void setRouting(const motion::OutputRouting& value, bool deviceOpen = true) {
        if (value == routing && deviceOpen == open) { return; }
        routing = value;
        open = deviceOpen;
        setDescription(open ? motion::ui::output::summary(routing) : juce::String("No audio device"));
        repaint();
    }
    void paintButton(juce::Graphics& g, bool highlighted, bool down) override {
        using namespace motion::ui::output;
        const auto bounds = getLocalBounds().toFloat();
        g.setColour(osci::Colours::surfaceRaised().brighter(down ? .08f : 0.0f));
        g.fillRoundedRectangle(bounds, motion::style::radius);
        if (highlighted || hasKeyboardFocus(true)) {
            g.setColour(hasKeyboardFocus(true) ? osci::Colours::accentColor().withAlpha(.8f) : osci::Colours::outlineSubtle().withAlpha(.8f));
            g.drawRoundedRectangle(bounds.reduced(.5f), motion::style::radius, 1.0f);
        }
        // Each part after its own colour's dot, as the panel's outputs show
        // them; short of room, the sound's name goes (its dot says it).
        auto area = getLocalBounds().reduced(8, 0).withTrimmedRight(16);
        g.setFont(motion::style::body());
        const auto width = [&](const juce::String& text) { return static_cast<int>(std::ceil(juce::TextLayout::getStringWidth(g.getCurrentFont(), text))) + 2; };
        const auto beamText = shortFormName(routing.beam) + " " + range(routing.beamFirst, routing.beamChannels());
        auto soundText = "Sound " + range(routing.sound, 2);
        const auto needed = (routing.soundOn() ? 12 + width(soundText) : 0) + (routing.beamOn() ? 12 + width(beamText) : 0) + (routing.soundOn() && routing.beamOn() ? 10 : 0);
        if (needed > area.getWidth()) { soundText = range(routing.sound, 2); }
        const auto part = [&](juce::Colour colour, const juce::String& text) {
            auto slot = area.removeFromLeft(std::min(area.getWidth(), 12 + width(text)));
            g.setColour(colour);
            g.fillEllipse(slot.removeFromLeft(12).toFloat().withTrimmedRight(2).withSizeKeepingCentre(6, 6));
            g.setColour(osci::Colours::text().withAlpha(isEnabled() ? .9f : .4f));
            g.drawText(text, slot, juce::Justification::centredLeft, true);
            area.removeFromLeft(10);
        };
        if (open && routing.soundOn()) { part(soundColour(), soundText); }
        if (open && routing.beamOn()) { part(beamColour(), beamText); }
        if (!open || (!routing.soundOn() && !routing.beamOn())) {
            g.setColour(osci::Colours::textMuted());
            g.drawText(open ? "Nothing" : "No audio device", area, juce::Justification::centredLeft, true);
        }
        const auto centre = juce::Point<float>(static_cast<float>(getWidth()) - 12.0f, static_cast<float>(getHeight()) * .5f);
        juce::Path chevron;
        chevron.startNewSubPath(centre.x - 3.5f, centre.y - 1.75f);
        chevron.lineTo(centre.x, centre.y + 1.75f);
        chevron.lineTo(centre.x + 3.5f, centre.y - 1.75f);
        g.setColour(osci::Colours::text().withAlpha(highlighted ? .85f : .55f));
        g.strokePath(chevron, juce::PathStrokeType(1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

private:
    motion::OutputRouting routing {motion::OutputRouting::off, motion::OutputMode::soundtrack, 0};
    bool open = false;
};

// Where the project plays: its sound on a pair of the interface's outputs,
// the beam (for a real oscilloscope) on as many as its form needs, both,
// either or neither. The outputs are drawn below, each with what it
// carries, so the choice reads at a glance. Changes apply as they are made.
class MotionOutputPanel final : public motion::ui::Popover {
public:
    struct Device {
        juce::String name;
        int outputs = 0;
    };

    MotionOutputPanel(motion::OutputRouting current, Device device, bool hasSound) : Popover("Output", "Done", "Close output"), routing(current), device(std::move(device)), hasSound(hasSound) {
        detail = this->device.name;
        primary.onClick = [this] { if (onDone) { onDone(); } };
        for (auto* label : {&soundCaption, &beamCaption, &onCaption}) {
            label->setFont(motion::style::body());
            label->setColour(juce::Label::textColourId, osci::Colours::textMuted());
            label->setBorderSize({});
            addAndMakeVisible(*label);
        }
        sound.setName("Sound outputs");
        sound.setTitle("Sound outputs");
        sound.setTooltip("The pair of outputs the project's sound plays on");
        form.setName("Beam form");
        form.setTitle("Beam form");
        form.setTooltip("XY draws the beam's position; XYZ adds its brightness for a scope's Z input (inverted for one that blanks on a positive voltage); XYRGB adds its colour for a laser or an RGB scope");
        place.setName("Beam outputs");
        place.setTitle("Beam outputs");
        place.setTooltip("The outputs the beam plays on, X first");
        settings.setButtonText("Audio settings...");
        settings.setName("Audio settings");
        settings.setTooltip("Choose the audio interface, and its outputs");
        motion::style::makeQuiet(settings);
        settings.onClick = [this] { if (onAudioSettings) { onAudioSettings(); } };
        sound.onChange = [this] { chooseSound(); };
        form.onChange = [this] { chooseForm(); };
        place.onChange = [this] { choosePlace(); };
        note.setFont(motion::style::caption());
        note.setColour(juce::Label::textColourId, osci::Colours::textMuted());
        note.setJustificationType(juce::Justification::topLeft);
        note.setBorderSize({});
        for (auto* component : std::initializer_list<juce::Component*> {&sound, &form, &place, &settings, &note}) { addAndMakeVisible(component); }
        rebuild();
    }

    // The device changed while open (one plugged in or out): the panel shows
    // it, and the routing the editor kept playable on it.
    // Opening more of the same device's outputs changes nothing here, so a
    // note saying what moved stays.
    void setDevice(Device next, motion::OutputRouting current) {
        if (next.name == device.name && next.outputs == device.outputs && current == routing) { return; }
        device = std::move(next);
        routing = current;
        detail = device.name;
        rebuild();
    }

    // Asked to apply a routing: an error says why it couldn't (the panel then
    // shows the routing as it was).
    std::function<juce::Result(motion::OutputRouting)> onChange;
    std::function<void()> onAudioSettings, onDone;

    void paint(juce::Graphics& g) override {
        Popover::paint(g);
        // Each row's colour, as its outputs below and the top bar show it.
        using namespace motion::ui::output;
        // Dim while that part is off, as nothing of it is playing.
        const auto dot = [&](juce::Label& caption, juce::Colour colour, bool on) {
            g.setColour(colour.withAlpha(on ? 1.0f : .3f));
            g.fillEllipse(juce::Rectangle<float>(6, 6).withCentre({static_cast<float>(caption.getX() - dotSpace) + 3.0f, static_cast<float>(caption.getBounds().getCentreY())}));
        };
        dot(soundCaption, soundColour(), routing.soundOn() && device.outputs > 0);
        dot(beamCaption, beamColour(), routing.beamOn() && device.outputs > 0);
        paintStrip(g);
    }

protected:
    void layoutBody(juce::Rectangle<int> area) override {
        // Captions clear of the colour dots painted at their left.
        auto line = area.removeFromTop(row);
        soundCaption.setBounds(line.removeFromLeft(caption).withTrimmedLeft(dotSpace));
        sound.setBounds(line);
        area.removeFromTop(rowGap);
        line = area.removeFromTop(row);
        beamCaption.setBounds(line.removeFromLeft(caption).withTrimmedLeft(dotSpace));
        const auto half = (line.getWidth() - 26) / 2;
        form.setBounds(line.removeFromLeft(half));
        onCaption.setBounds(line.removeFromLeft(26).withTrimmedLeft(7));
        place.setBounds(line);
        area.removeFromTop(10);
        // Without a device there are no outputs to draw.
        strip = device.outputs > 0 ? area.removeFromTop(stripHeight) : juce::Rectangle<int>();
        area.removeFromTop(device.outputs > 0 ? 4 : 2);
        note.setBounds(area.removeFromTop(noteHeight));
    }
    void resized() override {
        Popover::resized();
        // Audio settings sits in the footer, left of Done.
        // Its words line up with the heading above; its shape shows on hover.
        auto footer = getLocalBounds().reduced(inset).removeFromBottom(footerHeight);
        const auto words = juce::roundToInt(juce::TextLayout::getStringWidth(motion::style::body(), settings.getButtonText()));
        settings.setBounds(footer.withWidth(words + 20).translated(-10, 0));
    }

private:
    static constexpr int stripHeight = 46, noteHeight = 30, cellLimit = 16, dotSpace = 12;

    void chooseSound() {
        auto next = routing;
        next.sound = sound.getSelectedId() <= 1 ? motion::OutputRouting::off : (sound.getSelectedId() - 2) * 2;
        apply(next, true);
    }
    void chooseForm() {
        auto next = routing;
        next.beam = static_cast<motion::OutputMode>(form.getSelectedId() - 1);
        // Kept where it was if it still fits there, else on the first outputs
        // free of the sound, else from the first (and the sound makes room).
        if (next.beamOn() && !next.fits(device.outputs)) {
            const auto start = next.beamFirst;
            next.beamFirst = 0;
            for (int first = 0; first + next.beamChannels() <= device.outputs; ++first) {
                auto probe = next;
                probe.beamFirst = first;
                if (probe.fits(device.outputs)) {
                    next.beamFirst = first;
                    break;
                }
            }
            if (start + next.beamChannels() <= device.outputs && !next.fits(device.outputs)) { next.beamFirst = start; }
        }
        apply(next, false);
    }
    void choosePlace() {
        auto next = routing;
        next.beamFirst = place.getSelectedId() - 1;
        apply(next, false);
    }
    // The latest choice wins: the other moves to outputs still free, or
    // stops when there are none, and the note says which.
    motion::OutputRouting makeRoom(motion::OutputRouting next, bool soundChosen, juce::String& said) const {
        using namespace motion::ui::output;
        if (next.fits(device.outputs)) { return next; }
        if (soundChosen && next.soundOn()) {
            for (int first = 0; first + next.beamChannels() <= device.outputs; ++first) {
                auto probe = next;
                probe.beamFirst = first;
                if (probe.fits(device.outputs)) {
                    said = "The beam moved to outputs " + range(first, next.beamChannels()) + " to make room.";
                    return probe;
                }
            }
            next.beam = motion::OutputMode::soundtrack;
            said = "The beam is off: " + device.name + " has no outputs free beside the sound.";
            return next;
        }
        if (!soundChosen && next.beamOn()) {
            for (int first = 0; first + 2 <= device.outputs; first += 2) {
                auto probe = next;
                probe.sound = first;
                if (probe.fits(device.outputs)) {
                    said = "The sound moved to outputs " + range(first, 2) + " to make room.";
                    return probe;
                }
            }
            next.sound = motion::OutputRouting::off;
            said = "The sound is off: " + device.name + " has no outputs free beside the beam.";
        }
        return next;
    }
    // What choosing an output the other part is on does to it, said in the
    // list beside that output: "(beam moves to 3–4)", "(turns the sound off)".
    juce::String aside(motion::OutputRouting next, bool soundChosen) const {
        using namespace motion::ui::output;
        juce::String said;
        const auto made = makeRoom(next, soundChosen, said);
        if (soundChosen) {
            return made.beamOn() ? "  (beam moves to " + range(made.beamFirst, made.beamChannels()) + ")" : juce::String("  (turns the beam off)");
        }
        return made.soundOn() ? "  (sound moves to " + range(made.sound, 2) + ")" : juce::String("  (turns the sound off)");
    }
    void apply(motion::OutputRouting next, bool soundChosen) {
        juce::String said;
        next = makeRoom(next, soundChosen, said);
        if (next == routing || !next.fits(device.outputs)) {
            rebuild();
            return;
        }
        const auto result = onChange ? onChange(next) : juce::Result::ok();
        if (result.wasOk()) { routing = next; }
        rebuild(result.getErrorMessage(), result.wasOk() ? said : juce::String());
    }

    // The choices as the routing and the device allow: one on the other
    // part's outputs says what it does to that part, and a form the device
    // has too few outputs for says how many it needs.
    void rebuild(const juce::String& error = {}, const juce::String& moved = {}) {
        using namespace motion::ui::output;
        const auto outputs = device.outputs;
        sound.clear(juce::dontSendNotification);
        const auto quiet = hasSound ? juce::String() : juce::String("  (no audio yet)");
        sound.addItem("Off", 1);
        for (int first = 0; first + 2 <= outputs; first += 2) {
            auto probe = routing;
            probe.sound = first;
            const auto clash = probe.overlaps();
            sound.addItem("Outputs " + range(first, 2) + (clash ? aside(probe, true) : first == routing.sound ? quiet : juce::String()), first / 2 + 2);
        }
        if (outputs <= 0) { sound.addItem(juce::String::fromUTF8("\xe2\x80\x94"), 100); }
        sound.setSelectedId(outputs <= 0 ? 100 : routing.soundOn() ? routing.sound / 2 + 2 : 1, juce::dontSendNotification);
        sound.setEnabled(outputs > 0);
        form.setEnabled(outputs > 0);
        form.clear(juce::dontSendNotification);
        form.addItem("Off", static_cast<int>(motion::OutputMode::soundtrack) + 1);
        for (const auto mode : {motion::OutputMode::xy, motion::OutputMode::xyz, motion::OutputMode::xyzInverted, motion::OutputMode::xyrgb}) {
            const auto width = motion::beamOutputChannels(mode);
            const auto room = width <= outputs;
            form.addItem(formName(mode) + (room ? juce::String() : "  (needs " + juce::String(width) + " outputs)"), static_cast<int>(mode) + 1);
            form.setItemEnabled(static_cast<int>(mode) + 1, room);
        }
        form.setSelectedId(static_cast<int>(routing.beamOn() ? routing.beam : motion::OutputMode::soundtrack) + 1, juce::dontSendNotification);
        place.clear(juce::dontSendNotification);
        if (routing.beamOn()) {
            for (int first = 0; first + routing.beamChannels() <= outputs; ++first) {
                auto probe = routing;
                probe.beamFirst = first;
                const auto clash = probe.overlaps();
                place.addItem("Outputs " + range(first, routing.beamChannels()) + (clash ? aside(probe, false) : juce::String()), first + 1);
            }
            place.setSelectedId(routing.beamFirst + 1, juce::dontSendNotification);
        } else {
            place.addItem(juce::String::fromUTF8("\xe2\x80\x94"), 1);
            place.setSelectedId(1, juce::dontSendNotification);
        }
        place.setEnabled(routing.beamOn());
        onCaption.setAlpha(routing.beamOn() ? 1.0f : .4f);
        note.setColour(juce::Label::textColourId, error.isNotEmpty() ? motion::style::error() : osci::Colours::textMuted());
        note.setText(error.isNotEmpty() ? error : moved.isNotEmpty() ? moved : explain(), juce::dontSendNotification);
        // Without a device there are no outputs to draw, so no room for them.
        const auto height = heightFor(2, (device.outputs > 0 ? stripHeight + 4 : 2) + 10 + noteHeight);
        if (getHeight() != height) {
            setSize(widthFor(caption + 260), height);
        } else {
            resized();
        }
        repaint();
    }

    // What the panel can't show by itself.
    juce::String explain() const {
        if (device.outputs <= 0) { return "No audio device is open. Choose one in Audio settings."; }
        if (device.outputs < 4 && (routing.soundOn() || routing.beamOn())) { return device.name + " has " + juce::String(device.outputs) + " outputs, room for the sound or the beam: whichever you choose plays. A device with more plays both."; }
        if (routing.beam == motion::OutputMode::xyz || routing.beam == motion::OutputMode::xyzInverted || routing.beam == motion::OutputMode::xyrgb) {
            return "Brightness needs a DC-coupled interface: through AC-coupled outputs the beam drifts and its light flickers.";
        }
        if (routing.beamOn()) { return "Connect X to the scope's horizontal input and Y to its vertical one, in XY mode."; }
        return {};
    }

    // The interface's outputs, each with what it carries: L and R for the
    // sound, X, Y, Z (or R, G, B) for the beam, nothing for the rest.
    void paintStrip(juce::Graphics& g) const {
        using namespace motion::ui::output;
        if (strip.isEmpty() || device.outputs <= 0) { return; }
        const auto shown = std::min(device.outputs, cellLimit);
        const auto more = device.outputs - shown;
        auto cells = strip.withTrimmedRight(more > 0 ? 34 : 0).withTrimmedLeft(caption);
        const auto gap = 3.0f;
        const auto width = std::min(26.0f, (static_cast<float>(cells.getWidth()) - gap * static_cast<float>(shown - 1)) / static_cast<float>(shown));
        const auto top = static_cast<float>(cells.getY());
        const auto beamRoles = roles(routing.beam);
        g.setColour(osci::Colours::textMuted());
        g.setFont(motion::style::body());
        g.drawText("Outputs", strip.withWidth(caption).withHeight(22).withTrimmedLeft(dotSpace), juce::Justification::centredLeft);
        for (int index = 0; index < shown; ++index) {
            const auto x = static_cast<float>(cells.getX()) + static_cast<float>(index) * (width + gap);
            const juce::Rectangle<float> cell(x, top, width, 22.0f);
            juce::String role;
            std::optional<juce::Colour> colour;
            if (routing.soundOn() && index >= routing.sound && index < routing.sound + 2) {
                role = index == routing.sound ? "L" : "R";
                colour = soundColour();
            } else if (routing.beamOn() && index >= routing.beamFirst && index < routing.beamFirst + routing.beamChannels()) {
                role = beamRoles[index - routing.beamFirst];
                colour = beamColour();
            }
            if (colour.has_value()) {
                g.setColour(colour->withAlpha(.22f));
                g.fillRoundedRectangle(cell, 4.0f);
                g.setColour(colour->withAlpha(.9f));
                g.drawRoundedRectangle(cell.reduced(.5f), 4.0f, 1.0f);
                g.setFont(motion::style::heading());
                g.setColour(roleColour(role, colour == beamColour()));
                g.drawText(role, cell, juce::Justification::centred);
            } else {
                g.setColour(osci::Colours::outlineSubtle().withAlpha(.4f));
                g.drawRoundedRectangle(cell.reduced(.5f), 4.0f, 1.0f);
            }
            g.setFont(motion::style::caption());
            g.setColour(osci::Colours::textMuted().withAlpha(colour.has_value() ? 1.0f : .55f));
            g.drawText(juce::String(index + 1), cell.withY(top + 24.0f).withHeight(14.0f), juce::Justification::centred);
        }
        if (more > 0) {
            g.setFont(motion::style::caption());
            g.setColour(osci::Colours::textMuted().withAlpha(.6f));
            g.drawText("+" + juce::String(more), strip.withLeft(strip.getRight() - 30).withHeight(22), juce::Justification::centredRight);
        }
    }

    motion::OutputRouting routing;
    Device device;
    bool hasSound;
    juce::Rectangle<int> strip;
    juce::Label soundCaption {"Sound caption", "Sound"}, beamCaption {"Beam caption", "Beam"}, onCaption {"On caption", "on"}, note;
    juce::ComboBox sound, form, place;
    juce::TextButton settings;
};
