#include "MotionEditor.h"

namespace {
bool canSplitClip(const motion::Clip* clip, double time, const motion::Tempo& bpm) {
    return clip != nullptr && clip->valid() && std::isfinite(time) && time > clip->timing(bpm).start && time < clip->timing(bpm).end();
}
}

// Menu 0 (File) is built by buildFileMenu, so its commands only bind keys.
void MotionEditor::addCommand(int menu, juce::String name, juce::KeyPress key, juce::String shortcut, std::function<void()> action) {
    if (menu != 0) { menus.addMenuItem(menu, name, action, motion::style::shortcutText(shortcut)); }
    commands.push_back({menu, std::move(name), std::move(shortcut), key, std::move(action)});
}

void MotionEditor::buildFileMenu(juce::PopupMenu& menu) {
    menu.addItem(motion::style::menuItem("New project", 1001, "Cmd+N"));
    menu.addItem(motion::style::menuItem("Open project...", 1002, "Cmd+O"));
    menus.addRecentProjectsSubmenu(menu, processor, 1100, 1099);
    menu.addSeparator();
    menu.addItem(motion::style::menuItem("Save project", 1003, "Cmd+S"));
    menu.addItem(motion::style::menuItem("Save project as...", 1004, "Cmd+Shift+S"));
    menu.addSeparator();
    menu.addItem(motion::style::menuItem("Import source...", 1005, "Cmd+I"));
    menu.addSeparator();
    menu.addItem(motion::style::menuItem("Export XYRGB signal...", 1006, {}));
#if OSCI_PREMIUM
    menu.addItem(motion::style::menuItem("Export video...", 1007, {}));
#endif
}

bool MotionEditor::fileMenuItemSelected(int id) {
    if (menus.handleRecentProjectMenuItem(id, processor, *this, 1100, 1099)) { return true; }
    switch (id) {
        case 1001: resetToDefault(); return true;
        case 1002: CommonPluginEditor::openProject(); return true;
        case 1003: saveProject(); return true;
        case 1004: saveProjectAs(); return true;
        case 1005: chooseSourceFile(); return true;
        case 1006: exportSignal(); return true;
        case 1007: exportVideo(); return true;
        default: return false;
    }
}

