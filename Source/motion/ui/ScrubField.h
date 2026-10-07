#pragma once

#include "MotionStyle.h"
#include "TypedNumber.h"
#include "../model/PropertySchema.h"

// A compact numeric field. Horizontal drag scrubs (Shift fine, Cmd coarse),
// double-click or Return types a value, Escape cancels. Every gesture reports
// begin/change/end so owners can preview live and commit one undo step.
namespace motion::ui {
class ScrubField final : public juce::Component, public juce::SettableTooltipClient, private juce::TextEditor::Listener {
public:
    ScrubField() {
        setWantsKeyboardFocus(true);
        setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
        editor.setFont(motion::style::body());
        editor.setJustification(juce::Justification::centredRight);
        editor.setColour(juce::TextEditor::backgroundColourId, osci::Colours::veryDark());
        editor.setColour(juce::TextEditor::outlineColourId, osci::Colours::accentColor().withAlpha(.6f));
        editor.setColour(juce::TextEditor::focusedOutlineColourId, osci::Colours::accentColor());
        editor.setIndents(4, 3);
        editor.addListener(this);
        addChildComponent(editor);
    }

    std::function<void()> onBegin, onEnd, onCancel;
    std::function<void(double)> onChange;
    // Typed values arrive as a single complete edit.
    std::function<void(double)> onCommit;

    void setSpec(const motion::PropertySpec& value) { spec = value; repaint(); }
    void setAxisColour(std::optional<juce::Colour> colour) { axisColour = colour; repaint(); }
    void setPrefix(juce::String text) { prefix = std::move(text); repaint(); }
    // How the value reads and is typed when it isn't a plain number, such as a
    // time in the ruler's format. Without it, the spec's decimals and unit apply.
    struct Notation {
        std::function<juce::String(double)> format;
        std::function<std::optional<double>(const juce::String&)> parse;
    };
    void setNotation(Notation next) { notation = std::move(next); repaint(); }
    void setValue(double next) {
        if (dragging || editor.isVisible()) { return; }
        if (value != next) { value = next; repaint(); }
    }
    double getValue() const { return value; }
    // Fields recess into the panel they sit on; sheets sit on a darker one.
    void setFill(juce::Colour colour) { fill = colour; editor.setColour(juce::TextEditor::backgroundColourId, colour); repaint(); }
    // A tint over the field: lilac when something drives the value.
    void setAccent(std::optional<juce::Colour> colour) {
        if (accent != colour) {
            accent = colour;
            repaint();
        }
    }
    bool isEditing() const { return dragging || editor.isVisible(); }

    void paint(juce::Graphics& g) override {
        const auto bounds = getLocalBounds().toFloat();
        // A driven value is tinted in the colour of what drives it.
        g.setColour(accent.has_value() ? fill.interpolatedWith(*accent, isEnabled() ? .2f : .1f) : fill);
        g.fillRoundedRectangle(bounds, motion::style::radius);
        if (hovered || dragging || hasKeyboardFocus(false)) {
            g.setColour((hasKeyboardFocus(false) ? osci::Colours::accentColor() : osci::Colours::outlineSubtle()).withAlpha(.8f));
            g.drawRoundedRectangle(bounds.reduced(.5f), motion::style::radius, 1.0f);
        }
        auto text = getLocalBounds().reduced(5, 0);
        if (axisColour.has_value() || prefix.isNotEmpty()) {
            g.setFont(motion::style::caption());
            g.setColour(axisColour.value_or(osci::Colours::textMuted()).withAlpha(isEnabled() ? 1.0f : .4f));
            g.drawText(prefix, text.removeFromLeft(9), juce::Justification::centredLeft);
        }
        g.setFont(motion::style::body());
        g.setColour(osci::Colours::text().withAlpha(isEnabled() ? 1.0f : .4f));
        g.drawText(format(value), text, juce::Justification::centredRight);
    }
    void resized() override { editor.setBounds(getLocalBounds()); }

