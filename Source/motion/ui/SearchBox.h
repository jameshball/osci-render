#pragma once

#include <JuceHeader.h>
#include "../search/SearchIndex.h"
#include "MotionIcons.h"
#include "MotionStyle.h"

namespace motion::ui {
namespace search_style {
// A keycap: the keys in a raised cap, as menus and the shortcuts sheet show
// them; returns the width it takes at the right of `area`.
inline int drawKeycap(juce::Graphics& g, const juce::String& shortcut, juce::Rectangle<int> area, float alpha = 1.0f) {
    const auto text = style::shortcutText(shortcut);
    g.setFont(style::caption());
    const auto width = std::max(18, juce::roundToInt(juce::TextLayout::getStringWidth(style::caption(), text)) + 12);
    const auto cap = area.removeFromRight(width).withSizeKeepingCentre(width, 18).toFloat();
    g.setColour(osci::Colours::surfaceRaised().brighter(.08f).withMultipliedAlpha(alpha));
    g.fillRoundedRectangle(cap, style::radiusSmall);
    g.setColour(style::hairline());
    g.drawRoundedRectangle(cap.reduced(.5f), style::radiusSmall, 1.0f);
    g.setColour(osci::Colours::text().withAlpha(.9f * alpha));
    g.drawText(text, cap, juce::Justification::centred, false);
    return width;
}
// Where the glass sits in the field, and the kind marks in the list, so the
// typed words and the titles found start in line.
constexpr int markCentre = 19, textStart = 36;
}

// Search's field: a glass, the hint, and its keys. Arrow keys, Tab, Return
// and Escape steer the list below rather than the text; other command keys
// stay here rather than acting on the selection behind it.
class SearchField final : public juce::Component, private juce::Timer {
public:
    std::function<void(const juce::String&)> onQuery;
    std::function<void(int)> onMove, onPage;
    std::function<void()> onRun, onClose, onFocusGained, onFocusLost;
    juce::String shortcut;

