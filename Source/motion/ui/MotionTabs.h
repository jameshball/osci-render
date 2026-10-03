#pragma once

#include "MotionStyle.h"

// Panel tabs in the title style. Labels are never squeezed: the strip keeps
// each tab's natural width and hover fades in briefly.
class MotionTabs final : public juce::TabbedButtonBar {
public:
    MotionTabs() : juce::TabbedButtonBar(juce::TabbedButtonBar::TabsAtTop) {
        setLookAndFeel(&tabLookAndFeel);
        setMinimumTabScaleFactor(1.0);
    }
    ~MotionTabs() override { setLookAndFeel(nullptr); }

    void addTab(const juce::String& label) { juce::TabbedButtonBar::addTab(label, juce::Colours::transparentBlack, -1); }
    void setSelectedIndex(int index, juce::NotificationType notification = juce::sendNotification) { setCurrentTabIndex(index, notification != juce::dontSendNotification); }
    // The horizontal space either side of each label.
    void setTabPadding(int padding) {
        tabPadding = padding;
        resized();
        repaint();
    }
    int preferredWidth() const {
        int width = 0;
        for (int index = 0; index < getNumTabs(); ++index) { width += labelWidth(getTabNames()[index]) + 2 * tabPadding; }
        return width;
    }
    std::function<void(int)> onSelectionChanged;

    void paint(juce::Graphics& g) override { osci::PanelHeader::paintBackground(g, getLocalBounds().toFloat(), motion::style::background()); }

private:
    int tabPadding = 10;
    static int labelWidth(const juce::String& text) { return juce::GlyphArrangement::getStringWidthInt(motion::style::title(), text); }

    struct TabLookAndFeel final : juce::LookAndFeel_V4 {
        void drawTabAreaBehindFrontButton(juce::TabbedButtonBar&, juce::Graphics&, int, int) override {}
        int getTabButtonOverlap(int) override { return 0; }
        int getTabButtonSpaceAroundImage() override { return 0; }
    } tabLookAndFeel;

    class Tab final : public juce::TabBarButton {
    public:
        Tab(const juce::String& label, MotionTabs& owner) : juce::TabBarButton(label, owner), tabs(owner) { setWantsKeyboardFocus(true); }

        int getBestTabLength(int) override { return labelWidth(getButtonText()) + 2 * tabs.tabPadding; }
        bool hitTest(int x, int y) override { return getLocalBounds().contains(x, y); }

        void paintButton(juce::Graphics& g, bool, bool down) override {
            const auto bounds = getLocalBounds().toFloat();
            const bool selected = isFrontTab();
            const auto hover = fade.value();
            if (hover > 0.0f && isEnabled()) {
                g.setColour(juce::Colours::white.withAlpha((down ? .09f : .05f) * hover));
                g.fillRoundedRectangle(bounds.reduced(3.0f, 4.0f), motion::style::radius);
            }
            const auto text = selected ? motion::style::text() : motion::style::muted().interpolatedWith(motion::style::text(), hover * .6f);
            g.setColour(text.withMultipliedAlpha(isEnabled() ? 1.0f : .4f));
            g.setFont(motion::style::title());
            g.drawText(getButtonText(), getLocalBounds().withTrimmedBottom(2), juce::Justification::centred, false);
            if (selected) {
                g.setColour(motion::style::accent());
                g.fillRoundedRectangle(bounds.reduced(static_cast<float>(tabs.tabPadding), 0).withTop(bounds.getBottom() - 3.0f), 1.5f);
            }
            if (hasKeyboardFocus(false) && keyboardFocus) {
                g.setColour(motion::style::accent().withAlpha(.7f));
                g.drawRoundedRectangle(bounds.reduced(3.0f, 5.0f), motion::style::radius, 1.0f);
            }
        }
        void mouseEnter(const juce::MouseEvent& event) override { juce::TabBarButton::mouseEnter(event); fade.setTarget(true); }
        void mouseExit(const juce::MouseEvent& event) override { juce::TabBarButton::mouseExit(event); fade.setTarget(false); }
        void focusGained(FocusChangeType cause) override {
            keyboardFocus = cause == focusChangedByTabKey;
            repaint();
        }
        void focusLost(FocusChangeType) override { repaint(); }
        void mouseDown(const juce::MouseEvent& event) override {
            keyboardFocus = false;
            juce::TabBarButton::mouseDown(event);
        }
        bool keyPressed(const juce::KeyPress& key) override {
            const bool previous = key == juce::KeyPress::leftKey;
            const bool next = key == juce::KeyPress::rightKey;
            if (!previous && !next) { return juce::TabBarButton::keyPressed(key); }
            const int count = tabs.getNumTabs();
            int index = getIndex();
            for (int attempt = 0; attempt < count; ++attempt) {
                index = (index + (previous ? -1 : 1) + count) % count;
                auto* tab = tabs.getTabButton(index);
                if (tab != nullptr && tab->isEnabled()) {
                    tabs.setSelectedIndex(index);
                    tab->grabKeyboardFocus();
                    break;
                }
            }
            return true;
        }

    private:
        MotionTabs& tabs;
        motion::style::Fade fade {*this};
        bool keyboardFocus = false;
    };

    juce::TabBarButton* createTabButton(const juce::String& label, int) override { return new Tab(label, *this); }
    void currentTabChanged(int index, const juce::String&) override {
        if (onSelectionChanged) { onSelectionChanged(index); }
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MotionTabs)
};