void MotionEditor::registerCommands() {
    const auto command = juce::ModifierKeys::commandModifier;
    const auto shift = juce::ModifierKeys::shiftModifier;
    menus.addMenuSeparator(1);
    addCommand(1, "Cut", juce::KeyPress('x', command, 0), "Cmd+X", [this] { copySelection(true); });
    addCommand(1, "Copy", juce::KeyPress('c', command, 0), "Cmd+C", [this] { copySelection(false); });
    addCommand(1, "Paste", juce::KeyPress('v', command, 0), "Cmd+V", [this] { pasteClipboard(); });
    addCommand(1, "Delete", juce::KeyPress(), "Delete", [this] { timeline.deleteSelection(); });
    // Cut, Copy and Delete act on selected keys or clips; Paste needs a copy.
    const auto anySelection = [this] { return timeline.hasSelectedKeys() || timeline.hasSelectedClips(); };
    for (const auto* name : {"Cut", "Copy", "Delete"}) { menus.setMenuItemEnabled(1, name, anySelection); }
    menus.setMenuItemEnabled(1, "Paste", [this] { return !std::holds_alternative<std::monostate>(clipboard); });
    menus.addMenuSeparator(1);
    addCommand(1, "Select all", juce::KeyPress('a', command, 0), "Cmd+A", [this] {
        if (timelineTabs.getCurrentTabIndex() == 1) { curveEditor.selectAllKeys(); } else { timeline.selectAll(); }
    });
    // After Effects' Easy Ease, on the selected keys of the visible editor.
    const auto ease = [this](bool in, bool out) {
        const auto graph = timelineTabs.getCurrentTabIndex() == 1;
        const auto selected = graph ? curveEditor.hasSelectedKeys() : timeline.hasSelectedKeys();
        if (!selected) {
            statusBar.show("Select keyframes to ease.");
            return;
        }
        // Smooth keys already ease at their ends, so say what happened.
        const auto eased = graph ? curveEditor.easeSelected(in, out) : timeline.easeSelectedKeys(in, out);
        const juce::String what = in && out ? "Easy ease" : (in ? "Easy ease in" : "Easy ease out");
        statusBar.show(eased ? what + " applied: the keys stop with a third of each segment as influence." : "The selected keys already ease that way.", MotionStatusBar::Kind::notice);
    };
    menus.addMenuSeparator(1);
    addCommand(1, "Easy ease", juce::KeyPress(juce::KeyPress::F9Key), "F9", [ease] { ease(true, true); });
    addCommand(1, "Easy ease in", juce::KeyPress(juce::KeyPress::F9Key, shift, 0), "Shift+F9", [ease] { ease(true, false); });
    addCommand(1, "Easy ease out", juce::KeyPress(juce::KeyPress::F9Key, command | shift, 0), "Cmd+Shift+F9", [ease] { ease(false, true); });
    addCommand(2, "Split at playhead", juce::KeyPress('k', command, 0), "Cmd+K", [this] { splitAtPlayhead(); });
    addCommand(2, "Duplicate", juce::KeyPress('d', command, 0), "Cmd+D", [this] {
        std::vector<motion::Id> duplicates;
        const auto clips = timeline.copiedClips();
        std::vector<motion::Id> ids;
        for (const auto& copied : clips) { ids.push_back(copied.clip.id); }
        if (ids.empty()) { return; }
        const auto result = processor.document.duplicateClips(ids, duplicates);
        if (result.failed()) { statusBar.show("Cannot duplicate: " + result.getErrorMessage()); return; }
        timeline.selectClips(duplicates);
    });
    menus.addMenuSeparator(2);
    // After Effects conventions: [ and ] move the clip's start or end to the
    // playhead; Alt+[ and Alt+] trim it there.
    addCommand(2, "Move clip start to playhead", juce::KeyPress('[', 0, 0), "[", [this] { placeClipAtPlayhead(true, false); });
    addCommand(2, "Move clip end to playhead", juce::KeyPress(']', 0, 0), "]", [this] { placeClipAtPlayhead(false, false); });
    addCommand(2, "Trim clip start to playhead", juce::KeyPress('[', juce::ModifierKeys::altModifier, 0), "Alt+[", [this] { placeClipAtPlayhead(true, true); });
    addCommand(2, "Trim clip end to playhead", juce::KeyPress(']', juce::ModifierKeys::altModifier, 0), "Alt+]", [this] { placeClipAtPlayhead(false, true); });
    addCommand(2, "Show keyframe lanes", juce::KeyPress('u', 0, 0), "U", [this] { timeline.toggleLanesForSelection(); });
    menus.addMenuSeparator(2);
    const auto alt = juce::ModifierKeys::altModifier;
    for (const auto& [letter, group, name] : std::initializer_list<std::tuple<char, const char*, const char*>> {
             {'p', "Position", "Key position"}, {'r', "Rotation", "Key rotation"}, {'s', "Scale", "Key scale"}, {'c', "Colour", "Key colour"}, {'t', "Drawing", "Key drawing weight"}}) {
        const juce::String groupName(group);
        addCommand(2, name, juce::KeyPress(letter, alt | shift, 0), "Alt+Shift+" + juce::String::charToString(letter).toUpperCase(), [this, groupName] {
            if (!propertyInspector.toggleGroupKeys(groupName)) { statusBar.show("Select a clip or camera with " + groupName.toLowerCase() + " to key it."); }
        });
    }

    addCommand(3, "Play / pause", juce::KeyPress(juce::KeyPress::spaceKey), "Space", [this] { togglePlayback(); });
    addCommand(3, "Go to start", juce::KeyPress(juce::KeyPress::homeKey), "Home", [this] { seekAndReveal(0); });
    addCommand(3, "Go to end", juce::KeyPress(juce::KeyPress::endKey), "End", [this] { seekAndReveal(processor.document.project().duration); });
    addCommand(3, "Previous frame", juce::KeyPress(juce::KeyPress::leftKey), "Left", [this] { stepFrames(-1); });
    addCommand(3, "Next frame", juce::KeyPress(juce::KeyPress::rightKey), "Right", [this] { stepFrames(1); });
    addCommand(3, "Back ten frames", juce::KeyPress(juce::KeyPress::leftKey, shift, 0), "Shift+Left", [this] { stepFrames(-10); });
    addCommand(3, "Forward ten frames", juce::KeyPress(juce::KeyPress::rightKey, shift, 0), "Shift+Right", [this] { stepFrames(10); });
    addCommand(3, "Previous key or marker", juce::KeyPress('j', 0, 0), "J", [this] { jumpToKey(false); });
    addCommand(3, "Next key or marker", juce::KeyPress('k', 0, 0), "K", [this] { jumpToKey(true); });
    addCommand(3, "Previous edit point", juce::KeyPress(juce::KeyPress::upKey), "Up", [this] { jumpToEdit(false); });
    addCommand(3, "Next edit point", juce::KeyPress(juce::KeyPress::downKey), "Down", [this] { jumpToEdit(true); });
    menus.addMenuSeparator(3);
    addCommand(3, "Loop playback", juce::KeyPress('l', 0, 0), "L", [this] { toggleLoop(); });
    addCommand(3, "Set loop start at playhead", juce::KeyPress('i', 0, 0), "I", [this] { setLoopEdge(true); });
    addCommand(3, "Set loop end at playhead", juce::KeyPress('o', 0, 0), "O", [this] { setLoopEdge(false); });
    addCommand(3, "Loop selected clips", juce::KeyPress('l', shift, 0), "Shift+L", [this] { loopSelection(); });
    // View: what the lower panel shows, zoom, follow and track height.
    menus.addMenuSeparator(5);
    addCommand(5, "Show timeline", juce::KeyPress('1', juce::ModifierKeys::altModifier, 0), "Alt+1", [this] { timelineTabs.setSelectedIndex(0); });
    addCommand(5, "Show graph", juce::KeyPress('2', juce::ModifierKeys::altModifier, 0), "Alt+2", [this] { timelineTabs.setSelectedIndex(1); });
    menus.addMenuSeparator(5);
    // Zoom acts on whichever lower panel is showing.
    const auto zoom = [this](double factor) {
        if (curveEditor.isVisible()) { curveEditor.zoomTime(curveEditor.getWidth() * .5f, factor); curveEditor.repaint(); return; }
        timeline.zoomBy(factor);
    };
    addCommand(5, "Zoom in", juce::KeyPress('=', command, 0), "Cmd+=", [zoom] { zoom(1.5); });
    addCommand(5, "Zoom out", juce::KeyPress('-', command, 0), "Cmd+-", [zoom] { zoom(1 / 1.5); });
    addCommand(5, "Fit timeline to project", juce::KeyPress(), "F", [this] { timeline.fitProject(); });
    addCommand(5, "Taller tracks", juce::KeyPress('=', command | shift, 0), "Cmd+Shift+=", [this] { timeline.setDefaultTrackHeight(timeline.layout().trackHeight + 8); });
    addCommand(5, "Shorter tracks", juce::KeyPress('-', command | shift, 0), "Cmd+Shift+-", [this] { timeline.setDefaultTrackHeight(timeline.layout().trackHeight - 8); });
    menus.addToggleMenuItem(5, "Follow playhead", [this] {
        auto layout = timeline.layout();
        layout.follow = !layout.follow;
        timeline.setLayout(layout);
        saveLayout();
    }, [this] { return timeline.layout().follow; });
    menus.addMenuSeparator(5);
   #if JUCE_MAC
    const juce::String fullScreenKeys = "Ctrl+Cmd+F";
    commands.push_back({5, "Full screen", fullScreenKeys, juce::KeyPress('f', command | juce::ModifierKeys::ctrlModifier, 0), [this] { toggleFullScreen(); }});
   #else
    const juce::String fullScreenKeys = "F11";
   #endif
    menus.addToggleMenuItem(5, "Full screen", [this] { toggleFullScreen(); }, [this] { return isFullScreen(); }, motion::style::shortcutText(fullScreenKeys));
    menus.addMenuItem(5, "Reset window size and position", [this] { resetWindowSizeAndPosition(); });
    menus.addMenuSeparator(5);
    addCommand(5, "Keyboard shortcuts...", juce::KeyPress('/', command, 0), "Cmd+/", [this] { showShortcuts(); });
    // File items live in the File menu built by buildFileMenu; these bind keys.
    addCommand(0, "Import source...", juce::KeyPress('i', command, 0), "Cmd+I", [this] { chooseSourceFile(); });
}

