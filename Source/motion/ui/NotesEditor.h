#pragma once

#include "../MotionProcessor.h"
#include <set>

// Clip-local beat editor. Gestures preview immutable note content locally and
// commit once; an unrelated document revision cancels a stale gesture.
class MotionNotesEditor : public juce::Component, private juce::Timer {
public:
    explicit MotionNotesEditor(MotionProcessor& owner) : processor(owner) {
        setName("MIDI notes editor");
        setWantsKeyboardFocus(true);
        for (auto* button : {&create, &fitButton, &remove, &audition, &envelopeButton, &record, &cancelRecording}) { addAndMakeVisible(button); }
        record.setName("Record MIDI notes"); cancelRecording.setName("Cancel MIDI recording");
        record.setTooltip("Record unquantized notes, velocity and sustain into this clip. Existing notes are kept. Other controllers are not applied yet.");
        record.onClick = [this] {
            cancelDrag();
            auto& session = processor.midiRecordingSession();
            if (session.busy()) { session.stop(); }
            else { report(session.start(target)); }
            refresh();
        };
        cancelRecording.onClick = [this] { processor.midiRecordingSession().cancel(); refresh(); };
        recordingStatus.setName("MIDI recording status"); recordingStatus.setFont(juce::FontOptions(11));
        recordingStatus.setBorderSize({}); addAndMakeVisible(recordingStatus);
        startTimerHz(15);
        audition.setClickingTogglesState(true);
        audition.setColour(juce::TextButton::buttonOnColourId, osci::Colours::accentColor().withAlpha(.22f));
        audition.setColour(juce::TextButton::textColourOnId, osci::Colours::text());
        audition.setTooltip("Play this clip alone using a connected MIDI keyboard. Does not change notes or exports. MIDI input devices are selected in Audio settings.");
        audition.onClick = [this] { processor.setMidiAudition(audition.getToggleState() ? target : 0); refresh(); };
        envelopeButton.onClick = [this] { if (onEditInstrument) { onEditInstrument(target); } };
        create.onClick = [this] { report(processor.document.assignMidi(target, 0)); refresh(); fit(); };
        fitButton.onClick = [this] { fit(); repaint(); };
        remove.onClick = [this] { report(processor.document.clearMidi(target)); refresh(); };
        velocity.setName("Note velocity");
        velocity.setEditable(false, true);
        velocity.setJustificationType(juce::Justification::centred);
        velocity.setColour(juce::Label::backgroundColourId, osci::Colours::veryDark());
        velocity.onTextChange = [this] {
            if (updating) { return; }
            const auto* clip = currentClip();
            if (clip == nullptr || clip->midi == nullptr || isLocked() || selected.empty()) { return; }
            const auto text = velocity.getText().trim();
            const auto value = text.getIntValue();
            if (text.isEmpty() || !text.containsOnly("0123456789") || value < 1 || value > 127) { error = "Velocity must be between 1 and 127."; refresh(); return; }
            auto notes = clip->midi->notes();
            for (auto& note : notes) { if (selected.contains(note.id)) { note.velocity = value; } }
            commit(std::move(notes), "Change note velocity");
        };
        addAndMakeVisible(velocity);
        refresh();
    }
    std::function<void(motion::Id)> onEditInstrument;
    struct ViewState {
        motion::Id target = 0;
        std::set<motion::Id> selected;
        int pitch = 72, rowHeight = 16;
        double scroll = 0, zoom = 80;
    };
    ViewState viewState() const { return {target, selected, topPitch, rowHeight, scrollBeat, pixelsPerBeat}; }
    void restoreView(const ViewState& state) {
        cancelDrag();
        setSelection(state.target);
        selected = state.selected;
        topPitch = state.pitch; rowHeight = state.rowHeight; scrollBeat = state.scroll; pixelsPerBeat = state.zoom;
        refresh();
    }
    ~MotionNotesEditor() override { stopTimer(); processor.midiRecordingSession().cancel(); processor.setMidiAudition(0); }
    void setSelection(motion::Id id) {
        if (target == id) { refresh(); return; }
        processor.midiRecordingSession().cancel();
        processor.setMidiAudition(0);
        target = id; selected.clear(); cancelDrag(); error.clear(); fit(); refresh();
    }
    void refresh() {
        const auto* clip = currentClip();
        const bool recording = processor.midiRecordingSession().busy();
        const auto available = clip != nullptr && canAudition();
        if (!available && processor.getMidiAudition() != 0) { processor.setMidiAudition(0); }
        audition.setEnabled(available && !recording);
        envelopeButton.setEnabled(clip != nullptr && clip->composition == 0 && !isLocked() && !recording);
        audition.setTooltip(clip != nullptr && !available ? "This track is muted or excluded by solo. Make it audible to use MIDI audition."
            : "Play this clip alone using a connected MIDI keyboard. Does not change notes or exports. MIDI input devices are selected in Audio settings.");
        audition.setToggleState(clip != nullptr && processor.getMidiAudition() == target, juce::dontSendNotification);
        if (dragging && processor.document.revision() != dragRevision) { cancelDrag(); }
        const auto pattern = clip != nullptr ? clip->midi : nullptr;
        std::erase_if(selected, [&](auto id) { return pattern == nullptr || std::none_of(pattern->notes().begin(), pattern->notes().end(), [id](const auto& note) { return note.id == id; }); });
        create.setVisible(clip != nullptr && pattern == nullptr && !isLocked() && !recording);
        fitButton.setVisible(pattern != nullptr); remove.setVisible(pattern != nullptr);
        remove.setEnabled(!isLocked() && !recording);
        velocity.setVisible(pattern != nullptr); velocity.setEnabled(!isLocked() && !selected.empty() && !recording);
        updating = true;
        juce::String value = "-";
        if (pattern != nullptr) {
            int commonVelocity = -1;
            for (const auto& note : pattern->notes()) {
                if (!selected.contains(note.id)) { continue; }
                if (commonVelocity < 0) { commonVelocity = note.velocity; value = juce::String(commonVelocity); }
                else if (commonVelocity != note.velocity) { value = "Mixed"; break; }
            }
        }
        if (!velocity.isBeingEdited()) { velocity.setText(value, juce::dontSendNotification); }
        updating = false;
        record.setButtonText(recording ? (processor.midiRecordingSession().stopping() ? "Finishing..." : "Stop recording") : "Record notes");
        record.setEnabled(recording ? !processor.midiRecordingSession().stopping() : available && !isLocked() && clip->composition == 0);
        record.setColour(juce::TextButton::buttonColourId, recording ? juce::Colour(0xff8b3039) : osci::Colours::surfaceRaised());
        cancelRecording.setVisible(recording);
        // Clip-bound actions appear once a visual clip is chosen.
        record.setVisible(clip != nullptr || recording);
        audition.setVisible(clip != nullptr);
        envelopeButton.setVisible(clip != nullptr);
        const auto& status = processor.midiRecordingSession().message();
        recordingStatus.setText(error.isNotEmpty() ? error : status, juce::dontSendNotification);
        recordingStatus.setColour(juce::Label::textColourId, error.isNotEmpty() || processor.midiRecordingSession().hasError() ? juce::Colours::orange : osci::Colours::textMuted());
        recordingStatus.setVisible(error.isNotEmpty() || status.isNotEmpty());
        resized(); repaint();
    }
    void fitContents() { fit(); repaint(); }
    void visibilityChanged() override {
        if (isVisible()) { fit(); refresh(); }
        else { processor.midiRecordingSession().cancel(); processor.setMidiAudition(0); }
    }
    void resized() override {
        auto header = getLocalBounds().removeFromTop(30).reduced(6, 3);
        record.setBounds(header.removeFromRight(118)); header.removeFromRight(6);
        if (cancelRecording.isVisible()) { cancelRecording.setBounds(header.removeFromRight(64)); header.removeFromRight(6); }
        recordingStatus.setBounds(getLocalBounds().removeFromBottom(20).reduced(8, 0));
        audition.setBounds(header.removeFromRight(112)); header.removeFromRight(6);
        envelopeButton.setBounds(header.removeFromRight(92)); header.removeFromRight(6);
        remove.setBounds(header.removeFromRight(106)); header.removeFromRight(6);
        fitButton.setBounds(header.removeFromRight(46)); header.removeFromRight(6);
        velocity.setBounds(header.removeFromRight(48));
        create.setBounds(getLocalBounds().withSizeKeepingCentre(144, 28).translated(0, 24));
    }
    void paint(juce::Graphics& g) override {
        g.fillAll(osci::Colours::veryDark());
        g.setColour(osci::Colours::surfaceRaised()); g.fillRect(0, 0, getWidth(), 30);
        const auto* clip = currentClip();
        const auto pattern = preview != nullptr ? preview : (clip != nullptr ? clip->midi : nullptr);
        g.setFont(13.0f); g.setColour(osci::Colours::text());
        const auto titleEnd = pattern != nullptr ? velocity.getX() - 73 : envelopeButton.getX();
        g.drawText(clip == nullptr ? "Notes" : juce::String(clip->name), 12, 0, std::max(0, titleEnd - 12), 30, juce::Justification::centredLeft);
        if (pattern == nullptr) {
            g.setColour(osci::Colours::text().withAlpha(.65f));
            g.drawText(clip == nullptr ? "Select a visual clip to edit its notes." : isLocked() ? "This track is locked. Notes cannot be created." : processor.midiRecordingSession().busy() ? "Play your MIDI keyboard. Stop to keep the notes, or Cancel to discard." : "Create notes, record a performance, or assign a MIDI file from Assets.", getLocalBounds().reduced(12).translated(0, -12), juce::Justification::centred);
            return;
        }
        g.setColour(osci::Colours::text().withAlpha(.7f));
        g.drawText("Velocity", velocity.getX() - 60, 0, 55, 30, juce::Justification::centredRight);
        const auto grid = gridBounds();
        const auto lane = velocityBounds();
        for (int pitch = 0; pitch < 128; ++pitch) {
            const auto y = pitchY(pitch);
            if (y + rowHeight < grid.getY() || y >= grid.getBottom()) { continue; }
            const auto pc = pitch % 12;
            const bool black = pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10;
            g.setColour(black ? osci::Colours::veryDark() : osci::Colours::surfaceRaised());
            g.fillRect(0, y, keyboardWidth - 1, rowHeight);
            g.setColour(osci::Colours::text().withAlpha(black ? .018f : .035f));
            g.fillRect(grid.getX(), y, grid.getWidth(), rowHeight);
            g.setColour(osci::Colours::text().withAlpha(.065f)); g.drawHorizontalLine(y + rowHeight, 0, static_cast<float>(getWidth()));
            if (pc == 0 || rowHeight >= 16) {
                g.setColour(osci::Colours::text().withAlpha(.7f)); g.setFont(11.0f);
                g.drawText(juce::MidiMessage::getMidiNoteName(pitch, true, true, 4), 5, y, keyboardWidth - 10, rowHeight, juce::Justification::centredRight);
            }
        }
        g.setColour(osci::Colours::surfaceRaised()); g.fillRect(0, 30, getWidth(), rulerHeight);
        g.fillRect(lane.withX(0).withWidth(getWidth()));
        auto beatStep = snapStep();
        while (beatStep * pixelsPerBeat < 24) { beatStep *= 2; }
        const auto first = std::floor(scrollBeat / beatStep) * beatStep;
        for (double beat = first; beat < scrollBeat + grid.getWidth() / pixelsPerBeat + beatStep; beat += beatStep) {
            const auto x = beatX(beat);
            if (x < keyboardWidth) { continue; }
            const bool bar = std::abs(std::remainder(beat, processor.document.project().beatsPerBar)) < .00001;
            g.setColour(osci::Colours::text().withAlpha(bar ? .16f : .055f));
            g.drawVerticalLine(x, 30, static_cast<float>(lane.getBottom()));
            if (bar) {
                g.setColour(osci::Colours::text().withAlpha(.7f)); g.setFont(11.0f);
                g.drawText(juce::String(1 + static_cast<int>(std::round(beat / processor.document.project().beatsPerBar))), x + 4, 30, 40, rulerHeight, juce::Justification::centredLeft);
            }
        }
        {
            juce::Graphics::ScopedSaveState scope(g); g.reduceClipRegion(grid);
            for (const auto& note : pattern->notes()) {
                const auto bounds = noteBounds(note);
                if (!bounds.intersects(grid)) { continue; }
                const bool active = selected.contains(note.id);
                const auto fill = osci::Colours::surfaceRaised().interpolatedWith(osci::Colours::accentColor(), .18f + .16f * note.velocity / 127.0f);
                g.setColour(active ? fill.brighter(.25f) : fill); g.fillRoundedRectangle(bounds.toFloat(), 2);
                g.setColour(active ? osci::Colours::text().withAlpha(.8f) : osci::Colours::accentColor().withAlpha(.45f)); g.drawRoundedRectangle(bounds.toFloat().reduced(.5f), 2, 1);
                if (bounds.getWidth() > 32 && rowHeight >= 11) { g.setColour(osci::Colours::text().withAlpha(.9f)); g.setFont(10.0f); g.drawText(juce::MidiMessage::getMidiNoteName(note.pitch, true, true, 4), bounds.reduced(4, 0), juce::Justification::centredLeft); }
            }
            if (clip != nullptr) {
                const auto timing = clip->timing(processor.document.project().tempo());
                const auto scale = clip->curveBpm(processor.document.project().tempo()) / 60;
                const auto left = beatX(clip->localTime(timing.start, processor.document.project().tempo()) * scale);
                const auto right = beatX(clip->localTime(timing.end(), processor.document.project().tempo()) * scale);
                g.setColour(osci::Colours::veryDark().withAlpha(.65f));
                if (left > grid.getX()) { g.fillRect(grid.withRight(std::min(left, grid.getRight()))); }
                if (right < grid.getRight()) { g.fillRect(grid.withLeft(std::max(right, grid.getX()))); }
                g.setColour(osci::Colours::text().withAlpha(.3f));
                if (left >= grid.getX() && left < grid.getRight()) { g.drawVerticalLine(left, static_cast<float>(grid.getY()), static_cast<float>(grid.getBottom())); }
                if (right >= grid.getX() && right < grid.getRight()) { g.drawVerticalLine(right, static_cast<float>(grid.getY()), static_cast<float>(grid.getBottom())); }
            }
            if (marquee) { g.setColour(osci::Colours::accentColor().withAlpha(.12f)); g.fillRect(selectionBox); g.setColour(osci::Colours::accentColor()); g.drawRect(selectionBox, 1); }
        }
        for (const auto& note : pattern->notes()) {
            const auto x = beatX(note.start);
            if (x < keyboardWidth || x >= getWidth()) { continue; }
            const auto y = lane.getBottom() - 5 - juce::roundToInt(note.velocity / 127.0 * (lane.getHeight() - 12));
            g.setColour(selected.contains(note.id) ? osci::Colours::text() : osci::Colours::accentColor().withAlpha(.7f));
            g.fillRect(x, y, 2, lane.getBottom() - y - 4); g.fillEllipse(static_cast<float>(x - 2), static_cast<float>(y - 2), 6, 6);
        }
        g.setColour(osci::Colours::text().withAlpha(.55f)); g.setFont(10.0f);
        g.drawText("Velocity", 5, lane.getY() + 4, keyboardWidth - 8, 15, juce::Justification::centredLeft);
        if (clip != nullptr && clip->contains(processor.position.load(), processor.document.project().tempo())) {
            const auto beat = clip->localTime(processor.position.load(), processor.document.project().tempo()) * clip->curveBpm(processor.document.project().tempo()) / 60;
            const auto x = beatX(beat);
            if (x >= keyboardWidth && x < getWidth()) { g.setColour(osci::Colours::accentColor().withAlpha(.65f)); g.drawVerticalLine(x, 30, static_cast<float>(lane.getBottom())); }
        }
        g.setColour(error.isEmpty() ? osci::Colours::text().withAlpha(.55f) : juce::Colours::orange); g.setFont(11.0f);
        if (!recordingStatus.isVisible()) {
            g.drawText(error.isEmpty() ? (isLocked() ? "Track locked | Notes are read-only. Selection, Fit and navigation remain available." : "Double-click: add | Drag: move/resize | Delete: remove | Alt: bypass snap | Cmd/Ctrl-wheel: zoom") : error,
            8, getHeight() - 20, getWidth() - 16, 20, juce::Justification::centredLeft);
        }
    }
    void mouseDoubleClick(const juce::MouseEvent& event) override {
        if (processor.midiRecordingSession().busy()) { return; }
        const auto* clip = currentClip();
        if (clip == nullptr || clip->midi == nullptr || isLocked() || !gridBounds().contains(event.getPosition()) || hit(event.getPosition()) != 0) { return; }
        auto notes = clip->midi->notes();
        motion::Id id = 1;
        std::set<motion::Id> ids; for (const auto& note : notes) { ids.insert(note.id); }
        while (ids.contains(id)) { ++id; }
        notes.push_back({id, snap(beatAt(event.x), event.mods), snapStep(), pitchAt(event.y), 100, 1});
        selected = {id}; commit(std::move(notes), "Add MIDI note");
    }
    void mouseDown(const juce::MouseEvent& event) override {
        if (processor.midiRecordingSession().busy()) { return; }
        grabKeyboardFocus(); error.clear();
        const auto* clip = currentClip();
        if (clip == nullptr || clip->midi == nullptr) { return; }
        if (event.y >= 30 && event.y < gridBounds().getY() && event.x >= keyboardWidth) {
            const auto local = beatAt(event.x) * 60 / clip->curveBpm(processor.document.project().tempo());
            const auto timing = clip->timing(processor.document.project().tempo());
            processor.seek(std::clamp(timing.projectTime(local), timing.start, timing.end())); return;
        }
        const auto id = hit(event.getPosition());
        if (id == 0) {
            marqueeSelection = selected;
            if (!event.mods.isShiftDown()) { selected.clear(); }
            additiveMarquee = event.mods.isShiftDown();
            marquee = gridBounds().contains(event.getPosition()); anchor = event.getPosition(); selectionBox = {}; refresh(); return;
        }
        if (event.mods.isShiftDown()) { if (selected.contains(id)) { selected.erase(id); refresh(); return; } selected.insert(id); }
        else if (!selected.contains(id)) { selected = {id}; }
        if (isLocked()) { refresh(); return; }
        dragging = true; dragRevision = processor.document.revision(); original = clip->midi; preview = original; anchor = event.getPosition();
        dragMode = velocityBounds().contains(event.getPosition()) ? 2 : 0;
        for (const auto& note : original->notes()) { if (note.id == id && dragMode == 0 && event.x >= noteBounds(note).getRight() - 6) { dragMode = 1; } }
        refresh();
    }
    void mouseDrag(const juce::MouseEvent& event) override {
        if (processor.midiRecordingSession().busy()) { return; }
        if (marquee) {
            selectionBox = juce::Rectangle<int>(anchor, event.getPosition()).getIntersection(gridBounds());
            const auto* clip = currentClip();
            if (clip != nullptr && clip->midi != nullptr) {
                selected = additiveMarquee ? marqueeSelection : std::set<motion::Id>();
                for (const auto& note : clip->midi->notes()) { if (selectionBox.intersects(noteBounds(note))) { selected.insert(note.id); } }
            }
            repaint(); return;
        }
        if (!dragging || !original || processor.document.revision() != dragRevision) { cancelDrag(); return; }
        auto notes = original->notes();
        auto delta = (event.x - anchor.x) / pixelsPerBeat;
        if (!event.mods.isAltDown() && processor.document.project().gridSnap) { delta = std::round(delta / snapStep()) * snapStep(); }
        int pitchDelta = juce::roundToInt(static_cast<double>(anchor.y - event.y) / rowHeight);
        double earliest = 1000000; int low = 127, high = 0;
        for (const auto& note : notes) { if (selected.contains(note.id)) { earliest = std::min(earliest, note.start); low = std::min(low, note.pitch); high = std::max(high, note.pitch); } }
        pitchDelta = std::clamp(pitchDelta, -low, 127 - high);
        if (dragMode == 0) { delta = std::max(delta, -earliest); }
        for (auto& note : notes) {
            if (!selected.contains(note.id)) { continue; }
            if (dragMode == 2) { note.velocity = std::clamp(note.velocity + anchor.y - event.y, 1, 127); }
            else if (dragMode == 1) { note.duration = std::max(std::min(.001, note.duration), note.duration + delta); }
            else { note.start += delta; note.pitch += pitchDelta; }
        }
        if (notes == original->notes()) { preview = original; repaint(); return; }
        const auto result = motion::MidiNotes::create(std::move(notes));
        if (result) { preview = result.source; }
        repaint();
    }
    void mouseUp(const juce::MouseEvent&) override {
        if (marquee) { marquee = false; refresh(); return; }
        if (!dragging) { return; }
        auto result = preview;
        const bool changed = result != original && processor.document.revision() == dragRevision;
        const auto mode = dragMode; cancelDrag();
        if (changed) { report(processor.document.setMidiNotes(target, result, mode == 2 ? "Change note velocity" : mode == 1 ? "Resize MIDI notes" : "Move MIDI notes")); }
        refresh();
    }
    bool keyPressed(const juce::KeyPress& key) override {
        if (processor.midiRecordingSession().busy()) { if (key == juce::KeyPress::escapeKey) { processor.midiRecordingSession().cancel(); } return true; }
        if (key == juce::KeyPress::escapeKey) { if (marquee) { selected = marqueeSelection; } cancelDrag(); refresh(); return true; }
        const auto* clip = currentClip();
        if (clip == nullptr || clip->midi == nullptr) { return false; }
        if (key.getModifiers().isCommandDown() && key.getKeyCode() == 'A') { for (const auto& note : clip->midi->notes()) { selected.insert(note.id); } refresh(); return true; }
        if (isLocked()) { return false; }
        const std::vector<motion::Id> ids(selected.begin(), selected.end());
        if (ids.empty()) { return false; }
        if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey) {
            const auto result = clip->midi->withoutNotes(ids);
            if (result) { report(processor.document.setMidiNotes(target, result.source, "Delete MIDI notes")); } refresh(); return true;
        }
        const auto code = key.getKeyCode();
        if (code == juce::KeyPress::leftKey || code == juce::KeyPress::rightKey || code == juce::KeyPress::upKey || code == juce::KeyPress::downKey) {
            const auto beats = code == juce::KeyPress::leftKey ? -snapStep() : code == juce::KeyPress::rightKey ? snapStep() : 0;
            const auto pitch = code == juce::KeyPress::upKey ? 1 : code == juce::KeyPress::downKey ? -1 : 0;
            const auto result = clip->midi->moveNotes(ids, beats, pitch * (key.getModifiers().isShiftDown() ? 12 : 1));
            if (result) { report(processor.document.setMidiNotes(target, result.source, "Move MIDI notes")); } refresh(); return true;
        }
        return false;
    }
    void mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override {
        if (event.mods.isCommandDown() || event.mods.isCtrlDown()) {
            const auto under = beatAt(event.x);
            pixelsPerBeat = std::clamp(pixelsPerBeat * std::exp(wheel.deltaY * 2), 12.0, 600.0);
            scrollBeat = std::max(0.0, under - (event.x - keyboardWidth) / pixelsPerBeat);
        } else if (event.mods.isShiftDown() || std::abs(wheel.deltaX) > std::abs(wheel.deltaY)) {
            scrollBeat = std::max(0.0, scrollBeat - (event.mods.isShiftDown() ? wheel.deltaY : wheel.deltaX) * 4);
        } else { topPitch = std::clamp(topPitch + juce::roundToInt(wheel.deltaY * 12), std::min(127, gridBounds().getHeight() / rowHeight), 127); }
        repaint();
    }
