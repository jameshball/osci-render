#pragma once

#include "../MotionProcessor.h"
#include "ViewNavigation.h"
#include "MotionStyle.h"
#include "PlayheadStrip.h"
#include <set>

// Clip-local beat editor. Gestures preview immutable note content locally and
// commit once; an unrelated document revision cancels a stale gesture.
class MotionNotesEditor : public juce::Component, private juce::Timer {
public:
    explicit MotionNotesEditor(MotionProcessor& owner);
    std::function<void(motion::Id)> onEditInstrument;
    // Where the envelope popover points.
    juce::Component& envelopeAnchor() { return envelopeButton; }
    struct ViewState {
        motion::Id target = 0;
        std::set<motion::Id> selected;
        int pitch = 72, rowHeight = 16;
        double scroll = 0, zoom = 80;
    };
    ViewState viewState() const { return {target, selected, topPitch, rowHeight, scrollBeat, pixelsPerBeat}; }
    void restoreView(const ViewState& state);
    ~MotionNotesEditor() override { stopTimer(); processor.midiRecordingSession().cancel(); processor.setMidiAudition(0); }
    void repaintPlayhead() { playheadStrip.moveTo(*this, playheadX()); }
    void setSelection(motion::Id id);
    void refresh();
    void fitContents() { fit(); repaint(); }
    void visibilityChanged() override;
    void resized() override;
    void paint(juce::Graphics& g) override;
    void mouseDoubleClick(const juce::MouseEvent& event) override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent&) override;
    bool keyPressed(const juce::KeyPress& key) override;
    // The timeline's convention: wheel and trackpad pan (Shift makes the
    // wheel horizontal), Cmd/Ctrl+wheel or a pinch zooms time, Alt+wheel
    // changes the key height.
    void mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override;
    void mouseMagnify(const juce::MouseEvent& event, float scale) override {
        if (scale > 0) { zoomAround(event.x, scale); repaint(); }
    }
    void zoomAround(int x, double factor);
private:
    double pitchScroll = 0;
    void timerCallback() override;
    bool canAudition() const;
    const motion::Clip* currentClip() const;
    bool isLocked() const;
    void report(const juce::Result& result) { error = result.failed() ? result.getErrorMessage() : juce::String(); repaint(); }
    void commit(std::vector<motion::MidiNote> notes, const juce::String& label);
    void cancelDrag() { dragging = marquee = false; original.reset(); preview.reset(); }
    void fit();
    double snapStep() const { return std::max(.03125, processor.document.project().snapBeats); }
    double snap(double beat, const juce::ModifierKeys& mods) const { return std::max(0.0, !mods.isAltDown() && processor.document.project().gridSnap ? std::round(beat / snapStep()) * snapStep() : beat); }
    juce::Rectangle<int> gridBounds() const { return {keyboardWidth, 30 + rulerHeight, std::max(0, getWidth() - keyboardWidth), std::max(1, getHeight() - 30 - rulerHeight - 80)}; }
    juce::Rectangle<int> velocityBounds() const { return {keyboardWidth, getHeight() - 78, std::max(0, getWidth() - keyboardWidth), 58}; }
    std::optional<int> playheadX() const;
    int beatX(double beat) const { return keyboardWidth + juce::roundToInt(std::clamp((beat - scrollBeat) * pixelsPerBeat, -1000000.0, 1000000.0)); }
    double beatAt(int x) const { return std::max(0.0, scrollBeat + (x - keyboardWidth) / pixelsPerBeat); }
    int pitchY(int pitch) const { return gridBounds().getY() + (topPitch - pitch) * rowHeight; }
    int pitchAt(int y) const { return std::clamp(topPitch - (y - gridBounds().getY()) / rowHeight, 0, 127); }
    juce::Rectangle<int> noteBounds(const motion::MidiNote& note) const { return {beatX(note.start), pitchY(note.pitch) + 1, std::max(5, beatX(note.end()) - beatX(note.start)), rowHeight - 2}; }
    motion::Id hit(juce::Point<int> position) const;
    MotionProcessor& processor;
    motion::Id target = 0;
    std::set<motion::Id> selected, marqueeSelection;
    motion::PlayheadStrip playheadStrip;
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
