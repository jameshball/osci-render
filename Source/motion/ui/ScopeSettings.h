#pragma once

#include "MotionStyle.h"

#include "../MotionProcessor.h"
#include "ScrubField.h"

namespace motion::scope {
// Section headings and one-line rows share the Properties grid: names on the
// left, values in the last of three columns, beside where keys would be.
inline void heading(juce::Label& label, const juce::String& text) {
    label.setText(text, juce::dontSendNotification);
    label.setFont(motion::style::caption());
    label.setColour(juce::Label::textColourId, osci::Colours::text());
    label.setBorderSize({});
}
struct Grid {
    explicit Grid(int width) {
        const auto line = width - 12 - 18 - 12 - motion::style::gap;
        column = std::min(110, (line - motion::style::gap * 2) / 3);
        value = 2 * (column + motion::style::gap);
        keys = line + motion::style::gap;
    }
    int column = 0, value = 0, keys = 0;
};
inline constexpr int headingHeight = 18;

// A small text menu in a heading line: muted text and a caret, brighter
// when hovered.
class MenuLink final : public juce::Button {
public:
    explicit MenuLink(const juce::String& name) : juce::Button(name) {
        setButtonText(name);
        setWantsKeyboardFocus(false);
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
    }
    int idealWidth() const { return juce::roundToInt(std::ceil(juce::TextLayout::getStringWidth(motion::style::caption(), getButtonText()))) + caret + 8; }
    void paintButton(juce::Graphics& g, bool highlighted, bool down) override {
        const auto colour = (highlighted || down ? osci::Colours::text() : osci::Colours::textMuted());
        auto area = getLocalBounds();
        auto arrow = area.removeFromRight(caret).toFloat().withSizeKeepingCentre(7.0f, 4.0f);
        juce::Path path;
        path.addTriangle(arrow.getX(), arrow.getY(), arrow.getRight(), arrow.getY(), arrow.getCentreX(), arrow.getBottom());
        g.setColour(colour);
        g.fillPath(path);
        g.setFont(motion::style::caption());
        g.drawText(getButtonText(), area.withTrimmedRight(4), juce::Justification::centredRight, false);
    }
private:
    static constexpr int caret = 8;
};
}

// The heading over the Scope's animated rows.
class MotionScopeHeading final : public juce::Component {
public:
    MotionScopeHeading() {
        motion::scope::heading(label, "Beam");
        addAndMakeVisible(label);
    }
    static constexpr int preferredHeight() { return motion::scope::headingHeight; }
    void resized() override { label.setBounds(getLocalBounds()); }

private:
    juce::Label label;
};

// Below the Scope's animated rows, its few fixed options on the same grid:
// the overlay drawn over the beam, upsampling, and the project's scope
// timing (dwell, travel, settle) with presets for common displays. Timing
// edits are one undo step each; drags preview live and commit once.
class MotionScopePanel final : public juce::Component, private juce::ChangeListener, private juce::AudioProcessorParameter::Listener, private juce::AsyncUpdater {
public:
    explicit MotionScopePanel(MotionProcessor& owner) : processor(owner) {
        setName("Scope options");
        for (auto [label, text] : {std::pair {&displayTitle, "Display"}, std::pair {&timingTitle, "Timing"}}) {
            motion::scope::heading(*label, text);
            addAndMakeVisible(*label);
        }
        for (auto [label, text] : {std::pair {&overlayCaption, "Overlay"}, std::pair {&upsampleCaption, "Upsample"}}) { caption(*label, text); }
        auto& parameters = processor.visualiserParameters;
        overlay.setName("Scope overlay");
        overlay.setTitle("Scope overlay");
        overlay.setTooltip("Graticule or screen drawn over the beam");
        auto* screen = parameters.screenOverlay;
        for (int id = 1; id <= screen->max.load(); ++id) { overlay.addItem(screen->getText(screen->getNormalisedValue(static_cast<float>(id))), id); }
        overlay.setColour(juce::ComboBox::backgroundColourId, osci::Colours::veryDark());
        overlay.setColour(juce::ComboBox::outlineColourId, juce::Colours::transparentBlack);
        overlay.onChange = [this] {
            if (updating) { return; }
            processor.lastUndoParamId = {};
            processor.visualiserParameters.screenOverlay->setUnnormalisedValueNotifyingHost(static_cast<float>(overlay.getSelectedId()));
        };
        addAndMakeVisible(overlay);
        upsample.setName("Upsample Audio");
        upsample.setTitle("Upsample Audio");
        upsample.setTooltip(parameters.upsamplingEnabled->getDescription());
        upsample.onClick = [this] {
            processor.lastUndoParamId = {};
            processor.visualiserParameters.upsamplingEnabled->setBoolValueNotifyingHost(upsample.getToggleState());
        };
        addAndMakeVisible(upsample);
        for (std::size_t index = 0; index < fields.size(); ++index) {
            auto& field = fields[index];
            const auto& spec = specs[index];
            const auto label = juce::String(spec.label.data(), spec.label.size());
            caption(field.caption, label);
            field.editor.setSpec(spec);
            field.editor.setName("Scope " + label.toLowerCase());
            field.editor.setTitle("Scope " + label.toLowerCase());
            field.editor.setComponentID("motion.scope." + juce::String(spec.id.data(), spec.id.size()));
            field.editor.setTooltip(index == 0 ? "How long the beam stays dark at both ends of every jump"
                : index == 1 ? "Dark move time per unit of screen distance" : "Wait after each jump before drawing, for slow scopes and galvos");
            field.editor.onBegin = [this] { beginGesture(); };
            field.editor.onChange = [this, index](double value) { previewValue(index, value); };
            field.editor.onEnd = [this] { endGesture(); };
            field.editor.onCancel = [this] { refresh(); };
            field.editor.onCommit = [this, index](double value) { commitValue(index, value); };
            addAndMakeVisible(field.editor);
        }
        presets.setName("Scope presets");
        presets.setTitle("Scope presets");
        presets.setTooltip("Apply timing suited to a kind of display");
        presets.onClick = [this] { showPresets(); };
        addAndMakeVisible(presets);
        processor.document.addChangeListener(this);
        screen->addListener(this);
        parameters.upsamplingEnabled->addListener(this);
        refresh();
    }
    ~MotionScopePanel() override {
        processor.document.removeChangeListener(this);
        processor.visualiserParameters.screenOverlay->removeListener(this);
        processor.visualiserParameters.upsamplingEnabled->removeListener(this);
        cancelPendingUpdate();
    }

