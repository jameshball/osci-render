#include "MotionEditor.h"
#include "search/Vocabulary.h"
#include "ui/StartScreen.h"

// Search: everything osci-motion does, found by typing. The menus as they
// stand (their items, paths, keys, state and greyed items), what Add source
// adds, effects and modulators, the project's own objects and their
// properties, the selection's properties and effects, the Scope's and the
// composition's settings and their choices, the tools, places to go, and
// a few goals in plain words ("make it spin").

namespace {
using motion::search::Entry;
using motion::search::Kind;
using motion::search::State;
namespace vocabulary = motion::search::vocabulary;
const auto& separator = motion::search::pathSeparator;

// The singular of an Add source › Examples kind ("Lua scripts").
juce::String exampleKind(const juce::String& plural) {
    if (plural == "Text") {
        return plural;
    }
    return plural.endsWithChar('s') ? plural.dropLastCharacters(1) : plural;
}

juce::String capitalised(const juce::String& text) { return text.substring(0, 1).toUpperCase() + text.substring(1); }

juce::String quoted(const juce::String& text) { return juce::String::fromUTF8("\xe2\x80\x9c") + text + juce::String::fromUTF8("\xe2\x80\x9d"); }

// An item's name out of its menu, saying what it does: "Colour" in Clip ›
// Key is "Keyframe colour", a generator is "Add Spiral".
juce::String titleIn(const juce::String& path, const juce::String& text) {
    const auto submenu = path.fromLastOccurrenceOf(separator, false, false);
    const auto addSource = juce::String("Add source") + separator;
    if (path == addSource + "Generate" || path.startsWith(addSource + "Generate" + separator)) {
        return "Add " + text;
    }
    if (path == addSource + "Light" || path == addSource + "Mirror") {
        return "Add " + text;
    }
    if (path == addSource + "Light" + separator + "Presets") {
        return "Add " + text + " light";
    }
    if (path.startsWith(addSource + "Live")) {
        return text == "Blender..." ? juce::String("Add Blender source...") : "Add " + text;
    }
    if (path.startsWith(addSource + "Examples" + separator)) {
        return "Add example: " + capitalised(text) + " (" + exampleKind(submenu).toLowerCase() + ")";
    }
    if (path == "File" + separator + "Examples") {
        return "Open example: " + text;
    }
    if (submenu == "Open recent" && juce::File::isAbsolutePath(text)) {
        return "Open " + juce::File(text).getFileNameWithoutExtension();
    }
    if (submenu == "Key") {
        return "Keyframe " + text.toLowerCase();
    }
    if (submenu == "View from") {
        return "View from " + text.toLowerCase();
    }
    if (submenu == "Easy ease") {
        return text == "In and out" ? submenu : submenu + " " + text.toLowerCase();
    }
    if (submenu == "Export audio") {
        return "Export " + text.substring(0, 1).toLowerCase() + text.substring(1);
    }
    if (submenu == "Interface size" || submenu == "Beat grid") {
        return submenu + " " + text;
    }
    if (path == "Timing" && (text == "Seconds" || text == "Frames" || text.startsWith("Bars"))) {
        return "Show time in " + text.toLowerCase().replace(" / ", " and ");
    }
    return text;
}

// Alike items of a menu, so a search shows a few: the examples, a key's
// channels, light presets, the choices of a submenu.
juce::String familyIn(const juce::String& path, const juce::String& title) {
    if (path.contains("Examples")) {
        return "example";
    }
    if (title.startsWith("Keyframe ")) {
        return "keyframe";
    }
    if (path.endsWith("Presets")) {
        return "light preset";
    }
    if (title.startsWith("Show time in") || title.startsWith("Beat grid") || title.startsWith("Interface size") || title.startsWith("View from") || title.startsWith("Export beam")) {
        return title.upToLastOccurrenceOf(" ", false, false);
    }
    return {};
}

// Whether an item is one choice of several (a tick for the one chosen) or
// a switch (on or off).
enum class Choosing { neither, choice, toggle };
Choosing choosingOf(const juce::String& title, const juce::String& path) {
    if (title.startsWith("Show time in") || title.startsWith("Beat grid") || title.startsWith("Interface size")) {
        return Choosing::choice;
    }
    static const juce::StringArray toggles {"Snap to grid", "Follow playhead", "Full screen", "Draw for screen capture (OBS)"};
    if (toggles.contains(title) || path.startsWith("View" + separator + "Interface")) {
        return Choosing::toggle;
    }
    return Choosing::neither;
}

// Where an item of a menu sits, as its path says it.
Kind kindOf(const juce::String& path) {
    if (path.contains("Open recent") || path == "File" + separator + "Examples") {
        return Kind::place;
    }
    if (path.startsWith("Add source")) {
        return Kind::add;
    }
    if (path.startsWith("Help")) {
        return Kind::help;
    }
    return Kind::action;
}

// The id an item keeps whatever its words now (Undo names its step).
juce::String stableName(const juce::String& title, const juce::String& text) {
    if (title.startsWith("Undo")) {
        return "Undo";
    }
    if (title.startsWith("Redo")) {
        return "Redo";
    }
    if (title.endsWith("looking through camera")) {
        return "Look through camera";
    }
    if (title.startsWith("Detecting tempo")) {
        return "Detect tempo from audio";
    }
    return text;
}

// An id worth remembering in another session: not an object's, which
// another project numbers differently.
bool lastsBetweenProjects(const juce::String& id) {
    for (const auto* prefix : {"object:", "object property:", "tile:", "effect setting:", "track:"}) {
        if (id.startsWith(prefix)) {
            return false;
        }
    }
    return true;
}

// Marks the items of a greyed submenu that say why for themselves.
const juce::String greyedSubmenu = "greyed";

// The window's own component named `name`, inside `root`.
juce::Component* findNamed(juce::Component& root, const juce::String& name) {
    if (root.getName() == name) {
        return &root;
    }
    for (auto* child : root.getChildren()) {
        auto* found = findNamed(*child, name);
        if (found != nullptr) {
            return found;
        }
    }
    return nullptr;
}

// Those inside `root` whose name starts `prefix`.
void findAllStarting(juce::Component& root, const juce::String& prefix, juce::Array<juce::Component*>& found) {
    if (root.getName().startsWith(prefix)) {
        found.add(&root);
    }
    for (auto* child : root.getChildren()) {
        findAllStarting(*child, prefix, found);
    }
}

// Effect keywords: parameters only when they say something an effect
// alone does, and the names the effect goes by elsewhere.
juce::StringArray effectKeywords(const motion::EffectDefinition& definition) {
    static const juce::StringArray generic {"Strength", "Rotation", "Amount", "Depth", "Phase", "Zoom", "Density", "Twist"};
    juce::StringArray words {"effect"};
    for (const auto& parameter : definition.parameters) {
        const auto name = juce::String(parameter.name.data(), parameter.name.size());
        if (!generic.contains(name)) {
            words.addIfNotAlreadyThere(name);
        }
    }
    words.addArray(vocabulary::effectAliases(juce::String(definition.id.data(), definition.id.size())));
    return words;
}

// The sections of Properties a group sits in, for paths.
juce::String sectionOfGroup(const juce::String& group) {
    if (group == "Position" || group == "Rotation" || group == "Scale" || group == "Anchor") {
        return "Transform";
    }
    if (group == "Field of view") {
        return "Lens";
    }
    if (group == "Solid" || group == "Mirror") {
        return "Surface";
    }
    return "Appearance";
}

// Whether `menu` (or a submenu) has the item `id` ticked.
bool tickedIn(const juce::PopupMenu& menu, int id) {
    for (juce::PopupMenu::MenuItemIterator items(menu, true); items.next();) {
        const auto& item = items.getItem();
        if (item.itemID == id && item.subMenu == nullptr) {
            return item.isTicked;
        }
    }
    return false;
}
}