void MotionEditor::copySelection(bool cut) {
    if (timeline.hasSelectedKeys()) {
        clipboard = timeline.copiedKeys();
    } else if (timeline.hasSelectedClips()) {
        clipboard = timeline.copiedClips();
    } else {
        return;
    }
    if (cut) { timeline.deleteSelection(); }
}

void MotionEditor::pasteClipboard() {
    const auto time = processor.position.load();
    if (const auto* keys = std::get_if<std::vector<motion::Document::CopiedKey>>(&clipboard)) {
        const auto result = processor.document.pasteKeys(selection, *keys, time);
        if (result.failed()) { statusBar.show("Cannot paste keys: " + result.getErrorMessage()); }
        return;
    }
    if (const auto* clips = std::get_if<std::vector<motion::Document::CopiedClip>>(&clipboard)) {
        std::vector<motion::Id> pasted;
        const auto result = processor.document.pasteClips(*clips, time, pasted);
        if (result.failed()) { statusBar.show("Cannot paste clips: " + result.getErrorMessage()); return; }
        timeline.refreshTracks();
        timeline.selectClips(pasted);
    }
}

void MotionEditor::stepFrames(int frames) {
    const auto& project = processor.document.project();
    const auto rate = project.frameRate > 0 ? project.frameRate : 30.0;
    const auto current = std::round(processor.position.load() * rate);
    const auto next = std::clamp((current + frames) / rate, 0.0, project.duration);
    processor.playing.store(false);
    seekAndReveal(next);
}

