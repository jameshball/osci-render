#pragma once

#include "MotionStyle.h"
#include "FormControls.h"
#include "MotionIcons.h"
#include <osci_gui/osci_gui.h>

namespace motion::ui {
// The inside of a modal sheet: a title on the left (with the file it is
// about, muted, beside it), a form, and a footer with Cancel and the one
// action the sheet exists for. The overlay supplies the panel, the dimmed
// window behind it and the close button, which lines up with the title.
class Sheet : public juce::Component, private juce::KeyListener {
public:
    Sheet(const juce::String& heading, const juce::String& about, const juce::String& verb) : title(heading), subtitle(about) {
        setName(heading);
        getProperties().set(style::sheetProperty, true);
        setWantsKeyboardFocus(true);
        primary.setButtonText(verb);
        nameAction(verb);
        style::makePrimary(primary);
        cancel.setButtonText("Cancel");
        cancel.setName("Cancel");
        cancel.onClick = [this] { close(); };
        closeButton.iconSize = 16.0f;
        closeButton.setTooltip("Close (Esc)");
        closeButton.onClick = [this] { close(); };
        addAndMakeVisible(closeButton);
        addAndMakeVisible(cancel);
        addAndMakeVisible(primary);
    }

    static constexpr int headerHeight = 28, headerGap = 16, footerHeight = 28, footerGap = 20;
    // Choices take the control width; numbers a shorter, right-aligned field.
    static constexpr int row = 28, rowGap = 8, caption = 120, control = 168, number = 96;

    // Shows the sheet over `owner`; returns the overlay so callers can wire
    // its dismissal.
    static osci::ComponentOverlay* show(juce::Component& owner, std::unique_ptr<Sheet> sheet) {
        const auto size = juce::Point<int>(sheet->getWidth(), sheet->getHeight());
        auto overlay = std::make_unique<osci::ComponentOverlay>(std::move(sheet), juce::String(), size, false);
        auto* raw = overlay.get();
        osci::OverlayComponent::show(owner, std::move(overlay));
        return raw;
    }
    // Asks the overlay around the sheet to close (Cancel, or after the action).
    void close() {
        auto* overlay = findParentComponentOfClass<osci::OverlayComponent>();
        if (overlay != nullptr) { overlay->requestDismiss(); }
    }
    // The height a body of `rows` form rows (plus `extra` pixels) needs.
    static int bodyHeight(int rows) { return rows * row + std::max(0, rows - 1) * rowGap; }
    static int heightFor(int rows, int extra = 0) { return headerHeight + headerGap + bodyHeight(rows) + extra + footerGap + footerHeight; }
    // A form of fixed-width controls, optionally beside a square preview.
    static int widthFor(int preview = 0) { return (preview > 0 ? preview + 24 : 0) + caption + control; }

    // The sheet closes itself (its own close button, Cancel and Escape), so
    // the overlay's larger close button and click-outside dismissal go.
    // The overlay holds the keyboard while it animates in, so the sheet
    // listens to its keys too.
    void parentHierarchyChanged() override {
        auto* overlay = findParentComponentOfClass<osci::OverlayComponent>();
        if (overlay == host) { return; }
        if (host != nullptr) { host->removeKeyListener(this); }
        host = overlay;
        if (host == nullptr) { return; }
        host->setDismissible(false);
        host->addKeyListener(this);
    }
    ~Sheet() override {
        if (host != nullptr) { host->removeKeyListener(this); }
    }

    void paint(juce::Graphics& g) override {
        auto header = getLocalBounds().removeFromTop(headerHeight).withTrimmedRight(32);
        g.setFont(style::title());
        g.setColour(osci::Colours::text());
        const auto titleWidth = juce::roundToInt(juce::TextLayout::getStringWidth(style::title(), title)) + 1;
        g.drawText(title, header.removeFromLeft(titleWidth), juce::Justification::centredLeft, false);
        if (subtitle.isNotEmpty()) {
            header.removeFromLeft(8);
            g.setFont(style::body());
            g.setColour(osci::Colours::textMuted().withAlpha(.7f));
            g.drawText(subtitle, header, juce::Justification::centredLeft, true);
        }
        if (!footer) { return; }
        g.setColour(juce::Colours::white.withAlpha(.07f));
        g.fillRect(0, getHeight() - footerHeight - footerGap / 2 - 1, getWidth(), 1);
    }
    void resized() override {
        auto area = getLocalBounds();
        closeButton.setBounds(area.withHeight(headerHeight).removeFromRight(24).withSizeKeepingCentre(24, 24).translated(4, 0));
        if (footer) {
            auto line = area.removeFromBottom(footerHeight);
            primary.setBounds(line.removeFromRight(std::max(96, primary.getBestWidthForHeight(footerHeight) + 16)));
            line.removeFromRight(8);
            cancel.setBounds(line.removeFromRight(84));
            footerLeft = line;
            area.removeFromBottom(footerGap);
        }
        area.removeFromTop(headerHeight + headerGap);
        layoutBody(area);
    }
    bool keyPressed(const juce::KeyPress& key, juce::Component*) override { return keyPressed(key); }
    bool keyPressed(const juce::KeyPress& key) override {
        if (key == juce::KeyPress::escapeKey) {
            close();
            return true;
        }
        if (key.getKeyCode() == juce::KeyPress::returnKey && primary.isEnabled()) {
            primary.triggerClick();
            return true;
        }
        return false;
    }