void MotionEditor::setUpSearch() {
    searchField.shortcut = "Cmd+F";
    searchField.onFocusGained = [this] {
        if (searchPopup == nullptr) {
            openSearch();
        }
    };
    searchField.onFocusLost = [this] {
        // The keys gone to something else close it; the window losing them
        // (another app in front) doesn't.
        juce::MessageManager::callAsync([owner = juce::Component::SafePointer<MotionEditor>(this)] {
            if (owner != nullptr && !owner->searchField.hasFocus() && juce::Component::getCurrentlyFocusedComponent() != nullptr) {
                owner->closeSearch();
            }
        });
    };
    // Typing opens it too, wherever the keys came from; keys typed quickly
    // search once, when they pause.
    searchField.onQuery = [this](const juce::String&) {
        if (searchPopup == nullptr) {
            openSearch();
            return;
        }
        if (searchRefreshPending) {
            return;
        }
        searchRefreshPending = true;
        juce::MessageManager::callAsync([owner = juce::Component::SafePointer<MotionEditor>(this)] {
            if (owner == nullptr) {
                return;
            }
            owner->searchRefreshPending = false;
            owner->refreshSearch();
        });
    };
    searchField.onMove = [this](int delta) {
        if (searchPopup != nullptr) {
            searchPopup->results.move(delta);
        }
    };
    searchField.onPage = [this](int delta) {
        if (searchPopup != nullptr) {
            searchPopup->results.page(delta);
        }
    };
    searchField.onRun = [this] {
        if (searchPopup == nullptr) {
            return;
        }
        // Return pressed before the last keys were searched runs what they find.
        if (searchField.text() != searchedText) {
            refreshSearch();
        }
        const auto* chosen = searchPopup->results.current();
        if (chosen != nullptr) {
            runSearchResult(*chosen);
        }
    };
    searchField.onClose = [this] {
        closeSearch();
        restoreFocusAfterSearch();
    };
    addAndMakeVisible(searchField);
    searchIndex.recent = juce::StringArray::fromLines(processor.globalSettings.getString("motion.search.recent"));
    searchIndex.recent.removeEmptyStrings();
}

juce::String MotionEditor::searchBlocked() {
    if (startScreen != nullptr) {
        return "Choose a project on the start screen first";
    }
    const auto sheetUp = std::any_of(activeOverlays.begin(), activeOverlays.end(), [](const auto& overlay) { return !overlay->isDismissing(); });
    if (sheetUp || popover != nullptr) {
        return "Finish with the open window first";
    }
    if (sceneEditorOpen()) {
        return "Finish or cancel the editor over the Scene first";
    }
    return {};
}

void MotionEditor::openSearch(const juce::String& query) {
    const auto blocked = searchBlocked();
    if (blocked.isNotEmpty()) {
        if (searchField.hasFocus()) {
            restoreFocusAfterSearch();
        }
        sayAfterSearch(blocked);
        return;
    }
    // The Scope full screen covers the field and Properties.
    if (scopeFullScreen) {
        setScopeFullScreen(false);
    }
    if (query.isNotEmpty()) {
        searchField.setText(query, false);
    }
    if (!searchField.hasFocus()) {
        searchField.focus();
    }
    // Words brought from elsewhere are refined, not replaced.
    if (query.isNotEmpty()) {
        searchField.editor().setCaretPosition(query.length());
    }
    if (searchPopup != nullptr) {
        refreshSearch();
        return;
    }
    searchPopupClosing.reset();
    buildSearchIndex();
    searchPopup = std::make_unique<motion::ui::SearchPopup>(searchField);
    searchPopup->results.onRun = [this](const motion::search::Result& result, bool byMouse) { runSearchResult(result, byMouse); };
    searchPopup->onClickedAway = [this] {
        // After the click has gone where it was meant to.
        juce::MessageManager::callAsync([owner = juce::Component::SafePointer<MotionEditor>(this)] {
            if (owner == nullptr) {
                return;
            }
            owner->closeSearch();
            // A click on something that doesn't take the keys leaves them with
            // the field: they go back to where work was.
            if (owner->searchField.hasFocus()) {
                owner->restoreFocusAfterSearch();
            }
        });
    };
    refreshSearch();
    searchPopup->show();
}

void MotionEditor::closeSearch(bool byMouse) {
    if (byMouse && searchPopup != nullptr) {
        // The chosen row stays lit a moment, as a menu's does, so a second
        // click of a double-click lands on it.
        searchPopupClosing = std::move(searchPopup);
        searchPopupClosing->onClickedAway = nullptr;
        searchPopupClosing->results.onRun = nullptr;
        juce::Timer::callAfterDelay(300, [owner = juce::Component::SafePointer<MotionEditor>(this)] {
            if (owner != nullptr) {
                owner->searchPopupClosing.reset();
            }
        });
    }
    searchPopup.reset();
    if (searchField.text().isNotEmpty()) {
        searchField.setText({}, false);
    }
    searchedText = {};
}

void MotionEditor::refreshSearch() {
    if (searchPopup == nullptr) {
        return;
    }
    using Group = motion::ui::SearchResults::Group;
    const auto query = searchField.text();
    searchedText = query;
    std::vector<Group> groups;
    juce::String note;
    if (motion::search::Index::wordsOf(query).isEmpty()) {
        // Browsing: the last few picked, what suits the selection, then what
        // can run, by kind: one of a family at most (they are found by
        // typing), and a few of the project's objects.
        const auto browse = searchIndex.search({}).results;
        std::set<const Entry*> shown;
        Group recent {"Recent", {}};
        for (const auto& result : browse) {
            if (recent.results.size() >= 5 || !searchIndex.recent.contains(result.entry->id)) {
                continue;
            }
            recent.results.push_back(result);
            shown.insert(result.entry);
        }
        Group suggested {selection != 0 && !selectedSearchName.isEmpty() ? "For " + selectedSearchName : juce::String("Suggested"), {}};
        for (const auto& id : suggestedSearchIds) {
            const auto found = std::find_if(browse.begin(), browse.end(), [&](const auto& result) { return result.entry->id == id; });
            if (found != browse.end() && !shown.contains(found->entry) && found->unavailable.isEmpty()) {
                suggested.results.push_back(*found);
                shown.insert(found->entry);
            }
        }
        // Goals, which only typing their words would find otherwise.
        Group goals {"Try", {}};
        for (const auto& result : browse) {
            if (result.entry->id.startsWith("goal:") && !shown.contains(result.entry) && result.unavailable.isEmpty()) {
                goals.results.push_back(result);
                shown.insert(result.entry);
            }
        }
        groups.push_back(std::move(recent));
        groups.push_back(std::move(suggested));
        groups.push_back(std::move(goals));
        for (const auto kind : {Kind::action, Kind::add, Kind::property, Kind::setting, Kind::object, Kind::place, Kind::help}) {
            Group group {motion::search::kindHeading(kind), {}};
            for (const auto& result : browse) {
                const auto* entry = result.entry;
                const auto listed = entry->kind == kind && !shown.contains(entry) && result.unavailable.isEmpty() && entry->family.isEmpty();
                if (listed && (kind != Kind::object || group.results.size() < 8)) {
                    group.results.push_back(result);
                }
            }
            groups.push_back(std::move(group));
        }
    } else {
        auto found = searchIndex.search(query);
        note = found.note;
        // Alike entries beyond a few: say how to find the rest.
        if (found.folded > 0 && note.isEmpty() && found.foldedExample != nullptr) {
            const auto* example = found.foldedExample;
            if (example->id.startsWith("object property:")) {
                const auto what = example->family.fromFirstOccurrenceOf(":", false, false).toLowerCase();
                note = "More objects have " + what + ": add a name, as in " + quoted(example->path.upToFirstOccurrenceOf(separator, false, false).toLowerCase() + " " + what);
            } else {
                note = juce::String(found.folded) + " more like these: add a word to narrow them";
            }
        }
        if (found.results.empty()) {
            // Never a dead end: ways on.
            Group ways {"Nothing matches " + quoted(query.trim()) + ". Try", {}};
            note = "Or say it another way: one word for what to change";
            for (const auto* id : {"place:library:Effects", "menu:File/Import source...", "place:library:Sources", "menu:Help/Keyboard shortcuts..."}) {
                const auto entry = std::find_if(searchIndex.entries.begin(), searchIndex.entries.end(), [&](const Entry& candidate) { return candidate.id == id; });
                if (entry != searchIndex.entries.end()) {
                    motion::search::Result result;
                    result.entry = &*entry;
                    ways.results.push_back(result);
                }
            }
            groups.push_back(std::move(ways));
        } else {
            groups.push_back({{}, std::move(found.results)});
        }
    }
    searchPopup->results.setGroups(std::move(groups), query, note);
    searchPopup->place();
}

