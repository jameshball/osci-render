#pragma once

#include "DocumentMenu.h"
#include "MotionStyle.h"
#include "PreviewGesture.h"

#include "../MotionProcessor.h"
#include "ScrubField.h"
#include "FormControls.h"

namespace motion::scope {

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
        // The dropdowns' stroked chevron.
        const auto arrow = area.removeFromRight(caret).toFloat().withSizeKeepingCentre(7.0f, 3.5f);
        juce::Path path;
        path.startNewSubPath(arrow.getX(), arrow.getY());
        path.lineTo(arrow.getCentreX(), arrow.getBottom());
        path.lineTo(arrow.getRight(), arrow.getY());
        g.setColour(colour);
        g.strokePath(path, juce::PathStrokeType(1.3f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setFont(motion::style::caption());
        g.drawText(getButtonText(), area.withTrimmedRight(4), juce::Justification::centredRight, false);
    }
private:
    static constexpr int caret = 8;
};
}

// Below the Scope's animated rows, its few fixed options on the same grid:
// the overlay drawn over the beam, upsampling, whether the display has a
// brightness input or is XY only, and the project's scope timing (dwell,
// travel, settle) with presets for common displays. Timing edits are one
// undo step each; drags preview live and commit once.
class MotionScopePanel final : public juce::Component, private juce::ChangeListener, private juce::AudioProcessorParameter::Listener, private juce::AsyncUpdater {
public:
    explicit MotionScopePanel(MotionProcessor& owner) : processor(owner) {
        setName("Scope options");
        for (auto [label, text] : {std::pair {&displayTitle, "Display"}, std::pair {&timingTitle, "Timing"}}) {
            motion::style::inspector::styleHeading(*label, text);
            addAndMakeVisible(*label);
        }
        for (auto [label, text] : {std::pair {&overlayCaption, "Overlay"}, std::pair {&upsampleCaption, "Upsample"}, std::pair {&xyCaption, "XY only"}}) { caption(*label, text); }
        xyOnly.setName("Scope XY only");
        xyOnly.setTitle("Scope XY only");
        xyOnly.setTooltip("The scope has no brightness input, only X and Y: the beam never waits dark (it would draw dots), "
            "and how bright a line is shows by how long the beam takes over it");
        xyOnly.onClick = [this] { setXYOnly(xyOnly.getToggleState()); };
        addAndMakeVisible(xyOnly);
        auto& parameters = processor.visualiserParameters;
        overlay.setName("Scope overlay");
        overlay.setTitle("Scope overlay");
        overlay.setTooltip("Graticule or screen drawn over the beam");
        auto* screen = parameters.screenOverlay;
        // The shared parameter calls no overlay "Empty"; here it is "None".
        for (int id = 1; id <= screen->max.load(); ++id) { overlay.addItem(id == static_cast<int>(ScreenOverlay::Empty) ? juce::String("None") : screen->getText(screen->getNormalisedValue(static_cast<float>(id))), id); }
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
            field.editor.onBegin = [this] { gesture.begin(); };
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

    // Two sections, each opened like Properties' own: a hairline across the
    // panel, the heading, then rows on the grid.
    static constexpr int rowHeight = motion::style::controlHeight, rowGap = motion::style::gap;
    static constexpr int hairline = motion::style::gap, headingTop = 10, headingHeight = 26, headingBlock = headingHeight + motion::style::gap, sectionEnd = motion::style::padding + motion::style::gap;
    // Each section has three rows: Overlay, Upsample and XY only; Dwell, Travel and Settle.
    static constexpr int rowsPerSection = 3;
    static constexpr int preferredHeight() { return hairline + 2 * (headingBlock + rowsPerSection * rowHeight + (rowsPerSection - 1) * rowGap) + sectionEnd; }
    void paint(juce::Graphics& g) override {
        g.setColour(juce::Colours::white.withAlpha(.06f));
        for (const auto y : rules) { g.fillRect(0, y, getWidth(), 1); }
    }

    void refresh() {
        if (gesture.stale()) { gesture.reset(); }
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
        xyOnly.setToggleState(scope.xyOnly, juce::dontSendNotification);
        // With no brightness input the beam never waits dark: only its
        // travel between strokes means anything.
        for (const auto index : {std::size_t {0}, std::size_t {2}}) {
            fields[index].editor.setEnabled(!scope.xyOnly);
            fields[index].caption.setAlpha(scope.xyOnly ? .4f : 1.0f);
        }
        updating = false;
    }

    void resized() override {
        // The panel spans the inspector; its rows keep Properties' inset.
        auto area = getLocalBounds().reduced(motion::style::padding, 0);
        const motion::style::PropertyGrid grid(area.getWidth());
        const auto line = [&](juce::Label& caption) {
            auto row = area.removeFromTop(rowHeight);
            area.removeFromTop(rowGap);
            caption.setBounds(row.withWidth(grid.column + motion::style::gap + grid.column / 2));
            return row;
        };
        const auto section = [&](juce::Label& title, std::size_t index) {
            area.removeFromTop(index == 0 ? hairline : sectionEnd - rowGap);
            rules[index] = area.getY();
            auto heading = area.removeFromTop(headingBlock);
            title.setBounds(heading.withTrimmedTop(headingTop).withHeight(headingHeight - headingTop));
            return heading;
        };
        section(displayTitle, 0);
        auto overlayRow = line(overlayCaption);
        // Choices span the value columns, ending where the values do.
        overlay.setBounds(overlayRow.withX(area.getX() + grid.column + motion::style::gap).withRight(area.getX() + grid.value + grid.column));
        auto upsampleRow = line(upsampleCaption);
        upsample.setBounds(upsampleRow.withX(area.getX() + grid.value + grid.column - motion::ui::Switch::width - 2).withWidth(motion::ui::Switch::width + 2));
        auto xyRow = line(xyCaption);
        xyOnly.setBounds(xyRow.withX(area.getX() + grid.value + grid.column - motion::ui::Switch::width - 2).withWidth(motion::ui::Switch::width + 2));
        auto title = section(timingTitle, 1);
        presets.setBounds(title.withTrimmedTop(headingTop).withHeight(headingHeight - headingTop).withX(area.getX() + grid.value + grid.column - presets.idealWidth()).withWidth(presets.idealWidth()));
        for (auto& field : fields) { field.editor.setBounds(line(field.caption).withX(area.getX() + grid.value).withWidth(grid.column)); }
        repaint();
    }

private:
    static constexpr auto specs = std::to_array<motion::PropertySpec>({
        {"dwell", "Dwell", "Scope", "", 0, motion::ScopeProfile::maximumDwellMicros, 12, 1, 1, " µs"},
        {"travel", "Travel", "Scope", "", 0, motion::ScopeProfile::maximumTravelMicrosPerUnit, 30, 1, 1, " µs/u"},
        {"settle", "Settle", "Scope", "", 0, motion::ScopeProfile::maximumSettleMicros, 0, 1, 1, " µs"},
    });
    struct Field {
        juce::Label caption;
        motion::ui::ScrubField editor;
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
    void previewValue(std::size_t index, double value) {
        gesture.preview([index, value](motion::Project& project) { apply(project.scope, index, value); });
    }
    void endGesture() {
        gesture.commit("Change scope timing");
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
            auto profile = preset.profile;
            profile.xyOnly = current.xyOnly;
            menu.addItem(static_cast<int>(index + 1), juce::String(preset.name.data(), preset.name.size()), true, profile == current);
        }
        motion::ui::showDocumentMenu(menu, *this, processor.document, juce::PopupMenu::Options().withTargetComponent(&presets), [this](int result) {
            applyPreset(motion::scopeProfilePresets[static_cast<std::size_t>(result - 1)]);
        });
    }
    void setXYOnly(bool on) {
        if (processor.document.project().scope.xyOnly == on) { return; }
        processor.document.edit(on ? "Set the scope to XY only" : "Give the scope brightness", [on](motion::Project& project) { project.scope.xyOnly = on; });
    }
    void applyPreset(const motion::ScopeProfilePreset& preset) {
        // Timing alone: whether the display is XY only stays as it is.
        auto profile = preset.profile;
        profile.xyOnly = processor.document.project().scope.xyOnly;
        if (profile == processor.document.project().scope) { return; }
        processor.document.edit("Apply " + juce::String(preset.name.data(), preset.name.size()) + " scope timing", [profile](motion::Project& project) { project.scope = profile; });
    }

    MotionProcessor& processor;
    juce::Label displayTitle, timingTitle, overlayCaption, upsampleCaption, xyCaption;
    juce::ComboBox overlay;
    motion::ui::Switch upsample {"Upsample Audio"}, xyOnly {"Scope XY only"};
    std::array<int, 2> rules {};
    std::array<Field, 3> fields;
    motion::scope::MenuLink presets {"Presets"};
    motion::ui::PreviewGesture gesture {processor.document};
    bool updating = false;
};
