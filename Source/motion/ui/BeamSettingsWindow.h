#pragma once

#include "../MotionProcessor.h"
#include "../../visualiser/VisualiserSettings.h"
#include "ScrubField.h"

// Scope timing calibration for the project: dwell, travel and settle, with
// presets for common displays. Every edit is one undo step; drags preview
// live and commit once when released.
class MotionScopeProfilePanel final : public juce::Component, private juce::ChangeListener {
public:
    explicit MotionScopeProfilePanel(MotionProcessor& owner) : processor(owner) {
        setName("Scope timing");
        title.setText("Scope Timing", juce::dontSendNotification);
        title.setFont(motion::style::title());
        title.setColour(juce::Label::textColourId, motion::style::text());
        title.setBorderSize({});
        addAndMakeVisible(title);
        presets.setName("Scope presets");
        presets.setButtonText("Presets");
        presets.setTooltip("Apply timing suited to a kind of display.");
        presets.onClick = [this] { showPresets(); };
        addAndMakeVisible(presets);
        note.setText("Dwell holds the beam dark at both ends of every jump. Travel is the dark move time per unit of screen distance. "
            "Settle waits after each jump before drawing, for slow scopes and galvos.", juce::dontSendNotification);
        note.setFont(motion::style::small());
        note.setColour(juce::Label::textColourId, motion::style::muted());
        note.setJustificationType(juce::Justification::topLeft);
        note.setBorderSize({});
        addAndMakeVisible(note);
        for (std::size_t index = 0; index < fields.size(); ++index) {
            auto& field = fields[index];
            const auto& spec = specs[index];
            const auto label = juce::String(spec.label.data(), spec.label.size());
            field.caption.setText(label, juce::dontSendNotification);
            field.caption.setFont(motion::style::body());
            field.caption.setColour(juce::Label::textColourId, motion::style::muted());
            field.caption.setBorderSize({});
            field.editor.setSpec(spec);
            field.editor.setName("Scope " + label.toLowerCase());
            field.editor.setTitle("Scope " + label.toLowerCase());
            field.editor.setComponentID("motion.scope." + juce::String(spec.id.data(), spec.id.size()));
            field.editor.setTooltip(label + ": drag to scrub (Shift fine, Cmd coarse), double-click to type.");
            field.editor.onBegin = [this] { beginGesture(); };
            field.editor.onChange = [this, index](double value) { previewValue(index, value); };
            field.editor.onEnd = [this] { endGesture(); };
            field.editor.onCancel = [this] { refresh(); };
            field.editor.onCommit = [this, index](double value) { commitValue(index, value); };
            addAndMakeVisible(field.caption);
            addAndMakeVisible(field.editor);
        }
        processor.document.addChangeListener(this);
        refresh();
    }
    ~MotionScopeProfilePanel() override { processor.document.removeChangeListener(this); }

    int getPreferredHeight() const {
        return outerPadding + motion::style::padding * 3 + motion::style::headerHeight + static_cast<int>(fields.size()) * (motion::style::controlHeight + motion::style::gap) + noteHeight;
    }

    void refresh() {
        if (gesture.has_value() && processor.document.revision() != gesture->revision) { gesture.reset(); }
        const auto& scope = processor.document.project().scope;
        fields[0].editor.setValue(scope.dwellMicros);
        fields[1].editor.setValue(scope.travelMicrosPerUnit);
        fields[2].editor.setValue(scope.settleMicros);
    }

    void paint(juce::Graphics& g) override {
        g.fillAll(motion::style::background());
        g.setColour(motion::style::panel());
        g.fillRoundedRectangle(section().toFloat(), motion::style::panelRadius);
    }
    void resized() override {
        auto area = section().reduced(motion::style::padding);
        auto header = area.removeFromTop(motion::style::headerHeight);
        presets.setBounds(header.removeFromRight(90).withSizeKeepingCentre(90, motion::style::controlHeight));
        title.setBounds(header);
        for (auto& field : fields) {
            auto row = area.removeFromTop(motion::style::controlHeight);
            area.removeFromTop(motion::style::gap);
            field.caption.setBounds(row.removeFromLeft(110));
            field.editor.setBounds(row.removeFromLeft(std::min(row.getWidth(), 160)));
        }
        area.removeFromTop(motion::style::padding);
        note.setBounds(area.removeFromTop(noteHeight));
    }

private:
    static constexpr int outerPadding = 10, noteHeight = 44;
    static constexpr std::array<motion::PropertySpec, 3> specs {{
        {"dwell", "Dwell", "Scope", "", 0, motion::ScopeProfile::maximumDwellMicros, 12, 1, 1, " µs"},
        {"travel", "Travel", "Scope", "", 0, motion::ScopeProfile::maximumTravelMicrosPerUnit, 30, 1, 1, " µs/unit"},
        {"settle", "Settle", "Scope", "", 0, motion::ScopeProfile::maximumSettleMicros, 0, 1, 1, " µs"},
    }};
    struct Field {
        juce::Label caption;
        MotionScrubField editor;
    };
    struct Gesture {
        motion::Project before;
        std::uint64_t revision;
    };