    void mouseEnter(const juce::MouseEvent&) override { hovered = true; repaint(); }
    void mouseExit(const juce::MouseEvent&) override { hovered = false; repaint(); }
    void mouseDown(const juce::MouseEvent& event) override {
        if (!isEnabled() || !event.mods.isLeftButtonDown()) { return; }
        grabKeyboardFocus();
        startValue = value;
        lastX = event.position.x;
        accumulated = 0;
        moved = false;
        dragCancelled = false;
    }
    void mouseDrag(const juce::MouseEvent& event) override {
        if (!isEnabled() || !event.mods.isLeftButtonDown() || dragCancelled) { return; }
        const auto dx = event.position.x - lastX;
        lastX = event.position.x;
        if (!moved && std::abs(event.getDistanceFromDragStartX()) < 3) { return; }
        if (!moved) {
            moved = true;
            dragging = true;
            if (onBegin) { onBegin(); }
        }
        const auto scale = stepScale(event.mods);
        accumulated += dx * spec.step * scale;
        const auto next = spec.clamp(round(startValue + accumulated));
        if (next != value) {
            value = next;
            repaint();
            if (onChange) { onChange(value); }
        }
    }
    void mouseUp(const juce::MouseEvent&) override {
        if (dragging) {
            dragging = false;
            if (onEnd) { onEnd(); }
        }
    }
    void mouseDoubleClick(const juce::MouseEvent&) override { beginTyping(); }
    bool keyPressed(const juce::KeyPress& key) override {
        if (key == juce::KeyPress::returnKey) { beginTyping(); return true; }
        if (key == juce::KeyPress::escapeKey && dragging) {
            // Escape mid-drag puts the value back; the rest of the drag is ignored.
            dragging = false;
            dragCancelled = true;
            value = startValue;
            repaint();
            if (onCancel) { onCancel(); }
            return true;
        }
        if (key.getKeyCode() == juce::KeyPress::upKey || key.getKeyCode() == juce::KeyPress::downKey) {
            const auto direction = key.getKeyCode() == juce::KeyPress::upKey ? 1.0 : -1.0;
            const auto scale = stepScale(key.getModifiers());
            commit(spec.clamp(round(value + direction * spec.step * scale)));
            return true;
        }
        return false;
    }
    // Assistive technology and automation read the formatted value and set
    // it as one complete, validated edit.
    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override {
        struct Value final : juce::AccessibilityTextValueInterface {
            explicit Value(ScrubField& owner) : owner(owner) {}
            bool isReadOnly() const override { return !owner.isEnabled(); }
            juce::String getCurrentValueAsString() const override { return owner.format(owner.value, false); }
            void setValueAsString(const juce::String& text) override {
                const auto parsed = owner.parse(text);
                if (parsed.has_value()) { owner.commit(owner.spec.clamp(*parsed)); }
            }
            ScrubField& owner;
        };
        return std::make_unique<juce::AccessibilityHandler>(*this, juce::AccessibilityRole::editableText, juce::AccessibilityActions(),
            juce::AccessibilityHandler::Interfaces{std::make_unique<Value>(*this)});
    }
    void beginTyping() {
        if (!isEnabled()) { return; }
        editor.setText(format(value, false), juce::dontSendNotification);
        editor.setVisible(true);
        editor.grabKeyboardFocus();
        editor.selectAll();
    }

private:
    // Dragging and arrow keys alike: Shift for fine steps, Cmd for coarse.
    static double stepScale(const juce::ModifierKeys& mods) { return mods.isShiftDown() ? .1 : mods.isCommandDown() ? 10.0 : 1.0; }
    void textEditorReturnKeyPressed(juce::TextEditor&) override { finishTyping(true); }
    void textEditorEscapeKeyPressed(juce::TextEditor&) override { finishTyping(false); }
    void textEditorFocusLost(juce::TextEditor&) override { finishTyping(true); }
    void finishTyping(bool accept) {
        if (!editor.isVisible()) { return; }
        const auto parsed = parse(editor.getText());
        editor.setVisible(false);
        if (!accept) { if (onCancel) { onCancel(); } return; }
        if (!parsed.has_value()) { repaint(); return; }
        commit(spec.clamp(*parsed));
    }
    void commit(double next) {
        value = next;
        repaint();
        if (onCommit) { onCommit(next); }
    }
    double round(double raw) const {
        const auto quantum = std::pow(10.0, -spec.decimals);
        return std::round(raw / quantum) * quantum;
    }
    std::optional<double> parse(const juce::String& text) const { return notation.parse ? notation.parse(text) : parseNumber(text, "°"); }
    juce::String format(double number, bool withUnit = true) const {
        if (notation.format) { return notation.format(number); }
        auto text = juce::String(number, spec.decimals);
        if (text == "-" + juce::String(0.0, spec.decimals)) { text = text.substring(1); }
        return withUnit ? text + juce::String(juce::CharPointer_UTF8(spec.unit.data()), spec.unit.size()) : text;
    }

    motion::PropertySpec spec {"", "", "", "", -motion::unbounded, motion::unbounded, 0, .01, 2, ""};
    juce::TextEditor editor;
    std::optional<juce::Colour> axisColour;
    juce::String prefix;
    Notation notation;
    juce::Colour fill = osci::Colours::veryDark();
    std::optional<juce::Colour> accent;
    double value = 0, startValue = 0, accumulated = 0;
    float lastX = 0;
    bool hovered = false, dragging = false, moved = false, dragCancelled = false;
};
}