void MotionEditor::runSearchResult(const motion::search::Result& result, bool byMouse) {
    const auto* entry = result.entry;
    if (result.unavailable.isNotEmpty()) {
        // Said where the eyes are: in the list, which stays open.
        if (searchPopup != nullptr) {
            searchPopup->results.setNote(entry->title + ": " + result.unavailable);
        }
        return;
    }
    const auto blocked = searchBlocked();
    if (blocked.isNotEmpty()) {
        if (searchPopup != nullptr) {
            searchPopup->results.setNote(blocked);
        }
        return;
    }
    if (lastsBetweenProjects(entry->id)) {
        searchIndex.used(entry->id);
        processor.globalSettings.set("motion.search.recent", searchIndex.recent.joinIntoString("\n"));
    }
    // The list closes, and the keys go back where they were, once whatever
    // chose it has returned; then it runs, on the window as the user left it.
    juce::MessageManager::callAsync([owner = juce::Component::SafePointer<MotionEditor>(this), run = entry->run, title = entry->title, state = entry->state, shortcut = entry->shortcut, byMouse] {
        if (owner == nullptr) {
            return;
        }
        owner->closeSearch(byMouse);
        owner->restoreFocusAfterSearch();
        const auto revision = owner->processor.document.revision();
        const auto said = owner->searchNotesSaid;
        if (run != nullptr) {
            run();
        }
        // What came of it, once what it set off (a button's click comes as a
        // message of its own) has happened: a switch's state, or the change
        // made, with Undo; and its keys, for next time. A note the run gave
        // itself (where it went, how to go on) says more and stays.
        juce::MessageManager::callAsync([owner, title, state, shortcut, revision, said] {
            if (owner == nullptr || owner->searchNotesSaid != said) {
                return;
            }
            const auto changed = owner->processor.document.revision() != revision;
            const auto now = state != nullptr ? state() : State::none;
            juce::String text;
            if (now == State::on || now == State::off) {
                text = title + (now == State::on ? ": on" : ": off");
            } else if (now == State::chosen) {
                text = title;
            } else if (changed) {
                text = owner->processor.getUndoManager().getUndoDescription();
            }
            if (text.isEmpty()) {
                return;
            }
            // Once a session for each: the keys teach, then stop saying so.
            if (shortcut.isNotEmpty() && !owner->searchTaught.contains(title)) {
                owner->searchTaught.add(title);
                text += motion::style::dot() + "next time " + motion::style::shortcutText(shortcut);
            }
            if (!changed) {
                owner->sayAfterSearch(text);
                return;
            }
            owner->sayAfterSearch(text, "Undo", [owner] {
                if (owner != nullptr) {
                    owner->processor.getUndoManager().undo();
                }
            });
        });
    });
}

void MotionEditor::sayAfterSearch(const juce::String& text, const juce::String& actionLabel, std::function<void()> act) {
    ++searchNotesSaid;
    if (searchNote == nullptr) {
        searchNote = std::make_unique<motion::ui::SearchNote>(searchField);
    }
    searchNote->say(text, actionLabel, std::move(act));
}

void MotionEditor::goToForSearch(motion::Id target, const juce::String& what, std::function<void(MotionEditor&)> then) {
    const auto before = selection;
    const auto beforeName = before == 0 ? juce::String("the composition") : selectedSearchName;
    if (before != target) {
        select(target);
    }
    juce::MessageManager::callAsync([owner = juce::Component::SafePointer<MotionEditor>(this), before, beforeName, target, what, then] {
        if (owner == nullptr) {
            return;
        }
        if (then != nullptr) {
            then(*owner);
        }
        // A way back to what was selected, when going there changed it.
        if (before != target && beforeName.isNotEmpty()) {
            owner->sayAfterSearch("Showing " + what, "Back to " + beforeName, [owner, before] {
                if (owner != nullptr) {
                    owner->select(before);
                }
            });
        }
    });
}

void MotionEditor::revealForSearch(const std::string& property, const juce::String& what) {
    const auto shown = propertyInspector.revealProperty(property);
    if (!shown) {
        sayAfterSearch("Properties doesn't show " + what + " for this selection");
    }
}

void MotionEditor::globalFocusChanged(juce::Component* focused) {
    repaint();
    // Where work was: what to give the keys back to when search closes.
    if (focused != nullptr && isParentOf(focused) && focused != &searchField && !searchField.isParentOf(focused)) {
        lastWorkFocus = focused;
    }
}

void MotionEditor::restoreFocusAfterSearch() {
    auto* before = lastWorkFocus.getComponent();
    if (before != nullptr && before->isShowing() && before->isEnabled()) {
        before->grabKeyboardFocus();
    } else {
        // The window itself, never nothing: its keys still work.
        grabKeyboardFocus();
    }
}

void MotionEditor::menuOpened() {
    menuOpenings.push_back(juce::Time::getMillisecondCounterHiRes());
}

void MotionEditor::menusActivated(bool active) {
    if (active) {
        menuOpenings.clear();
        return;
    }
    // Menus held open long enough to read count as looked in; one swept
    // past on the way to another doesn't.
    const auto now = juce::Time::getMillisecondCounterHiRes();
    for (std::size_t index = 0; index < menuOpenings.size(); ++index) {
        const auto until = index + 1 < menuOpenings.size() ? menuOpenings[index + 1] : now;
        if (until - menuOpenings[index] >= 600) {
            menusLookedIn.push_back(menuOpenings[index]);
        }
    }
    menuOpenings.clear();
    std::erase_if(menusLookedIn, [now](double opened) { return now - opened > 30000; });
    const auto shown = processor.globalSettings.getInt("motion.search.hints", 0);
    if (menusLookedIn.size() < 4 || searchHintShown || shown >= 3 || !searchIndex.recent.isEmpty() || searchPopup != nullptr) {
        return;
    }
    searchHintShown = true;
    processor.globalSettings.set("motion.search.hints", shown + 1);
    searchField.pulse();
    sayAfterSearch("Looking for something? Search finds any action, setting or property.", "Search", [this] { openSearch(); });
}

bool MotionEditor::menuItemTicked(int index, int id) {
    const juce::ScopedValueSetter<bool> indexing(indexingMenus, true);
    auto& model = static_cast<juce::MenuBarModel&>(menus);
    const auto names = model.getMenuBarNames();
    if (index < 0 || index >= names.size()) {
        return false;
    }
    return tickedIn(model.getMenuForIndex(index, names[index]), id);
}

void MotionEditor::addMenuEntries(const juce::PopupMenu& menu, const juce::String& path, std::function<void(int)> choose, std::function<bool(int)> ticked, const juce::String& unavailable) {
    juce::String section;
    for (juce::PopupMenu::MenuItemIterator items(menu); items.next();) {
        const auto& item = items.getItem();
        if (item.isSeparator) {
            continue;
        }
        if (item.isSectionHeader) {
            section = item.text;
            continue;
        }
        const auto where = section.isNotEmpty() ? path + separator + section : path;
        if (item.subMenu != nullptr) {
            // A greyed submenu greys what it holds, saying why (or each item
            // says why for itself).
            auto reason = unavailable;
            if (reason.isEmpty() && !item.isEnabled) {
                reason = vocabulary::reasonForSubmenu(item.text);
                if (reason.isEmpty()) {
                    reason = greyedSubmenu;
                }
            }
            addMenuEntries(*item.subMenu, where + separator + item.text, choose, ticked, reason);
            continue;
        }
        if (item.itemID == 0 && item.action == nullptr) {
            continue;
        }
        // A placeholder ("No recent projects"), search itself, and Import
        // source again (it is File's too).
        const auto again = path == "Add source" && item.text == "Import source...";
        if ((!item.isEnabled && item.text.startsWith("No ")) || item.text == "Search..." || again) {
            continue;
        }
        Entry entry;
        entry.title = titleIn(where, item.text);
        entry.id = "menu:" + where.replace(separator, "/") + "/" + stableName(entry.title, item.text);
        entry.kind = kindOf(where);
        entry.path = where;
        entry.keywords = vocabulary::menuKeywords(entry.title);
        if (entry.title != item.text && !juce::File::isAbsolutePath(item.text)) {
            entry.keywords.add(item.text);
        }
        entry.keywords.addArray(vocabulary::generatorKeywords(item.text));
        entry.relevance = vocabulary::relevanceFor(entry.title);
        entry.literal = entry.kind == Kind::place || where.contains("Examples");
        entry.family = familyIn(where, entry.title);
        // An example is found by name, below what the app itself does.
        if (entry.family == "example") {
            entry.relevance -= 40;
            entry.keywords.add("example");
        }
        entry.shortcut = item.shortcutKeyDescription;
        if (unavailable.isNotEmpty() || !item.isEnabled) {
            const auto reason = vocabulary::reasonFor(entry.title, where, unavailable == greyedSubmenu ? juce::String() : unavailable);
            entry.unavailable = [reason] { return reason; };
        }
        // State asked as it is shown: the menu as it stands then.
        const auto choosing = choosingOf(entry.title, where);
        const auto id = item.itemID;
        if (choosing != Choosing::neither && ticked != nullptr && id != 0) {
            const auto toggle = choosing == Choosing::toggle;
            entry.state = [ticked, id, toggle] {
                const auto on = ticked(id);
                return toggle ? (on ? State::on : State::off) : (on ? State::chosen : State::other);
            };
        }
        if (item.action != nullptr) {
            entry.run = item.action;
        } else if (choose != nullptr) {
            entry.run = [choose, id] { choose(id); };
        } else {
            continue;
        }
        // An item already found under the same name and path (a menu shown twice).
        const auto duplicate = std::any_of(searchIndex.entries.begin(), searchIndex.entries.end(), [&](const Entry& other) { return other.id == entry.id; });
        if (!duplicate) {
            searchIndex.entries.push_back(std::move(entry));
        }
    }
}