    SearchField() {
        field.setName("Search");
        field.setTitle("Search");
        field.setDescription("Search actions, settings and properties");
        field.setFont(style::body());
        field.setColour(juce::TextEditor::backgroundColourId, juce::Colours::transparentBlack);
        field.setColour(juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
        field.setColour(juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);
        field.setJustification(juce::Justification::centredLeft);
        field.setIndents(search_style::textStart + 1, 0);
        field.setSelectAllWhenFocused(true);
        field.onTextChange = [this] {
            if (onQuery) { onQuery(field.getText()); }
            repaint();
        };
        field.owner = this;
        addAndMakeVisible(field);
    }

    juce::String text() const { return field.getText(); }
    // `notify`: as if typed, so the list follows.
    void setText(const juce::String& text, bool notify = true) {
        field.setText(text, notify);
        repaint();
    }
    bool hasFocus() const { return field.hasKeyboardFocus(false); }
    void focus() { field.grabKeyboardFocus(); }
    void unfocus() { field.giveAwayKeyboardFocus(); }
    juce::TextEditor& editor() { return field; }
    // Draws the eye to it: its edge pulses twice.
    void pulse() {
        pulseStart = juce::Time::getMillisecondCounterHiRes();
        startTimerHz(60);
    }

    void paint(juce::Graphics& g) override {
        const auto bounds = getLocalBounds().toFloat().reduced(.5f);
        const auto focused = hasFocus();
        // The same fill focused or not: the edge says which.
        g.setColour(osci::Colours::surfaceRaised());
        g.fillRoundedRectangle(bounds, style::radiusControl);
        auto edge = focused ? style::accent().withAlpha(.8f) : (isMouseOver(true) ? osci::Colours::textMuted().withAlpha(.35f) : style::hairline());
        if (pulseStart > 0) {
            const auto t = (juce::Time::getMillisecondCounterHiRes() - pulseStart) / 1200.0;
            edge = edge.interpolatedWith(style::accent(), static_cast<float>(std::sin(juce::MathConstants<double>::pi * std::fmod(t * 2.0, 1.0))));
        }
        g.setColour(edge);
        g.drawRoundedRectangle(bounds, style::radiusControl, 1.0f);
        // One pixel in, as the list's marks are inside its window's edge.
        const auto glass = juce::Rectangle<float>(static_cast<float>(search_style::markCentre - 7), 0, 16, static_cast<float>(getHeight()));
        icons::draw(g, icons::Icon::search, glass, focused ? osci::Colours::textMuted() : osci::Colours::textSubtle(), style::iconSmall);
    }
    // The keys that open it, while it waits for them.
    void paintOverChildren(juce::Graphics& g) override {
        if (hasFocus() || field.getText().isNotEmpty() || shortcut.isEmpty() || getWidth() < 150) { return; }
        search_style::drawKeycap(g, shortcut, getLocalBounds().withTrimmedRight(5), .8f);
    }
    void resized() override {
        field.setBounds(getLocalBounds());
        // As much of the hint as fits.
        const auto hint = getWidth() >= 360 ? "Search actions, settings, properties..." : getWidth() >= 240 ? "Search actions, settings..." : "Search...";
        field.setTextToShowWhenEmpty(hint, osci::Colours::textSubtle());
    }
    void mouseEnter(const juce::MouseEvent&) override { repaint(); }
    void mouseExit(const juce::MouseEvent&) override { repaint(); }

private:
    void timerCallback() override {
        if (juce::Time::getMillisecondCounterHiRes() - pulseStart > 2400) {
            pulseStart = 0;
            stopTimer();
        }
        repaint();
    }
    struct Field final : juce::TextEditor {
        SearchField* owner = nullptr;
        bool keyPressed(const juce::KeyPress& key) override {
            const auto modifiers = key.getModifiers();
            const auto plain = !modifiers.isAnyModifierKeyDown();
            if (key == juce::KeyPress::upKey || key == juce::KeyPress::downKey) {
                if (owner->onMove) { owner->onMove(key == juce::KeyPress::upKey ? -1 : 1); }
                return true;
            }
            if (key.getKeyCode() == juce::KeyPress::tabKey) {
                if (owner->onMove) { owner->onMove(modifiers.isShiftDown() ? -1 : 1); }
                return true;
            }
            if (key == juce::KeyPress::pageUpKey || key == juce::KeyPress::pageDownKey) {
                if (owner->onPage) { owner->onPage(key == juce::KeyPress::pageUpKey ? -1 : 1); }
                return true;
            }
            if (key == juce::KeyPress::returnKey && plain) {
                if (owner->onRun) { owner->onRun(); }
                return true;
            }
            if (key == juce::KeyPress::escapeKey) {
                // A query clears first; an empty field closes.
                if (getText().isNotEmpty()) {
                    clear();
                    if (owner->onQuery) { owner->onQuery({}); }
                } else if (owner->onClose) {
                    owner->onClose();
                }
                return true;
            }
            // The keys that open search, pressed again, take the whole query.
            if (opensSearch(key)) {
                selectAll();
                return true;
            }
            if (juce::TextEditor::keyPressed(key)) { return true; }
            // The app's own keys (save, open, quit, hide...) go on; any other
            // key the text doesn't use stays here: Cmd+D, Delete or F9 mustn't
            // act on the selection hidden behind the list.
            return !forApp(key);
        }
        static bool opensSearch(const juce::KeyPress& key) {
            const auto command = juce::ModifierKeys::commandModifier;
            return key == juce::KeyPress('f', command, 0) || key == juce::KeyPress('p', command | juce::ModifierKeys::shiftModifier, 0) || key == juce::KeyPress(juce::KeyPress::F3Key);
        }
        static bool forApp(const juce::KeyPress& key) {
            const auto modifiers = key.getModifiers();
            if (!modifiers.isCommandDown() || modifiers.isAltDown()) { return false; }
            const auto code = juce::CharacterFunctions::toLowerCase(static_cast<juce::juce_wchar>(key.getKeyCode()));
            return juce::String("sonqhmw`,").containsChar(code);
        }
        void focusGained(FocusChangeType cause) override {
            juce::TextEditor::focusGained(cause);
            owner->repaint();
            if (owner->onFocusGained) { owner->onFocusGained(); }
        }
        void focusLost(FocusChangeType cause) override {
            juce::TextEditor::focusLost(cause);
            owner->repaint();
            if (owner->onFocusLost) { owner->onFocusLost(); }
        }
    };
    Field field;
    double pulseStart = 0;
};

// What a search found, in groups (browsing: recent, suggested, then by
// kind; searching: one, best first): what each does with the typed letters
// marked, where it lives or why it can't run, and its state and keys at the
// right. The row Return runs is marked.
class SearchResults final : public juce::Component {
public:
    struct Group {
        juce::String heading;
        std::vector<search::Result> results;
    };
    // `byMouse`: clicked, rather than Return.
    std::function<void(const search::Result&, bool byMouse)> onRun;
    static constexpr int rowHeight = 28, headingHeight = 28, footerHeight = 26, visibleRows = 12;

