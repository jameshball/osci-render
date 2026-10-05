#pragma once

#include <JuceHeader.h>
#include "MotionStyle.h"

// The keyboard and gesture reference: grouped two-column rows (keys right-
// aligned, actions left-aligned) under a filter field.
class MotionShortcutsOverlay final : public osci::OverlayComponent {
public:
    struct Entry { juce::String keys, action; };
    struct Section { juce::String title; std::vector<Entry> entries; };

    explicit MotionShortcutsOverlay(std::vector<Section> content) : sections(std::move(content)) {
        setOverlayTitle("Keyboard shortcuts");
        filter.setName("Filter shortcuts");
        filter.setTitle("Filter shortcuts");
        filter.setTextToShowWhenEmpty("Filter: type an action or a key", osci::Colours::textMuted());
        filter.onTextChange = [this] { list.setFilter(filter.getText()); resized(); };
        filter.onEscapeKey = [this] { if (filter.isEmpty()) { dismiss(); } else { filter.clear(); } };
        list.sections = &sections;
        viewport.setViewedComponent(&list, false);
        viewport.setScrollBarsShown(true, false);
        viewport.setScrollBarThickness(8);
        addPanelContentAndMakeVisible(filter);
        addPanelContentAndMakeVisible(viewport);
    }
    void visibilityChanged() override {
        if (isShowing()) { juce::MessageManager::callAsync([safe = juce::Component::SafePointer<juce::TextEditor>(&filter)] { if (safe != nullptr) { safe->grabKeyboardFocus(); } }); }
    }

protected:
    juce::Point<int> getPreferredPanelSize() const override { return {640, 620}; }
    void resizeContent(juce::Rectangle<int> area) override {
        filter.setBounds(area.removeFromTop(30));
        area.removeFromTop(10);
        viewport.setBounds(area);
        list.setSize(area.getWidth() - 10, list.contentHeight());
        viewport.setViewPosition(0, 0);
    }

private:
    struct List final : juce::Component {
        const std::vector<Section>* sections = nullptr;
        juce::String query;
        static constexpr int rowHeight = 22, headingHeight = 34, keyWidth = 190;
        void setFilter(const juce::String& text) { query = text.trim(); repaint(); }
        bool matches(const Entry& entry) const {
            return query.isEmpty() || entry.action.containsIgnoreCase(query) || entry.keys.containsIgnoreCase(query) || motion::style::shortcutText(entry.keys).containsIgnoreCase(query);
        }
        int contentHeight() const {
            int height = 0;
            for (const auto& section : *sections) {
                const auto shown = std::count_if(section.entries.begin(), section.entries.end(), [this](const Entry& entry) { return matches(entry); });
                if (shown > 0) { height += headingHeight + static_cast<int>(shown) * rowHeight; }
            }
            return std::max(height, 60);
        }
        void paint(juce::Graphics& g) override {
            int y = 0;
            bool any = false;
            for (const auto& section : *sections) {
                std::vector<const Entry*> shown;
                for (const auto& entry : section.entries) {
                    if (matches(entry)) { shown.push_back(&entry); }
                }
                if (shown.empty()) { continue; }
                any = true;
                g.setColour(osci::Colours::accentColor());
                g.setFont(motion::style::title());
                g.drawText(section.title, 0, y + 10, getWidth(), 20, juce::Justification::centredLeft);
                g.setColour(juce::Colours::white.withAlpha(.08f));
                g.fillRect(0, y + headingHeight - 3, getWidth(), 1);
                y += headingHeight;
                for (const auto* entry : shown) {
                    g.setFont(motion::style::body());
                    g.setColour(osci::Colours::text());
                    g.drawText(motion::style::shortcutText(entry->keys), 0, y, keyWidth - 16, rowHeight, juce::Justification::centredRight);
                    g.setColour(osci::Colours::text().withAlpha(.78f));
                    g.drawText(entry->action, keyWidth, y, getWidth() - keyWidth, rowHeight, juce::Justification::centredLeft);
                    y += rowHeight;
                }
            }
            if (!any) {
                g.setColour(osci::Colours::textMuted());
                g.setFont(motion::style::body());
                g.drawText("No shortcut matches \"" + query + "\".", getLocalBounds().removeFromTop(40), juce::Justification::centred);
            }
        }
    };
    std::vector<Section> sections;
    juce::TextEditor filter;
    juce::Viewport viewport;
    List list;
};