void MotionEditor::buildSearchIndex() {
    searchIndexGeneration = processor.document.generation();
    searchIndexRevision = processor.document.revision();
    searchIndex.entries.clear();
    suggestedSearchIds.clear();
    const auto& project = processor.document.project();
    const auto target = motion::findPropertyTarget(project, selection);
    selectedSearchName = target.has_value() ? juce::String(target->name.data(), target->name.size()) : juce::String();
    for (const auto& track : project.tracks) {
        if (track.id == selection) {
            selectedSearchName = juce::String(track.name);
        }
    }
    indexMenus();
    indexAdding();
    indexSelection();
    indexSettings();
    indexObjects();
    indexTools();
    indexGoals();
    // For a clip, what's done to it rather than the rows Properties shows
    // beside the list anyway.
    const auto clipSelected = target.has_value() && !target->isGroup && !target->camera && !target->beam && motion::findClip(project, selection) != nullptr;
    if (clipSelected) {
        suggestedSearchIds = {"menu:Clip/Split at playhead", "menu:Clip/Duplicate", "menu:Clip/Key/Position", "goal:spin", "goal:fade", "track:Rename", "menu:Clip/Show key lanes"};
    } else if (selectionIsCamera()) {
        suggestedSearchIds.insert(0, "goal:shake");
        suggestedSearchIds.insert(1, "menu:Scene/Look through camera");
    }
    // What browsing suggests when nothing suits the selection.
    if (suggestedSearchIds.isEmpty()) {
        for (const auto* id : {"menu:File/Import source...", "menu:Add source/Draw a shape...", "menu:File/Export video...", "place:scope", "setting:output", "menu:Help/Keyboard shortcuts..."}) {
            suggestedSearchIds.add(id);
        }
    } else {
        suggestedSearchIds.add("place:library:Effects");
    }
    searchIndex.changed();
}

void MotionEditor::indexMenus() {
    const juce::Component::SafePointer<MotionEditor> owner(this);
    // The menus as they stand: building them is what keeps their items' state.
    const juce::ScopedValueSetter<bool> indexing(indexingMenus, true);
    auto& model = static_cast<juce::MenuBarModel&>(menus);
    const auto names = model.getMenuBarNames();
    for (int index = 0; index < names.size(); ++index) {
        const auto choose = [owner, index](int id) {
            if (owner != nullptr) {
                static_cast<juce::MenuBarModel&>(owner->menus).menuItemSelected(id, index);
            }
        };
        const auto ticked = [owner, index](int id) { return owner != nullptr && owner->menuItemTicked(index, id); };
        addMenuEntries(model.getMenuForIndex(index, names[index]), names[index], choose, ticked);
    }
    addMenuEntries(addSourceMenu(), "Add source", {}, {});
    const auto chooseInScene = [owner](int id) {
        if (owner != nullptr) {
            owner->sceneMenuChosen(id);
        }
    };
    const auto tickedInScene = [owner](int id) { return owner != nullptr && tickedIn(owner->sceneMenu(true), id); };
    addMenuEntries(sceneMenu(true), "Scene", chooseInScene, tickedInScene);
    // Live switches whose menu items aren't ticked.
    for (auto& entry : searchIndex.entries) {
        if (entry.title == "Loop playback") {
            entry.state = [owner] { return owner != nullptr && owner->processor.document.project().looping ? State::on : State::off; };
        }
    }
}

void MotionEditor::indexAdding() {
    const juce::Component::SafePointer<MotionEditor> owner(this);
    const auto acting = [owner](std::function<void(MotionEditor&)> act) {
        return [owner, act = std::move(act)] {
            if (owner != nullptr) {
                act(*owner);
            }
        };
    };
    auto& entries = searchIndex.entries;
    const auto& project = processor.document.project();
    // Effects, added to what Properties shows.
    const auto effectsOwner = effectStack.effectOwner();
    const auto effectsTo = !effectsOwner.has_value() ? juce::String() : *effectsOwner == 0 ? juce::String("to the composition") : "to " + selectedSearchName;
    juce::String noEffects = "Audio takes no effects";
    if (selectionIsCamera()) {
        noEffects = "A camera takes no effects";
    } else if (selection == project.beam.id) {
        noEffects = "The Scope takes no effects";
    }
    for (const auto& definition : motion::effectCatalog()) {
        Entry entry {
            .id = "effect:" + juce::String(definition.id.data(), definition.id.size()),
            .title = "Add " + juce::String(definition.name.data(), definition.name.size()) + " effect",
            .kind = Kind::add,
            .path = effectsTo.isNotEmpty() ? effectsTo : juce::String("Effects"),
            .keywords = effectKeywords(definition),
            .run = acting([type = std::string(definition.id)](MotionEditor& editor) { editor.effectStack.addEffect(type); }),
        };
        if (!effectsOwner.has_value()) {
            entry.unavailable = [noEffects] { return noEffects; };
        }
        entries.push_back(std::move(entry));
    }
    // Modulators, of each kind.
    for (const auto& kind : MotionModulatorLibrary::kinds()) {
        entries.push_back({
            .id = juce::String("modulator:") + kind.name,
            .title = juce::String("Add ") + kind.name + " modulator",
            .kind = Kind::add,
            .path = "Modulators",
            .keywords = vocabulary::modulatorKeywords(kind.name),
            .run = acting([waveform = kind.waveform](MotionEditor& editor) {
                editor.libraryTabs.setSelectedIndex(2);
                editor.modulatorLibrary.add(waveform);
            }),
        });
    }
    entries.push_back({.id = "add:track", .title = "Add track", .kind = Kind::add, .path = "Timeline", .keywords = {"new track", "layer", "lane"},
        .run = acting([](MotionEditor& editor) { editor.timeline.addNewTrack(); })});
}

