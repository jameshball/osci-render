#pragma once

#include "../MotionProcessor.h"
#include "Chip.h"
#include "DocumentMenu.h"
#include "../model/ModulationGraph.h"
#include "MotionIcons.h"
#include "ScrubField.h"

namespace motion::ui {
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
    void setup(juce::Component& owner, const juce::String& text, const juce::String& name, const PropertySpec& spec);
    void setVisible(bool visible) { label.setVisible(visible); field.setVisible(visible); }
    void layout(juce::Rectangle<int>& area);
};

// One modulator route, seen from either end: what it connects to above its
// amount, add or multiply, and removal. Rows are reused as routes change, so
// owners call show() rather than rebuilding them.
class RouteRow final : public juce::Component {
public:
    static constexpr int height = style::controlHeight * 2;

    RouteRow(Document& document, std::function<void(const juce::Result&)> report);
    // `text` names the route's other end: its target, or its modulator.
    void show(const ModulationRoute& value, const juce::String& text);
    bool isEditing() const { return amount.isEditing(); }
    void resized() override;

    // Shows `routes` in `rows`, reusing rows in order and adding or removing
    // the difference.
    static void showAll(std::vector<std::unique_ptr<RouteRow>>& rows, juce::Component& parent, const std::vector<std::pair<ModulationRoute, juce::String>>& routes, Document& document, const std::function<void(const juce::Result&)>& report);

private:
    void apply(double value, ModulationMode next);

    Document& document;
    std::function<void(const juce::Result&)> report;
    ModulationRoute route;
    juce::Label label;
    MotionScrubField amount;
    juce::TextButton mode;
    osci::CloseButton remove {"Remove route"};
};
}

