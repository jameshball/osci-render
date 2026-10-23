#pragma once

#include "../MotionProcessor.h"
#include "MotionStyle.h"
#include "../render/CompositionRenderer.h"
#include <array>
#include <functional>

// With nothing selected, Properties shows the composition's own settings:
// how long it runs, its frame rate and meter, and the project's beam and
// sample rate, in the same two-column rows as a clip's timing; and for the
// project itself, what it is about, as the start screen shows it under its
// name. Both sections open as Properties' own do.
class MotionCompositionPanel final : public juce::Component {
public:
    explicit MotionCompositionPanel(MotionProcessor& owner) : processor(owner) {
        setName("Composition settings");
        motion::style::inspector::styleHeading(title, "Timing");
        addAndMakeVisible(title);
        const std::array<const char*, 5> labels {"Duration", "Frames", "Meter", "Samples", "Beam"};
        for (std::size_t index = 0; index < captions.size(); ++index) {
            motion::style::inspector::styleCaption(captions[index], labels[index]);
            addAndMakeVisible(captions[index]);
        }
        length.setName("Composition duration");
        length.setTitle("Composition duration");
        length.setTooltip("Never shorter than its last clip. In the ruler's units; add s for seconds");
        length.setEditable(false, true);
        length.setJustificationType(juce::Justification::centredRight);
        length.setFont(motion::style::body());
        length.setColour(juce::Label::backgroundColourId, osci::Colours::veryDark());
        addAndMakeVisible(length);
        length.onTextChange = [this] { if (!updating) { applyLength(); } };
        frameRate.setName("Composition frame rate");
        const std::array<const char*, 8> rateNames {"23.976 fps", "24 fps", "25 fps", "29.97 fps", "30 fps", "50 fps", "60 fps", "120 fps"};
        for (std::size_t index = 0; index < rateNames.size(); ++index) { frameRate.addItem(rateNames[index], static_cast<int>(index) + 1); }
        meter.setName("Composition meter");
        for (const auto beats : {2, 3, 4, 5, 6, 7}) { meter.addItem(juce::String(beats) + "/4", beats); }
        // The beam's base frequency: whole times a frame is drawn, so each
        // frame plays whole; the items are made for the frame rate in refresh().
        beam.setName("Composition beam frequency");
        beam.setTitle("Beam frequency");
        beam.setTooltip("The beam's base frequency: how many times a second the picture is drawn. Each frame is drawn a whole number of times, so it follows the frame rate. Below 45 Hz a scope's picture flickers; higher draws each frame with fewer points.");
        frameRate.setTooltip("Frames a second. The beam draws each frame a whole number of times (see Beam)");
        for (auto* combo : {&frameRate, &meter, &beam}) {
            motion::style::styleField(*combo);
            addAndMakeVisible(*combo);
        }
        frameRate.onChange = [this] { if (!updating) { applyFrameRate(); } };
        meter.onChange = [this] { if (!updating) { applyMeter(); } };
        beam.onChange = [this] { if (!updating) { applyBeam(); } };
        // What the project is drawn and exported at: the beam's time to draw.
        sampleRate.setName("Composition sample rate");
        sampleRate.setTitle("Sample rate");
        sampleRate.setTooltip("What the project is drawn and exported at (" + motion::style::kilohertz(motion::Project::defaultSampleRate) + " by default). A higher rate gives the beam time to draw more each cycle.");
        for (std::size_t index = 0; index < sampleRates.size(); ++index) { sampleRate.addItem(motion::style::kilohertz(sampleRates[index]), static_cast<int>(index) + 1); }
        motion::style::styleField(sampleRate);
        sampleRate.onChange = [this] { if (!updating) { applySampleRate(); } };
        addAndMakeVisible(sampleRate);
        motion::style::inspector::styleHeading(aboutTitle, "About");
        addChildComponent(aboutTitle);
        about.setName("Project description");
        about.setTitle("Project description");
        about.setMultiLine(true, true);
        about.setReturnKeyStartsNewLine(false);
        about.setScrollbarsShown(false);
        about.setInputRestrictions(motion::Project::longestDescription);
        about.setFont(motion::style::body());
        about.setTextToShowWhenEmpty("Describe this project for the start screen", osci::Colours::textMuted().withAlpha(.6f));
        motion::style::styleField(about);
        about.setIndents(8, 6);
        about.onReturnKey = [this] {
            applyDescription();
            about.giveAwayKeyboardFocus();
        };
        about.onFocusLost = [this] { applyDescription(); };
        about.onTextChange = [this] { fitAbout(); };
        about.onEscapeKey = [this] {
            about.setText(processor.document.mainProject().description, false);
            about.giveAwayKeyboardFocus();
        };
        addChildComponent(about);
    }