void MotionEditor::indexSelection() {
    const juce::Component::SafePointer<MotionEditor> owner(this);
    const auto acting = [owner](std::function<void(MotionEditor&)> act) {
        return [owner, act = std::move(act)] {
            if (owner != nullptr) {
                act(*owner);
            }
        };
    };
    auto& entries = searchIndex.entries;
    const auto& project = processor.document.project();
    const auto target = motion::findPropertyTarget(project, selection);
    const auto selectedPath = selectedSearchName.isNotEmpty() ? selectedSearchName : juce::String("Properties");

    // Its properties as Properties shows them: going to one scrolls to it
    // and marks it.
    for (const auto& row : propertyInspector.searchableRows()) {
        Entry entry {
            .id = "property:" + juce::String(row.property),
            .title = row.group,
            .kind = Kind::property,
            .path = selectedPath + separator + row.section,
            .keywords = row.labels,
            .run = acting([property = row.property, group = row.group](MotionEditor& editor) { editor.revealForSearch(property, group.toLowerCase()); }),
            .relevance = 15,
        };
        entry.keywords.add(row.section);
        entry.keywords.addArray(vocabulary::propertyKeywords(row.group));
        if (row.unavailable.isNotEmpty()) {
            entry.unavailable = [reason = row.unavailable] { return reason; };
        }
        if (suggestedSearchIds.size() < 5 && row.unavailable.isEmpty()) {
            suggestedSearchIds.add(entry.id);
        }
        entries.push_back(std::move(entry));
    }

    // Its timing: greyed, saying why, when it isn't a clip.
    const auto isClip = target.has_value() && !target->isGroup && !target->camera && !target->beam && findNamed(clipTimingPanel, "Clip speed") != nullptr;
    for (const auto& [title, name, words] : std::initializer_list<std::tuple<const char*, const char*, juce::StringArray>> {
             {"Start", "Clip start", {"in point", "begin", "clip"}},
             {"Duration", "Clip duration", {"length", "how long", "trim", "clip"}},
             {"Offset", "Clip source offset", {"slip", "source offset", "clip"}},
             {"Speed", "Clip speed", {"retime", "time stretch", "slow motion", "slow down", "speed up", "faster", "slower", "playback rate", "clip"}},
         }) {
        Entry entry {
            .id = "timing:" + juce::String(name),
            .title = title,
            .kind = Kind::property,
            .path = (isClip ? selectedPath : juce::String("Clip")) + separator + "Timing",
            .keywords = words,
            .run = acting([name = juce::String(name)](MotionEditor& editor) {
                auto* found = findNamed(editor.clipTimingPanel, name);
                if (found == nullptr) {
                    editor.sayAfterSearch("Select a clip first");
                    return;
                }
                osci::RevealOptions options;
                options.align = osci::RevealOptions::Align::centre;
                options.focus = true;
                editor.propertyInspector.reveal(*found, std::move(options));
            }),
            .relevance = isClip ? 15 : 0,
        };
        if (!isClip) {
            entry.unavailable = [] { return juce::String("Select a clip first"); };
        }
        entries.push_back(std::move(entry));
    }

    // The effects on it: their settings, not another effect.
    juce::Array<juce::Component*> cards;
    findAllStarting(effectStack, "", cards);
    for (auto* component : cards) {
        if (!component->getName().endsWith(" effect") || !component->isShowing()) {
            continue;
        }
        const auto card = component->getName();
        juce::Array<juce::Component*> fields;
        findAllStarting(*component, "Effect ", fields);
        for (auto* field : fields) {
            if (!field->isShowing() || field->getTitle().isEmpty()) {
                continue;
            }
            const juce::Component::SafePointer<juce::Component> safe(field);
            entries.push_back({
                .id = "effect setting:" + card + "/" + field->getTitle(),
                .title = field->getTitle(),
                .kind = Kind::property,
                .path = selectedPath + separator + capitalised(card),
                .keywords = {card},
                .run = acting([safe](MotionEditor& editor) {
                    if (safe == nullptr) {
                        return;
                    }
                    osci::RevealOptions options;
                    options.align = osci::RevealOptions::Align::centre;
                    editor.propertyInspector.reveal(*safe, std::move(options));
                }),
                .relevance = 12,
            });
        }
    }

    // Tiles in Properties as the choices they are: a surface style, wireframe
    // or solid, how a camera shades, a light's preset.
    juce::Array<juce::Component*> tiles;
    for (const auto* grid : {"Surface styles: ", "Solidity: ", "Shading: ", "Light presets: "}) {
        findAllStarting(propertyInspector, grid, tiles);
    }
    for (auto* component : tiles) {
        auto* tile = dynamic_cast<juce::Button*>(component);
        if (tile == nullptr || !tile->isShowing()) {
            continue;
        }
        const auto grid = tile->getName().upToFirstOccurrenceOf(": ", false, false), choice = tile->getName().fromFirstOccurrenceOf(": ", false, false);
        juce::String title, section = "Surface";
        if (grid == "Surface styles") {
            title = "Surface: " + choice;
        } else if (grid == "Solidity") {
            title = choice == "Solid" ? juce::String("Make solid") : juce::String("Make wireframe");
        } else if (grid == "Shading") {
            title = "Shade by " + choice.toLowerCase();
            section = "Shading";
        } else {
            title = "Light preset: " + choice;
            section = "Presets";
        }
        const juce::Component::SafePointer<juce::Button> safe(tile);
        // Solidity, shading and presets are one of several; each surface style is a switch.
        const auto oneOf = grid != "Surface styles";
        entries.push_back({
            .id = "tile:" + tile->getName(),
            .title = title,
            .kind = Kind::setting,
            .path = selectedPath + separator + section,
            .keywords = {choice, grid},
            .state = [safe, oneOf] {
                const auto on = safe != nullptr && safe->getToggleState();
                return oneOf ? (on ? State::chosen : State::other) : (on ? State::on : State::off);
            },
            .run = [safe] {
                if (safe != nullptr) {
                    safe->triggerClick();
                }
            },
            .relevance = 12,
            .family = oneOf ? grid : juce::String(),
        });
    }

    // Its track: mute, solo, lock, rename; greyed, saying why, without one.
    const motion::Track* track = nullptr;
    for (const auto& candidate : project.tracks) {
        const auto holds = std::any_of(candidate.clips.begin(), candidate.clips.end(), [this](const auto& clip) { return clip.id == selection; });
        if (candidate.id == selection || holds) {
            track = &candidate;
        }
    }
    const auto trackName = track != nullptr ? juce::String(track->name) : juce::String("track");
    // Every track's switches, the selection's first; greyed, saying why,
    // when there is none.
    for (const auto& [verb, chip, words] : std::initializer_list<std::tuple<const char*, const char*, juce::StringArray>> {
             {"Mute", "Mute track ", {"silence", "hide", "disable"}},
             {"Solo", "Solo track ", {"isolate", "only this"}},
             {"Lock", "Lock track ", {"protect", "freeze"}},
         }) {
        auto any = false;
        for (const auto& each : project.tracks) {
            auto* button = dynamic_cast<juce::Button*>(findNamed(timeline, juce::String(chip) + juce::String(each.id)));
            if (button == nullptr) {
                continue;
            }
            any = true;
            const juce::Component::SafePointer<juce::Button> safe(button);
            entries.push_back({
                .id = "track:" + juce::String(verb) + " " + juce::String(each.id),
                .title = juce::String(verb) + " " + juce::String(each.name),
                .kind = Kind::action,
                .path = "Track",
                .keywords = words,
                .state = [safe] { return safe != nullptr && safe->getToggleState() ? State::on : State::off; },
                .run = [safe] {
                    if (safe != nullptr) {
                        safe->triggerClick();
                    }
                },
                .relevance = &each == track ? 10 : 0,
                .family = "track:" + juce::String(verb),
            });
        }
        if (!any) {
            entries.push_back({.id = "track:" + juce::String(verb), .title = juce::String(verb) + " track", .kind = Kind::action, .path = "Track", .keywords = words,
                .unavailable = [] { return juce::String("Add a source first"); }, .run = [] {}});
        }
    }
    {
        auto* label = track != nullptr ? dynamic_cast<juce::Label*>(findNamed(timeline, "Track name " + juce::String(track->id))) : nullptr;
        const juce::Component::SafePointer<juce::Label> safe(label);
        Entry rename {
            .id = "track:Rename",
            .title = "Rename " + trackName,
            .kind = Kind::action,
            .path = "Track",
            .keywords = {"rename", "name", "label", "title"},
            .run = [safe] {
                if (safe != nullptr) {
                    safe->showEditor();
                }
            },
        };
        if (label == nullptr) {
            rename.unavailable = [] { return juce::String("Select a clip or track first"); };
        }
        entries.push_back(std::move(rename));
    }

    // The selected keys' interpolation, in the timeline or the Graph.
    const auto keysSelected = graphShown() ? curveEditor.keySelection().count > 0 : timeline.hasSelectedKeys();
    for (const auto& [title, shape, words] : std::initializer_list<std::tuple<const char*, motion::Interpolation, juce::StringArray>> {
             {"Hold interpolation", motion::Interpolation::hold, {"constant", "step", "stepped"}},
             {"Linear interpolation", motion::Interpolation::linear, {"straight"}},
             {"Auto interpolation", motion::Interpolation::smooth, {"smooth", "auto bezier"}},
             {"Bezier interpolation", motion::Interpolation::cubic, {"handles", "curve"}},
         }) {
        Entry entry {
            .id = "keys:" + juce::String(title),
            .title = title,
            .kind = Kind::action,
            .path = "Keys",
            .keywords = words,
            .run = acting([shape](MotionEditor& editor) {
                if (editor.graphShown()) {
                    editor.curveEditor.setSelectedInterpolation(shape);
                } else {
                    editor.timeline.setSelectedInterpolation(shape);
                }
            }),
            .family = "interpolation",
        };
        if (!keysSelected) {
            entry.unavailable = [] { return juce::String("Select keys first"); };
        }
        entries.push_back(std::move(entry));
    }
}