    // The display settings below keep their own top padding.
    juce::Rectangle<int> section() const { return getLocalBounds().reduced(outerPadding, 0).withTrimmedTop(outerPadding); }
    void changeListenerCallback(juce::ChangeBroadcaster*) override { refresh(); }
    static void apply(motion::ScopeProfile& scope, std::size_t index, double value) {
        const auto clamped = specs[index].clamp(value);
        if (index == 0) {
            scope.dwellMicros = clamped;
        } else if (index == 1) {
            scope.travelMicrosPerUnit = clamped;
        } else {
            scope.settleMicros = clamped;
        }
    }
    void beginGesture() {
        gesture = Gesture{processor.document.project(), processor.document.revision()};
        changed = false;
    }
    void previewValue(std::size_t index, double value) {
        if (!gesture.has_value()) { return; }
        auto updated = gesture->before;
        apply(updated.scope, index, value);
        processor.document.preview(std::move(updated));
        gesture->revision = processor.document.revision();
        changed = true;
    }
    void endGesture() {
        if (gesture.has_value() && changed && processor.document.revision() == gesture->revision) {
            processor.document.commit("Change scope timing", std::move(gesture->before));
        }
        gesture.reset();
        changed = false;
        refresh();
    }
    void commitValue(std::size_t index, double value) {
        auto scope = processor.document.project().scope;
        apply(scope, index, value);
        if (scope == processor.document.project().scope) { refresh(); return; }
        const auto control = "scope." + juce::String(specs[index].id.data(), specs[index].id.size());
        processor.document.editCoalesced("Change scope timing", control, [scope](motion::Project& project) { project.scope = scope; });
    }
    void showPresets() {
        juce::PopupMenu menu;
        const auto& current = processor.document.project().scope;
        for (std::size_t index = 0; index < motion::scopeProfilePresets.size(); ++index) {
            const auto& preset = motion::scopeProfilePresets[index];
            menu.addItem(static_cast<int>(index + 1), juce::String(preset.name.data(), preset.name.size()), true, preset.profile == current);
        }
        juce::Component::SafePointer<MotionScopeProfilePanel> safe(this);
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&presets), [safe](int result) {
            if (safe == nullptr || result <= 0 || result > static_cast<int>(motion::scopeProfilePresets.size())) { return; }
            safe->applyPreset(motion::scopeProfilePresets[static_cast<std::size_t>(result - 1)]);
        });
    }
    void applyPreset(const motion::ScopeProfilePreset& preset) {
        if (preset.profile == processor.document.project().scope) { return; }
        const auto profile = preset.profile;
        processor.document.edit("Apply " + juce::String(preset.name.data(), preset.name.size()) + " scope timing", [profile](motion::Project& project) { project.scope = profile; });
    }

    MotionProcessor& processor;
    juce::Label title, note;
    juce::TextButton presets;
    std::array<Field, 3> fields;
    std::optional<Gesture> gesture;
    bool changed = false;
};

// Beam settings: the project's scope timing above the display settings,
// scrolling together when the window is short.
class MotionBeamSettingsWindow final : public juce::DialogWindow {
public:
    MotionBeamSettingsWindow(juce::String name, VisualiserSettings& settings, MotionScopeProfilePanel& scope, int windowWidth, int windowHeight, int maximumWindowWidth)
        : juce::DialogWindow(name, osci::Colours::darker(), true, true), content(settings, scope) {
        setContentNonOwned(&viewport, false);
        centreWithSize(windowWidth, windowHeight);
        setResizeLimits(windowWidth, windowHeight, maximumWindowWidth, juce::jmax(windowHeight, content.preferredHeight(windowWidth)));
        setResizable(true, false);
        viewport.setColour(juce::ScrollBar::trackColourId, juce::Colours::white);
        viewport.setViewedComponent(&content, false);
        viewport.setScrollBarsShown(true, false, true, false);
        setAlwaysOnTop(true);
    }

    void closeButtonPressed() override { setVisible(false); }
    void resized() override {
        DialogWindow::resized();
        content.fit(viewport);
    }

private:
    struct Content final : juce::Component {
        Content(VisualiserSettings& visualiser, MotionScopeProfilePanel& profile) : settings(visualiser), scope(profile) {
            addAndMakeVisible(scope);
            addAndMakeVisible(settings);
        }
        int preferredHeight(int width) { return scope.getPreferredHeight() + settings.getPreferredHeight(width); }
        void fit(juce::Viewport& viewport) {
            auto width = juce::jmax(1, viewport.getWidth());
            if (preferredHeight(width) > viewport.getHeight()) { width = juce::jmax(1, width - viewport.getScrollBarThickness()); }
            scope.setBounds(0, 0, width, scope.getPreferredHeight());
            settings.setSizeToFitWidth(width);
            settings.setTopLeftPosition(0, scope.getBottom());
            setSize(width, settings.getBottom());
        }
        void paint(juce::Graphics& g) override { g.fillAll(motion::style::background()); }
        VisualiserSettings& settings;
        MotionScopeProfilePanel& scope;
    };
    Content content;
    juce::Viewport viewport;
};
