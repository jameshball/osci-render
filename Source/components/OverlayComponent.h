#pragma once

#include <JuceHeader.h>
#include "../LookAndFeel.h"
#include "SvgButton.h"

// Base class for full-editor overlay panels (semi-transparent background with
// a rounded dark panel in the centre).  Subclasses populate the content area;
// the overlay handles the backdrop, border, and dismiss callback.
class OverlayComponent : public juce::Component {
public:
    OverlayComponent() {
        setOpaque(false);
        setAlwaysOnTop(true);
        setInterceptsMouseClicks(true, true);
        // Allow keyboard focus so the Escape key handler can fire even when
        // no child has focus. Children (e.g. a license-key TextEditor) can
        // still grab focus normally.
        setWantsKeyboardFocus(true);

        configureLabel(overlayTitleLabel,
                       juce::Font(juce::FontOptions(18.0f, juce::Font::bold)),
                       juce::Justification::centredLeft);
        overlayTitleLabel.setVisible(false);
        addAndMakeVisible(overlayTitleLabel);

        closeOverlayButton.setTooltip("Close");
        closeOverlayButton.onClick = [this] { dismiss(); };
        addAndMakeVisible(closeOverlayButton);
    }

    ~OverlayComponent() override = default;

    // Called when the user requests to close the overlay.
    std::function<void()> onDismissRequested;

    // When true, the editor won't hide the visualiser or dim the background.
    bool lightweight = false;

    void setOverlayTitle(const juce::String& title) {
        overlayTitleLabel.setText(title, juce::dontSendNotification);
        overlayTitleLabel.setVisible(title.isNotEmpty());
    }

    void setDismissible(bool shouldBeDismissible) {
        dismissible = shouldBeDismissible;
        closeOverlayButton.setVisible(dismissible);
    }

    void paint(juce::Graphics& g) override {
        if (lightweight) return;
        g.fillAll(juce::Colours::black.withAlpha(0.6f));

        auto cornerRadius = 12.0f;
        auto panelFloat = panelBounds.toFloat();

        g.setColour(Colours::veryDark());
        g.fillRoundedRectangle(panelFloat, cornerRadius);

        g.setColour(Colours::accentColor().withAlpha(0.6f));
        g.drawRoundedRectangle(panelFloat.reduced(0.5f), cornerRadius, 1.5f);
    }

    void resized() override {
        auto bounds = getLocalBounds();
        auto preferred = getPreferredPanelSize();
        if (preferred.x > 0 && preferred.y > 0) {
            auto available = bounds.reduced(40, 24);
            panelBounds.setSize(juce::jmin(preferred.x, available.getWidth()),
                                juce::jmin(preferred.y, available.getHeight()));
            panelBounds.setCentre(bounds.getCentre());
        } else {
            auto horizontalMargin = juce::jmax(40, bounds.getWidth() / 6);
            auto verticalMargin = juce::jmax(24, bounds.getHeight() / 10);
            panelBounds = bounds.reduced(horizontalMargin, verticalMargin);
        }

        auto contentArea = panelBounds.reduced(24);
        if (overlayTitleLabel.isVisible() || closeOverlayButton.isVisible()) {
            auto titleArea = contentArea.removeFromTop(30);
            closeOverlayButton.setBounds(titleArea.removeFromRight(30).reduced(2));
            overlayTitleLabel.setBounds(titleArea);
            contentArea.removeFromTop(10);
        } else {
            closeOverlayButton.setBounds({});
            overlayTitleLabel.setBounds({});
        }

        resizeContent(contentArea);
    }

    void mouseDown(const juce::MouseEvent& e) override {
        // Clicking outside the panel dismisses the overlay, but only if the
        // user isn't actively editing text inside the overlay. Otherwise a
        // misclick a few pixels off the panel would discard a half-typed
        // license key.
        if (!dismissible || panelBounds.contains(e.getPosition()))
            return;

        if (auto* focused = juce::Component::getCurrentlyFocusedComponent())
            if (dynamic_cast<juce::TextEditor*>(focused) != nullptr
                && (focused == this || isParentOf(focused)))
                return;

        dismiss();
    }

    bool keyPressed(const juce::KeyPress& key) override {
        if (dismissible && key == juce::KeyPress::escapeKey) {
            dismiss();
            return true;
        }
        return juce::Component::keyPressed(key);
    }

protected:
    // Subclasses lay out their children inside contentArea.
    virtual void resizeContent(juce::Rectangle<int> contentArea) = 0;
    virtual juce::Point<int> getPreferredPanelSize() const { return {}; }

    void dismiss() {
        if (onDismissRequested)
            onDismissRequested();
    }

    juce::Rectangle<int> panelBounds;

    // Helper shared by subclasses for configuring labels.
    static void configureLabel(juce::Label& label, const juce::Font& font,
                               juce::Justification justification) {
        label.setColour(juce::Label::textColourId, juce::Colours::white);
        label.setFont(font);
        label.setJustificationType(justification);
        label.setEditable(false, false, false);
        label.setInterceptsMouseClicks(false, false);
    }

private:
    juce::Label overlayTitleLabel;
    SvgButton closeOverlayButton {
        "closeOverlay",
        juce::String::createStringFromData(BinaryData::close_svg, BinaryData::close_svgSize),
        juce::Colours::white,
        juce::Colours::white
    };
    bool dismissible = true;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OverlayComponent)
};