void MotionEditor::indexSettings() {
    const juce::Component::SafePointer<MotionEditor> owner(this);
    const auto acting = [owner](std::function<void(MotionEditor&)> act) {
        return [owner, act = std::move(act)] {
            if (owner != nullptr) {
                act(*owner);
            }
        };
    };
    auto& entries = searchIndex.entries;
    const auto& project = processor.document.project();
    const auto selectedPath = selectedSearchName.isNotEmpty() ? selectedSearchName : juce::String("Properties");
    const auto revealIn = [](juce::Component& root, const juce::String& name, bool focus) {
        const juce::Component::SafePointer<juce::Component> safeRoot(&root);
        return [safeRoot, name, focus](MotionEditor& editor) {
            auto* found = safeRoot != nullptr ? findNamed(*safeRoot, name) : nullptr;
            jassert(found != nullptr);
            if (found == nullptr) {
                return;
            }
            osci::RevealOptions options;
            options.align = osci::RevealOptions::Align::centre;
            options.focus = focus;
            editor.propertyInspector.reveal(*found, std::move(options));
        };
    };

    // Choices in settings, set directly: "Canvas: Square · 1024 × 1024".
    const auto beam = project.beam.id;
    const auto addChoices = [&](juce::Component& root, const juce::String& name, const juce::String& label, const juce::String& path) {
        auto* combo = dynamic_cast<juce::ComboBox*>(findNamed(root, name));
        if (combo == nullptr) {
            return;
        }
        const juce::Component::SafePointer<juce::ComboBox> safe(combo);
        for (int index = 0; index < combo->getNumItems(); ++index) {
            const auto id = combo->getItemId(index);
            const auto text = combo->getItemText(index);
            if (id == 0 || text.isEmpty() || !combo->isItemEnabled(id)) {
                continue;
            }
            entries.push_back({
                .id = "choice:" + name + "/" + text,
                .title = label + ": " + text,
                .kind = Kind::setting,
                .path = path,
                .keywords = {name},
                .state = [safe, id] { return safe != nullptr && safe->getSelectedId() == id ? State::chosen : State::other; },
                // Set where it is, leaving the selection as it was.
                .run = [safe, id] {
                    if (safe != nullptr) {
                        safe->setSelectedId(id, juce::sendNotificationSync);
                    }
                },
                // Below the setting itself ("fps" is Frame rate first).
                .relevance = -10,
                .family = label,
            });
        }
    };
    addChoices(compositionSettings, "Composition canvas", "Canvas", "Composition" + separator + "Format");
    addChoices(compositionSettings, "Composition frame rate", "Frame rate", "Composition" + separator + "Timing");
    addChoices(compositionSettings, "Composition sample rate", "Sample rate", "Composition" + separator + "Format");
    addChoices(compositionSettings, "Composition beam frequency", "Beam frequency", "Composition" + separator + "Format");
    addChoices(compositionSettings, "Composition meter", "Meter", "Composition" + separator + "Timing");
    addChoices(scopePanel, "Scope overlay", "Scope overlay", "Scope" + separator + "Display");
    // A text clip's animation; greyed, saying why, for anything else.
    if (findNamed(propertyInspector, "Text animation") != nullptr && textAnimation.isShowing()) {
        addChoices(textAnimation, "Text animation", "Text animation", selectedPath + separator + "Text");
    } else {
        entries.push_back({.id = "setting:text animation", .title = "Text animation", .kind = Kind::setting, .path = "Text clip", .keywords = {"typewriter", "type on", "letters", "animate text"},
            .unavailable = [] { return juce::String("Select a text clip first"); }, .run = [] {}});
    }

    // The Scope's settings: its picture, then how it shows and jumps.
    const auto inScope = [acting, beam](std::function<void(MotionEditor&)> then) {
        return acting([beam, then](MotionEditor& editor) { editor.goToForSearch(beam, "the Scope's settings", then); });
    };
    if (selection != beam) {
        for (const auto& spec : motion::beamPropertySpecs) {
            const auto label = juce::String(spec.label.data(), spec.label.size());
            // The beam's frequency is the composition's (Beam frequency).
            if (label == "Frequency") {
                continue;
            }
            entries.push_back({
                .id = "scope:" + juce::String(spec.id.data(), spec.id.size()),
                .title = label,
                .kind = Kind::setting,
                .path = "Scope" + separator + "Picture",
                .keywords = vocabulary::scopeKeywords(label),
                .run = inScope([property = std::string(spec.id), label](MotionEditor& editor) { editor.revealForSearch(property, label.toLowerCase()); }),
            });
        }
    }
    for (const auto& [title, name, section, words] : std::initializer_list<std::tuple<const char*, const char*, const char*, juce::StringArray>> {
             {"Upsample", "Upsample audio", "Display", {"resample", "smoother"}},
             {"XY only", "Scope XY only", "Jumps", {"z input", "blanking", "two channels"}},
             {"Dwell", "Scope dwell", "Jumps", {"blank", "wait"}},
             {"Travel per unit", "Scope travel per unit", "Jumps", {"jump", "slew"}},
             {"Settle", "Scope settle", "Jumps", {"jump"}},
             {"Scope presets", "Scope presets", "Jumps", {"analog", "laser", "galvo", "vector"}},
         }) {
        entries.push_back({.id = "scope:" + juce::String(name), .title = title, .kind = Kind::setting, .path = "Scope" + separator + section, .keywords = words, .run = inScope(revealIn(scopePanel, name, false))});
    }
    entries.push_back({.id = "place:scope", .title = "Scope settings", .kind = Kind::place, .path = "Scope", .keywords = {"oscilloscope settings", "monitor"},
        .run = acting([beam](MotionEditor& editor) { editor.goToForSearch(beam, "the Scope's settings", {}); })});

    // The composition's settings, by the names their fields go by.
    for (const auto& [title, name, section, words] : std::initializer_list<std::tuple<const char*, const char*, const char*, juce::StringArray>> {
             {"Duration", "Composition duration", "Timing", {"length", "how long"}},
             {"Frame rate", "Composition frame rate", "Timing", {"frames", "fps", "frames per second"}},
             {"Meter", "Composition meter", "Timing", {"time signature", "beats per bar"}},
             {"Canvas", "Composition canvas", "Format", {"size", "aspect", "square", "landscape", "portrait", "resolution", "vertical", "9:16", "16:9", "instagram", "reels"}},
             {"Sample rate", "Composition sample rate", "Format", {"samples", "khz", "flicker", "detail"}},
             {"Beam frequency", "Composition beam frequency", "Format", {"frequency", "hz", "refresh rate", "flicker", "flickering", "redraw"}},
         }) {
        entries.push_back({.id = "composition:" + juce::String(name), .title = title, .kind = Kind::setting, .path = "Composition" + separator + section, .keywords = words,
            .run = acting([reveal = revealIn(compositionSettings, name, false)](MotionEditor& editor) { editor.goToForSearch(0, "the composition", reveal); })});
    }
    entries.push_back({.id = "composition:Project description", .title = "Description", .kind = Kind::setting, .path = "Composition" + separator + "About", .keywords = {"about", "notes", "start screen"},
        .run = acting([reveal = revealIn(propertyInspector, "Project description", false)](MotionEditor& editor) { editor.goToForSearch(0, "the composition", reveal); })});
    entries.push_back({.id = "place:composition", .title = "Composition settings", .kind = Kind::place, .path = "Composition", .keywords = {"project settings", "format", "timing"},
        .run = acting([](MotionEditor& editor) { editor.goToForSearch(0, "the composition", {}); })});
    entries.push_back({.id = "setting:output", .title = "Output", .kind = Kind::setting, .path = "Status bar",
        .keywords = {"routing", "channels", "device", "sound outputs", "beam outputs", "beam form", "xy mode", "xyz", "xyrgb", "z input", "laser", "speakers", "stereo"},
        .run = acting([](MotionEditor& editor) { editor.showOutputPanel(); })});
    entries.push_back({.id = "setting:tempo", .title = "Tempo", .kind = Kind::setting, .path = "Transport", .keywords = {"bpm", "beats per minute"},
        .run = acting([](MotionEditor& editor) { editor.tempoValue.showEditor(); }), .relevance = 6});
    entries.push_back({.id = "action:tap tempo", .title = "Tap tempo", .kind = Kind::action, .path = "Transport", .keywords = {"tap", "bpm", "beat"},
        .run = acting([](MotionEditor& editor) { editor.sayAfterSearch("Press Tap, beside the tempo, on the beat four times or more"); })});
    entries.push_back({.id = "action:auto-key", .title = "Auto-key", .kind = Kind::action, .path = "Transport", .keywords = {"record keys", "auto keyframe", "animate mode", "record"},
        .state = [owner] { return owner != nullptr && owner->autoKeyButton.getToggleState() ? State::on : State::off; },
        .run = acting([](MotionEditor& editor) { editor.setAutoKey(!editor.autoKeyButton.getToggleState()); })});
    {
        Entry history {.id = "action:undo history", .title = "Undo history", .kind = Kind::action, .path = "Edit", .keywords = {"history", "steps", "go back", "revert"},
            .run = acting([](MotionEditor& editor) { osci::showUndoHistory(editor.processor.getUndoManager(), false, editor.undoButton, [&editor] { editor.refreshUndo(); }); })};
        if (!processor.getUndoManager().canUndo()) {
            history.unavailable = [] { return juce::String("Nothing to undo"); };
        }
        entries.push_back(std::move(history));
    }
}

