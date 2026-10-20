#pragma once

#include "../MotionProcessor.h"
#include "TypedNumber.h"
#include "MotionStyle.h"
#include "../render/CompositionRenderer.h"
#include <array>
#include <functional>

// With nothing selected, Properties shows the composition's own settings:
// how long it runs, its frame rate, tempo and meter, in the same two-column
// rows as a clip's timing; and for the project itself, what it is about, as
// the start screen shows it under its name.
class MotionCompositionPanel final : public juce::Component {
public:
    explicit MotionCompositionPanel(MotionProcessor& owner) : processor(owner) {
        setName("Composition settings");
        motion::style::inspector::styleHeading(title, "Timing");
        addAndMakeVisible(title);
        const std::array<const char*, 5> labels {"Duration", "Rate", "Tempo", "Meter", "Beam"};
        for (std::size_t index = 0; index < captions.size(); ++index) {
            motion::style::inspector::styleCaption(captions[index], labels[index]);
            addAndMakeVisible(captions[index]);
        }
        for (auto [field, name, tip] : {std::tuple {&length, "Composition duration", "Never shorter than its last clip"},
                                        std::tuple {&tempo, "Composition tempo", "Tempo at the start; changes on the ruler take over from there"}}) {
            field->setName(name);
            field->setTitle(name);
            field->setTooltip(tip);
            field->setEditable(false, true);
            field->setJustificationType(juce::Justification::centredRight);
            field->setFont(motion::style::body());
            field->setColour(juce::Label::backgroundColourId, osci::Colours::veryDark());
            addAndMakeVisible(*field);
        }
        length.onTextChange = [this] { if (!updating) { applyLength(); } };
        tempo.onTextChange = [this] { if (!updating) { applyTempo(); } };
        frameRate.setName("Composition frame rate");
        const std::array<const char*, 8> rateNames {"23.976 fps", "24 fps", "25 fps", "29.97 fps", "30 fps", "50 fps", "60 fps", "120 fps"};
        for (std::size_t index = 0; index < rateNames.size(); ++index) { frameRate.addItem(rateNames[index], 500 + static_cast<int>(index)); }
        meter.setName("Composition meter");
        for (const auto beats : {2, 3, 4, 5, 6, 7}) { meter.addItem(juce::String(beats) + "/4", 400 + beats); }
        // The beam's base frequency: whole times a frame is drawn, so each
        // frame plays whole; the items are made for the frame rate in refresh().
        beam.setName("Composition beam frequency");
        beam.setTitle("Beam frequency");
        beam.setTooltip("The beam's base frequency: how many times a second the picture is drawn. Each frame is drawn a whole number of times, so it follows the frame rate. Below 45 Hz a scope's picture flickers; higher draws each frame with fewer points.");
        frameRate.setTooltip("Frames a second. The beam draws each frame a whole number of times (see Beam)");
        for (auto* combo : {&frameRate, &meter, &beam}) {
            motion::style::styleField(*combo);
            combo->onChange = [this, combo] { if (!updating && onTiming) { onTiming(combo->getSelectedId()); } };
            addAndMakeVisible(*combo);
        }
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

    // The Timing menu's own command ids, so both change things the same way.
    std::function<void(int)> onTiming;
    std::function<void(const juce::String&)> onError;
    std::function<void()> onHeightChanged;
    static constexpr std::array<double, 8> rates {24000.0 / 1001, 24, 25, 30000.0 / 1001, 30, 50, 60, 120};
    // The Timing menu's ids for the beam's frequency: Auto, then 1 to 8 cycles a frame.
    static constexpr int beamCommand = 800, beamChoiceCount = 8;
    static juce::String hertz(double value) { return juce::String(value, value == std::floor(value) ? 0 : 2) + " Hz"; }
    static std::vector<std::pair<int, juce::String>> beamChoices(double frameRate) {
        std::vector<std::pair<int, juce::String>> choices;
        choices.emplace_back(beamCommand, "Auto" + motion::style::dot() + hertz(motion::beamCycleRate(frameRate)));
        for (int cycles = 1; cycles <= beamChoiceCount; ++cycles) {
            const auto times = cycles == 1 ? juce::String("once a frame") : cycles == 2 ? juce::String("twice a frame") : juce::String(cycles) + " times a frame";
            const auto rate = motion::beamCycleRate(frameRate, cycles);
            choices.emplace_back(beamCommand + cycles, hertz(rate) + motion::style::dot() + times + (rate < 45 ? " (flickers)" : ""));
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
        const auto timing = motion::style::inspector::headingHeight + 3 * motion::style::inspector::row + 10;
        return timing + (aboutShown ? aboutGap + motion::style::inspector::headingHeight + aboutHeight + 10 : 0);
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
        if (!length.isBeingEdited()) { length.setText(juce::String(project.duration, 3) + "s", juce::dontSendNotification); }
        if (!tempo.isBeingEdited()) { tempo.setText(juce::String(project.bpm, project.bpm == std::round(project.bpm) ? 0 : 2) + " BPM", juce::dontSendNotification); }
        int rate = 0;
        for (std::size_t index = 0; index < rates.size(); ++index) { if (std::abs(project.frameRate - rates[index]) < 1.0e-9) { rate = 500 + static_cast<int>(index); } }
        frameRate.setSelectedId(rate, juce::dontSendNotification);
        if (rate == 0) { frameRate.setText(juce::String(project.frameRate, 3) + " fps", juce::dontSendNotification); }
        meter.setSelectedId(400 + project.beatsPerBar, juce::dontSendNotification);
        // Beam: Auto, or once, twice... a frame, each named for its frequency.
        const auto& main = processor.document.mainProject();
        beam.clear(juce::dontSendNotification);
        for (const auto& [id, text] : beamChoices(main.frameRate)) { beam.addItem(text, id); }
        beam.setSelectedId(beamCommand + main.beamCycles, juce::dontSendNotification);
        updating = false;
    }
    void resized() override {
        auto area = getLocalBounds();
        title.setBounds(area.removeFromTop(motion::style::inspector::headingHeight));
        motion::style::inspector::layoutFields(area, captions, {&length, &frameRate, &tempo, &meter, &beam});
        area.removeFromTop(aboutGap);
        aboutTitle.setBounds(area.removeFromTop(motion::style::inspector::headingHeight));
        area.removeFromTop(4);
        const motion::style::PropertyGrid grid(area.getWidth());
        about.setBounds(area.removeFromTop(aboutHeight).withWidth(grid.value + grid.column));
        fitAbout();
    }

private:
    static constexpr int aboutGap = 14;
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
        const auto value = motion::ui::parseNumber(length.getText(), "s");
        auto& document = processor.document;
        double lastClip = 0;
        for (const auto& track : document.project().tracks) {
            for (const auto& clip : track.clips) { lastClip = std::max(lastClip, clip.timing(document.project().tempo()).end()); }
        }
        // At least one frame: anything shorter has nothing to draw.
        const auto rate = document.project().frameRate;
        const auto frame = rate > 0 ? 1.0 / rate : 1.0 / 60.0;
        if (!value.has_value() || *value < frame - 1.0e-9 || *value > 24 * 3600) {
            if (onError) { onError("Enter a length from one frame (" + juce::String(frame, 3) + " s) to 24 hours."); }
        } else if (*value < lastClip - 1.0e-9) {
            if (onError) { onError("The composition runs at least to its last clip (" + juce::String(lastClip, 2) + " s)."); }
        } else if (std::abs(*value - document.project().duration) > 1.0e-9) {
            document.edit("Change composition duration", [length = *value](motion::Project& project) { project.duration = length; });
        }
        refresh();
    }
    void applyTempo() {
        const auto value = motion::ui::parseNumber(tempo.getText(), "BPM");
        if (!value.has_value() || *value < 1 || *value > 1000) {
            if (onError) { onError("Enter a tempo between 1 and 1000 BPM."); }
        } else if (*value != processor.document.project().bpm) {
            const auto result = processor.document.changeTempo(*value);
            if (result.failed() && onError) { onError(result.getErrorMessage()); }
        }
        refresh();
    }

    MotionProcessor& processor;
    bool shown = false, updating = false;
    bool aboutShown = false;
    juce::Label title, length, tempo, aboutTitle;
    juce::TextEditor about;
    std::array<juce::Label, 5> captions;
    juce::ComboBox frameRate, meter, beam;
};