    SearchResults() {
        viewport.setViewedComponent(&list, false);
        viewport.setScrollBarsShown(true, false);
        style::fadeScrolling(viewport, style::popoverSurface());
        addAndMakeVisible(viewport);
        list.owner = this;
        setInterceptsMouseClicks(false, true);
        // Clicks leave the keys with the field.
        for (auto* part : std::initializer_list<juce::Component*> {this, &viewport, &list}) { part->setMouseClickGrabsKeyboardFocus(false); }
    }

    // `typed`: the query, for the empty state; `note`: what to say about
    // the results (some of the words matched).
    void setGroups(std::vector<Group> found, const juce::String& typed, const juce::String& note) {
        groups = std::move(found);
        query = typed.trim();
        listNote = note;
        rowNote = {};
        lines.clear();
        for (std::size_t group = 0; group < groups.size(); ++group) {
            if (groups[group].results.empty()) { continue; }
            if (groups[group].heading.isNotEmpty()) { lines.push_back({static_cast<int>(group), -1}); }
            for (std::size_t index = 0; index < groups[group].results.size(); ++index) { lines.push_back({static_cast<int>(group), static_cast<int>(index)}); }
        }
        selected = firstRow();
        list.setSize(getWidth(), contentHeight());
        viewport.setViewPosition(0, 0);
        repaint();
        list.repaint();
        // Read out: how many there are and the first.
        if (query.isNotEmpty()) {
            const auto rows = count();
            announce(rows == 0 ? juce::String("No results") : juce::String(rows) + (rows == 1 ? " result. " : " results. ") + describe(current()));
        }
    }

    // Says `note` in the foot, about the chosen row (why it can't run),
    // until another row is chosen.
    void setNote(const juce::String& note) {
        rowNote = note;
        repaint();
        announce(note);
    }
    void move(int delta) {
        if (lines.empty()) { return; }
        auto next = selected;
        for (int step = 0; step < static_cast<int>(lines.size()); ++step) {
            next = (next + delta + static_cast<int>(lines.size())) % static_cast<int>(lines.size());
            if (lines[static_cast<size_t>(next)].row >= 0) { break; }
        }
        select(next);
        if (!rowNote.isEmpty()) {
            rowNote = {};
            repaint();
        }
        announce(describe(current()));
    }
    void page(int delta) {
        for (int step = 0; step < visibleRows - 1; ++step) {
            const auto before = selected;
            move(delta);
            // Paging stops at the ends rather than wrapping.
            if ((delta > 0 && selected < before) || (delta < 0 && selected > before)) {
                select(before);
                break;
            }
        }
    }
    const search::Result* current() const {
        if (selected < 0 || selected >= static_cast<int>(lines.size())) { return nullptr; }
        return resultOf(lines[static_cast<size_t>(selected)]);
    }
    bool empty() const { return firstRow() < 0; }
    int contentHeight() const {
        int height = 0;
        for (const auto& line : lines) { height += heightOf(line); }
        return height;
    }
    // The height to show it all, up to a dozen rows (the last cut, so it
    // shows there is more), with the hints below.
    int preferredHeight() const {
        const auto body = empty() ? rowHeight : std::min(contentHeight(), visibleRows * rowHeight + rowHeight / 2);
        return body + footerHeight + 10;
    }