// Right-clicking the Scene: framing, views along an axis, picking parts and
// keying the selection.
void MotionEditor::showSceneMenu() {
    const std::array<std::pair<const char*, const char*>, 6> views {{{"Front", "1"}, {"Right", "3"}, {"Top", "7"}, {"Back", "Ctrl+1"}, {"Left", "Ctrl+3"}, {"Bottom", "Ctrl+7"}}};
    const std::array<std::pair<const char*, const char*>, 5> keys {{{"Key position", "Alt+Shift+P"}, {"Key rotation", "Alt+Shift+R"}, {"Key scale", "Alt+Shift+S"}, {"Key colour", "Alt+Shift+C"}, {"Key drawing weight", "Alt+Shift+T"}}};
    juce::PopupMenu menu;
    menu.addItem(motion::style::menuItem("Frame selection", 7, "F"));
    menu.addItem(motion::style::menuItem("Reset view", 8, "0"));
    juce::PopupMenu from;
    for (std::size_t index = 0; index < views.size(); ++index) { from.addItem(motion::style::menuItem(views[index].first, static_cast<int>(index) + 1, views[index].second)); }
    menu.addSubMenu("View from", from);
    menu.addSeparator();
    menu.addItem(motion::style::menuItem("Pick parts", 40, "Tab"));
    if (selection != 0) {
        menu.addSeparator();
        for (std::size_t index = 0; index < keys.size(); ++index) { menu.addItem(motion::style::menuItem(keys[index].first, 20 + static_cast<int>(index), keys[index].second)); }
        menu.addSeparator();
        menu.addItem(motion::style::menuItem("Centre anchor", 41, {}));
        menu.addItem(motion::style::menuItem("Show in timeline", 30, {}));
    }
    const auto options = juce::PopupMenu::Options().withTargetComponent(composition).withMousePosition();
    motion::ui::showDocumentMenu(menu, *this, processor.document, options, [this](int result) {
        using Preset = MotionCompositionView::ViewPreset;
        const std::array<Preset, 6> presets {Preset::front, Preset::right, Preset::top, Preset::back, Preset::left, Preset::bottom};
        if (result >= 1 && result <= 6) { composition.setViewPreset(presets[static_cast<std::size_t>(result - 1)]); }
        if (result == 7) { composition.frameSelection(); }
        if (result == 8) { composition.resetView(); }
        const std::array<const char*, 5> groups {"Position", "Rotation", "Scale", "Colour", "Drawing"};
        if (result >= 20 && result < 25 && !propertyInspector.toggleGroupKeys(groups[static_cast<std::size_t>(result - 20)])) {
            statusBar.show("The selection has no such property to key.");
        }
        if (result == 30) { timelineTabs.setSelectedIndex(0); timeline.revealSelection(); }
        if (result == 40) { composition.setPartMode(true); }
        if (result == 41) {
            const auto problem = composition.centreAnchor(selection);
            if (problem.isNotEmpty()) { statusBar.show(problem); }
        }
    });
}

void MotionEditor::togglePlayback() {
    processor.playing.store(!processor.playing.load());
}

void MotionEditor::seekAndReveal(double time) {
    processor.seek(time);
    timeline.revealTime(time);
}

// L toggles looping. Without a range yet, it loops the selection, or the
// four bars from the playhead's bar.
void MotionEditor::toggleLoop() {
    const auto& project = processor.document.project();
    if (!project.hasLoop()) {
        if (timeline.hasSelectedClips()) { loopSelection(); return; }
        const auto tempo = project.tempo();
        const auto bar = std::max(1, project.beatsPerBar);
        // Near the end, the four bars end at the end instead.
        const auto last = std::max(0.0, tempo.beats(project.duration) - 4 * bar);
        const auto first = std::min(std::floor(tempo.beats(processor.position.load()) / bar) * bar, last);
        if (!setLoop(tempo.seconds(first), std::min(project.duration, tempo.seconds(first + 4 * bar)), true, "Set loop")) {
            statusBar.show("The composition is too short to loop.");
        }
        return;
    }
    setLoop(project.loopStart, project.loopEnd, !project.looping, project.looping ? "Stop looping" : "Loop playback");
}

void MotionEditor::setLoopEdge(bool start) {
    const auto& project = processor.document.project();
    const auto time = std::clamp(processor.position.load(), 0.0, project.duration);
    const auto first = start ? time : (project.hasLoop() ? project.loopStart : 0.0);
    const auto last = start ? (project.hasLoop() && project.loopEnd > time ? project.loopEnd : project.duration) : time;
    if (last - first < 1 / std::max(1.0, project.frameRate)) { statusBar.show(start ? "The loop start must be at least a frame before its end." : "The loop end must be at least a frame after its start."); return; }
    // Like a DAW's loop markers, I and O move the range; L switches looping.
    setLoop(first, last, project.looping, start ? "Set loop start" : "Set loop end");
}