void MotionEditor::indexObjects() {
    const juce::Component::SafePointer<MotionEditor> owner(this);
    const auto acting = [owner](std::function<void(MotionEditor&)> act) {
        return [owner, act = std::move(act)] {
            if (owner != nullptr) {
                act(*owner);
            }
        };
    };
    auto& entries = searchIndex.entries;
    const auto& project = processor.document.project();
    const auto selectedGroups = [this] {
        std::set<juce::String> groups;
        for (const auto& row : propertyInspector.searchableRows()) {
            groups.insert(row.group);
        }
        return groups;
    }();
    // Another object's property: going there selects it.
    std::set<juce::String> groupsSomewhere;
    const auto addProperty = [&](motion::Id id, const juce::String& name, const juce::String& group, const std::string& property) {
        groupsSomewhere.insert(group);
        entries.push_back({
            .id = "object property:" + juce::String(id) + "/" + group,
            .title = name + "'s " + group.toLowerCase(),
            .kind = Kind::property,
            .path = name + separator + sectionOfGroup(group),
            .keywords = vocabulary::propertyKeywords(group),
            .run = acting([id, property, name, group](MotionEditor& editor) {
                editor.goToForSearch(id, name + "'s " + group.toLowerCase(), [property, group](MotionEditor& shown) { shown.revealForSearch(property, group.toLowerCase()); });
            }),
            .family = "object property:" + group,
        });
    };
    const auto selectClip = [](motion::Id id) {
        return [id](MotionEditor& editor) {
            editor.select(id);
            editor.timelineTabs.setSelectedIndex(0);
            editor.timeline.revealSelection();
        };
    };
    // The project's own objects, by name: going to one selects it.
    for (const auto& track : project.tracks) {
        entries.push_back({.id = "object:" + juce::String(track.id), .title = juce::String(track.name), .kind = Kind::object, .path = "Track", .keywords = {"track"}, .run = acting(selectClip(track.id)), .literal = true});
        for (const auto& clip : track.clips) {
            const auto name = juce::String(clip.name);
            entries.push_back({.id = "object:" + juce::String(clip.id), .title = name, .kind = Kind::object, .path = "Clip on " + juce::String(track.name), .keywords = {"clip"}, .run = acting(selectClip(clip.id)), .literal = true});
            if (clip.id == selection || track.kind != motion::TrackKind::visual) {
                continue;
            }
            const auto* generator = motion::generatorFor(project, clip.id);
            std::set<juce::String> done;
            for (const auto& spec : motion::objectInspectorSpecs) {
                const auto group = juce::String(spec.group.data(), spec.group.size());
                const auto wanted = group == "Position" || group == "Rotation" || group == "Scale" || group == "Anchor" || group == "Colour" || group == "Opacity";
                if (!wanted || done.contains(group) || !motion::hasProperty(generator, spec.id)) {
                    continue;
                }
                done.insert(group);
                addProperty(clip.id, name, group, std::string(spec.id));
            }
        }
    }
    for (const auto& camera : project.cameras) {
        const auto name = juce::String(camera.name);
        entries.push_back({.id = "object:" + juce::String(camera.id), .title = name, .kind = Kind::object, .path = "Camera", .keywords = {"camera"},
            .run = acting([id = camera.id](MotionEditor& editor) { editor.select(id); }), .literal = true});
        if (camera.id == selection) {
            continue;
        }
        for (const auto& [group, property] : std::initializer_list<std::pair<const char*, const char*>> {{"Position", "position.x"}, {"Rotation", "rotation.x"}, {"Field of view", "fov"}}) {
            addProperty(camera.id, name, group, property);
        }
    }
    for (const auto& marker : project.markers) {
        entries.push_back({.id = "object:" + juce::String(marker.id), .title = marker.name, .kind = Kind::object, .path = "Marker" + motion::style::dot() + motion::style::trimmed(marker.time, 2) + "s",
            .keywords = {"marker"}, .run = acting([time = marker.time](MotionEditor& editor) { editor.seekAndReveal(time); }), .literal = true});
    }
    for (const auto& modulator : project.modulators) {
        entries.push_back({.id = "object:" + juce::String(modulator.id), .title = juce::String(modulator.name), .kind = Kind::object, .path = "Modulator", .keywords = {"modulator"},
            .run = acting([id = modulator.id](MotionEditor& editor) {
                editor.libraryTabs.setSelectedIndex(2);
                editor.modulatorLibrary.select(id);
            }),
            .literal = true});
    }
    for (const auto& asset : project.assets) {
        if (asset == nullptr) {
            continue;
        }
        entries.push_back({.id = "object:" + juce::String(asset->id), .title = asset->name, .kind = Kind::object, .path = "Source", .keywords = {"source"},
            .run = acting([id = asset->id](MotionEditor& editor) {
                editor.libraryTabs.setSelectedIndex(0);
                editor.assetLibrary.revealAsset(id);
            }),
            .literal = true});
    }
    // Properties nothing in the project has: found, with what to do.
    for (const auto* group : {"Position", "Rotation", "Scale", "Colour", "Opacity"}) {
        if (selectedGroups.contains(group) || groupsSomewhere.contains(group)) {
            continue;
        }
        entries.push_back({.id = "property:any:" + juce::String(group), .title = group, .kind = Kind::property, .path = "Properties", .keywords = vocabulary::propertyKeywords(group),
            .unavailable = [] { return juce::String("Add a source first"); }, .run = [] {}});
    }
    // A source's own surface and light, which only the selection's
    // Properties show: found, saying to select one.
    for (const auto* group : {"Solid", "Lit by lights", "Mirror", "Beam share"}) {
        if (selectedGroups.contains(group)) {
            continue;
        }
        const auto reason = "Select a clip to change its " + juce::String(group).toLowerCase();
        entries.push_back({.id = "property:any:" + juce::String(group), .title = group, .kind = Kind::property, .path = "Clip" + separator + sectionOfGroup(group), .keywords = vocabulary::propertyKeywords(group),
            .unavailable = [reason] { return reason; }, .run = [] {}});
    }
}