    void paint(juce::Graphics& g) override {
        auto foot = getLocalBounds().removeFromBottom(footerHeight);
        g.setColour(style::hairline());
        g.fillRect(foot.removeFromTop(1).reduced(8, 0));
        g.setFont(style::caption());
        g.setColour(osci::Colours::textMuted());
        foot = foot.withTrimmedLeft(12).withTrimmedRight(10);
        // What Return does to the chosen row, in a word.
        const auto keys = juce::String::fromUTF8("\xe2\x86\x91\xe2\x86\x93 move    \xe2\x86\xb5 ") + verbOf(current()) + "    esc close";
        const auto keysWidth = juce::roundToInt(juce::TextLayout::getStringWidth(style::caption(), keys)) + 16;
        g.drawText(keys, foot, juce::Justification::centredRight, false);
        const auto rows = count();
        juce::String left;
        if (rowNote.isNotEmpty()) {
            g.setColour(osci::Colours::text());
            left = rowNote;
        } else if (listNote.isNotEmpty()) {
            left = listNote;
        } else if (query.isEmpty()) {
            left = "Type what you want to do or change";
        } else if (rows > 0) {
            left = juce::String(rows) + (rows == 1 ? " result" : " results");
        }
        g.drawText(left, foot.withTrimmedRight(keysWidth), juce::Justification::centredLeft, true);
    }
    void resized() override {
        viewport.setBounds(getLocalBounds().withTrimmedTop(4).withTrimmedBottom(footerHeight + 4));
        list.setSize(viewport.getWidth(), contentHeight());
    }

private:
    int count() const {
        int rows = 0;
        for (const auto& line : lines) { rows += line.row >= 0 ? 1 : 0; }
        return rows;
    }
    // A row as a screen reader says it: what it does, its state, where it
    // lives, and why it can't run.
    static juce::String describe(const search::Result* result) {
        if (result == nullptr) { return {}; }
        const auto* entry = result->entry;
        juce::StringArray parts {entry->title};
        const auto state = entry->state != nullptr ? entry->state() : search::State::none;
        if (state == search::State::on || state == search::State::off) { parts.add(state == search::State::on ? "on" : "off"); }
        if (state == search::State::chosen) { parts.add("chosen"); }
        if (entry->path.isNotEmpty()) { parts.add(entry->path.replace(search::pathSeparator, ", ")); }
        if (result->unavailable.isNotEmpty()) { parts.add(result->unavailable); }
        return parts.joinIntoString(". ");
    }
    // What Return does to `result`: run it, go to it, set it, turn it on...
    static juce::String verbOf(const search::Result* result) {
        if (result == nullptr) {
            return "run";
        }
        if (result->unavailable.isNotEmpty()) {
            return "say why";
        }
        const auto* entry = result->entry;
        const auto state = entry->state != nullptr ? entry->state() : search::State::none;
        if (state == search::State::on) {
            return "turn off";
        }
        if (state == search::State::off) {
            return "turn on";
        }
        if (state == search::State::chosen || state == search::State::other) {
            return "choose";
        }
        switch (entry->kind) {
            case search::Kind::add: return "add";
            case search::Kind::property: return "go to";
            case search::Kind::setting: return "go to";
            case search::Kind::object: return "select";
            case search::Kind::place: return "open";
            case search::Kind::help: return "open";
            case search::Kind::action: return "run";
        }
        return "run";
    }
    static void announce(const juce::String& text) {
        if (text.isNotEmpty()) { juce::AccessibilityHandler::postAnnouncement(text, juce::AccessibilityHandler::AnnouncementPriority::medium); }
    }
    // A heading (row < 0) or a result of a group.
    struct Line {
        int group = 0, row = -1;
    };

    const search::Result* resultOf(const Line& line) const {
        if (line.row < 0) { return nullptr; }
        return &groups[static_cast<size_t>(line.group)].results[static_cast<size_t>(line.row)];
    }
    int heightOf(const Line& line) const { return line.row >= 0 ? rowHeight : headingHeight; }
    int firstRow() const {
        for (std::size_t index = 0; index < lines.size(); ++index) {
            if (lines[index].row >= 0) { return static_cast<int>(index); }
        }
        return -1;
    }
    int lineTop(int line) const {
        int y = 0;
        for (int index = 0; index < line; ++index) { y += heightOf(lines[static_cast<size_t>(index)]); }
        return y;
    }
    int lineAt(int y) const {
        int top = 0;
        for (std::size_t index = 0; index < lines.size(); ++index) {
            const auto height = heightOf(lines[index]);
            if (y < top + height) { return static_cast<int>(index); }
            top += height;
        }
        return -1;
    }
    void select(int line) {
        selected = line;
        if (line >= 0) {
            const auto top = lineTop(line);
            // A heading just above the row comes into view with it.
            const auto shownTop = line > 0 && lines[static_cast<size_t>(line - 1)].row < 0 ? top - headingHeight : top;
            const auto view = viewport.getViewArea();
            if (shownTop < view.getY()) {
                viewport.setViewPosition(0, shownTop);
            } else if (top + rowHeight > view.getBottom()) {
                viewport.setViewPosition(0, top + rowHeight - view.getHeight());
            }
        }
        list.repaint();
    }