void MotionEditor::loopSelection() {
    const auto& project = processor.document.project();
    double first = std::numeric_limits<double>::max(), last = 0;
    for (const auto& track : project.tracks) {
        for (const auto& clip : track.clips) {
            if (!timeline.selectedClipIds().contains(clip.id)) { continue; }
            const auto timing = clip.timing(project.tempo());
            first = std::min(first, timing.start);
            last = std::max(last, timing.end());
        }
    }
    if (last <= first) { statusBar.show("Select clips to loop."); return; }
    setLoop(first, last, true, "Loop selection");
}

// False when the range is shorter than a frame; an unchanged loop is fine.
bool MotionEditor::setLoop(double start, double end, bool enabled, juce::String label) {
    const auto& current = processor.document.project();
    const auto frame = current.frameRate > 0 ? 1 / current.frameRate : 1.0 / 30;
    if (!(end - start >= frame) || start < 0) { return false; }
    if (current.loopStart == start && current.loopEnd == end && current.looping == enabled) { return true; }
    // Switching looping on or off is transport state, not an edit.
    if (current.loopStart == start && current.loopEnd == end) {
        processor.document.changeView([enabled](motion::Composition& state) { state.looping = enabled; });
        return true;
    }
    const motion::Document::ViewChange view(processor.document);
    processor.document.edit(label, [start, end, enabled](motion::Project& state) {
        state.loopStart = start; state.loopEnd = end; state.looping = enabled;
    });
    return true;
}

// Up/Down, like Premiere: the previous or next clip start or end.
void MotionEditor::jumpToEdit(bool forward) {
    const auto now = processor.position.load();
    std::optional<double> best;
    for (const auto time : timeline.editPoints()) {
        const bool candidate = forward ? time > now + 1.0e-6 : time < now - 1.0e-6;
        if (candidate && (!best.has_value() || (forward ? time < *best : time > *best))) { best = time; }
    }
    if (!best.has_value()) { return; }
    const auto time = std::clamp(*best, 0.0, processor.document.project().duration);
    seekAndReveal(time);
}

// J/K, like After Effects: the previous or next visible item - a key of the
// selection, a marker or a tempo change.
void MotionEditor::jumpToKey(bool forward) {
    const auto& project = processor.document.project();
    const auto now = processor.position.load();
    std::optional<double> best;
    const auto consider = [&](double time) {
        const bool candidate = forward ? time > now + 1.0e-6 : time < now - 1.0e-6;
        if (candidate && (!best.has_value() || (forward ? time < *best : time > *best))) { best = time; }
    };
    const auto target = motion::findPropertyTarget(project, selection);
    if (target.has_value() && target->properties != nullptr && target->rate != 0) {
        for (const auto& [name, curve] : *target->properties) {
            for (const auto& key : curve.keyframes()) { consider(target->projectTime(key.time)); }
        }
    }
    for (const auto& marker : project.markers) { consider(marker.time); }
    if (project.tempoChanges != nullptr) {
        const auto tempo = project.tempo();
        for (const auto& change : *project.tempoChanges) { consider(tempo.seconds(change.beat)); }
    }
    if (!best.has_value()) { return; }
    const auto time = std::clamp(*best, 0.0, processor.document.project().duration);
    seekAndReveal(time);
}

void MotionEditor::placeClipAtPlayhead(bool start, bool trim) {
    // Like After Effects, [ ] and Alt+[ ] act on every selected clip at once.
    const auto& project = processor.document.project();
    const auto tempo = project.tempo();
    const auto time = processor.position.load();
    auto ids = timeline.selectedClipIds();
    if (ids.empty() && selection != 0) { ids.insert(selection); }
    std::vector<std::pair<motion::Id, motion::ClipTiming>> timings;
    int skipped = 0;
    for (const auto& track : project.tracks) {
        for (const auto& clip : track.clips) {
            if (!ids.contains(clip.id)) { continue; }
            auto timing = clip.timing(tempo);
            if (trim) {
                if (start) {
                    if (time >= timing.end()) { ++skipped; continue; }
                    timing.offset = timing.localTime(time);
                    timing.setStart(time);
                } else {
                    if (time <= timing.start) { ++skipped; continue; }
                    timing.setEnd(time);
                }
            } else {
                const auto target = start ? time : time - timing.duration();
                if (target < 0) { ++skipped; continue; }
                timing.moveTo(target);
            }
            timings.emplace_back(clip.id, timing);
        }
    }
    if (timings.empty()) {
        if (skipped > 0) { statusBar.show(trim ? "The playhead is outside the selected clips." : "The selected clips cannot start before 0."); }
        return;
    }
    const auto result = processor.document.setClipTimings(timings);
    if (result.failed()) {
        statusBar.show(result.getErrorMessage());
    } else if (skipped > 0) {
        statusBar.show(juce::String(skipped) + (skipped == 1 ? " clip was" : " clips were") + " left unchanged.", MotionStatusBar::Kind::notice);
    }
}