    // A caption beside its control, both on one form row.
    static void formRow(juce::Rectangle<int>& area, juce::Label& label, juce::Component& field, int width = control) {
        auto line = area.removeFromTop(row);
        label.setBounds(line.removeFromLeft(caption));
        field.setBounds(width > 0 ? line.removeFromLeft(width) : line);
        area.removeFromTop(rowGap);
    }
    static void styleCaption(juce::Label& label) {
        label.setFont(style::body());
        label.setColour(juce::Label::textColourId, osci::Colours::textMuted());
        label.setBorderSize({});
        label.setJustificationType(juce::Justification::centredLeft);
    }
    // Fields recess into the sheet's darker panel.
    static juce::Colour fieldFill() { return osci::Colours::surfaceSunken(); }

protected:
    virtual void layoutBody(juce::Rectangle<int> area) = 0;
    // What automation and screen readers call the primary button.
    void nameAction(const juce::String& name) {
        primary.setName(name);
        primary.setTitle(name);
    }
    juce::TextButton primary, cancel;
    // Sheets that only show something (a reference) have no footer.
    void removeFooter() {
        footer = false;
        primary.setVisible(false);
        cancel.setVisible(false);
    }
    // The footer's free space left of the buttons, for a status line.
    juce::Rectangle<int> footerLeft;
    bool footer = true;
    juce::Component::SafePointer<osci::OverlayComponent> host;
    icons::Button closeButton {"Close", icons::Icon::close};

private:
    juce::String title, subtitle;
};
}

namespace motion::ui {
// The inside of a popover that edits what it points at: a short muted title,
// a compact form, and the one action bottom-right with any problem beside
// it. Escape (or a click elsewhere) closes it without applying.
class Popover : public juce::Component {
public:
    Popover(const juce::String& heading, const juce::String& verb, const juce::String& actionName) : title(heading) {
        setName(heading);
        primary.setButtonText(verb);
        primary.setName(actionName);
        primary.setTitle(actionName);
        style::makePrimary(primary);
        status.setFont(style::caption());
        status.setColour(juce::Label::textColourId, style::error());
        status.setBorderSize({});
        status.setJustificationType(juce::Justification::centredLeft);
        addAndMakeVisible(primary);
        addAndMakeVisible(status);
    }

    static constexpr int titleHeight = 18, titleGap = 10, row = 26, rowGap = 6, caption = 64, footerHeight = 26, footerGap = 12;
    static int heightFor(int rows, int extra = 0) { return titleHeight + titleGap + rows * row + std::max(0, rows - 1) * rowGap + extra + footerGap + footerHeight; }

    void setError(const juce::String& message) { status.setText(message, juce::dontSendNotification); }

    void paint(juce::Graphics& g) override {
        auto heading = getLocalBounds().removeFromTop(titleHeight);
        g.setFont(style::heading());
        g.setColour(osci::Colours::textMuted());
        g.drawText(title, heading, juce::Justification::centredLeft, true);
        if (detail.isNotEmpty()) {
            g.setFont(style::caption());
            g.setColour(osci::Colours::textMuted().withAlpha(.6f));
            g.drawText(detail, heading, juce::Justification::centredRight, true);
        }
    }
    void resized() override {
        auto area = getLocalBounds();
        auto footer = area.removeFromBottom(footerHeight);
        primary.setBounds(footer.removeFromRight(std::max(72, primary.getBestWidthForHeight(footerHeight) + 12)));
        footer.removeFromRight(8);
        status.setBounds(footer);
        area.removeFromBottom(footerGap);
        area.removeFromTop(titleHeight + titleGap);
        layoutBody(area);
    }
    static void formRow(juce::Rectangle<int>& area, juce::Label& label, juce::Component& field, int width = 0) {
        auto line = area.removeFromTop(row);
        label.setBounds(line.removeFromLeft(caption));
        field.setBounds(width > 0 ? line.removeFromLeft(width) : line);
        area.removeFromTop(rowGap);
    }

protected:
    virtual void layoutBody(juce::Rectangle<int> area) = 0;
    juce::TextButton primary;
    juce::Label status;
    // Muted text at the title's right, such as where a tempo change sits.
    juce::String detail;

private:
    juce::String title;
};
}

