#include "MotionEditor.h"
#include "../JucewrightAutomation.h"
#include "model/TempoDetection.h"
#include "ui/ShortcutsOverlay.h"
#include "ui/StartScreen.h"

namespace {
bool canSplitClip(const motion::Clip* clip, double time, const motion::Tempo& bpm) {
    return clip != nullptr && clip->valid() && std::isfinite(time) && time > clip->timing(bpm).start && time < clip->timing(bpm).end();
}
}

// File is built by buildFileMenu, so its commands only bind keys.
void MotionEditor::addCommand(int menu, juce::String name, juce::KeyPress key, juce::String shortcut, std::function<void()> action, juce::String label) {
    if (menu != fileMenu) { menus.addMenuItem(menu, label.isNotEmpty() ? label : name, action, motion::style::shortcutText(shortcut)); }
    commands.push_back({menu, std::move(name), std::move(shortcut), key, std::move(action)});
}

namespace {
// The example projects that ship with osci-motion, as resources.
std::vector<juce::String> exampleProjects() { return motion::ui::examples::resources(".osci-motion"); }
constexpr int firstExampleId = 1200;
// Open recent: one for each recent project, and one to clear the list.
constexpr int firstRecentId = 1100;
constexpr int clearRecentId = 1099;
// One for each output mode.
constexpr int firstExportAudioId = 1020;
}

void MotionEditor::buildFileMenu(juce::PopupMenu& menu) {
    menu.addItem(motion::style::menuItem("New project", 1001, "Cmd+N"));
    menu.addItem(motion::style::menuItem("Open project...", 1002, "Cmd+O"));
    juce::PopupMenu recent;
    const int added = processor.createRecentProjectsPopupMenuItems(recent, firstRecentId, true, true);
    if (added == 0) {
        recent.addItem(firstRecentId, "No recent projects", false);
    }
    recent.addSeparator();
    recent.addItem(clearRecentId, "Clear recent projects", added > 0);
    menu.addSubMenu("Open recent", recent);
    // Each opens as a new project, to look into or start from.
    juce::PopupMenu examples;
    const auto resources = exampleProjects();
    for (std::size_t index = 0; index < resources.size(); ++index) {
        examples.addItem(firstExampleId + static_cast<int>(index), motion::ui::examples::displayName(resources[index]));
    }
    if (examples.getNumItems() > 0) { menu.addSubMenu("Examples", examples); }
    menu.addItem(1008, "Start screen...");
    menu.addSeparator();
    menu.addItem(motion::style::menuItem("Save project", 1003, "Cmd+S"));
    menu.addItem(motion::style::menuItem("Save project as...", 1004, "Cmd+Shift+S"));
    menu.addSeparator();
    menu.addItem(motion::style::menuItem("Import source...", 1005, "Cmd+I"));
    menu.addSeparator();
    if (juce::JUCEApplicationBase::isStandaloneApp()) {
        menu.addItem(1009, "Audio settings...");
        menu.addSeparator();
    }
    // One export at a time. Audio as each Output choice plays it, whatever
    // the audio device has room for.
    juce::PopupMenu audio;
    using Mode = MotionProcessor::OutputMode;
    for (const auto mode : {Mode::soundtrack, Mode::xy, Mode::xyz, Mode::xyzInverted, Mode::xyrgb}) {
        const auto channels = motion::ui::output::roles(mode).size();
        const auto name = motion::ui::output::exportName(mode) + (channels > 2 ? " (" + juce::String(channels) + " ch)" : juce::String());
        audio.addItem(firstExportAudioId + static_cast<int>(mode), name + "...", mode != Mode::soundtrack || soundtrackClip() != 0);
    }
    menu.addSubMenu("Export audio", audio, exportState == nullptr);
#if OSCI_PREMIUM
    auto video = motion::style::menuItem("Export video...", 1007, {});
    video.isEnabled = exportState == nullptr;
    menu.addItem(std::move(video));
#endif
}

bool MotionEditor::fileMenuItemSelected(int id) {
    if (menus.handleRecentProjectMenuItem(id, processor, *this, firstRecentId, clearRecentId)) { return true; }
    const auto examples = exampleProjects();
    if (id >= firstExampleId && id < firstExampleId + static_cast<int>(examples.size())) {
        openExample(examples[static_cast<std::size_t>(id - firstExampleId)]);
        return true;
    }
    if (id >= firstExportAudioId && id <= firstExportAudioId + static_cast<int>(MotionProcessor::OutputMode::xyzInverted)) {
        exportSignal(static_cast<MotionProcessor::OutputMode>(id - firstExportAudioId));
        return true;
    }
    switch (id) {
        case 1001: resetToDefault(); return true;
        case 1002: CommonPluginEditor::openProject(); return true;
        case 1003: saveProject(); return true;
        case 1004: saveProjectAs(); return true;
        case 1005: chooseSourceFile(); return true;
        case 1007: exportVideo(); return true;
        case 1008: showStartScreen(); return true;
        case 1009: openAudioSettings(); return true;
        default: return false;
    }
}