void MotionEditor::splitAtPlayhead() {
    const auto time = processor.position.load();
    const auto& tracks = processor.document.project().tracks;
    const bool canSplit = std::any_of(tracks.begin(), tracks.end(), [&](const motion::Track& track) {
        return std::any_of(track.clips.begin(), track.clips.end(), [&](const motion::Clip& clip) {
            return clip.id == selection && canSplitClip(&clip, time, processor.document.project().tempo());
        });
    });
    if (!canSplit) {
        return;
    }
    const auto id = processor.document.newId();
    processor.document.tryEdit("Split clip", [&](motion::Project& project) {
        const auto* original = motion::findClipTrack(project, selection);
        if (original == nullptr || original->locked) { return false; }
        auto parts = motion::findClip(project, selection)->split(time, id, project.tempo());
        if (!parts.has_value()) { return false; }
        // The right half keeps the left's routes and internal links.
        std::map<motion::Id, motion::Id> owners {{parts->first.id, id}};
        for (auto& effect : parts->second.effects) {
            const auto clone = processor.document.newId();
            owners.emplace(effect.id, clone);
            effect.id = clone;
        }
        *motion::changeClip(project, selection) = std::move(parts->first);
        if (!motion::changeClipTrack(project, selection)->insert(std::move(parts->second), project.tempo())) { return false; }
        motion::cloneDrivers(project, owners, [this] { return processor.document.newId(); });
        return true;
    });
}

void MotionEditor::showShortcuts() {
    using Overlay = MotionShortcutsOverlay;
    std::vector<Overlay::Section> sections;
    const std::array<std::pair<int, const char*>, 5> menuOrder {{{3, "Transport"}, {1, "Edit"}, {2, "Clip and keys"}, {5, "View"}, {0, "File"}}};
    for (const auto& [menu, title] : menuOrder) {
        Overlay::Section section {title, {}};
        if (menu == 0) {
            for (const auto& [keys, action] : std::initializer_list<std::pair<const char*, const char*>> {{"Cmd+N", "New project"}, {"Cmd+O", "Open project"}, {"Cmd+S", "Save project"}, {"Cmd+Shift+S", "Save project as"}}) {
                section.entries.push_back({keys, action});
            }
        }
        if (menu == 1) {
            section.entries.push_back({"Cmd+Z", "Undo"});
            section.entries.push_back({"Cmd+Shift+Z", "Redo"});
        }
        for (const auto& command : commands) {
            if (command.menu == menu && command.shortcut.isNotEmpty()) { section.entries.push_back({command.shortcut, command.name.trimCharactersAtEnd(".")}); }
        }
        sections.push_back(std::move(section));
    }
    sections.push_back({"Timeline", {{"V / B / S / R", "Move, ripple trim, slip and stretch tools"}, {"M", "Add a marker at the playhead"},
        {"Drag empty space", "Select clips (Shift adds)"}, {"Alt while dragging", "Bypass snapping"}, {"Drag a row's bottom edge", "Resize the track (double-click resets)"},
        {"Double-click a lane", "Add a key"}}});
    sections.push_back({"Scrolling and zoom (timeline, graph)", {{"Wheel / two fingers", "Pan (Shift: horizontal)"}, {"Cmd+wheel / pinch", "Zoom time around the pointer"},
        {"Alt+wheel", "Track height, value range or key height"}}});
    sections.push_back({"Graph", {{"Double-click", "Add a key"}, {"Drag a box", "Select keys (Shift adds)"}, {"Drag box edges", "Scale key times"},
        {"F / Shift+F", "Frame all curves / this curve"}, {"Right-click a key", "Interpolation and easing"}, {"Drag the time axis", "Scrub"}}});
    sections.push_back({"Scene", {{"G / R / S", "Move, rotate and scale tools"}, {"F", "Frame the selection"}, {"0", "Reset the view"},
        {"1 / 3 / 7", "Front, right and top views (Ctrl: opposite side)"}, {"N", "Fly through the scene"}, {"P", "Show the motion path"},
        {"Two fingers / wheel", "Orbit / zoom"}, {"Shift + two fingers", "Pan"}, {"Alt+drag", "Orbit with the mouse"}}});
    Overlay::show(*this, std::move(sections));
}