namespace motion::ui {
// A message that needs acknowledging (a project that did not open), or a
// question with a destructive answer (Remove, beside Cancel).
class MessageSheet final : public Sheet {
public:
    MessageSheet(const juce::String& heading, const juce::String& message, const juce::String& verb, std::function<void()> confirmed = {})
        : Sheet(heading, {}, verb), onConfirm(std::move(confirmed)) {
        nameAction(verb);
        cancel.setVisible(onConfirm != nullptr);
        text.setText(message, juce::dontSendNotification);
        text.setFont(style::body());
        text.setColour(juce::Label::textColourId, osci::Colours::text().withAlpha(.85f));
        text.setJustificationType(juce::Justification::topLeft);
        text.setBorderSize({});
        addAndMakeVisible(text);
        primary.onClick = [this] {
            auto confirm = onConfirm;
            close();
            if (confirm) { confirm(); }
        };
        const auto width = 380;
        const auto lines = juce::jmax(1, juce::roundToInt(std::ceil(juce::TextLayout::getStringWidth(style::body(), message) / (width - 8.0f))) + message.retainCharacters("\n").length());
        setSize(width, headerHeight + headerGap + lines * 18 + footerGap + footerHeight);
    }
    static void show(juce::Component& owner, const juce::String& heading, const juce::String& message, const juce::String& verb = "OK", std::function<void()> confirmed = {}) {
        auto sheet = std::make_unique<MessageSheet>(heading, message, verb, std::move(confirmed));
        const auto size = juce::Point<int>(sheet->getWidth(), sheet->getHeight());
        osci::OverlayComponent::show(owner, std::make_unique<osci::ComponentOverlay>(std::move(sheet), juce::String(), size, false));
    }

protected:
    void layoutBody(juce::Rectangle<int> area) override { text.setBounds(area); }

private:
    std::function<void()> onConfirm;
    juce::Label text;
};

// Work the user waits on (opening a project, preparing a video): what is
// happening, a slim progress bar, and Cancel.
class ProgressSheet final : public Sheet, private juce::Timer {
public:
    ProgressSheet(const juce::String& heading, const juce::String& about, const juce::String& message, std::function<double()> readProgress, std::function<void()> cancelWork, const juce::String& cancelName)
        : Sheet(heading, about, {}), progress(std::move(readProgress)), stop(std::move(cancelWork)) {
        primary.setVisible(false);
        cancel.setName(cancelName);
        cancel.setTitle(cancelName);
        cancel.onClick = [this] {
            // Stopping may close the sheet, destroying this button.
            const juce::Component::SafePointer<ProgressSheet> safe(this);
            if (stop) { stop(); }
            if (safe == nullptr) { return; }
            cancel.setEnabled(false);
            status.setText("Cancelling...", juce::dontSendNotification);
        };
        // Closing means cancelling: the close button and Escape do the same.
        closeButton.onClick = [this] { if (cancel.isEnabled()) { cancel.triggerClick(); } };
        status.setText(message, juce::dontSendNotification);
        status.setFont(style::body());
        status.setColour(juce::Label::textColourId, osci::Colours::textMuted());
        status.setBorderSize({});
        addAndMakeVisible(status);
        setSize(380, heightFor(1, 14));
        if (progress) { startTimerHz(20); }
    }

    void paint(juce::Graphics& g) override {
        Sheet::paint(g);
        // A thin track that fills in the accent; without a measure it waits.
        if (progress) {
            g.setColour(juce::Colours::white.withAlpha(.08f));
            g.fillRoundedRectangle(bar, bar.getHeight() * .5f);
            g.setColour(osci::Colours::accentColor().withAlpha(.8f));
            g.fillRoundedRectangle(bar.withWidth(bar.getWidth() * static_cast<float>(std::clamp(shown, 0.0, 1.0))), bar.getHeight() * .5f);
        }
    }
    bool keyPressed(const juce::KeyPress& key) override {
        if (key == juce::KeyPress::escapeKey) {
            if (cancel.isEnabled()) { cancel.triggerClick(); }
            return true;
        }
        return false;
    }
    void resized() override {
        Sheet::resized();
        // Cancel takes the primary's place on the right.
        cancel.setBounds(cancel.getBounds().withX(getWidth() - cancel.getWidth()));
    }

protected:
    void layoutBody(juce::Rectangle<int> area) override {
        status.setBounds(area.removeFromTop(row));
        area.removeFromTop(6);
        bar = area.removeFromTop(4).toFloat();
    }

private:
    void timerCallback() override {
        const auto next = progress();
        if (std::abs(next - shown) > 1.0e-4) {
            shown = next;
            repaint();
        }
    }
    std::function<double()> progress;
    std::function<void()> stop;
    double shown = 0;
    juce::Rectangle<float> bar;
    juce::Label status;
};
}