private:
    void timerCallback() override {
        const auto message = processor.midiRecordingSession().message();
        const auto busy = processor.midiRecordingSession().busy();
        if (message != lastRecordingStatus || busy != wasRecording) {
            if (wasRecording && !busy) { fit(); }
            lastRecordingStatus = message; wasRecording = busy; refresh();
        }
    }
    bool canAudition() const {
        const auto& project = processor.document.project();
        for (const auto& track : project.tracks) {
            for (const auto& clip : track.clips) {
                if (clip.id == target) { return motion::trackIsAudible(project, track); }
            }
        }
        return false;
    }
    const motion::Clip* currentClip() const {
        for (const auto& track : processor.document.project().tracks) {
            if (track.kind != motion::TrackKind::visual) { continue; }
            for (const auto& clip : track.clips) { if (clip.id == target) { return &clip; } }
        }
        return nullptr;
    }
    bool isLocked() const {
        for (const auto& track : processor.document.project().tracks) {
            for (const auto& clip : track.clips) { if (clip.id == target) { return track.locked; } }
        }
        return false;
    }
    void report(const juce::Result& result) { error = result.failed() ? result.getErrorMessage() : juce::String(); repaint(); }
    void commit(std::vector<motion::MidiNote> notes, const juce::String& label) {
        const auto result = motion::MidiNotes::create(std::move(notes));
        if (result) { report(processor.document.setMidiNotes(target, result.source, label)); } else { error = juce::String(result.error); }
        refresh();
    }
    void cancelDrag() { dragging = marquee = false; original.reset(); preview.reset(); }
    void fit() {
        const auto* clip = currentClip();
        scrollBeat = 0; topPitch = 72; pixelsPerBeat = 80; rowHeight = 16;
        if (clip == nullptr) { return; }
        double first = std::max(0.0, clip->localTime(clip->timing(processor.document.project().tempo()).start, processor.document.project().tempo()) * clip->curveBpm(processor.document.project().tempo()) / 60);
        double last = first + 8;
        if (clip->midi != nullptr && !clip->midi->notes().empty()) {
            first = clip->midi->notes().front().start; last = std::max(first + 4, clip->midi->length());
            int high = 0, low = 127;
            for (const auto& note : clip->midi->notes()) { high = std::max(high, note.pitch); low = std::min(low, note.pitch); }
            rowHeight = std::clamp(gridBounds().getHeight() / (high - low + 5), 6, 20);
            topPitch = std::min(127, high + 2);
        }
        scrollBeat = first; pixelsPerBeat = std::clamp(std::max(200, getWidth() - keyboardWidth - 24) / (last - first), 12.0, 160.0);
    }
    double snapStep() const { return std::max(.03125, processor.document.project().snapBeats); }
    double snap(double beat, const juce::ModifierKeys& mods) const { return std::max(0.0, !mods.isAltDown() && processor.document.project().gridSnap ? std::round(beat / snapStep()) * snapStep() : beat); }
    juce::Rectangle<int> gridBounds() const { return {keyboardWidth, 30 + rulerHeight, std::max(0, getWidth() - keyboardWidth), std::max(1, getHeight() - 30 - rulerHeight - 80)}; }
    juce::Rectangle<int> velocityBounds() const { return {keyboardWidth, getHeight() - 78, std::max(0, getWidth() - keyboardWidth), 58}; }
    int beatX(double beat) const { return keyboardWidth + juce::roundToInt(std::clamp((beat - scrollBeat) * pixelsPerBeat, -1000000.0, 1000000.0)); }
    double beatAt(int x) const { return std::max(0.0, scrollBeat + (x - keyboardWidth) / pixelsPerBeat); }
    int pitchY(int pitch) const { return gridBounds().getY() + (topPitch - pitch) * rowHeight; }
    int pitchAt(int y) const { return std::clamp(topPitch - (y - gridBounds().getY()) / rowHeight, 0, 127); }
    juce::Rectangle<int> noteBounds(const motion::MidiNote& note) const { return {beatX(note.start), pitchY(note.pitch) + 1, std::max(5, beatX(note.end()) - beatX(note.start)), rowHeight - 2}; }
    motion::Id hit(juce::Point<int> position) const {
        const auto* clip = currentClip();
        if (clip == nullptr || clip->midi == nullptr) { return 0; }
        for (auto it = clip->midi->notes().rbegin(); it != clip->midi->notes().rend(); ++it) {
            if ((gridBounds().contains(position) && noteBounds(*it).contains(position)) || (velocityBounds().contains(position) && std::abs(position.x - beatX(it->start)) <= 5)) { return it->id; }
        }
        return 0;
    }
    MotionProcessor& processor;
    motion::Id target = 0;
    std::set<motion::Id> selected, marqueeSelection;
    std::shared_ptr<const motion::MidiNotes> original, preview;
    std::uint64_t dragRevision = 0;
    bool dragging = false, marquee = false, updating = false, additiveMarquee = false;
    int dragMode = 0, topPitch = 72, rowHeight = 16;
    static constexpr int keyboardWidth = 62, rulerHeight = 22;
    double scrollBeat = 0, pixelsPerBeat = 80;
    juce::Point<int> anchor;
    juce::Rectangle<int> selectionBox;
    juce::String error, lastRecordingStatus;
    bool wasRecording = false;
    juce::Label recordingStatus;
    juce::TextButton record{"Record notes"}, cancelRecording{"Cancel"};
    juce::TextButton envelopeButton {"Envelope..."};
    juce::TextButton create {"Create notes"}, fitButton {"Fit"}, remove {"Remove MIDI"}, audition {"MIDI audition"};
    juce::Label velocity;
};