void MotionEditor::refreshTiming() {
    timeline.repaint();
    curveEditor.repaint();
}

// The menu bar's Timing menu.
static const std::array<double, 8> timingRates { 24000.0 / 1001, 24, 25, 30000.0 / 1001, 30, 50, 60, 120 };

juce::PopupMenu MotionEditor::timingMenu() {
    const auto& project = processor.document.project();
    juce::PopupMenu menu;
    menu.setLookAndFeel(&getLookAndFeel());
    // What the ruler's right-click offers, at the playhead.
    menu.addItem(motion::style::menuItem("Add marker...", 700, "M"));
    menu.addItem(701, "Add tempo change...");
    menu.addSeparator();
    menu.addItem(600, detectingTempo ? "Detecting tempo..." : "Detect tempo from soundtrack", !detectingTempo && soundtrackClip() != 0);
    menu.addSeparator();
    menu.addItem(101, "Seconds", true, project.timeDisplay == motion::TimeDisplay::seconds);
    menu.addItem(102, "Frames", true, project.timeDisplay == motion::TimeDisplay::frames);
    menu.addItem(103, "Bars / beats", true, project.timeDisplay == motion::TimeDisplay::beats);
    menu.addSeparator();
    menu.addItem(200, "Snap to grid", true, project.gridSnap);
    const std::array<const char*, 7> names { "1 bar", "1 beat", "1/8 note", "1/16 note", "1/32 note", "1/8 triplet", "1/16 triplet" };
    juce::PopupMenu beatGrid;
    for (std::size_t i = 0; i < names.size(); ++i) { beatGrid.addItem(300 + static_cast<int>(i), names[i], true, std::abs(project.snapBeats - snapDivision(static_cast<int>(i))) < 1.0e-9); }
    menu.addSubMenu("Beat grid", beatGrid, project.timeDisplay == motion::TimeDisplay::beats);
    juce::PopupMenu meter;
    for (const auto beats : { 2, 3, 4, 5, 6, 7 }) { meter.addItem(400 + beats, juce::String(beats) + "/4", true, project.beatsPerBar == beats); }
    menu.addSubMenu("Meter", meter);
    const std::array<const char*, 8> rateNames { "23.976 fps", "24 fps", "25 fps", "29.97 fps", "30 fps", "50 fps", "60 fps", "120 fps" };
    juce::PopupMenu frames;
    for (std::size_t i = 0; i < timingRates.size(); ++i) { frames.addItem(500 + static_cast<int>(i), rateNames[i], true, std::abs(project.frameRate - timingRates[i]) < 1.0e-9); }
    menu.addSubMenu("Frame rate", frames);
    return menu;
}

double MotionEditor::snapDivision(int index) const {
    const std::array<double, 7> divisions { static_cast<double>(processor.document.project().beatsPerBar), 1, 0.5, 0.25, 0.125, 1.0 / 3, 1.0 / 6 };
    return divisions[static_cast<std::size_t>(std::clamp(index, 0, 6))];
}

// Display and snapping are view options (no undo step); meter and frame
// rate are edits.
bool MotionEditor::applyTiming(int result) {
    if (result == 600) { detectTempo(); return true; }
    if (result == 700 && timeline.onEditMarker) {
        timeline.onEditMarker(0, std::clamp(processor.position.load(), 0.0, processor.document.project().duration));
        return true;
    }
    if (result == 701) {
        timeline.editTempoAt(processor.position.load());
        return true;
    }
    const auto& current = processor.document.project();
    if (result >= 101 && result <= 103) {
        const auto display = static_cast<motion::TimeDisplay>(result - 101);
        if (current.timeDisplay != display) { processor.document.changeView([display](motion::Composition& state) { state.timeDisplay = display; }); }
        return true;
    }
    if (result == 200) {
        processor.document.changeView([](motion::Composition& state) { state.gridSnap = !state.gridSnap; });
        return true;
    }
    if (result >= 300 && result < 307) {
        const auto subdivision = snapDivision(result - 300);
        processor.document.changeView([subdivision](motion::Composition& state) { state.snapBeats = subdivision; state.gridSnap = true; });
        return true;
    }
    if (result >= 402 && result <= 407) {
        if (current.beatsPerBar == result - 400) { return true; }
        processor.document.edit("Change meter", [result](motion::Project& state) {
            if (std::abs(state.snapBeats - state.beatsPerBar) < 1.0e-9) { state.snapBeats = result - 400; }
            state.beatsPerBar = result - 400;
        });
        return true;
    }
    if (result >= 500 && result < 508) {
        const auto rate = timingRates[static_cast<std::size_t>(result - 500)];
        if (std::abs(current.frameRate - rate) >= 1.0e-9) { processor.document.edit("Change frame rate", [rate](motion::Project& state) { state.frameRate = rate; }); }
        return true;
    }
    return false;
}

