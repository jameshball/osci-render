#pragma once

#include <JuceHeader.h>
#include "Sheet.h"

// The keyboard and gesture reference: sections of actions, each with its
// keys as caps on the right, under a search field.
class MotionShortcutsOverlay final : public motion::ui::Sheet {
public:
    struct Entry { juce::String keys, action; };
    struct Section { juce::String title; std::vector<Entry> entries; };

    explicit MotionShortcutsOverlay(std::vector<Section> content) : Sheet("Keyboard shortcuts", {}, {}), sections(std::move(content)) {
        removeFooter();
        filter.setName("Filter shortcuts");
        filter.setTitle("Filter shortcuts");
        filter.setTextToShowWhenEmpty("Search actions or keys", osci::Colours::textMuted().withAlpha(.6f));
        filter.setFont(motion::style::body());
        filter.onTextChange = [this] { list.setFilter(filter.getText()); resized(); };
        filter.onEscapeKey = [this] { if (filter.isEmpty()) { close(); } else { filter.clear(); } };
        list.sections = &sections;
        viewport.setViewedComponent(&list, false);
        viewport.setScrollBarsShown(true, false);
        viewport.setScrollBarThickness(6);
        addAndMakeVisible(filter);
        addAndMakeVisible(viewport);
        setSize(560, 600);
    }
    // Show the reference in a sheet over `owner`.
    static void show(juce::Component& owner, std::vector<Section> content) {
        auto sheet = std::make_unique<MotionShortcutsOverlay>(std::move(content));
        const auto size = juce::Point<int>(sheet->getWidth(), sheet->getHeight());
        osci::OverlayComponent::show(owner, std::make_unique<osci::ComponentOverlay>(std::move(sheet), juce::String(), size, false));
    }
    void visibilityChanged() override {
        if (isShowing()) { juce::MessageManager::callAsync([safe = juce::Component::SafePointer<juce::TextEditor>(&filter)] { if (safe != nullptr) { safe->grabKeyboardFocus(); } }); }
    }

protected:
    void layoutBody(juce::Rectangle<int> area) override {
        // The search field ends where the keycaps do, clear of the scrollbar.
        constexpr int scrollGutter = 10;
        filter.setBounds(area.removeFromTop(row).withTrimmedRight(scrollGutter));
        area.removeFromTop(8);
        viewport.setBounds(area);
        list.setSize(area.getWidth() - scrollGutter, list.contentHeight());
        viewport.setViewPosition(0, 0);
    }

private:
    struct List final : juce::Component {
        const std::vector<Section>* sections = nullptr;
        juce::String query;
        static constexpr int rowHeight = 24, headingHeight = 32;
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
        // Keys as caps, right-aligned: a shortcut's glyphs share one cap,
        // a gesture's words sit in plain text.
        static void paintKeys(juce::Graphics& g, const juce::String& keys, juce::Rectangle<int> area) {
            // Arrow keys read as arrows, like the other glyphs.
            auto text = motion::style::shortcutText(keys);
            for (const auto& [word, arrow] : std::initializer_list<std::pair<const char*, const char*>> {{"Left", "\u2190"}, {"Right", "\u2192"}, {"Up", "\u2191"}, {"Down", "\u2193"}}) {
                if (text.endsWith(word) && !text.contains(" ")) { text = text.dropLastCharacters(juce::String(word).length()) + juce::String::fromUTF8(arrow); }
            }
            g.setFont(motion::style::body());
            const auto width = juce::roundToInt(juce::TextLayout::getStringWidth(motion::style::body(), text)) + 12;
            const auto cap = area.removeFromRight(width).withSizeKeepingCentre(width, 18).toFloat();
            g.setColour(osci::Colours::surfaceRaised().brighter(.08f));
            g.fillRoundedRectangle(cap, motion::style::radius);
            g.setColour(juce::Colours::white.withAlpha(.06f));
            g.drawRoundedRectangle(cap.reduced(.5f), motion::style::radius, 1.0f);
            g.setColour(osci::Colours::text());
            g.drawText(text, cap, juce::Justification::centred, false);
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
                // Sections break the list; rows within one need no rules.
                if (y > 0) {
                    g.setColour(juce::Colours::white.withAlpha(.07f));
                    g.fillRect(0, y + 4, getWidth(), 1);
                }
                g.setColour(osci::Colours::text());
                g.setFont(motion::style::heading());
                g.drawText(section.title, 0, y + 12, getWidth(), 16, juce::Justification::centredLeft);
                y += headingHeight;
                for (const auto* entry : shown) {
                    g.setFont(motion::style::body());
                    g.setColour(osci::Colours::text().withAlpha(.85f));
                    g.drawText(entry->action, 0, y, getWidth() - 160, rowHeight, juce::Justification::centredLeft, true);
                    paintKeys(g, entry->keys, juce::Rectangle<int>(0, y, getWidth(), rowHeight));
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