namespace motion::ui {
// What a modulator does, drawn: two cycles of an oscillator, an envelope's
// ADSR, or a controller's sweep. Shared by the cards and the shape picker.
inline void paintModulatorShape(juce::Graphics& g, const Modulator& modulator, juce::Rectangle<float> area, juce::Colour colour, float stroke = 1.4f) {
    juce::Path path;
    const auto point = [&](double x, double y) { return juce::Point<float>(area.getX() + static_cast<float>(x) * area.getWidth(), area.getCentreY() - static_cast<float>(y) * area.getHeight() * .5f); };
    if (modulator.kind == ModulatorKind::envelope) {
        const auto total = std::max(1.0e-3, modulator.attack + modulator.decay + modulator.release + .3);
        const auto bottom = -1.0, sustain = -1.0 + 2.0 * std::clamp(modulator.sustain, 0.0, 1.0);
        double x = 0;
        path.startNewSubPath(point(0, bottom));
        x += modulator.attack / total; path.lineTo(point(x, 1));
        x += modulator.decay / total; path.lineTo(point(x, sustain));
        x += .3 / total; path.lineTo(point(x, sustain));
        path.lineTo(point(1, bottom));
    } else if (modulator.kind == ModulatorKind::controller && modulator.controller == MidiControl::pitchBend) {
        // A bend: centred, a dip and back.
        path.startNewSubPath(point(0, 0));
        for (int step = 1; step <= 40; ++step) {
            const auto x = step / 40.0;
            path.lineTo(point(x, -.85 * std::exp(-std::pow((x - .5) * 5, 2))));
        }
    } else if (modulator.kind == ModulatorKind::controller) {
        path.startNewSubPath(point(0, -.8));
        for (int step = 1; step <= 40; ++step) {
            const auto x = step / 40.0;
            path.lineTo(point(x, -.8 + 1.6 * (x * x * (3 - 2 * x)) + .12 * std::sin(x * 19)));
        }
    } else if (modulator.shape.waveform == ModulationWaveform::soundtrack) {
        // Loudness: a level that follows the music.
        Modulation shape;
        shape.waveform = ModulationWaveform::noiseSmooth;
        shape.seed = 7;
        for (int step = 0; step <= 48; ++step) {
            const auto x = step / 48.0;
            const auto y = -.9 + 1.7 * std::abs(shape.atCycles(x * 6)) * (.6 + .4 * std::sin(x * 3.1));
            if (step == 0) { path.startNewSubPath(point(x, y)); } else { path.lineTo(point(x, y)); }
        }
    } else {
        auto shape = modulator.shape;
        shape.phase = 0;
        const auto steps = static_cast<int>(area.getWidth());
        for (int step = 0; step <= steps; ++step) {
            const auto x = static_cast<double>(step) / steps;
            const auto y = shape.atCycles(x * 2) * .9;
            if (step == 0) { path.startNewSubPath(point(x, y)); } else { path.lineTo(point(x, y)); }
        }
    }
    g.setColour(colour);
    g.strokePath(path, juce::PathStrokeType(stroke, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

inline juce::String modulatorKindName(const Modulator& modulator) {
    if (modulator.kind == ModulatorKind::envelope) { return "Envelope"; }
    if (modulator.kind == ModulatorKind::controller) { return modulator.controller == MidiControl::pitchBend ? "Pitch bend" : "MIDI CC"; }
    if (modulator.shape.waveform == ModulationWaveform::soundtrack) { return "Loudness"; }
    if (modulator.shape.waveform == ModulationWaveform::noiseSmooth || modulator.shape.waveform == ModulationWaveform::noiseHold) { return "Random"; }
    return "LFO";
}
}

// Library tab: the composition's shared modulators as cards, the selected one
// edited below with what it drives. Routes are made from a property's Graph.
class MotionModulatorLibrary final : public juce::Component, private juce::ChangeListener {
public:
    explicit MotionModulatorLibrary(MotionProcessor& owner);
    ~MotionModulatorLibrary() override { processor.document.removeChangeListener(this); }

    std::function<void(const juce::String&)> onError;
    motion::Id selectedModulator() const { return selected; }
    void select(motion::Id id) { selected = id; refresh(); }

    void refresh();

    void resized() override;
    void paint(juce::Graphics& g) override { g.fillAll(osci::Colours::surface()); }

private:
    static constexpr int cardHeight = 40;
    // The modulator as a card: what it does, its name and how much it drives.
    class Card final : public juce::Component {
    public:
        Card(MotionModulatorLibrary& library, const motion::Modulator& value, int routeCount);
        void paint(juce::Graphics& g) override;
        void mouseEnter(const juce::MouseEvent&) override { fade.setTarget(true); }
        void mouseExit(const juce::MouseEvent&) override { fade.setTarget(false); }
        void mouseDown(const juce::MouseEvent&) override { owner.select(modulator.id); }
        // Dragging the card onto a property in Properties routes it there.
        void mouseDrag(const juce::MouseEvent& event) override;
    private:
        MotionModulatorLibrary& owner;
        motion::Modulator modulator;
        int routes;
        motion::style::Fade fade {*this};
    };
    // One oscillator shape, drawn.
    class ShapeButton final : public juce::Button {
    public:
        ShapeButton(const juce::String& text, motion::ModulatorKind kind, motion::ModulationWaveform waveform);
        void buttonStateChanged() override { fade.follow(*this); }
        void paintButton(juce::Graphics& g, bool highlighted, bool down) override;
    private:
        motion::Modulator shape;
        motion::style::Fade fade {*this};
    };
    const motion::Modulator* current() const;
    struct Kind { const char* name; motion::ModulatorKind kind; motion::ModulationWaveform waveform; };
    static const std::array<Kind, 10>& kinds();
    static std::size_t kindIndex(const motion::Modulator& modulator);
    // Changing the kind renames a modulator that still has its default name.
    void setKind(std::size_t index);
    void add(motion::ModulatorKind kind = motion::ModulatorKind::oscillator, motion::ModulationWaveform waveform = motion::ModulationWaveform::sine);
    void change(const std::function<void(motion::Modulator&)>& update);
    void bind(motion::ui::LabelledScrub& row, std::function<void(motion::Modulator&, double)> apply);
    void report(const juce::Result& result) {
        if (result.failed() && onError) { onError(result.getErrorMessage()); }
    }
    void showRoutes();
    void changeListenerCallback(juce::ChangeBroadcaster*) override;
    // The selected modulator's output over two cycles, above its settings.
    class Content final : public juce::Component {
    public:
        explicit Content(MotionModulatorLibrary& library) : owner(library) {}
        void paint(juce::Graphics& g) override;
    private:
        MotionModulatorLibrary& owner;
    };

    MotionProcessor& processor;
    motion::Id selected = 0;
    juce::Viewport viewport;
    Content content {*this};
    juce::TextButton addButton;
    motion::icons::Button remove {"Delete modulator", motion::icons::Icon::trash};
    std::array<std::unique_ptr<ShapeButton>, 10> shapes;
    std::vector<std::pair<motion::Modulator, int>> listedCards;
    motion::style::Disclosure more {"More modulator settings"};
    bool showMore = false;
    motion::ui::Chip hertz {"Hz"}, beatsChip {"Beats"};
    juce::ComboBox source, controller, channel;
    std::vector<motion::Id> sourceIds;
    juce::Label name, routesTitle, routesHint;
    motion::ui::LabelledScrub rate, phase, seed, attack, decay, sustain, release, velocity, lowest, highest;
    std::vector<std::unique_ptr<Card>> cards;
    std::vector<std::unique_ptr<motion::ui::RouteRow>> routes;
    juce::Rectangle<int> previewArea;
    int separatorY = 0;
};

// Graph column: modulators routed into the selected property, and its link.
class MotionRoutingPanel final : public juce::Component, private juce::ChangeListener {
public:
    explicit MotionRoutingPanel(MotionProcessor& owner);
    ~MotionRoutingPanel() override { processor.document.removeChangeListener(this); }

    std::function<void(const juce::String&)> onError;

    void setTarget(motion::Id id, std::string property);
    void refresh();
    int preferredHeight() const;
    void resized() override;
    void paint(juce::Graphics& g) override;

private:
    void showLinkMenu();
    void report(const juce::Result& result) {
        if (result.failed() && onError) { onError(result.getErrorMessage()); }
    }
    void changeListenerCallback(juce::ChangeBroadcaster*) override;

public:
    std::function<void()> onLayoutChanged;

private:
    MotionProcessor& processor;
    motion::Id targetId = 0;
    std::string propertyName;
    juce::TextButton link, unlink;
    juce::Label linkSource;
    motion::ui::LabelledScrub scale, offset, delay;
    std::vector<std::unique_ptr<motion::ui::RouteRow>> rows;
    std::vector<std::pair<motion::Id, std::string>> choices;
};