    std::function<void(const juce::String&)> onError;
    std::function<void()> onHeightChanged;
    static constexpr std::array<double, 8> rates {24000.0 / 1001, 24, 25, 30000.0 / 1001, 30, 50, 60, 120};
    // The rates a project can be drawn at, from what audio runs at to four
    // times 192 kHz.
    static constexpr std::array<double, 8> sampleRates {44100, 48000, 88200, 96000, 176400, 192000, 384000, 768000};
    // The beam's frequencies: Auto, then 1 to 8 cycles a frame.
    static std::vector<juce::String> beamChoices(double frameRate) {
        std::vector<juce::String> choices;
        choices.push_back("Auto" + motion::style::dot() + motion::style::hertz(motion::beamCycleRate(frameRate)));
        for (int cycles = 1; cycles <= motion::Project::mostBeamCycles; ++cycles) {
            const auto times = cycles == 1 ? juce::String("once a frame") : cycles == 2 ? juce::String("twice a frame") : juce::String(cycles) + " times a frame";
            const auto rate = motion::beamCycleRate(frameRate, cycles);
            choices.push_back(motion::style::hertz(rate) + motion::style::dot() + times + (rate < 45 ? " (flickers)" : ""));
        }
        return choices;
    }
    // Where the beam's frequency is set, for the status bar's rates to lead to.
    juce::Component& beamField() { return beam; }