void MotionEditor::registerCommands() {
    const auto command = juce::ModifierKeys::commandModifier;
    const auto shift = juce::ModifierKeys::shiftModifier;
    const auto alt = juce::ModifierKeys::altModifier;
    // A command in File only binds its key, as File is built by buildFileMenu.
    constexpr int keyOnly = fileMenu;
    menus.addMenuSeparator(editMenu);
    addCommand(editMenu, "Cut", juce::KeyPress('x', command, 0), "Cmd+X", [this] { copySelection(true); });
    addCommand(editMenu, "Copy", juce::KeyPress('c', command, 0), "Cmd+C", [this] { copySelection(false); });
    addCommand(editMenu, "Paste", juce::KeyPress('v', command, 0), "Cmd+V", [this] { pasteClipboard(); });
    addCommand(editMenu, "Delete", juce::KeyPress(), "Delete", [this] {
        if (graphShown()) { curveEditor.deleteSelected(); } else { timeline.deleteSelection(); }
    });
    // Cut, Copy and Delete act on what the shown editor has selected: the
    // Graph's keys, or the timeline's keys or clips. Paste needs a copy.
    const auto anySelection = [this] { return graphShown() ? curveEditor.hasSelectedKeys() : timeline.hasSelectedKeys() || timeline.hasSelectedClips(); };
    for (const auto* name : {"Cut", "Copy", "Delete"}) { menus.setMenuItemEnabled(editMenu, name, anySelection); }
    menus.setMenuItemEnabled(editMenu, "Paste", [this] { return !std::holds_alternative<std::monostate>(clipboard); });
    menus.addMenuSeparator(editMenu);
    addCommand(editMenu, "Select all", juce::KeyPress('a', command, 0), "Cmd+A", [this] {
        if (graphShown()) { curveEditor.selectAllKeys(); } else { timeline.selectAll(); }
    });
    addCommand(clipMenu, "Split at playhead", juce::KeyPress('k', command, 0), "Cmd+K", [this] { splitAtPlayhead(); });
    addCommand(clipMenu, "Duplicate", juce::KeyPress('d', command, 0), "Cmd+D", [this] {
        std::vector<motion::Id> duplicates;
        const auto clips = timeline.copiedClips();
        std::vector<motion::Id> ids;
        for (const auto& copied : clips) { ids.push_back(copied.clip.id); }
        if (ids.empty()) {
            statusBar.show("Select clips to duplicate them.", MotionStatusBar::Kind::notice);
            return;
        }
        const auto result = processor.document.duplicateClips(ids, duplicates);
        if (result.failed()) { statusBar.show("Couldn't duplicate: " + result.getErrorMessage()); return; }
        timeline.selectClips(duplicates);
    });
    addCommand(clipMenu, "Group tracks", juce::KeyPress('g', command, 0), "Cmd+G", [this] { timeline.groupSelectedTracks(); });
    // After Effects' Pre-compose keys; Final Cut's and CapCut's Alt+G works too.
    addCommand(clipMenu, "Make composition", juce::KeyPress('c', command | shift, 0), "Cmd+Shift+C", [this] { timeline.createCompositionFromSelection(); });
    addCommand(keyOnly, "Make composition", juce::KeyPress('g', alt, 0), {}, [this] { timeline.createCompositionFromSelection(); });
    menus.addMenuSeparator(clipMenu);
    // After Effects conventions: [ and ] move the clip's start or end to the
    // playhead; Alt+[ and Alt+] trim it there.
    addCommand(clipMenu, "Move start to playhead", juce::KeyPress('[', 0, 0), "[", [this] { placeClipAtPlayhead(true, false); });
    addCommand(clipMenu, "Move end to playhead", juce::KeyPress(']', 0, 0), "]", [this] { placeClipAtPlayhead(false, false); });
    addCommand(clipMenu, "Trim start to playhead", juce::KeyPress('[', alt, 0), "Alt+[", [this] { placeClipAtPlayhead(true, true); });
    addCommand(clipMenu, "Trim end to playhead", juce::KeyPress(']', alt, 0), "Alt+]", [this] { placeClipAtPlayhead(false, true); });
    menus.addMenuSeparator(clipMenu);
    addCommand(clipMenu, "Show key lanes", juce::KeyPress('u', 0, 0), "U", [this] { timeline.toggleLanesForSelection(); });
    // Key: each group Properties shows, named in the menu by the group alone.
    const std::array<std::tuple<char, const char*, const char*>, 5> keyed {{{'p', "Position", "Key position"}, {'r', "Rotation", "Key rotation"}, {'s', "Scale", "Key scale"}, {'c', "Colour", "Key colour"}, {'t', "Opacity", "Key opacity"}}};
    menus.beginSubMenu(clipMenu, "Key");
    for (const auto& [letter, group, name] : keyed) {
        const juce::String groupName(group);
        addCommand(clipMenu, name, juce::KeyPress(letter, alt | shift, 0), "Alt+Shift+" + juce::String::charToString(letter).toUpperCase(), [this, groupName] {
            if (!propertyInspector.toggleGroupKeys(groupName)) { statusBar.show("Select a clip or camera with " + groupName.toLowerCase() + " to key it."); }
        }, groupName);
        menus.setMenuItemEnabled(clipMenu, groupName, [this, groupName] { return propertyInspector.showsGroup(groupName); });
    }
    menus.endSubMenu(clipMenu);
    menus.setMenuItemEnabled(clipMenu, "Key", [this, keyed] {
        return std::any_of(keyed.begin(), keyed.end(), [this](const auto& entry) { return propertyInspector.showsGroup(std::get<1>(entry)); });
    });
    // After Effects' Easy Ease, on the selected keys of the visible editor.
    const auto keysSelected = [this] { return graphShown() ? curveEditor.hasSelectedKeys() : timeline.hasSelectedKeys(); };
    const auto ease = [this, keysSelected](bool in, bool out) {
        if (!keysSelected()) {
            statusBar.show("Select keys to ease.", MotionStatusBar::Kind::notice);
            return;
        }
        // Smooth keys already ease at their ends, so say what happened.
        const auto eased = graphShown() ? curveEditor.easeSelected(in, out) : timeline.easeSelectedKeys(in, out);
        const juce::String what = in && out ? "Easy ease" : (in ? "Easy ease in" : "Easy ease out");
        statusBar.show(eased ? what + " applied: the keys stop with a third of each segment as influence." : "The selected keys already ease that way.", MotionStatusBar::Kind::notice);
    };
    menus.beginSubMenu(clipMenu, "Easy ease");
    addCommand(clipMenu, "Easy ease", juce::KeyPress(juce::KeyPress::F9Key), "F9", [ease] { ease(true, true); }, "In and out");
    addCommand(clipMenu, "Easy ease in", juce::KeyPress(juce::KeyPress::F9Key, shift, 0), "Shift+F9", [ease] { ease(true, false); }, "In");
    addCommand(clipMenu, "Easy ease out", juce::KeyPress(juce::KeyPress::F9Key, command | shift, 0), "Cmd+Shift+F9", [ease] { ease(false, true); }, "Out");
    menus.endSubMenu(clipMenu);
    menus.setMenuItemEnabled(clipMenu, "Easy ease", keysSelected);
    // The Clip menu's commands act on the selection; with none they say so
    // when their keys are pressed, and show it in the menu first.
    menus.setMenuItemEnabled(clipMenu, "Split at playhead", [this] { return canSplitAtPlayhead(); });
    menus.setMenuItemEnabled(clipMenu, "Group tracks", [this] { return timeline.canGroupTracks(); });
    for (const auto* name : {"Duplicate", "Show key lanes"}) { menus.setMenuItemEnabled(clipMenu, name, [this] { return timeline.hasSelectedClips(); }); }
    for (const auto* name : {"Move start to playhead", "Move end to playhead", "Trim start to playhead", "Trim end to playhead"}) {
        menus.setMenuItemEnabled(clipMenu, name, [this] { return clipSelected(); });
    }

    addCommand(transportMenu, "Play / pause", juce::KeyPress(juce::KeyPress::spaceKey), "Space", [this] { togglePlayback(); });
    addCommand(transportMenu, "Go to start", juce::KeyPress(juce::KeyPress::homeKey), "Home", [this] { seekAndReveal(0); });
    addCommand(transportMenu, "Go to end", juce::KeyPress(juce::KeyPress::endKey), "End", [this] { seekAndReveal(processor.document.project().duration); });
    menus.addMenuSeparator(transportMenu);
    addCommand(transportMenu, "Previous frame", juce::KeyPress(juce::KeyPress::leftKey), "Left", [this] { stepFrames(-1); });
    addCommand(transportMenu, "Next frame", juce::KeyPress(juce::KeyPress::rightKey), "Right", [this] { stepFrames(1); });
    addCommand(transportMenu, "Back ten frames", juce::KeyPress(juce::KeyPress::leftKey, shift, 0), "Shift+Left", [this] { stepFrames(-10); });
    addCommand(transportMenu, "Forward ten frames", juce::KeyPress(juce::KeyPress::rightKey, shift, 0), "Shift+Right", [this] { stepFrames(10); });
    menus.addMenuSeparator(transportMenu);
    addCommand(transportMenu, "Previous key or marker", juce::KeyPress('j', 0, 0), "J", [this] { jumpToKey(false); });
    addCommand(transportMenu, "Next key or marker", juce::KeyPress('k', 0, 0), "K", [this] { jumpToKey(true); });
    addCommand(transportMenu, "Previous edit point", juce::KeyPress(juce::KeyPress::upKey), "Up", [this] { jumpToEdit(false); });
    addCommand(transportMenu, "Next edit point", juce::KeyPress(juce::KeyPress::downKey), "Down", [this] { jumpToEdit(true); });
    menus.addMenuSeparator(transportMenu);
    addCommand(transportMenu, "Loop playback", juce::KeyPress('l', 0, 0), "L", [this] { toggleLoop(); });
    addCommand(transportMenu, "Set loop start at playhead", juce::KeyPress('i', 0, 0), "I", [this] { setLoopEdge(true); });
    addCommand(transportMenu, "Set loop end at playhead", juce::KeyPress('o', 0, 0), "O", [this] { setLoopEdge(false); });
    addCommand(transportMenu, "Loop selected clips", juce::KeyPress('l', shift, 0), "Shift+L", [this] { loopSelection(); });
    menus.addMenuSeparator(transportMenu);
    // As a video player's: > faster, < slower.
    // macOS reports Shift+. as >; Windows reports the . key with Shift. A layout
    // with its own < and > key (AZERTY) types them without Shift.
    addCommand(transportMenu, "Play faster", juce::KeyPress('>', shift, 0), ">", [this] { changeSpeed(1); });
    addCommand(transportMenu, "Play slower", juce::KeyPress('<', shift, 0), "<", [this] { changeSpeed(-1); });
    addCommand(keyOnly, "Play faster", juce::KeyPress('>', 0, 0), {}, [this] { changeSpeed(1); });
    addCommand(keyOnly, "Play slower", juce::KeyPress('<', 0, 0), {}, [this] { changeSpeed(-1); });
   #if JUCE_WINDOWS
    addCommand(keyOnly, "Play faster", juce::KeyPress('.', shift, 0), {}, [this] { changeSpeed(1); });
    addCommand(keyOnly, "Play slower", juce::KeyPress(',', shift, 0), {}, [this] { changeSpeed(-1); });
   #endif
    // View: what the lower panel shows, zoom, follow and track height, then
    // the window (its size added when the menu opens).
    addCommand(viewMenu, "Show Timeline", juce::KeyPress('1', alt, 0), "Alt+1", [this] { timelineTabs.setSelectedIndex(0); });
    addCommand(viewMenu, "Show Graph", juce::KeyPress('2', alt, 0), "Alt+2", [this] { timelineTabs.setSelectedIndex(1); });
    menus.addMenuSeparator(viewMenu);
    // Zoom acts on whichever lower panel is showing.
    const auto zoom = [this](double factor) {
        if (curveEditor.isVisible()) { curveEditor.zoomTime(curveEditor.getWidth() * .5f, factor); curveEditor.repaint(); return; }
        timeline.zoomBy(factor);
    };
    addCommand(viewMenu, "Zoom in", juce::KeyPress('=', command, 0), "Cmd+=", [zoom] { zoom(1.5); });
    addCommand(viewMenu, "Zoom out", juce::KeyPress('-', command, 0), "Cmd+-", [zoom] { zoom(1 / 1.5); });
    addCommand(viewMenu, "Fit timeline to composition", juce::KeyPress(), "F", [this] { timeline.fitProject(); });
    addCommand(viewMenu, "Taller tracks", juce::KeyPress('=', command | shift, 0), "Cmd+Shift+=", [this] { timeline.setDefaultTrackHeight(timeline.layout().trackHeight + 8); });
    addCommand(viewMenu, "Shorter tracks", juce::KeyPress('-', command | shift, 0), "Cmd+Shift+-", [this] { timeline.setDefaultTrackHeight(timeline.layout().trackHeight - 8); });
    menus.addToggleMenuItem(viewMenu, "Follow playhead", [this] {
        auto layout = timeline.layout();
        layout.follow = !layout.follow;
        timeline.setLayout(layout);
        saveLayout();
    }, [this] { return timeline.layout().follow; });
    menus.addMenuSeparator(viewMenu);
   #if JUCE_MAC
    const juce::String fullScreenKeys = "Ctrl+Cmd+F";
    commands.push_back({viewMenu, "Full screen", fullScreenKeys, juce::KeyPress('f', command | juce::ModifierKeys::ctrlModifier, 0), [this] { toggleFullScreen(); }});
   #else
    const juce::String fullScreenKeys = "F11";
   #endif
    menus.addToggleMenuItem(viewMenu, "Full screen", [this] { toggleFullScreen(); }, [this] { return isFullScreen(); }, motion::style::shortcutText(fullScreenKeys));
    menus.addMenuItem(viewMenu, "Reset window size and position", [this] { resetWindowSizeAndPosition(); });
    // File items live in the File menu built by buildFileMenu; these bind keys.
    addCommand(keyOnly, "Import source...", juce::KeyPress('i', command, 0), "Cmd+I", [this] { chooseSourceFile(); });
    // In Help; its key is listed with File's.
    addCommand(keyOnly, "Keyboard shortcuts", juce::KeyPress('/', command, 0), "Cmd+/", [this] { showShortcuts(); });
}