void MotionEditor::indexTools() {
    const juce::Component::SafePointer<MotionEditor> owner(this);
    const auto acting = [owner](std::function<void(MotionEditor&)> act) {
        return [owner, act = std::move(act)] {
            if (owner != nullptr) {
                act(*owner);
            }
        };
    };
    auto& entries = searchIndex.entries;
    const auto click = [](juce::Button& button) {
        return [safe = juce::Component::SafePointer<juce::Button>(&button)] {
            if (safe != nullptr) {
                safe->triggerClick();
            }
        };
    };
    const auto toggled = [](juce::Button& button) {
        return [safe = juce::Component::SafePointer<juce::Button>(&button)] { return safe != nullptr && safe->getToggleState() ? State::on : State::off; };
    };
    // The library's tabs.
    for (const auto& [title, index, words] : std::initializer_list<std::tuple<const char*, int, juce::StringArray>> {
             {"Show Sources", 0, {"library", "assets", "files", "media"}},
             {"Show Effects", 1, {"library", "effect list", "browse effects"}},
             {"Show Modulators", 2, {"library", "lfo"}},
         }) {
        entries.push_back({.id = "place:library:" + juce::String(title).fromFirstOccurrenceOf(" ", false, false), .title = title, .kind = Kind::place, .path = "Library", .keywords = words,
            .run = acting([index](MotionEditor& editor) { editor.libraryTabs.setSelectedIndex(index); })});
    }
    // The Scene's tools, as its strip has them (its menu has the rest).
    for (auto* tool : std::initializer_list<juce::Button*> {&sceneTools.move, &sceneTools.rotate, &sceneTools.scale, &sceneTools.anchor, &sceneTools.path, &sceneTools.faces, &sceneTools.fly}) {
        Entry entry {.id = "scene:" + tool->getName(), .title = tool->getName(), .kind = Kind::action, .path = "Scene", .state = toggled(*tool), .run = click(*tool)};
        // The tooltip's key, "(G)", is the shortcut.
        const auto tip = tool->getTooltip();
        const auto open = tip.lastIndexOfChar('('), close = tip.lastIndexOfChar(')');
        if (open >= 0 && close == open + 2) {
            entry.shortcut = tip.substring(open + 1, close);
        }
        if (!tool->isEnabled()) {
            const auto reason = selectionIsCamera() ? juce::String("A camera has no scale or anchor") : juce::String("Not for the selection");
            entry.unavailable = [reason] { return reason; };
        }
        entries.push_back(std::move(entry));
    }
    // The timeline's tools.
    using Tool = MotionTimelineView::Tool;
    for (const auto& [title, tool, key, words] : std::initializer_list<std::tuple<const char*, Tool, const char*, juce::StringArray>> {
             {"Select tool", Tool::move, "V", {"move clips", "arrow"}},
             {"Slip tool", Tool::slip, "S", {"slip content"}},
             {"Stretch tool", Tool::stretch, "R", {"time stretch"}},
             {"Ripple trim tool", Tool::ripple, "B", {"ripple edit"}},
         }) {
        entries.push_back({.id = "timeline:" + juce::String(title), .title = title, .kind = Kind::action, .path = "Timeline", .keywords = words, .shortcut = key,
            .state = [owner, tool] { return owner != nullptr && owner->timeline.currentTool() == tool ? State::chosen : State::other; },
            .run = acting([tool](MotionEditor& editor) { editor.timeline.setTool(tool); })});
    }
    // The Scope's own controls.
    entries.push_back({.id = "scope:" + scopeTools.textureOutput.getName(), .title = scopeTools.textureOutput.getName(), .kind = Kind::action, .path = "Scope",
        .keywords = {"syphon", "spout", "stream", "obs", "send"}, .state = toggled(scopeTools.textureOutput), .run = click(scopeTools.textureOutput)});
    for (auto* tool : std::initializer_list<juce::Button*> {&scopeTools.popout, &scopeTools.fullScreen}) {
        entries.push_back({.id = "scope:" + tool->getName(), .title = tool->getName(), .kind = Kind::action, .path = "Scope", .run = click(*tool)});
    }
}

void MotionEditor::indexGoals() {
    const juce::Component::SafePointer<MotionEditor> owner(this);
    const auto acting = [owner](std::function<void(MotionEditor&)> act) {
        return [owner, act = std::move(act)] {
            if (owner != nullptr) {
                act(*owner);
            }
        };
    };
    const auto& project = processor.document.project();
    const auto target = motion::findPropertyTarget(project, selection);
    const auto visual = target.has_value() && !target->isGroup && !target->camera && !target->beam;
    // Goals lead the rows they go to.
    const auto needsClip = [visual](Entry entry) {
        entry.relevance = 15;
        if (!visual) {
            entry.unavailable = [] { return juce::String("Select a clip first"); };
        }
        return entry;
    };
    using Waveform = motion::ModulationWaveform;
    struct Drive {
        std::string property;
        double amount;
        motion::ModulationMode mode;
    };
    // Adds a modulator driving `drives` of `id`, as one undo step; shows the
    // first in the Graph and says what it did, with Undo.
    const auto drive = [acting](juce::String name, motion::Modulation shape, std::vector<Drive> drives, juce::String done) {
        return acting([name, shape, drives, done](MotionEditor& editor) {
            const auto id = editor.selection;
            auto& document = editor.processor.document;
            for (const auto& each : drives) {
                if (!motion::drivableProperty(document.project(), id, each.property)) {
                    editor.sayAfterSearch("This can't be driven by a modulator");
                    return;
                }
            }
            motion::Modulator modulator;
            modulator.id = document.newId();
            modulator.name = name.toStdString();
            modulator.shape = shape;
            std::vector<motion::ModulationRoute> routes;
            for (const auto& each : drives) {
                routes.push_back({document.newId(), modulator.id, id, each.property, each.amount, each.mode});
            }
            document.edit(name, [modulator, routes](motion::Project& project) {
                project.modulators.push_back(modulator);
                project.routes.insert(project.routes.end(), routes.begin(), routes.end());
            });
            editor.modulatorLibrary.refresh();
            editor.modulate(id, drives.front().property, editor.selectionIsCamera());
            const juce::Component::SafePointer<MotionEditor> safe(&editor);
            editor.sayAfterSearch(done + ": its modulator is in Modulators, and its curve in the Graph", "Undo", [safe] {
                if (safe != nullptr) {
                    safe->processor.getUndoManager().undo();
                }
            });
        });
    };
    motion::Modulation spin;
    spin.waveform = Waveform::saw;
    spin.tempoSync = true;
    spin.beatsPerCycle = 4;
    motion::Modulation pulse;
    pulse.waveform = Waveform::soundtrack;
    motion::Modulation shake;
    shake.waveform = Waveform::noiseSmooth;
    shake.rateHz = 4;
    const auto add = motion::ModulationMode::add, multiply = motion::ModulationMode::multiply;
    auto& entries = searchIndex.entries;
    entries.push_back(needsClip({.id = "goal:spin", .title = "Make it spin", .kind = Kind::action, .path = "How to", .keywords = {"spin", "rotate forever", "keep turning", "revolve", "spinning"},
        .run = drive("Spin", spin, {{"rotation.z", 180, add}}, "Spins once every 4 beats")}));
    {
        auto pulsing = needsClip({.id = "goal:pulse", .title = "Pulse to the music", .kind = Kind::action, .path = "How to", .keywords = {"pulse", "beat", "audio reactive", "react to music", "bounce to the beat", "sidechain"},
            .run = drive("Pulse", pulse, {{"scale.x", .3, multiply}, {"scale.y", .3, multiply}}, "Grows with the soundtrack's loudness")});
        const auto hasSoundtrack = std::any_of(project.tracks.begin(), project.tracks.end(), [](const auto& track) { return track.kind == motion::TrackKind::audio && !track.clips.empty(); });
        if (visual && !hasSoundtrack) {
            pulsing.unavailable = [] { return juce::String("Add a soundtrack first"); };
        }
        entries.push_back(std::move(pulsing));
    }
    // Fading is keys, set by hand: shown where, and how.
    entries.push_back(needsClip({.id = "goal:fade", .title = "Fade it in or out", .kind = Kind::action, .path = "How to", .keywords = {"fade in", "fade out", "appear", "disappear", "dissolve", "fading"},
        .run = acting([](MotionEditor& editor) {
            editor.revealForSearch("opacity", "opacity");
            editor.sayAfterSearch("Click Opacity's diamond to key it, move the playhead, then change it: 0% to 100% fades in");
        })}));
    entries.push_back(needsClip({.id = "goal:path", .title = "Move it along a path", .kind = Kind::action, .path = "How to", .keywords = {"motion path", "travel", "animate position", "move over time"},
        .run = acting([](MotionEditor& editor) {
            editor.revealForSearch("position.x", "position");
            editor.sayAfterSearch("Key Position at two times or more; Show motion path draws the way it goes in the Scene");
        })}));
    {
        Entry shaking {.id = "goal:shake", .title = "Shake the camera", .kind = Kind::action, .path = "How to", .keywords = {"camera shake", "shake", "handheld", "jitter", "wobble"},
            .run = drive("Shake", shake, {{"rotation.x", .8, add}, {"rotation.y", .8, add}}, "Shakes the camera"), .relevance = 15};
        if (!selectionIsCamera()) {
            shaking.unavailable = [] { return juce::String("Select a camera first (the Cameras row)"); };
        }
        entries.push_back(std::move(shaking));
    }
}
