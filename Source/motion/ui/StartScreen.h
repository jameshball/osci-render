#pragma once

#include "MotionStyle.h"
#include "MotionIcons.h"
#include "FormControls.h"
#include <osci_gui/osci_gui.h>

namespace motion::ui {
// A project on the start screen: one opened lately, or an example.
struct StartCard {
    // `detail`: a line about it, under its name; `when`: when it was last
    // saved, beside the name.
    juce::String name, detail, when;
    juce::Image picture;
    juce::File file;
    juce::String resource;
    // The project open now: choosing it just carries on with it.
    bool open = false;
    bool isExample() const { return resource.isNotEmpty(); }
};

// What the start screen shows first: the projects opened lately and the
// examples, as pictures in a grid, beside the ways to begin. It sits over the
// project open now, which Escape, a click outside or Continue go back to.
class StartScreen final : public juce::Component, private juce::KeyListener {
public:
    struct Actions {
        std::function<void()> newProject, openProject, showShortcuts;
        std::function<void(const juce::File&)> openRecent, revealRecent, forgetRecent;
        std::function<void(const juce::String&)> openExample;
        std::function<void(bool)> showAtStartup;
    };
    static constexpr int width = 896, height = 556;

    // `continuing`: the name of the project open now, when it has anything
    // in it to go back to.
    StartScreen(std::vector<StartCard> recentCards, std::vector<StartCard> exampleCards, const juce::String& continuing, bool atStartup, const juce::String& version, Actions toTake)
        : actions(std::move(toTake)), versionText(version), grid(*this) {
        setName("Start screen");
        setWantsKeyboardFocus(false);
        grid.recents = std::move(recentCards);
        grid.examples = std::move(exampleCards);
        rows.add(new RailRow("New project", icons::Icon::newProject, "Cmd+N", true));
        rows.getLast()->onClick = [this] { finish(actions.newProject); };
        rows.add(new RailRow("Open...", icons::Icon::folder, "Cmd+O"));
        rows.getLast()->onClick = [this] { finish(actions.openProject); };
        if (continuing.isNotEmpty()) {
            rows.add(new RailRow("Continue", icons::Icon::play, "Esc"));
            rows.getLast()->detail = continuing;
            rows.getLast()->onClick = [this] { finish({}); };
        }
        rows.add(new RailRow("Keyboard shortcuts", icons::Icon::keyboard, "Cmd+/"));
        rows.getLast()->onClick = [this] { finish(actions.showShortcuts); };
        for (auto* row : rows) { addAndMakeVisible(row); }
        startup.setToggleState(atStartup, juce::dontSendNotification);
        startup.setTooltip("Show this screen when osci-motion opens");
        startup.onClick = [this] {
            if (actions.showAtStartup != nullptr) { actions.showAtStartup(startup.getToggleState()); }
        };
        addAndMakeVisible(startup);
        startupLabel.setText("Show at startup", juce::dontSendNotification);
        startupLabel.setFont(style::caption());
        startupLabel.setColour(juce::Label::textColourId, osci::Colours::textMuted());
        startupLabel.setInterceptsMouseClicks(false, false);
        addAndMakeVisible(startupLabel);

        search.setName("Search projects");
        search.setTitle("Search projects");
        search.setFont(style::body());
        search.setTextToShowWhenEmpty("Search", osci::Colours::textMuted().withAlpha(.6f));
        search.setColour(juce::TextEditor::backgroundColourId, osci::Colours::surfaceSunken());
        search.setColour(juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
        search.setColour(juce::TextEditor::focusedOutlineColourId, style::accent().withAlpha(.5f));
        search.setJustification(juce::Justification::centredLeft);
        search.setIndents(28, 0);
        search.onTextChange = [this] { grid.setFilter(search.getText()); };
        search.onEscapeKey = [this] {
            if (search.isEmpty()) {
                finish({});
            } else {
                search.clear();
                grid.setFilter({});
            }
        };
        search.onReturnKey = [this] { grid.openSelectedOrFirst(); };
        search.addKeyListener(this);
        addAndMakeVisible(search);

        viewport.setViewedComponent(&grid, false);
        viewport.setScrollBarsShown(true, false);
        viewport.setScrollBarThickness(6);
        addAndMakeVisible(viewport);
        addAndMakeVisible(mark);
        setSize(width, height);
    }
    ~StartScreen() override {
        if (host != nullptr) { host->removeKeyListener(this); }
        search.removeKeyListener(this);
    }

    // Keys heard elsewhere while the screen is up: the editor can hold the
    // focus as the window opens.
    bool handleKey(const juce::KeyPress& key) { return keyPressed(key); }
    static osci::ComponentOverlay* show(juce::Component& owner, std::unique_ptr<StartScreen> screen) {
        auto overlay = std::make_unique<osci::ComponentOverlay>(std::move(screen), juce::String(), juce::Point<int>(width, height), false);
        auto* raw = overlay.get();
        osci::OverlayComponent::show(owner, std::move(overlay));
        return raw;
    }

    // The overlay holds the keyboard while it animates in, so its keys are
    // heard too.
    void parentHierarchyChanged() override {
        auto* overlay = findParentComponentOfClass<osci::OverlayComponent>();
        if (overlay == host) {
            return;
        }
        if (host != nullptr) { host->removeKeyListener(this); }
        host = overlay;
        if (host != nullptr) { host->addKeyListener(this); }
    }
    void visibilityChanged() override {
        if (!isShowing()) {
            return;
        }
        grid.appear();
        juce::MessageManager::callAsync([safe = juce::Component::SafePointer<juce::Component>(&grid)] {
            if (safe != nullptr && safe->isShowing()) { safe->grabKeyboardFocus(); }
        });
    }

    void paint(juce::Graphics& g) override {
        const auto header = getLocalBounds().removeFromTop(headerHeight);
        auto text = header.withTrimmedLeft(mark.getRight() + 10);
        g.setColour(osci::Colours::text());
        g.setFont(style::display());
        const auto nameWidth = juce::roundToInt(juce::TextLayout::getStringWidth(style::display(), "osci-motion")) + 2;
        g.drawText("osci-motion", text.removeFromLeft(nameWidth), juce::Justification::centredLeft, false);
        g.setFont(style::caption());
        g.setColour(osci::Colours::textMuted().withAlpha(.7f));
        g.drawText(versionText, text.withTrimmedLeft(8).withTrimmedTop(4), juce::Justification::centredLeft, false);
        // The rail and the grid, set apart by a rule.
        g.setColour(style::hairline());
        g.fillRect(railWidth + railGap / 2, header.getBottom() + headerGap, 1, getHeight() - header.getBottom() - headerGap);
        g.fillRect(0, startup.getY() - 12, railWidth, 1);
    }
    // The search field's glass, over its fill.
    void paintOverChildren(juce::Graphics& g) override {
        icons::draw(g, icons::Icon::search, search.getBounds().toFloat().removeFromLeft(28).translated(2, 0), osci::Colours::textMuted().withAlpha(.8f), 16.0f);
    }
    void resized() override {
        auto area = getLocalBounds();
        auto header = area.removeFromTop(headerHeight);
        mark.setBounds(header.removeFromLeft(34).withSizeKeepingCentre(34, 26));
        // Clear of the overlay's close button.
        header.removeFromRight(40);
        search.setBounds(header.removeFromRight(232).withSizeKeepingCentre(232, 28));
        area.removeFromTop(headerGap);
        auto rail = area.removeFromLeft(railWidth);
        area.removeFromLeft(railGap);
        auto bottom = rail.removeFromBottom(24);
        startup.setBounds(bottom.removeFromLeft(Switch::width + 4).withTrimmedLeft(4));
        bottom.removeFromLeft(8);
        startupLabel.setBounds(bottom);
        for (auto* row : rows) {
            // Shortcuts stand apart from the ways to begin.
            if (row->getButtonText() == "Keyboard shortcuts") {
                rail.removeFromTop(13);
            }
            row->setBounds(rail.removeFromTop(row->detail.isNotEmpty() ? 46 : 36));
            rail.removeFromTop(2);
        }
        viewport.setBounds(area);
        grid.layOut(area.getWidth() - scrollGutter);
    }

private:
    static constexpr int headerHeight = 32, headerGap = 22, railWidth = 196, railGap = 25, scrollGutter = 10;

    // Runs `action` once the screen has gone: one closing animation, and
    // what comes next (a sheet asking to keep changes) not under it.
    void finish(std::function<void()> action) {
        auto* overlay = findParentComponentOfClass<osci::OverlayComponent>();
        if (overlay == nullptr) {
            if (action != nullptr) { action(); }
            return;
        }
        if (finishing) {
            return;
        }
        finishing = true;
        auto previous = std::move(overlay->onDismissRequested);
        overlay->onDismissRequested = [previous = std::move(previous), action = std::move(action)]() mutable {
            // Run from copies: dismissing destroys this callback.
            auto before = std::move(previous);
            auto then = std::move(action);
            if (before != nullptr) { before(); }
            if (then != nullptr) { then(); }
        };
        overlay->requestDismiss();
    }
    void choose(const StartCard& card) {
        if (card.open) {
            finish({});
        } else if (card.isExample()) {
            finish([open = actions.openExample, resource = card.resource] { if (open != nullptr) { open(resource); } });
        } else {
            finish([open = actions.openRecent, file = card.file] { if (open != nullptr) { open(file); } });
        }
    }

    bool keyPressed(const juce::KeyPress& key, juce::Component* origin) override {
        if (origin == &search) {
            if (key == juce::KeyPress::downKey) {
                grid.grabKeyboardFocus();
                grid.moveSelection(0, 0);
                return true;
            }
            // The field would type the letter of a shortcut.
            return key.getModifiers().isCommandDown() && keyPressed(key);
        }
        return keyPressed(key);
    }
    bool keyPressed(const juce::KeyPress& key) override {
        const auto command = juce::ModifierKeys::commandModifier;
        if (key == juce::KeyPress::escapeKey) {
            finish({});
            return true;
        }
        if (key == juce::KeyPress('n', command, 0)) {
            finish(actions.newProject);
            return true;
        }
        if (key == juce::KeyPress('o', command, 0)) {
            finish(actions.openProject);
            return true;
        }
        if (key == juce::KeyPress::leftKey || key == juce::KeyPress::rightKey || key == juce::KeyPress::upKey || key == juce::KeyPress::downKey) {
            const auto across = key == juce::KeyPress::leftKey ? -1 : key == juce::KeyPress::rightKey ? 1 : 0;
            const auto down = key == juce::KeyPress::upKey ? -1 : key == juce::KeyPress::downKey ? 1 : 0;
            grid.moveSelection(across, down);
            return true;
        }
        if (key == juce::KeyPress::returnKey || key == juce::KeyPress::spaceKey) {
            grid.openSelectedOrFirst();
            return true;
        }
        // Typing searches.
        const auto character = key.getTextCharacter();
        if (character > ' ' && !key.getModifiers().isCommandDown() && !key.getModifiers().isCtrlDown()) {
            search.grabKeyboardFocus();
            search.insertTextAtCaret(juce::String::charToString(character));
            return true;
        }
        return false;
    }

    // A way to begin: an icon, its name (with a line about it), and its keys.
    class RailRow final : public juce::Button {
    public:
        RailRow(const juce::String& name, icons::Icon glyph, const juce::String& keys, bool primary = false) : juce::Button(name), icon(glyph), shortcut(style::shortcutText(keys)), lead(primary) {
            setButtonText(name);
            setTitle(name);
            setWantsKeyboardFocus(false);
            setMouseCursor(juce::MouseCursor::PointingHandCursor);
        }
        juce::String detail;
        void buttonStateChanged() override { fade.follow(*this); }
        void paintButton(juce::Graphics& g, bool, bool down) override {
            auto area = getLocalBounds().toFloat();
            const auto hover = fade.value();
            if (lead) {
                g.setColour(style::accentFill().withMultipliedAlpha(1.0f + hover));
                g.fillRoundedRectangle(area, 8.0f);
            }
            if (hover > 0 || down) {
                g.setColour((down ? style::pressFill() : style::hoverFill()).withMultipliedAlpha(down ? 1.0f : hover));
                g.fillRoundedRectangle(area, 8.0f);
            }
            auto content = area.reduced(12, 0);
            const auto glyph = content.removeFromLeft(18);
            icons::draw(g, icon, glyph.withHeight(36.0f).withY(area.getY() + (detail.isNotEmpty() ? 5.0f : 0.0f)), lead ? style::accent() : osci::Colours::text().withAlpha(.6f + .3f * hover), 18.0f);
            content.removeFromLeft(12);
            g.setFont(style::caption());
            g.setColour(osci::Colours::textMuted().withAlpha(.55f + .35f * hover));
            const auto keysWidth = juce::TextLayout::getStringWidth(style::caption(), shortcut) + 2;
            g.drawText(shortcut, content.removeFromRight(keysWidth).withHeight(36.0f).withY(area.getY() + (detail.isNotEmpty() ? 5.0f : 0.0f)), juce::Justification::centredRight, false);
            content.removeFromRight(6);
            g.setFont(style::body());
            g.setColour(osci::Colours::text().withAlpha(lead ? 1.0f : .88f + .12f * hover));
            if (detail.isEmpty()) {
                g.drawText(getButtonText(), content, juce::Justification::centredLeft, true);
                return;
            }
            g.drawText(getButtonText(), content.withHeight(18).withY(area.getY() + 7), juce::Justification::centredLeft, true);
            g.setFont(style::caption());
            g.setColour(osci::Colours::textMuted());
            g.drawText(detail, content.withHeight(16).withY(area.getY() + 24), juce::Justification::centredLeft, true);
        }
    private:
        icons::Icon icon;
        juce::String shortcut;
        bool lead = false;
        style::Fade fade {*this};
    };

    // A small Lissajous figure drifting as a scope at rest does: the app's
    // mark beside its name.
    class Mark final : public juce::Component {
    public:
        Mark() { setInterceptsMouseClicks(false, false); }
        void paint(juce::Graphics& g) override {
            const auto area = getLocalBounds().toFloat().reduced(4, 3);
            juce::Path figure;
            constexpr int steps = 180;
            for (int step = 0; step <= steps; ++step) {
                const auto t = juce::MathConstants<float>::twoPi * static_cast<float>(step) / steps;
                const auto x = area.getCentreX() + area.getWidth() * .5f * std::sin(3.0f * t + phase);
                const auto y = area.getCentreY() + area.getHeight() * .5f * std::sin(2.0f * t);
                if (step == 0) { figure.startNewSubPath(x, y); } else { figure.lineTo(x, y); }
            }
            figure.closeSubPath();
            const auto green = style::key();
            for (const auto& [thickness, alpha] : std::initializer_list<std::pair<float, float>> {{6.0f, .06f}, {3.2f, .16f}, {1.3f, .95f}}) {
                g.setColour(green.withAlpha(alpha));
                g.strokePath(figure, juce::PathStrokeType(thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }
        }
    private:
        float phase = .6f;
        double last = juce::Time::getMillisecondCounterHiRes();
        juce::VBlankAttachment drift {this, [this] {
            const auto now = juce::Time::getMillisecondCounterHiRes();
            phase += static_cast<float>((now - last) * .00035);
            last = now;
            repaint();
        }};
    };

    // The cards: projects opened lately, then the examples, each a picture
    // with its name and a line about it.
    class Grid final : public juce::Component, public juce::TooltipClient, private juce::Timer {
    public:
        explicit Grid(StartScreen& screen) : owner(screen) {
            setWantsKeyboardFocus(true);
            setMouseClickGrabsKeyboardFocus(false);
            setName("Projects");
            setTitle("Projects");
        }
        std::vector<StartCard> recents, examples;

        void setFilter(const juce::String& text) {
            query = text.trim();
            selected = -1;
            layOut(getWidth());
            if (auto* viewport = findParentComponentOfClass<juce::Viewport>()) { viewport->setViewPosition(0, 0); }
        }
        // The cards rise in from the first time the grid is drawn.
        void appear() {
            if (appearedAt == 0) {
                appearedAt = juce::Time::getMillisecondCounterHiRes() + 140;
                startTimerHz(60);
                // The keys come here, wherever the window's focus was.
                juce::MessageManager::callAsync([safe = juce::Component::SafePointer<Grid>(this)] {
                    if (safe != nullptr && !safe->owner.search.hasKeyboardFocus(false)) { safe->grabKeyboardFocus(); }
                });
            }
        }
        void layOut(int width) {
            placed.clear();
            headings.clear();
            emptyRecents = {};
            const auto cardWidth = (width - gutter * (columns - 1)) / columns;
            const auto pictureHeight = cardWidth * 3 / 4;
            const auto cardHeight = pictureHeight + textHeight;
            int y = 0;
            const auto section = [&](const juce::String& title, const std::vector<StartCard>& cards) {
                std::vector<const StartCard*> shown;
                for (const auto& card : cards) {
                    if (query.isEmpty() || card.name.containsIgnoreCase(query) || card.detail.containsIgnoreCase(query)) { shown.push_back(&card); }
                }
                // Two rows of recent projects keep the examples in view; a
                // search looks through them all.
                if (&cards == &recents && query.isEmpty() && shown.size() > maxRecents) { shown.resize(maxRecents); }
                if (shown.empty() && (query.isNotEmpty() || &cards != &recents)) {
                    return;
                }
                headings.push_back({title, juce::Rectangle<int>(0, y, width, headingHeight)});
                y += headingHeight;
                if (shown.empty()) {
                    emptyRecents = juce::Rectangle<int>(0, y, width, 84);
                    y += 84 + sectionGap;
                    return;
                }
                for (std::size_t index = 0; index < shown.size(); ++index) {
                    const auto column = static_cast<int>(index) % columns, row = static_cast<int>(index) / columns;
                    placed.push_back({shown[index], juce::Rectangle<int>(column * (cardWidth + gutter), y + row * (cardHeight + rowGap), cardWidth, cardHeight), pictureHeight});
                }
                const auto rowCount = (static_cast<int>(shown.size()) + columns - 1) / columns;
                y += rowCount * cardHeight + (rowCount - 1) * rowGap + sectionGap;
            };
            section("Recent", recents);
            section("Examples", examples);
            hover.assign(placed.size(), 0.0f);
            setSize(width, std::max(y, 120));
            repaint();
        }
        void moveSelection(int across, int down) {
            if (placed.empty()) {
                return;
            }
            if (selected < 0) {
                selected = 0;
            } else if (across != 0) {
                selected = juce::jlimit(0, static_cast<int>(placed.size()) - 1, selected + across);
            } else if (down != 0) {
                // The card above or below: the nearest in the next row.
                const auto from = placed[static_cast<std::size_t>(selected)].bounds;
                int best = -1;
                for (int index = 0; index < static_cast<int>(placed.size()); ++index) {
                    const auto& bounds = placed[static_cast<std::size_t>(index)].bounds;
                    const auto beyond = down > 0 ? bounds.getY() > from.getY() : bounds.getY() < from.getY();
                    if (!beyond) {
                        continue;
                    }
                    const auto rowDistance = std::abs(bounds.getY() - from.getY()), columnDistance = std::abs(bounds.getX() - from.getX());
                    if (best < 0) {
                        best = index;
                        continue;
                    }
                    const auto& current = placed[static_cast<std::size_t>(best)].bounds;
                    const auto currentRow = std::abs(current.getY() - from.getY()), currentColumn = std::abs(current.getX() - from.getX());
                    if (rowDistance < currentRow || (rowDistance == currentRow && columnDistance < currentColumn)) { best = index; }
                }
                if (best >= 0) { selected = best; }
            }
            const auto bounds = placed[static_cast<std::size_t>(selected)].bounds;
            if (auto* viewport = findParentComponentOfClass<juce::Viewport>()) {
                const auto view = viewport->getViewArea();
                if (bounds.getY() - headingHeight < view.getY()) {
                    viewport->setViewPosition(0, std::max(0, bounds.getY() - headingHeight));
                } else if (bounds.getBottom() > view.getBottom()) {
                    viewport->setViewPosition(0, bounds.getBottom() - view.getHeight() + 4);
                }
            }
            repaint();
        }
        void openSelectedOrFirst() {
            if (placed.empty()) {
                return;
            }
            owner.choose(*placed[static_cast<std::size_t>(std::max(0, selected))].card);
        }

        juce::String getTooltip() override {
            const auto index = cardAt(getMouseXYRelative());
            if (index < 0) {
                return {};
            }
            const auto& card = *placed[static_cast<std::size_t>(index)].card;
            // What it is in full, and where it is.
            return card.isExample() ? card.detail : card.detail + "\n" + card.file.getFullPathName();
        }
        void mouseMove(const juce::MouseEvent& event) override { setHovered(cardAt(event.getPosition())); }
        void mouseExit(const juce::MouseEvent&) override { setHovered(-1); }
        void mouseDown(const juce::MouseEvent& event) override {
            const auto index = cardAt(event.getPosition());
            pressed = index;
            if (index < 0 || !event.mods.isPopupMenu()) {
                return;
            }
            const auto& card = *placed[static_cast<std::size_t>(index)].card;
            juce::PopupMenu menu;
            menu.addItem(1, card.open ? "Continue" : "Open");
            if (!card.isExample()) {
#if JUCE_MAC
                menu.addItem(2, "Show in Finder");
#elif JUCE_WINDOWS
                menu.addItem(2, "Show in Explorer");
#else
                menu.addItem(2, "Show in folder");
#endif
                menu.addSeparator();
                menu.addItem(3, "Remove from recent projects");
            }
            const juce::Component::SafePointer<Grid> safe(this);
            const auto chosenCard = card;
            menu.showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea({event.getScreenX(), event.getScreenY(), 1, 1}), [safe, chosenCard](int choice) {
                if (safe == nullptr) {
                    return;
                }
                if (choice == 1) { safe->owner.choose(chosenCard); }
                if (choice == 2 && safe->owner.actions.revealRecent != nullptr) { safe->owner.actions.revealRecent(chosenCard.file); }
                if (choice == 3) { safe->forget(chosenCard.file); }
            });
        }
        void mouseUp(const juce::MouseEvent& event) override {
            const auto index = cardAt(event.getPosition());
            if (index >= 0 && index == pressed && !event.mods.isPopupMenu() && event.getDistanceFromDragStart() < 6) {
                selected = index;
                owner.choose(*placed[static_cast<std::size_t>(index)].card);
            }
            pressed = -1;
        }

        void paint(juce::Graphics& g) override {
            appear();
            const auto now = juce::Time::getMillisecondCounterHiRes();
            for (const auto& heading : headings) {
                g.setFont(style::heading());
                g.setColour(osci::Colours::textMuted());
                g.drawText(heading.first, heading.second.withTrimmedBottom(10), juce::Justification::bottomLeft, false);
            }
            if (!emptyRecents.isEmpty()) {
                const auto box = emptyRecents.toFloat().reduced(.5f);
                juce::Path outline;
                outline.addRoundedRectangle(box, 10.0f);
                juce::Path dashed;
                const float dashes[] {4.0f, 4.0f};
                juce::PathStrokeType(1.0f).createDashedStroke(dashed, outline, dashes, 2);
                g.setColour(juce::Colours::white.withAlpha(.12f));
                g.fillPath(dashed);
                g.setFont(style::body());
                g.setColour(osci::Colours::text().withAlpha(.8f));
                g.drawText("Projects you open and save will appear here", box.withTrimmedBottom(18), juce::Justification::centred, false);
                g.setFont(style::caption());
                g.setColour(osci::Colours::textMuted());
                g.drawText("each with a picture of how it looks. Start from an example below.", box.withTrimmedTop(22), juce::Justification::centred, false);
            }
            if (placed.empty() && query.isNotEmpty()) {
                g.setFont(style::body());
                g.setColour(osci::Colours::textMuted());
                g.drawText("No project matches \"" + query + "\".", getLocalBounds().removeFromTop(80), juce::Justification::centred, true);
            }
            for (std::size_t index = 0; index < placed.size(); ++index) {
                // Cards rise into place one after another as the screen opens.
                const auto delay = static_cast<double>(std::min<std::size_t>(index, 9)) * 32.0;
                const auto raw = appearedAt == 0 ? 0.0 : juce::jlimit(0.0, 1.0, (now - appearedAt - delay) / 300.0);
                const auto appear = static_cast<float>(1.0 - std::pow(1.0 - raw, 3.0));
                if (appear <= 0) {
                    continue;
                }
                paintCard(g, placed[index], hover[index], static_cast<int>(index) == selected, appear);
            }
        }

    private:
        static constexpr std::size_t maxRecents = 6;
        static constexpr int columns = 3, gutter = 18, rowGap = 18, textHeight = 58, headingHeight = 30, sectionGap = 18;
        struct Placed {
            const StartCard* card;
            juce::Rectangle<int> bounds;
            int pictureHeight;
        };

        int cardAt(juce::Point<int> point) const {
            for (std::size_t index = 0; index < placed.size(); ++index) {
                if (placed[index].bounds.contains(point)) { return static_cast<int>(index); }
            }
            return -1;
        }
        void setHovered(int index) {
            if (index == hovered) {
                return;
            }
            hovered = index;
            startTimerHz(60);
            setMouseCursor(index >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
        }
        void forget(const juce::File& file) {
            if (owner.actions.forgetRecent != nullptr) { owner.actions.forgetRecent(file); }
            recents.erase(std::remove_if(recents.begin(), recents.end(), [&file](const StartCard& card) { return card.file == file; }), recents.end());
            selected = -1;
            hovered = -1;
            layOut(getWidth());
        }

        // A scope's graticule with a faint figure of the project's own (its
        // name chooses the ratio and phase): a project not yet pictured.
        static void paintPlaceholder(juce::Graphics& g, juce::Rectangle<float> area, const juce::String& name) {
            g.setColour(juce::Colours::white.withAlpha(.03f));
            for (int line = 1; line < 8; ++line) {
                const auto x = area.getX() + area.getWidth() * static_cast<float>(line) / 8;
                g.fillRect(x, area.getY(), 1.0f, area.getHeight());
            }
            for (int line = 1; line < 6; ++line) {
                const auto y = area.getY() + area.getHeight() * static_cast<float>(line) / 6;
                g.fillRect(area.getX(), y, area.getWidth(), 1.0f);
            }
            static constexpr std::array<std::pair<float, float>, 8> ratios {{{1, 2}, {2, 3}, {3, 4}, {3, 5}, {4, 5}, {1, 3}, {5, 6}, {2, 5}}};
            const auto hash = static_cast<std::uint32_t>(name.hashCode());
            const auto [a, b] = ratios[hash % ratios.size()];
            const auto phase = static_cast<float>((hash >> 8) % 628) / 100.0f;
            juce::Path trace;
            const auto box = area.withSizeKeepingCentre(area.getHeight() * .56f, area.getHeight() * .56f);
            constexpr int steps = 360;
            for (int step = 0; step <= steps; ++step) {
                const auto t = juce::MathConstants<float>::twoPi * static_cast<float>(step) / steps;
                const auto x = box.getCentreX() + box.getWidth() * .5f * std::sin(a * t + phase), y = box.getCentreY() + box.getHeight() * .5f * std::sin(b * t);
                if (step == 0) { trace.startNewSubPath(x, y); } else { trace.lineTo(x, y); }
            }
            g.setColour(style::key().withAlpha(.05f));
            g.strokePath(trace, juce::PathStrokeType(5.0f));
            g.setColour(style::key().withAlpha(.2f));
            g.strokePath(trace, juce::PathStrokeType(1.1f));
        }
        void paintCard(juce::Graphics& g, const Placed& place, float lit, bool chosen, float appear) {
            const auto& card = *place.card;
            constexpr float corner = 10.0f;
            auto area = place.bounds.toFloat().translated(0, (1.0f - appear) * 12.0f - 2.0f * lit);
            const auto picture = area.removeFromTop(static_cast<float>(place.pictureHeight));
            if (appear < 1.0f) { g.beginTransparencyLayer(appear); }
            // A soft shadow under a lifted card.
            if (lit > 0) {
                for (int layer = 0; layer < 4; ++layer) {
                    const auto spread = 2.0f + 3.0f * static_cast<float>(layer);
                    g.setColour(juce::Colours::black.withAlpha(.10f * lit));
                    g.fillRoundedRectangle(picture.translated(0, 4.0f + static_cast<float>(layer) * 2.0f).expanded(spread * .5f), corner + spread * .5f);
                }
            }
            {
                juce::Graphics::ScopedSaveState saved(g);
                juce::Path clip;
                clip.addRoundedRectangle(picture, corner);
                g.reduceClipRegion(clip);
                g.setColour(juce::Colour(0xff030504));
                g.fillRect(picture);
                if (card.picture.isValid()) {
                    // Drawn a little larger as the pointer reaches it.
                    const auto grown = picture.withSizeKeepingCentre(picture.getWidth() * (1.0f + .035f * lit), picture.getHeight() * (1.0f + .035f * lit));
                    g.setImageResamplingQuality(juce::Graphics::highResamplingQuality);
                    g.setOpacity(.84f + .16f * lit);
                    g.drawImage(card.picture, grown, juce::RectanglePlacement::fillDestination);
                    g.setOpacity(1.0f);
                } else {
                    paintPlaceholder(g, picture, card.name);
                }
                // A faint sheen along the top edge, as on glass.
                g.setGradientFill(juce::ColourGradient(juce::Colours::white.withAlpha(.05f), picture.getX(), picture.getY(), juce::Colours::transparentWhite, picture.getX(), picture.getY() + picture.getHeight() * .45f, false));
                g.fillRect(picture);
                if (card.open) {
                    const auto badge = juce::Rectangle<float>(picture.getX() + 8, picture.getY() + 8, juce::TextLayout::getStringWidth(style::caption(), "Open now") + 26, 20);
                    g.setColour(juce::Colours::black.withAlpha(.6f));
                    g.fillRoundedRectangle(badge, 10.0f);
                    g.setColour(style::accent());
                    g.fillEllipse(badge.getX() + 7, badge.getCentreY() - 2.5f, 5, 5);
                    g.setFont(style::caption());
                    g.setColour(osci::Colours::text());
                    g.drawText("Open now", badge.withTrimmedLeft(16), juce::Justification::centredLeft, false);
                }
            }
            if (chosen) {
                g.setColour(style::accent());
                g.drawRoundedRectangle(picture.expanded(3.0f), corner + 3.0f, 2.0f);
            } else {
                g.setColour(juce::Colours::white.withAlpha(.09f + .16f * lit));
                g.drawRoundedRectangle(picture.reduced(.5f), corner, 1.0f);
            }
            auto text = area.withTrimmedTop(10).withTrimmedLeft(2);
            auto nameLine = text.removeFromTop(17);
            if (card.when.isNotEmpty()) {
                g.setFont(style::caption());
                g.setColour(osci::Colours::textMuted().withAlpha(.75f));
                const auto whenWidth = juce::TextLayout::getStringWidth(style::caption(), card.when) + 2;
                g.drawText(card.when, nameLine.removeFromRight(whenWidth), juce::Justification::centredRight, false);
                nameLine.removeFromRight(8);
            }
            g.setFont(style::body());
            g.setColour(osci::Colours::text().withAlpha(.9f + .1f * lit));
            g.drawText(card.name, nameLine, juce::Justification::centredLeft, true);
            g.setFont(style::caption());
            g.setColour(osci::Colours::textMuted());
            g.drawFittedText(card.detail, text.removeFromTop(30).toNearestInt(), juce::Justification::topLeft, 2, 1.0f);
            if (appear < 1.0f) { g.endTransparencyLayer(); }
        }

        StartScreen& owner;
        juce::String query;
        std::vector<Placed> placed;
        std::vector<std::pair<juce::String, juce::Rectangle<int>>> headings;
        juce::Rectangle<int> emptyRecents;
        std::vector<float> hover;
        int hovered = -1, pressed = -1, selected = -1;
        double appearedAt = 0, lastFrame = 0;
        // Hover eases in and out; the cards rise in once. Runs only while
        // something moves.
        void timerCallback() override {
            const auto now = juce::Time::getMillisecondCounterHiRes();
            const auto step = lastFrame == 0 ? 16.0 : std::min(100.0, now - lastFrame);
            lastFrame = now;
            auto moving = appearedAt != 0 && now < appearedAt + 300.0 + 9 * 32.0 + 50.0;
            const auto ease = static_cast<float>(1.0 - std::exp(-step / 55.0));
            for (std::size_t index = 0; index < hover.size(); ++index) {
                const auto target = static_cast<int>(index) == hovered ? 1.0f : 0.0f;
                if (std::abs(hover[index] - target) < .002f) {
                    hover[index] = target;
                    continue;
                }
                hover[index] += (target - hover[index]) * ease;
                moving = true;
            }
            repaint();
            if (!moving) {
                stopTimer();
                lastFrame = 0;
            }
        }
    };

    Actions actions;
    juce::String versionText;
    juce::OwnedArray<RailRow> rows;
    Switch startup {"Show at startup"};
    juce::Label startupLabel;
    juce::TextEditor search;
    juce::Viewport viewport;
    Mark mark;
    Grid grid;
    osci::OverlayComponent* host = nullptr;
    bool finishing = false;
};
}