    static constexpr int preferredHeight() { return sectionGap + motion::scope::headingHeight + 2 * rowHeight + rowGap + sectionGap + motion::scope::headingHeight + 3 * rowHeight + 2 * rowGap; }

    void refresh() {
        if (gesture.has_value() && processor.document.revision() != gesture->revision) { gesture.reset(); }
        const auto& scope = processor.document.project().scope;
        fields[0].editor.setValue(scope.dwellMicros);
        fields[1].editor.setValue(scope.travelMicrosPerUnit);
        fields[2].editor.setValue(scope.settleMicros);
        auto& parameters = processor.visualiserParameters;
        // The realistic screens need a square canvas.
        const auto square = VisualiserGeometry::isSquare(processor.recordingParameters.getCanvasSize());
        updating = true;
        overlay.setItemEnabled(static_cast<int>(ScreenOverlay::Real), square);
        overlay.setItemEnabled(static_cast<int>(ScreenOverlay::VectorDisplay), square);
        overlay.setSelectedId(static_cast<int>(parameters.screenOverlay->getValueUnnormalised()), juce::dontSendNotification);
        upsample.setToggleState(parameters.upsamplingEnabled->getBoolValue(), juce::dontSendNotification);
        updating = false;
    }

    void resized() override {
        const motion::scope::Grid grid(getWidth());
        auto area = getLocalBounds().withTrimmedTop(sectionGap);
        const auto line = [&](juce::Label& caption) {
            auto row = area.removeFromTop(rowHeight);
            area.removeFromTop(rowGap);
            caption.setBounds(row.withWidth(grid.column + motion::style::gap + grid.column / 2));
            return row;
        };
        displayTitle.setBounds(area.removeFromTop(motion::scope::headingHeight));
        auto overlayRow = line(overlayCaption);
        // From the rows' modulate column to the values' right edge.
        const auto menuLeft = grid.value - motion::style::gap - 20;
        overlay.setBounds(overlayRow.withX(menuLeft).withRight(grid.value + grid.column));
        auto upsampleRow = line(upsampleCaption);
        // The tick box (drawn 4 px in, 14 px wide) ends where the values' text does.
        const auto tickRight = grid.value + grid.column - valueInset;
        upsample.setBounds(upsampleRow.withX(tickRight - 4 - 14).withWidth(rowHeight));
        area.removeFromTop(sectionGap - rowGap);
        auto title = area.removeFromTop(motion::scope::headingHeight);
        timingTitle.setBounds(title);
        presets.setBounds(title.withX(grid.value + grid.column - presets.idealWidth()).withWidth(presets.idealWidth()));
        for (auto& field : fields) { field.editor.setBounds(line(field.caption).withX(grid.value).withWidth(grid.column)); }
    }

private:
    // The fields' text sits this far inside their right edge.
    static constexpr int rowHeight = motion::style::controlHeight, rowGap = motion::style::gap, sectionGap = 14, valueInset = 5;
    static constexpr std::array<motion::PropertySpec, 3> specs {{
        {"dwell", "Dwell", "Scope", "", 0, motion::ScopeProfile::maximumDwellMicros, 12, 1, 1, " µs"},
        {"travel", "Travel", "Scope", "", 0, motion::ScopeProfile::maximumTravelMicrosPerUnit, 30, 1, 1, " µs/u"},
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

    void caption(juce::Label& label, const juce::String& text) {
        label.setText(text, juce::dontSendNotification);
        label.setFont(motion::style::caption());
        label.setColour(juce::Label::textColourId, osci::Colours::textMuted());
        addAndMakeVisible(label);
    }
    void changeListenerCallback(juce::ChangeBroadcaster*) override { refresh(); }
    void parameterValueChanged(int, float) override { triggerAsyncUpdate(); }
    void parameterGestureChanged(int, bool) override {}
    void handleAsyncUpdate() override { refresh(); }
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
        juce::Component::SafePointer<MotionScopePanel> safe(this);
        menu.setLookAndFeel(&getLookAndFeel());
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
    juce::Label displayTitle, timingTitle, overlayCaption, upsampleCaption;
    juce::ComboBox overlay;
    juce::ToggleButton upsample;
    std::array<Field, 3> fields;
    motion::scope::MenuLink presets {"Presets"};
    std::optional<Gesture> gesture;
    bool changed = false, updating = false;
};