    void setShown(bool value) {
        if (shown == value) { return; }
        shown = value;
        refresh();
        if (onHeightChanged) { onHeightChanged(); }
    }
    int preferredHeight() const {
        if (!shown) {
            return 0;
        }
        const auto timing = headingBlock + 3 * motion::style::inspector::row;
        return timing + (aboutShown ? aboutGap + ruleGap + headingBlock + aboutHeight : 0) + 10;
    }
    void refresh() {
        if (!shown) { return; }
        // Only the project has a description, not a composition entered.
        const auto mainShown = processor.document.editingComposition() == 0;
        if (mainShown != aboutShown) {
            aboutShown = mainShown;
            aboutTitle.setVisible(aboutShown);
            about.setVisible(aboutShown);
            resized();
            if (onHeightChanged) { onHeightChanged(); }
        }
        if (!about.hasKeyboardFocus(true)) {
            about.setText(processor.document.mainProject().description, false);
            fitAbout();
        }
        const auto& project = processor.document.project();
        updating = true;
        // As the clip's Timing reads a duration: in the ruler's units.
        if (!length.isBeingEdited()) { length.setText(juce::String(project.timeGrid().durationLabel(0, project.duration)), juce::dontSendNotification); }
        int rate = 0;
        for (std::size_t index = 0; index < rates.size(); ++index) { if (std::abs(project.frameRate - rates[index]) < 1.0e-9) { rate = static_cast<int>(index) + 1; } }
        frameRate.setSelectedId(rate, juce::dontSendNotification);
        if (rate == 0) { frameRate.setText(juce::String(project.frameRate, 3) + " fps", juce::dontSendNotification); }
        meter.setSelectedId(project.beatsPerBar, juce::dontSendNotification);
        // Beam: Auto, or once, twice... a frame, each named for its frequency.
        const auto& main = processor.document.mainProject();
        beam.clear(juce::dontSendNotification);
        const auto choices = beamChoices(main.frameRate);
        for (std::size_t cycles = 0; cycles < choices.size(); ++cycles) { beam.addItem(choices[cycles], static_cast<int>(cycles) + 1); }
        beam.setSelectedId(main.beamCycles + 1, juce::dontSendNotification);
        int sampleRateId = 0;
        for (std::size_t index = 0; index < sampleRates.size(); ++index) {
            if (std::abs(main.sampleRate - sampleRates[index]) < .5) { sampleRateId = static_cast<int>(index) + 1; }
        }
        sampleRate.setSelectedId(sampleRateId, juce::dontSendNotification);
        if (sampleRateId == 0) { sampleRate.setText(motion::style::kilohertz(main.sampleRate), juce::dontSendNotification); }
        updating = false;
    }
    void resized() override {
        auto area = getLocalBounds();
        title.setBounds(area.removeFromTop(motion::style::inspector::headingHeight));
        area.removeFromTop(motion::style::gap);
        motion::style::inspector::layoutFields(area, captions, {&length, &frameRate, &meter, &sampleRate, &beam}, true);
        area.removeFromTop(aboutGap);
        aboutRule = area.getY();
        area.removeFromTop(ruleGap);
        aboutTitle.setBounds(area.removeFromTop(motion::style::inspector::headingHeight));
        area.removeFromTop(motion::style::gap);
        // Nothing to key beside it, so it takes the whole width.
        about.setBounds(area.removeFromTop(aboutHeight));
        fitAbout();
        repaint();
    }
    void paint(juce::Graphics& g) override {
        if (aboutShown) {
            g.setColour(motion::style::hairline());
            g.fillRect(0, aboutRule, getWidth(), 1);
        }
    }

private:
    // As Properties opens a section: a rule after the rows above, then the
    // heading clear of it, then the rows a gap below.
    static constexpr int aboutGap = motion::style::padding + motion::style::gap, ruleGap = 10;
    static constexpr int headingBlock = motion::style::inspector::headingHeight + motion::style::gap;
    int aboutRule = 0;
    // The field grows with what is written, two lines to seven.
    int aboutHeight = 46;
    void fitAbout() {
        if (about.getWidth() <= 0) {
            return;
        }
        const auto wanted = juce::jlimit(46, 120, about.getTextHeight() + 8);
        if (wanted == aboutHeight) {
            return;
        }
        aboutHeight = wanted;
        about.setSize(about.getWidth(), aboutHeight);
        if (onHeightChanged) { onHeightChanged(); }
    }
    void applyDescription() {
        const auto text = about.getText().replaceCharacters("\r\n", "  ").trim();
        if (text != processor.document.mainProject().description) {
            processor.document.edit("Change the description", [text](motion::Project& project) { project.description = text; });
        }
    }
    void applyLength() {
        auto& document = processor.document;
        const auto grid = document.project().timeGrid();
        const auto value = grid.parseDuration(length.getText().trim().toStdString(), 0);
        double lastClip = 0;
        for (const auto& track : document.project().tracks) {
            for (const auto& clip : track.clips) { lastClip = std::max(lastClip, clip.timing(document.project().tempo()).end()); }
        }
        // At least one frame: anything shorter has nothing to draw.
        const auto rate = document.project().frameRate;
        const auto frame = rate > 0 ? 1.0 / rate : 1.0 / 60.0;
        if (!value.has_value()) {
            if (onError) { onError("Enter a length like the ruler shows (" + juce::String(grid.durationLabel(0, document.project().duration)) + "), or seconds with s."); }
        } else if (*value < frame - 1.0e-9 || *value > 24 * 3600) {
            if (onError) { onError("Enter a length from one frame (" + juce::String(frame, 3) + " s) to 24 hours."); }
        } else if (*value < lastClip - 1.0e-9) {
            if (onError) { onError("The composition runs at least to its last clip (" + juce::String(lastClip, 2) + " s)."); }
        } else if (std::abs(*value - document.project().duration) > 1.0e-9) {
            document.edit("Change composition duration", [length = *value](motion::Project& project) { project.duration = length; });
        }
        refresh();
    }
    // The project's, so every composition in it: one undo step.
    void applySampleRate() {
        const auto index = sampleRate.getSelectedId() - 1;
        if (index < 0 || index >= static_cast<int>(sampleRates.size())) { return; }
        const auto rate = sampleRates[static_cast<std::size_t>(index)];
        if (std::abs(processor.document.mainProject().sampleRate - rate) >= .5) {
            processor.document.edit("Change the sample rate", [rate](motion::Project& project) { project.sampleRate = rate; });
        }
    }
    void applyFrameRate() {
        const auto index = frameRate.getSelectedId() - 1;
        if (index < 0 || index >= static_cast<int>(rates.size())) { return; }
        const auto rate = rates[static_cast<std::size_t>(index)];
        if (std::abs(processor.document.project().frameRate - rate) >= 1.0e-9) {
            processor.document.edit("Change frame rate", [rate](motion::Project& project) { project.frameRate = rate; });
        }
    }
    // A beat grid of a whole bar stays a whole bar.
    void applyMeter() {
        const auto beats = meter.getSelectedId();
        if (beats < 2 || beats > 7 || beats == processor.document.project().beatsPerBar) { return; }
        processor.document.edit("Change meter", [beats](motion::Project& project) {
            if (std::abs(project.snapBeats - project.beatsPerBar) < 1.0e-9) { project.snapBeats = beats; }
            project.beatsPerBar = beats;
        });
    }
    // The project's, as the sample rate is.
    void applyBeam() {
        const auto cycles = beam.getSelectedId() - 1;
        if (cycles < 0 || cycles > motion::Project::mostBeamCycles || cycles == processor.document.mainProject().beamCycles) { return; }
        processor.document.edit("Change beam frequency", [cycles](motion::Project& project) { project.beamCycles = cycles; });
    }

    MotionProcessor& processor;
    bool shown = false, updating = false;
    bool aboutShown = false;
    juce::Label title, length, aboutTitle;
    juce::TextEditor about;
    std::array<juce::Label, 5> captions;
    juce::ComboBox frameRate, meter, beam, sampleRate;
};