    // The title with the typed letters marked, in at most `width`.
    static void drawTitle(juce::Graphics& g, const search::Result& result, juce::Rectangle<int> area, bool dimmed) {
        const auto& title = result.entry->title;
        const auto plain = osci::Colours::text().withAlpha(dimmed ? .45f : 1.0f);
        // The typed letters bold and bright; green is kept for the selection.
        const auto rest = osci::Colours::text().withAlpha(.82f);
        if (result.highlights.empty() || dimmed) {
            g.setFont(style::body());
            g.setColour(plain);
            g.drawText(title, area, juce::Justification::centredLeft, true);
            return;
        }
        juce::AttributedString text;
        std::size_t next = 0;
        for (int index = 0; index < title.length(); ++index) {
            const auto mark = next < result.highlights.size() && result.highlights[next] == index;
            if (mark) { ++next; }
            text.append(juce::String::charToString(title[index]), mark ? style::title() : style::body(), mark ? osci::Colours::text() : rest);
        }
        text.setJustification(juce::Justification::centredLeft);
        text.setWordWrap(juce::AttributedString::none);
        juce::TextLayout layout;
        layout.createLayout(text, 4000.0f);
        if (layout.getWidth() > static_cast<float>(area.getWidth())) {
            // Too long: the plain title, cut with an ellipsis.
            g.setFont(style::body());
            g.setColour(plain);
            g.drawText(title, area, juce::Justification::centredLeft, true);
            return;
        }
        layout.draw(g, area.toFloat());
    }
    static int titleWidth(const search::Result& result) {
        return juce::roundToInt(juce::TextLayout::getStringWidth(style::title(), result.entry->title)) + 2;
    }
    // A switch's state or the choice made, then the keys, at the right;
    // returns the width taken.
    static int drawRight(juce::Graphics& g, const search::Result& result, juce::Rectangle<int> area, bool dimmed) {
        const auto* entry = result.entry;
        int taken = 0;
        // The state in the rightmost slot, so the pills line up whatever the
        // keys; then the keys.
        const auto state = entry->state != nullptr ? entry->state() : search::State::none;
        constexpr int slot = 30;
        if (state == search::State::on || state == search::State::off) {
            const auto on = state == search::State::on;
            const juce::String text = on ? "On" : "Off";
            g.setFont(style::caption());
            const auto pill = area.removeFromRight(slot).withSizeKeepingCentre(slot, 18).toFloat();
            // On is neutral, as the app's switches are: green is the selection's.
            if (on) {
                g.setColour(style::onFill().withMultipliedAlpha(dimmed ? .5f : 1.0f));
                g.fillRoundedRectangle(pill, 9.0f);
            } else {
                g.setColour(osci::Colours::outline());
                g.drawRoundedRectangle(pill.reduced(.5f), 9.0f, 1.0f);
            }
            g.setColour(on ? osci::Colours::text() : osci::Colours::textSubtle());
            g.drawText(text, pill, juce::Justification::centred, false);
            area.removeFromRight(6);
            taken += slot + 6;
        } else if (state == search::State::chosen) {
            // The choice made: a tick, as a menu shows it.
            const auto tick = area.removeFromRight(slot).withSizeKeepingCentre(10, 8).toFloat();
            juce::Path path;
            path.startNewSubPath(tick.getX(), tick.getCentreY());
            path.lineTo(tick.getX() + tick.getWidth() * .38f, tick.getBottom());
            path.lineTo(tick.getRight(), tick.getY());
            g.setColour(osci::Colours::text());
            g.strokePath(path, juce::PathStrokeType(1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            area.removeFromRight(6);
            taken += slot + 6;
        }
        if (entry->shortcut.isNotEmpty()) {
            const auto width = search_style::drawKeycap(g, entry->shortcut, area, dimmed ? .5f : 1.0f);
            taken += width + 6;
        }
        return taken;
    }
    // A mark for what kind it is; an action, the most common, goes unmarked
    // but for a key's diamond on what keys.
    static void drawMark(juce::Graphics& g, const search::Entry& entry, juce::Point<float> centre, juce::Colour colour) {
        const auto box = juce::Rectangle<float>(16, 16).withCentre(centre);
        switch (entry.kind) {
            case search::Kind::action:
                if (entry.family == "keyframe") {
                    g.setColour(colour);
                    style::drawDiamond(g, centre, 5.0f, false);
                }
                return;
            case search::Kind::add: icons::draw(g, icons::Icon::add, box, colour, style::iconSmall); return;
            case search::Kind::property: {
                // A value on its track: a row Properties shows, to go to.
                juce::Path slider;
                slider.startNewSubPath(centre.x - 6.0f, centre.y);
                slider.lineTo(centre.x + 6.0f, centre.y);
                g.setColour(colour);
                g.strokePath(slider, juce::PathStrokeType(1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                g.fillEllipse(juce::Rectangle<float>(6.0f, 6.0f).withCentre({centre.x - 1.5f, centre.y}));
                return;
            }
            case search::Kind::setting: icons::draw(g, icons::Icon::settings, box, colour, style::iconSmall); return;
            case search::Kind::object: icons::draw(g, icons::Icon::select, box, colour, style::iconSmall); return;
            case search::Kind::place: icons::draw(g, icons::Icon::folder, box, colour, style::iconSmall); return;
            case search::Kind::help: icons::draw(g, icons::Icon::keyboard, box, colour, style::iconSmall); return;
        }
    }

    struct List final : juce::Component {
        SearchResults* owner = nullptr;
        int hovered = -1;
        void paint(juce::Graphics& g) override {
            const auto clip = g.getClipBounds();
            int y = 0;
            bool first = true;
            for (std::size_t index = 0; index < owner->lines.size(); ++index) {
                const auto& line = owner->lines[index];
                const auto height = owner->heightOf(line);
                const juce::Rectangle<int> area(0, y, getWidth(), height);
                y += height;
                if (line.row < 0) {
                    // A group: a rule above all but the first, its name in sentence case.
                    if (area.intersects(clip)) {
                        if (!first) {
                            g.setColour(style::hairline());
                            g.fillRect(area.getX() + 8, area.getY() + 4, area.getWidth() - 16, 1);
                        }
                        g.setFont(style::heading());
                        g.setColour(osci::Colours::textMuted());
                        g.drawText(owner->groups[static_cast<size_t>(line.group)].heading, area.withTrimmedLeft(search_style::textStart).withTrimmedTop(12), juce::Justification::centredLeft, false);
                    }
                    first = false;
                    continue;
                }
                first = false;
                if (!area.intersects(clip)) { continue; }
                const auto& result = *owner->resultOf(line);
                const auto chosen = static_cast<int>(index) == owner->selected;
                const auto dimmed = result.unavailable.isNotEmpty();
                if (chosen || static_cast<int>(index) == hovered) {
                    g.setColour(chosen ? style::accentFill() : style::hoverFill());
                    g.fillRoundedRectangle(area.reduced(4, 1).toFloat(), style::radiusControl);
                    if (chosen) {
                        g.setColour(style::accent());
                        g.fillRoundedRectangle(juce::Rectangle<float>(5.0f, static_cast<float>(area.getCentreY() - 9), 2.0f, 18.0f), 1.0f);
                    }
                }
                drawMark(g, *result.entry, {static_cast<float>(search_style::markCentre), static_cast<float>(area.getCentreY())}, osci::Colours::textMuted().withMultipliedAlpha(dimmed ? .5f : 1.0f));
                auto row = area.withTrimmedLeft(search_style::textStart).withTrimmedRight(10);
                const auto rightWidth = drawRight(g, result, row, dimmed);
                row.removeFromRight(rightWidth > 0 ? rightWidth + 4 : 0);
                // The title first; then, as room allows, why it can't run,
                // the word that found it, and where it lives.
                const auto titleSpace = std::min(row.getWidth(), titleWidth(result));
                drawTitle(g, result, row.removeFromLeft(titleSpace), dimmed);
                row.removeFromLeft(10);
                auto detail = result.entry->path;
                if (result.via.isNotEmpty()) {
                    const auto quoted = juce::String::fromUTF8("\xe2\x80\x9c") + result.via + juce::String::fromUTF8("\xe2\x80\x9d");
                    detail = detail.isNotEmpty() ? detail + style::dot() + quoted : quoted;
                }
                if (dimmed) { detail = detail.isNotEmpty() ? detail + style::dot() + result.unavailable : result.unavailable; }
                // Quieter than a greyed title; on the title's baseline (the
                // smaller font centres a pixel higher).
                g.setFont(style::caption());
                g.setColour(osci::Colours::textMuted().withMultipliedAlpha(dimmed ? .65f : 1.0f));
                g.drawText(detail, row.translated(0, 1), juce::Justification::centredLeft, true);
            }
        }
        void mouseMove(const juce::MouseEvent& event) override { setHovered(owner->lineAt(event.y)); }
        void mouseExit(const juce::MouseEvent&) override { setHovered(-1); }
        void mouseUp(const juce::MouseEvent& event) override {
            const auto line = owner->lineAt(event.y);
            if (line < 0 || owner->lines[static_cast<size_t>(line)].row < 0 || !event.mouseWasClicked()) { return; }
            owner->selected = line;
            repaint();
            if (owner->onRun != nullptr) { owner->onRun(*owner->resultOf(owner->lines[static_cast<size_t>(line)]), true); }
        }
        void setHovered(int line) {
            if (line >= 0 && owner->lines[static_cast<size_t>(line)].row < 0) { line = -1; }
            if (line == hovered) { return; }
            hovered = line;
            repaint();
        }
    };

    std::vector<Group> groups;
    std::vector<Line> lines;
    juce::String query, listNote, rowNote;
    int selected = -1;
    osci::ScrollFadeViewport viewport;
    List list;
};

// A window of its own under the field, above everything (the Scope's
// OpenGL included), that never takes the keys: they stay with the field.
class SearchWindow : public juce::Component, private juce::ComponentListener {
public:
    // A click anywhere but the field or this window, or the app going to
    // the back or its window being minimised.
    std::function<void()> onClickedAway;

    explicit SearchWindow(juce::Component& fieldToFollow) : field(fieldToFollow), top(fieldToFollow.getTopLevelComponent()) {
        setOpaque(false);
        setWantsKeyboardFocus(false);
        setMouseClickGrabsKeyboardFocus(false);
        if (top != nullptr) { top->addComponentListener(this); }
        juce::Desktop::getInstance().addGlobalMouseListener(&clicks);
        setLookAndFeel(&field.getLookAndFeel());
        watch.startTimerHz(5);
    }
    ~SearchWindow() override {
        juce::Desktop::getInstance().removeGlobalMouseListener(&clicks);
        if (top != nullptr) { top->removeComponentListener(this); }
        setLookAndFeel(nullptr);
    }

    void show() {
        if (!isOnDesktop()) {
            addToDesktop(juce::ComponentPeer::windowIsTemporary | juce::ComponentPeer::windowIgnoresKeyPresses | juce::ComponentPeer::windowHasDropShadow);
        }
        place();
        setVisible(true);
        toFront(false);
    }
    // Under the field, at least as wide as a readable row (at most 640):
    // when wider than the field, lined up with its right edge; kept within
    // the window and the screen.
    void place() {
        const auto anchor = field.getScreenBounds();
        const auto width = std::clamp(anchor.getWidth(), 560, std::max(anchor.getWidth(), 640));
        auto bounds = juce::Rectangle<int>(anchor.getRight() - width, anchor.getBottom() + 4, width, wantedHeight());
        const auto* window = field.getTopLevelComponent();
        if (window != nullptr) {
            bounds = bounds.constrainedWithin(window->getScreenBounds().reduced(6));
        }
        const auto* display = juce::Desktop::getInstance().getDisplays().getDisplayForRect(anchor);
        if (display != nullptr) {
            bounds = bounds.constrainedWithin(display->userArea.reduced(8));
        }
        setBounds(bounds);
    }
    void paint(juce::Graphics& g) override { style::fillFloatingPanel(g, getLocalBounds().toFloat(), style::popoverSurface()); }

protected:
    virtual int wantedHeight() const = 0;
    // Whether a click on `clicked` (in the field or this window) keeps it.
    virtual bool keeps(juce::Component* clicked) const { return clicked == this || isParentOf(clicked) || clicked == &field || field.isParentOf(clicked); }
    juce::Component& field;

private:
    void componentMovedOrResized(juce::Component&, bool, bool) override { place(); }
    void away() {
        if (onClickedAway) { onClickedAway(); }
    }
    // Every click in the app, to tell one away from the field and window.
    struct Clicks final : juce::MouseListener {
        SearchWindow& owner;
        explicit Clicks(SearchWindow& window) : owner(window) {}
        void mouseDown(const juce::MouseEvent& event) override {
            auto* clicked = event.eventComponent;
            if (clicked == nullptr || !owner.keeps(clicked)) { owner.away(); }
        }
    };
    // The app going to the back, or the window being minimised (neither a
    // click it sees): what floats over it goes too.
    struct Watch final : juce::Timer {
        SearchWindow& owner;
        explicit Watch(SearchWindow& window) : owner(window) {}
        void timerCallback() override {
            auto* peer = owner.top != nullptr ? owner.top->getPeer() : nullptr;
            if (owner.isVisible() && (!juce::Process::isForegroundProcess() || peer == nullptr || peer->isMinimised())) { owner.away(); }
        }
    };
    juce::Component::SafePointer<juce::Component> top;
    Clicks clicks {*this};
    Watch watch {*this};
};

// The results, under the field.
class SearchPopup final : public SearchWindow {
public:
    SearchResults results;
    explicit SearchPopup(juce::Component& fieldToFollow) : SearchWindow(fieldToFollow) {
        setName("Search results");
        setTitle("Search results");
        addAndMakeVisible(results);
    }
    void resized() override { results.setBounds(getLocalBounds().reduced(1)); }

private:
    int wantedHeight() const override { return results.preferredHeight(); }
};

// What came of running a result, said by the field where the eyes are:
// why it couldn't run, where it went, a switch's new state; with a way
// back when it changed the selection.
class SearchNote final : public SearchWindow, private juce::Timer {
public:
    explicit SearchNote(juce::Component& fieldToFollow) : SearchWindow(fieldToFollow) {
        setName("Search note");
        message.setFont(style::body());
        message.setColour(juce::Label::textColourId, osci::Colours::text());
        message.setInterceptsMouseClicks(false, false);
        // A longer note wraps rather than squeezing its letters.
        message.setMinimumHorizontalScale(1.0f);
        addAndMakeVisible(message);
        style::makeQuiet(action);
        // The one thing to press reads as one: the accent, on a wash.
        action.setColour(juce::TextButton::textColourOffId, style::accent());
        action.setColour(juce::TextButton::buttonColourId, style::hoverFill());
        action.setWantsKeyboardFocus(false);
        action.setMouseClickGrabsKeyboardFocus(false);
        addChildComponent(action);
        onClickedAway = [this] {
            stopTimer();
            setVisible(false);
        };
    }
    // Says `text` for a few seconds, with `actionLabel` doing `act` if given.
    void say(const juce::String& text, const juce::String& actionLabel = {}, std::function<void()> act = {}) {
        message.setText(text, juce::dontSendNotification);
        action.setButtonText(actionLabel);
        action.setVisible(actionLabel.isNotEmpty());
        action.onClick = [this, act] {
            setVisible(false);
            if (act) { act(); }
        };
        // Long enough to read: a second for every few words, and more with
        // something to press.
        remaining = std::max(3500, text.length() * 60) + (actionLabel.isNotEmpty() ? 2500 : 0);
        show();
        resized();
        startTimer(250);
    }
    void resized() override {
        // The text in line with the field's and the list's.
        auto area = getLocalBounds().withTrimmedLeft(search_style::textStart + 1).withTrimmedRight(11);
        if (action.isVisible()) {
            action.setBounds(area.removeFromRight(actionWidth()).withSizeKeepingCentre(actionWidth(), 24));
            area.removeFromRight(8);
        }
        message.setBounds(area);
    }

private:
    // One line, or two for a longer note.
    int wantedHeight() const override {
        const auto width = getWidth() > 0 ? getWidth() : 560;
        const auto room = width - search_style::textStart - 12 - (action.isVisible() ? actionWidth() + 8 : 0);
        return juce::TextLayout::getStringWidth(style::body(), message.getText()) > static_cast<float>(room) ? 56 : 38;
    }
    int actionWidth() const { return juce::roundToInt(juce::TextLayout::getStringWidth(style::buttonFont(action), action.getButtonText())) + 20; }
    // A click on it, but its button, sends it away too.
    bool keeps(juce::Component* clicked) const override { return clicked == &action || action.isParentOf(clicked) || clicked == &field || field.isParentOf(clicked); }
    // It waits while the pointer is over it.
    void timerCallback() override {
        if (isMouseOver(true)) { return; }
        remaining -= getTimerInterval();
        if (remaining <= 0) {
            stopTimer();
            setVisible(false);
        }
    }
    int remaining = 0;
    juce::Label message;
    juce::TextButton action;
};
}