void MotionEditor::copySelection(bool cut) {
    if (graphShown()) {
        auto keys = curveEditor.copiedKeys();
        if (keys.empty()) { return; }
        clipboard = std::move(keys);
        if (cut) { curveEditor.deleteSelected(); }
        return;
    }
    if (timeline.hasSelectedKeys()) {
        // Only a clip's own keys paste; an effect's are left where they are,
        // and the clipboard keeps what it had.
        auto keys = timeline.copiedKeys();
        if (keys.empty()) {
            statusBar.show("Only a clip's own keys can be copied so far, not an effect's.", MotionStatusBar::Kind::notice);
            return;
        }
        clipboard = std::move(keys);
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
        if (result.failed()) { statusBar.show("Couldn't paste keys: " + result.getErrorMessage()); }
        return;
    }
    if (const auto* clips = std::get_if<std::vector<motion::Document::CopiedClip>>(&clipboard)) {
        std::vector<motion::Id> pasted;
        const auto result = processor.document.pasteClips(*clips, time, pasted);
        if (result.failed()) { statusBar.show("Couldn't paste clips: " + result.getErrorMessage()); return; }
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
    const std::array<std::pair<const char*, const char*>, 5> keys {{{"Key position", "Alt+Shift+P"}, {"Key rotation", "Alt+Shift+R"}, {"Key scale", "Alt+Shift+S"}, {"Key colour", "Alt+Shift+C"}, {"Key opacity", "Alt+Shift+T"}}};
    juce::PopupMenu menu;
    menu.addItem(motion::style::menuItem("Frame selection", 7, "F"));
    menu.addItem(motion::style::menuItem("Reset view", 8, "0"));
    juce::PopupMenu from;
    // Each view as the side of a cube it looks at, in the order Side lists them.
    for (std::size_t index = 0; index < views.size(); ++index) {
        from.addItem(motion::style::menuItem(views[index].first, static_cast<int>(index) + 1, views[index].second).setImage(motion::ui::menu_icons::viewCube(static_cast<motion::ui::menu_icons::Side>(index))));
    }
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
        const std::array<const char*, 5> groups {"Position", "Rotation", "Scale", "Colour", "Opacity"};
        if (result >= 20 && result < 25 && !propertyInspector.toggleGroupKeys(groups[static_cast<std::size_t>(result - 20)])) {
            statusBar.show("The selection has no such property to key.", MotionStatusBar::Kind::notice);
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
void MotionEditor::setAutoKey(bool on) {
    autoKeyButton.setToggleState(on, juce::dontSendNotification);
    autoKeyButton.setTooltip(on ? "Auto-key: on" : "Auto-key: off");
    autoKeyButton.tint = on ? std::optional<juce::Colour>(motion::style::error()) : std::nullopt;
    autoKeyButton.repaint();
    timeline.setAutoKey(on);
    if (on) { statusBar.show("Auto-key on: every change is keyed at the playhead.", MotionStatusBar::Kind::notice); }
}

void MotionEditor::editRecorded(const motion::Project& before) {
    if (animating) { return; }
    const auto& project = processor.document.project();
    const auto time = project.frameTime(processor.position.load());
    auto changes = motion::staticChanges(before, project);
    std::erase_if(changes, [&](const auto& change) { return !motion::canAnimateAt(project, change, time); });
    if (changes.empty()) { return; }
    if (autoKeyButton.getToggleState()) {
        const juce::ScopedValueSetter<bool> busy(animating, true);
        processor.document.amend([&](motion::Project& edited) { motion::animateChanges(edited, changes, time); });
        return;
    }
    // Changes in a row to the same values offer them all, each from where
    // it first was.
    if (!motion::sameTime(time, pendingAnimationTime)) { pendingAnimation.clear(); }
    for (const auto& change : changes) {
        const auto found = std::find_if(pendingAnimation.begin(), pendingAnimation.end(), [&](const auto& item) { return item.target == change.target && item.property == change.property; });
        if (found == pendingAnimation.end()) {
            pendingAnimation.push_back(change);
        } else {
            found->after = change.after;
        }
    }
    std::erase_if(pendingAnimation, [](const auto& change) { return change.before == change.after; });
    pendingAnimationTime = time;
    if (pendingAnimation.empty()) { return; }
    // Named by what changed: one setting, one group of them, or how many.
    std::set<motion::Id> targets;
    std::set<juce::String> groups;
    juce::String setting;
    for (const auto& change : pendingAnimation) {
        targets.insert(change.target);
        const auto spec = motion::specFor(project, change.target, change.property);
        groups.insert(spec.has_value() ? juce::String(spec->group.data(), spec->group.size()) : juce::String(change.property));
        setting = spec.has_value() ? juce::String(spec->label.data(), spec->label.size()) : juce::String(change.property);
    }
    const auto what = pendingAnimation.size() == 1 ? setting : groups.size() == 1 ? *groups.begin() : juce::String(static_cast<int>(pendingAnimation.size())) + " settings";
    const auto target = motion::findPropertyTarget(project, *targets.begin());
    const auto where = targets.size() > 1 ? juce::String(static_cast<int>(targets.size())) + " items" : target.has_value() ? juce::String(target->name.data(), target->name.size()) : juce::String("it");
    statusBar.show(what + " changed for the whole of " + where + ".", MotionStatusBar::Kind::notice, "Animate this change", [this] { animatePending(); });
}

void MotionEditor::animatePending() {
    auto changes = std::exchange(pendingAnimation, {});
    const auto time = pendingAnimationTime;
    const juce::ScopedValueSetter<bool> busy(animating, true);
    const auto animated = processor.document.tryEdit("Animate change", [&](motion::Project& project) { return motion::animateChanges(project, changes, time); });
    statusBar.show(animated ? "Animated: keys at the start and at the playhead." : "That has changed since, so there's nothing to animate now.", MotionStatusBar::Kind::notice);
}

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
            statusBar.show("The composition is too short to loop.", MotionStatusBar::Kind::notice);
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
    if (last <= first) { statusBar.show("Select clips to loop.", MotionStatusBar::Kind::notice); return; }
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
        state.loopStart = start;
        state.loopEnd = end;
        state.looping = enabled;
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
        if (skipped > 0) { statusBar.show(trim ? "The playhead is outside the selected clips." : "The selected clips can't start before 0."); }
        return;
    }
    const auto result = processor.document.setClipTimings(timings);
    if (result.failed()) {
        statusBar.show(result.getErrorMessage());
    } else if (skipped > 0) {
        statusBar.show(juce::String(skipped) + (skipped == 1 ? " clip was" : " clips were") + " left unchanged.", MotionStatusBar::Kind::notice);
    }
}

bool MotionEditor::canSplitAtPlayhead() const {
    const auto& project = processor.document.project();
    return canSplitClip(motion::findClip(project, selection), processor.position.load(), project.tempo());
}

bool MotionEditor::clipSelected() const { return timeline.hasSelectedClips() || motion::findClip(processor.document.project(), selection) != nullptr; }

void MotionEditor::splitAtPlayhead() {
    const auto time = processor.position.load();
    if (!canSplitAtPlayhead()) {
        statusBar.show("Select a clip under the playhead to split it there.", MotionStatusBar::Kind::notice);
        return;
    }
    const auto* track = motion::findClipTrack(processor.document.project(), selection);
    if (track != nullptr && track->locked) {
        statusBar.show("Unlock the track to split its clip.", MotionStatusBar::Kind::notice);
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
    // The menus in the menu bar's order, then the panels, then moving about.
    const std::array<std::pair<int, const char*>, 5> menuOrder {{{fileMenu, "File"}, {editMenu, "Edit"}, {clipMenu, "Clip and keys"}, {transportMenu, "Transport"}, {viewMenu, "View"}}};
    for (const auto& [menu, title] : menuOrder) {
        Overlay::Section section {title, {}};
        if (menu == fileMenu) {
            for (const auto& [keys, action] : std::initializer_list<std::pair<const char*, const char*>> {{"Cmd+N", "New project"}, {"Cmd+O", "Open project"}, {"Cmd+S", "Save project"}, {"Cmd+Shift+S", "Save project as"}, {"Cmd+I", "Import source"}}) {
                section.entries.push_back({keys, action});
            }
        }
        if (menu == editMenu) {
            section.entries.push_back({"Cmd+Z", "Undo"});
            section.entries.push_back({"Cmd+Shift+Z", "Redo"});
            section.entries.push_back({"Right-click Undo / Redo", "Go back or on several steps"});
        }
        for (const auto& command : commands) {
            if (command.menu == menu && command.shortcut.isNotEmpty()) { section.entries.push_back({command.shortcut, command.name.trimCharactersAtEnd(".")}); }
        }
        sections.push_back(std::move(section));
    }
    sections.push_back({"Timeline", {{"V / B / S / R", "Move, ripple trim, slip and stretch tools"}, {"M", "Add a marker at the playhead"}, {"Cmd+click the ruler", "Move the nearer loop point there"},
        {"Drag empty space", "Select clips (Shift adds)"}, {"Shift+click a clip", "Select a range of clips"}, {"Cmd+Shift+click a clip", "Add a range of clips"}, {"Cmd+click a clip", "Add or remove a clip"},
        {"Alt while dragging", "Bypass snapping"}, {"Drag a row's bottom edge", "Resize the track (double-click resets)"}, {"Double-click a lane", "Add a key"},
        {"Alt+Left / Right", "Nudge keys or clips a frame"}, {"Alt+Shift+Left / Right", "Nudge keys or clips ten frames"}}});
    sections.push_back({"Graph", {{"Double-click", "Add a key"}, {"Drag a box", "Select keys (Shift adds)"}, {"Drag box edges", "Scale key times"},
        {"F / Shift+F", "Frame all curves / this curve"}, {"Right-click a key", "Interpolation and easing"}, {"Drag the time axis", "Scrub"},
        {"Alt+arrows", "Nudge the selected keys"}, {"Alt+Shift+arrows", "Nudge them further"}, {"Right-click a curve's name", "Show or hide curves"}}});
    sections.push_back({"Scene", {{"G / R / S", "Move, rotate and scale tools"}, {"Y", "Anchor tool"}, {"Tab", "Pick parts"}, {"E", "Extract the picked parts"}, {"F", "Frame the selection"}, {"0", "Reset the view"},
        {"Click again", "Select what is behind"}, {"1 / 3 / 7", "Front, right and top views"}, {"Ctrl+1 / 3 / 7", "Back, left and bottom views"}, {"N", "Fly through the scene"}, {"P", "Show the motion path"},
        {"Two fingers / wheel", "Orbit / zoom"}, {"Shift + two fingers", "Pan"}, {"Alt+drag", "Orbit with the mouse"}}});
    sections.push_back({"Navigation", {{"Wheel / two fingers", "Pan the timeline or graph"}, {"Shift+wheel", "Pan sideways"}, {"Cmd+wheel / pinch", "Zoom time around the pointer"},
        {"Alt+wheel", "Track height, value range or key height"}, {"Middle-drag", "Grab and move the view"}}});
    Overlay::show(*this, std::move(sections));
}

namespace {
// When a file was last saved, as a person would say it.
juce::String edited(juce::Time time) {
    const auto seconds = (juce::Time::getCurrentTime() - time).inSeconds();
    const auto count = [](juce::int64 amount, const char* unit) { return juce::String(amount) + " " + unit + (amount == 1 ? "" : "s") + " ago"; };
    if (seconds < 60) { return "Just now"; }
    if (seconds < 3600) { return count(static_cast<juce::int64>(seconds / 60), "minute"); }
    if (seconds < 86400) { return count(static_cast<juce::int64>(seconds / 3600), "hour"); }
    const auto days = static_cast<juce::int64>(seconds / 86400);
    if (days == 1) { return "Yesterday"; }
    if (days < 7) { return count(days, "day"); }
    return time.formatted("%e %b %Y").trim();
}
}

void MotionEditor::showStartScreen() {
    if (startScreenOverlay != nullptr) {
        return;
    }
    const juce::File current(processor.currentProjectFile);
    std::vector<motion::ui::StartCard> recents, examples;
    for (int index = 0; index < processor.getNumRecentProjectFiles(); ++index) {
        const auto file = processor.getRecentProjectFile(index);
        // The project open now is Continue's, not one to switch to.
        if (!file.existsAsFile() || file == current) {
            continue;
        }
        const auto summary = motion::ProjectSummary::peek(file);
        motion::ui::StartCard card;
        card.name = file.getFileNameWithoutExtension();
        // What it is, or else where it is.
        card.detail = summary.description.isNotEmpty() ? summary.description : "In " + file.getParentDirectory().getFileName();
        card.when = edited(file.getLastModificationTime());
        card.file = file;
        card.picture = summary.picture;
        recents.push_back(std::move(card));
    }
    for (const auto& resource : exampleProjects()) {
        motion::ui::StartCard card;
        card.name = motion::ui::examples::displayName(resource);
        int size = 0;
        const auto* data = BinaryData::getNamedResource(resource.toRawUTF8(), size);
        const auto summary = data != nullptr ? motion::ProjectSummary::peek(data, static_cast<std::size_t>(size)) : motion::ProjectSummary();
        card.detail = summary.description;
        card.resource = resource;
        card.picture = summary.picture;
        examples.push_back(std::move(card));
    }
    // Carrying on is offered when there is something to carry on with.
    const auto& project = processor.document.mainProject();
    const auto anything = std::any_of(project.tracks.begin(), project.tracks.end(), [](const motion::Track& track) { return !track.clips.empty(); });
    juce::String continuing;
    if (anything) {
        // An unsaved one by its own name, such as the example it came from.
        const auto named = juce::String(project.name).trim();
        continuing = current.existsAsFile() ? current.getFileNameWithoutExtension() : named.isNotEmpty() ? named : juce::String("Untitled project");
        if (processor.document.unsaved()) { continuing << motion::style::dot() << "edited"; }
    }
    const juce::Component::SafePointer<MotionEditor> owner(this);
    motion::ui::StartScreen::Actions actions;
    actions.newProject = [owner] { if (owner != nullptr) { owner->resetToDefault(); } };
    actions.openProject = [owner] { if (owner != nullptr) { owner->CommonPluginEditor::openProject(); } };
    actions.showShortcuts = [owner] { if (owner != nullptr) { owner->showShortcuts(); } };
    actions.openRecent = [owner](const juce::File& file) { if (owner != nullptr) { owner->openProject(file); } };
    actions.revealRecent = [](const juce::File& file) { file.revealToUser(); };
    actions.forgetRecent = [owner](const juce::File& file) {
        if (owner != nullptr) { owner->processor.removeRecentProjectFile(file); }
    };
    actions.openExample = [owner](const juce::String& resource) { if (owner != nullptr) { owner->openExample(resource); } };
    actions.showAtStartup = [owner](bool show) {
        if (owner != nullptr) {
            owner->processor.globalSettings.set("showStartScreen", show);
            owner->processor.globalSettings.save();
        }
    };
    const auto atStartup = processor.globalSettings.getBool("showStartScreen", true);
    auto screen = std::make_unique<motion::ui::StartScreen>(std::move(recents), std::move(examples), continuing, atStartup, juce::String("Version ") + JucePlugin_VersionString, std::move(actions));
    startScreen = screen.get();
    startScreenOverlay = motion::ui::StartScreen::show(*this, std::move(screen));
}

// After the command line is read: a launch that only renders keeps no
// recovery, and one that opens a file or render skips the start screen, as
// one by automation does, or once it is turned off.
void MotionEditor::atLaunch() {
    const auto startScreen = restoreAtLaunch();
    // Files given with the launch open now, over what was restored.
    launched = true;
    if (launchFiles.isNotEmpty()) {
        CommonPluginEditor::handleCommandLine(std::exchange(launchFiles, juce::String()));
        return;
    }
    if (startScreen) { showStartScreenOnceReady(); }
}

// Reopens the work left when osci-motion last closed unexpectedly. True when
// the start screen is to show.
bool MotionEditor::restoreAtLaunch() {
    if (!juce::JUCEApplicationBase::isStandaloneApp() || commandLineRender != juce::File() || !processor.remembersFiles) {
        return false;
    }
    // One instance at a time keeps the recovery file: a second one opened
    // alongside neither reopens nor overwrites the first one's session.
    static juce::InterProcessLock recoveryLock("osci-motion recovery");
    static const bool ownsRecovery = recoveryLock.enter(0);
    if (ownsRecovery) { recovery = std::make_unique<motion::SessionRecovery>(processor.applicationFolder); }
    // A new project makes a new editor: what follows is for the app opening.
    static bool opened = false;
    if (std::exchange(opened, true)) {
        recoveredState = {processor.document.generation(), processor.document.revision(), processor.document.unsaved()};
        return false;
    }
    const auto left = recovery != nullptr ? recovery->file() : juce::File();
    juce::MemoryBlock session;
    if (left.existsAsFile() && left.loadFileAsData(session) && session.getSize() > 0) {
        processor.setStateInformation(session.getData(), static_cast<int>(session.getSize()));
        statusBar.show("Reopened your work from " + left.getLastModificationTime().formatted("%H:%M") + ", after osci-motion closed unexpectedly. Save it to keep it.", MotionStatusBar::Kind::notice);
    }
    recoveredState = {processor.document.generation(), processor.document.revision(), processor.document.unsaved()};
    return !osci::isJucewrightAutomationLaunch() && processor.globalSettings.getBool("showStartScreen", true);
}

// After Privacy & Terms, when they are to be accepted first.
void MotionEditor::showStartScreenOnceReady() {
    if (commandLineProject != CommandLineProject::none || projectLoad != nullptr) {
        return;
    }
    if (processor.legalNoticePending.load()) {
        const juce::Component::SafePointer<MotionEditor> owner(this);
        juce::Timer::callAfterDelay(250, [owner] {
            if (owner != nullptr) { owner->showStartScreenOnceReady(); }
        });
        return;
    }
    showStartScreen();
}

// Every few seconds the session has changed, it is written for recovery.
void MotionEditor::keepRecovery() {
    const auto now = juce::Time::getMillisecondCounterHiRes();
    if (recovery == nullptr || now < recoveryDue) {
        return;
    }
    recoveryDue = now + 10000;
    const auto& document = processor.document;
    const std::tuple<std::uint64_t, std::uint64_t, bool> current {document.generation(), document.revision(), document.unsaved()};
    if (current == recoveredState) {
        return;
    }
    juce::MemoryBlock session;
    processor.getStateInformation(session);
    if (recovery->write(std::move(session))) { recoveredState = current; }
}

// Written at once, with the picture of the Scope drawn a moment ago; with
// none yet (the Scope hidden), the project keeps the picture it had.
void MotionEditor::beforeSaving(std::function<void()> write) {
    if (recentFrame.isValid()) { processor.picture = motion::ProjectSummary::encode(recentFrame); }
    processor.savingProjectFile = true;
    write();
    processor.savingProjectFile = false;
}

void MotionEditor::refreshTiming() {
    timeline.repaint();
    curveEditor.repaint();
}

// The menu bar's Timing menu. Meter, frame rate and the beam's frequency
// are set in Properties > Composition.
juce::PopupMenu MotionEditor::timingMenu() {
    const auto& project = processor.document.project();
    juce::PopupMenu menu;
    menu.setLookAndFeel(&getLookAndFeel());
    // What the ruler's right-click offers, at the playhead.
    menu.addItem(motion::style::menuItem("Add marker...", 700, "M"));
    menu.addItem(701, "Add tempo change...");
    menu.addSeparator();
    menu.addItem(600, detectingTempo ? "Detecting tempo..." : "Detect tempo from audio", !detectingTempo && soundtrackClip() != 0);
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
    return menu;
}

double MotionEditor::snapDivision(int index) const {
    const std::array<double, 7> divisions { static_cast<double>(processor.document.project().beatsPerBar), 1, 0.5, 0.25, 0.125, 1.0 / 3, 1.0 / 6 };
    return divisions[static_cast<std::size_t>(std::clamp(index, 0, 6))];
}

// Display and snapping are view options (no undo step).
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
    return false;
}

void MotionEditor::tap() {
    if (tapTempo.count() == 0) {
        tapGeneration = processor.document.generation();
        tapScope = processor.document.scopeGeneration();
    }
    const auto bpm = tapTempo.tap(juce::Time::getMillisecondCounterHiRes() / 1000);
    tapButton.setButtonText(tapTempo.count() > 1 ? juce::String(static_cast<int>(tapTempo.count())) : "Tap");
    if (bpm.has_value()) {
        tappedBpm = bpm;
        tempoValue.setText(motion::style::bpm(*bpm), juce::dontSendNotification);
    }
    if (tapCommit == nullptr) {
        tapCommit = std::make_unique<juce::TimedCallback>([this] {
            tapCommit->stopTimer();
            tapButton.setButtonText("Tap");
            tapTempo.reset();
            if (!tappedBpm.has_value()) { return; }
            const auto bpm = *tappedBpm;
            tappedBpm.reset();
            // Another project or composition opened since: the taps were for the last.
            if (processor.document.generation() != tapGeneration || processor.document.scopeGeneration() != tapScope) {
                refreshTiming();
                return;
            }
            const auto result = processor.document.changeTempo(bpm);
            if (result.failed()) { statusBar.show(result.getErrorMessage()); } else { statusBar.show("Tempo " + motion::style::bpm(bpm) + " from tapping.", MotionStatusBar::Kind::notice); }
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
        statusBar.show(audio == nullptr ? "Import some audio first: drop a file or use File > Import source." : "Tempo is set on the main composition. Go back to it first.");
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
                owner->statusBar.show("Couldn't find a steady tempo in the audio. Set it by hand or tap it.", MotionStatusBar::Kind::warning);
                return;
            }
            double moved = 0;
            const auto bars = owner->processor.document.project().timeDisplay == motion::TimeDisplay::beats;
            const auto result = owner->processor.document.setTempoFromAudio(id, estimate->bpm, estimate->firstDownbeat, moved);
            if (result.failed()) { owner->statusBar.show(result.getErrorMessage()); return; }
            // Detection can land an octave off: the other reading is one click away.
            const auto other = estimate->bpm * 2 <= 200 ? estimate->bpm * 2 : estimate->bpm / 2;
            auto message = "Tempo " + motion::style::bpm(estimate->bpm) + " from the audio" + (estimate->confidence < 0.4 ? " (low confidence)" : "");
            if (moved > 0) { message += ", which moved " + juce::String(moved, 3) + " s so its first downbeat falls on a bar"; }
            message += bars ? "." : ". The ruler shows bars and beats.";
            owner->statusBar.show(message, MotionStatusBar::Kind::notice, "Use " + motion::style::bpm(other), [owner, other, generation] {
                if (owner == nullptr || owner->processor.document.generation() != generation) { return; }
                const auto changed = owner->processor.document.changeTempo(other);
                if (changed.failed()) { owner->statusBar.show(changed.getErrorMessage()); }
                owner->refreshTiming();
            });
        });
    });
}