void MotionEditor::tap() {
    const auto bpm = tapTempo.tap(juce::Time::getMillisecondCounterHiRes() / 1000);
    tapButton.setButtonText(tapTempo.count() > 1 ? juce::String(static_cast<int>(tapTempo.count())) : "Tap");
    if (bpm.has_value()) {
        tappedBpm = bpm;
        tempoValue.setText(juce::String(*bpm, 1), juce::dontSendNotification);
    }
    if (tapCommit == nullptr) {
        tapCommit = std::make_unique<juce::TimedCallback>([this] {
            tapCommit->stopTimer();
            tapButton.setButtonText("Tap");
            tapTempo.reset();
            if (!tappedBpm.has_value()) { return; }
            const auto bpm = *tappedBpm;
            tappedBpm.reset();
            const auto result = processor.document.changeTempo(bpm);
            if (result.failed()) { statusBar.show(result.getErrorMessage()); } else { statusBar.show("Tempo " + juce::String(bpm, 1) + " BPM from tapping.", MotionStatusBar::Kind::notice); }
            refreshTiming();
        });
    }
    tapCommit->startTimer(1600);
}

// The first clip on an audio track: the piece the picture is cut to.
motion::Id MotionEditor::soundtrackClip() const {
    for (const auto& track : processor.document.mainProject().tracks) {
        if (track.kind != motion::TrackKind::audio) { continue; }
        for (const auto& clip : track.clips) { return clip.id; }
    }
    return 0;
}

// Analyses the soundtrack off the message thread, then sets a steady tempo
// and slides the soundtrack onto the bar grid in one undo step.
void MotionEditor::detectTempo() {
    const auto& project = processor.document.mainProject();
    const auto id = soundtrackClip();
    const auto* clip = motion::findClip(project, id);
    const auto asset = clip != nullptr ? motion::findAsset(project.assets, clip->asset) : nullptr;
    const auto audio = asset != nullptr ? asset->audio : nullptr;
    if (audio == nullptr || processor.document.editingComposition() != 0) {
        statusBar.show(audio == nullptr ? "Import a soundtrack first." : "Tempo is set on the main composition. Go back to it first.");
        return;
    }
    detectingTempo = true;
    statusBar.show("Detecting tempo...", MotionStatusBar::Kind::notice);
    const juce::Component::SafePointer<MotionEditor> owner(this);
    const auto generation = processor.document.generation();
    const auto beatsPerBar = project.beatsPerBar;
    juce::Thread::launch([owner, audio, id, generation, beatsPerBar] {
        std::vector<float> mono(audio->frameCount());
        const auto channels = audio->channelCount();
        for (std::size_t channel = 0; channel < channels; ++channel) {
            const auto samples = audio->channel(channel);
            for (std::size_t i = 0; i < mono.size(); ++i) { mono[i] += samples[i] / static_cast<float>(channels); }
        }
        const auto estimate = motion::TempoDetection::estimate(mono, audio->sampleRate(), beatsPerBar);
        juce::MessageManager::callAsync([owner, estimate, id, generation] {
            if (owner == nullptr) { return; }
            owner->detectingTempo = false;
            if (owner->processor.document.generation() != generation) { return; }
            if (!estimate.has_value()) {
                owner->statusBar.show("Could not find a steady tempo in the soundtrack. Set it by hand or tap it.");
                return;
            }
            double moved = 0;
            const auto bars = owner->processor.document.project().timeDisplay == motion::TimeDisplay::beats;
            const auto result = owner->processor.document.setTempoFromAudio(id, estimate->bpm, estimate->firstDownbeat, moved);
            if (result.failed()) { owner->statusBar.show(result.getErrorMessage()); return; }
            const auto bpm = juce::String(estimate->bpm, estimate->bpm == std::round(estimate->bpm) ? 0 : 2);
            const auto other = estimate->bpm * 2 <= 200 ? estimate->bpm * 2 : estimate->bpm / 2;
            owner->statusBar.show("Tempo " + bpm + " BPM from the soundtrack" + (estimate->confidence < 0.4 ? " (low confidence - check it)" : "")
                + ". If it feels " + (other > estimate->bpm ? "twice as fast" : "half as fast") + ", type " + juce::String(other, other == std::round(other) ? 0 : 2) + " in BPM"
                + (moved > 0 ? ". Moved it " + juce::String(moved, 3) + " s so its first downbeat is on a bar." : "; its downbeats are already on the bars.")
                + (bars ? "" : " The ruler now shows bars and beats."), MotionStatusBar::Kind::notice);
        });
    });
}
