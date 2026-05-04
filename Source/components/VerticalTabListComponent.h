#pragma once

#include <JuceHeader.h>

// A generic tab list component that lays out tabs vertically or horizontally.
// Manages N tabs with equal size and optional gaps.
// Tracks active tab index and notifies via callback.
// Use inside a osci::ScrollFadeViewport when tabs may exceed available space.
class VerticalTabListComponent : public juce::Component {
public:
    enum Orientation { Vertical, Horizontal };

    // Base class for individual tab items. Subclass this and override paint/resized.
    class Tab : public juce::Component {
    public:
        virtual ~Tab() = default;

        // Whether this tab is currently the active (selected) tab.
        bool isActiveTab() const { return activeState; }

        // Call from mouseDown (or any trigger) to request that this tab become active.
        void requestActivation() {
            if (activationCallback) activationCallback();
        }

    protected:
        // Called when this tab's active state changes.  Override for custom behavior.
        virtual void activeStateChanged() {}

    private:
        friend class VerticalTabListComponent;
        bool activeState = false;
        std::function<void()> activationCallback;
    };

    void addTab(std::unique_ptr<Tab> tab) {
        int index = tabs.size();
        tab->activationCallback = [this, index]() { setActiveTabIndex(index); };
        auto* rawPtr = tab.get();
        tabs.add(tab.release());
        addAndMakeVisible(rawPtr);
    }

    void setActiveTabIndex(int index) {
        if (index < 0 || index >= tabs.size()) return;
        if (index == activeIndex) return;
        if (activeIndex >= 0 && activeIndex < tabs.size()) {
            tabs[activeIndex]->activeState = false;
            tabs[activeIndex]->activeStateChanged();
            tabs[activeIndex]->repaint();
        }
        activeIndex = index;
        tabs[activeIndex]->activeState = true;
        tabs[activeIndex]->activeStateChanged();
        tabs[activeIndex]->repaint();
        if (onTabChanged) onTabChanged(activeIndex);
    }

    // Set active tab without triggering the onTabChanged callback.
    void setActiveTabIndexSilent(int index) {
        if (index < 0 || index >= tabs.size()) return;
        if (activeIndex >= 0 && activeIndex < tabs.size()) {
            tabs[activeIndex]->activeState = false;
            tabs[activeIndex]->activeStateChanged();
            tabs[activeIndex]->repaint();
        }
        activeIndex = index;
        tabs[activeIndex]->activeState = true;
        tabs[activeIndex]->activeStateChanged();
        tabs[activeIndex]->repaint();
    }

    int getActiveTabIndex() const { return activeIndex; }
    int getNumTabs() const { return tabs.size(); }

    Tab* getTab(int index) {
        if (index < 0 || index >= tabs.size()) return nullptr;
        return tabs[index];
    }

    void setTabGap(int gap) { tabGap = gap; }
    void setMinTabSize(int s) { minTabSize = s; }
    // Legacy alias for vertical mode
    void setMinTabHeight(int h) { minTabSize = h; }

    void setOrientation(Orientation o) { orientation = o; }
    Orientation getOrientation() const { return orientation; }

    // Total size needed along the primary axis if every tab gets at least minTabSize.
    int getRequiredSize() const {
        int n = tabs.size();
        if (n == 0) return 0;
        return n * minTabSize + (n - 1) * tabGap;
    }
    // Legacy alias
    int getRequiredHeight() const { return getRequiredSize(); }

    // Callback when active tab changes via user interaction.
    std::function<void(int newIndex)> onTabChanged;

    void resized() override {
        int n = tabs.size();
        if (n == 0) return;

        bool horiz = (orientation == Horizontal);
        int totalGaps = tabGap * (n - 1);
        int availSize = (horiz ? getWidth() : getHeight()) - totalGaps;
        int tabSize = juce::jmax(minTabSize, availSize / n);
        int extra = availSize - tabSize * n;

        int pos = 0;
        for (int i = 0; i < n; ++i) {
            int s = tabSize + (i < extra ? 1 : 0);
            if (horiz)
                tabs[i]->setBounds(pos, 0, s, getHeight());
            else
                tabs[i]->setBounds(0, pos, getWidth(), s);
            pos += s + tabGap;
        }
    }

private:
    juce::OwnedArray<Tab> tabs;
    int activeIndex = 0;
    int tabGap = 1;
    int minTabSize = 30;
    Orientation orientation = Vertical;
};
