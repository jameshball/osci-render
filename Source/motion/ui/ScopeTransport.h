#pragma once

#include "MotionIcons.h"
#include "MotionStyle.h"
#include <functional>

// The full-screen Scope's transport, along its foot as a video player's is:
// to the start, play or pause, to the end, a bar to seek through the whole
// composition (its loop shaded), the time, and the speed. It shows while the
// pointer moves over the picture and fades once it rests, so the picture is
// all there is the rest of the time. A child of the Scope, so an OpenGL view
// never covers it.
class MotionScopeTransport final : public juce::Component, private juce::Timer {
public:
    MotionScopeTransport() {
        setName("Full-screen transport");
        start.setTooltip("To the start (Home)");
        end.setTooltip("To the end (End)");
        play.setTooltip("Play / pause (Space)");
        speed.setName("Playback speed");
        speed.setTooltip("Playback speed (< and > to change it)");
        for (auto* button : std::initializer_list<juce::Button*> {&start, &play, &end, &speed}) {
            button->setWantsKeyboardFocus(false);
            addAndMakeVisible(button);
        }
        start.onClick = [this] { if (onStart) { onStart(); } };
        end.onClick = [this] { if (onEnd) { onEnd(); } };
        play.onClick = [this] { if (onPlay) { onPlay(); } };
        speed.onClick = [this] { if (onSpeedMenu) { onSpeedMenu(speed); } };
        speed.setButtonText(speedText(1.0));
        // Its buttons' moves count as its own, for knowing it is hovered.
        addMouseListener(this, true);
        setAlpha(0.0f);
        setInterceptsMouseClicks(false, false);
    }
    ~MotionScopeTransport() override { if (watched != nullptr) { watched->removeMouseListener(&pointer); } }
    // Wakes when the pointer moves anywhere over `picture` (the Scope).
    void watch(juce::Component& picture) {
        watched = &picture;
        picture.addMouseListener(&pointer, true);
    }

    std::function<void()> onStart, onEnd, onPlay;
    std::function<void(double)> onSeek;
    std::function<void(juce::Component&)> onSpeedMenu;

    struct State {
        double position = 0, duration = 0, loopStart = 0, loopEnd = 0, speed = 1;
        bool playing = false, looping = false;
        juce::String time, length;
        bool operator==(const State&) const = default;
    };
    void setState(const State& next) {
        if (next == state) { return; }
        const auto playingChanged = next.playing != state.playing;
        state = next;
        if (playingChanged) {
            play.setIcon(state.playing ? motion::icons::Icon::pause : motion::icons::Icon::play);
            play.setTooltip(state.playing ? "Pause (Space)" : "Play (Space)");
        }
        speed.setButtonText(speedText(state.speed));
        repaint();
    }
    static juce::String speedText(double speed) {
        return (speed == 0.25 ? juce::String::fromUTF8("\xc2\xbc") : speed == 0.5 ? juce::String::fromUTF8("\xc2\xbd") : juce::String(static_cast<int>(speed))) + juce::String::fromUTF8("\xc3\x97");
    }

    // The pointer moved over the picture: show, and fade again once it rests.
    void wake() {
        lastActivity = juce::Time::getMillisecondCounterHiRes();
        setInterceptsMouseClicks(true, true);
        if (!isTimerRunning()) { startTimerHz(60); }
    }
    // Gone with full screen: hidden at once, ready to fade in next time.
    void sleep() {
        stopTimer();
        setAlpha(0.0f);
        setInterceptsMouseClicks(false, false);
        scrubbing = false;
        hovered = false;
    }

    static constexpr int preferredHeight = 40;
    void paint(juce::Graphics& g) override {
        const auto bounds = getLocalBounds().toFloat();
        motion::style::fillFloatingPanel(g, bounds, osci::Colours::veryDark());
        // The bar: the whole composition; the loop shaded on it; played so far
        // filled; the playhead a knob.
        const auto track = trackArea();
        const auto centre = track.getCentreY();
        const auto rail = juce::Rectangle<float>(track.getX(), centre - 2.0f, track.getWidth(), 4.0f);
        g.setColour(juce::Colours::white.withAlpha(.14f));
        g.fillRoundedRectangle(rail, 2.0f);
        if (state.duration > 0 && state.loopEnd > state.loopStart) {
            const auto from = xOf(state.loopStart), to = xOf(state.loopEnd);
            g.setColour(juce::Colours::white.withAlpha(state.looping ? .3f : .12f));
            g.fillRoundedRectangle(juce::Rectangle<float>(from, centre - 2.0f, std::max(2.0f, to - from), 4.0f), 2.0f);
        }
        const auto head = xOf(shownPosition());
        g.setColour(motion::style::accent());
        g.fillRoundedRectangle(rail.withRight(head), 2.0f);
        const auto grown = scrubbing || hovered;
        g.setColour(juce::Colours::white);
        g.fillEllipse(juce::Rectangle<float>(grown ? 12.0f : 9.0f, grown ? 12.0f : 9.0f).withCentre({head, centre}));
        g.setFont(motion::style::caption());
        g.setColour(osci::Colours::text().withAlpha(.85f));
        g.drawText(state.time, timeArea(), juce::Justification::centredRight, false);
        g.setColour(osci::Colours::textMuted());
        g.drawText(state.length, lengthArea(), juce::Justification::centredLeft, false);
    }
    void resized() override {
        auto area = getLocalBounds().reduced(6, 0);
        const auto cell = juce::Rectangle<int>(0, 0, 28, 28);
        for (auto* button : std::initializer_list<juce::Component*> {&start, &play, &end}) { button->setBounds(cell.withCentre({area.removeFromLeft(30).getCentreX(), area.getCentreY()})); }
        speed.setBounds(area.removeFromRight(38).withSizeKeepingCentre(34, 22));
    }
    void mouseDown(const juce::MouseEvent& event) override {
        if (!trackArea().expanded(0, 10).contains(event.position)) { return; }
        scrubbing = true;
        seekTo(event.position.x);
    }
    void mouseDrag(const juce::MouseEvent& event) override {
        if (scrubbing) { seekTo(event.position.x); }
    }
    void mouseUp(const juce::MouseEvent&) override {
        scrubbing = false;
        repaint();
    }
    void mouseMove(const juce::MouseEvent&) override {
        hovered = true;
        wake();
        repaint();
    }
    void mouseEnter(const juce::MouseEvent&) override {
        hovered = true;
        wake();
    }
    void mouseExit(const juce::MouseEvent& event) override {
        // Onto one of its own buttons is still on it.
        hovered = getLocalBounds().contains(event.getEventRelativeTo(this).getPosition());
        repaint();
    }

private:
    void timerCallback() override {
        // Held up while the pointer is on it or seeking; otherwise two seconds.
        if (hovered || scrubbing) { lastActivity = juce::Time::getMillisecondCounterHiRes(); }
        const auto idle = juce::Time::getMillisecondCounterHiRes() - lastActivity;
        const auto target = idle < 2000 ? 1.0f : 0.0f;
        const auto alpha = getAlpha();
        const auto next = target > alpha ? std::min(1.0f, alpha + .2f) : std::max(0.0f, alpha - .06f);
        if (next != alpha) { setAlpha(next); }
        if (next == 0.0f && target == 0.0f) {
            setInterceptsMouseClicks(false, false);
            stopTimer();
        }
    }
    // Where the bar runs: between the buttons and the time, the length and the speed.
    juce::Rectangle<float> trackArea() const {
        const auto left = static_cast<float>(end.getRight()) + 8.0f + timeWidth + 10.0f;
        const auto right = static_cast<float>(speed.getX()) - 8.0f - timeWidth - 10.0f;
        return {left, 0.0f, std::max(10.0f, right - left), static_cast<float>(getHeight())};
    }
    juce::Rectangle<float> timeArea() const { return {static_cast<float>(end.getRight()) + 8.0f, 0.0f, timeWidth, static_cast<float>(getHeight())}; }
    juce::Rectangle<float> lengthArea() const { return {trackArea().getRight() + 10.0f, 0.0f, timeWidth, static_cast<float>(getHeight())}; }
    float xOf(double time) const {
        const auto track = trackArea();
        const auto fraction = state.duration > 0 ? std::clamp(time / state.duration, 0.0, 1.0) : 0.0;
        return track.getX() + static_cast<float>(fraction) * track.getWidth();
    }
    // While seeking, the knob is where the pointer put it, not a block behind.
    double shownPosition() const { return scrubbing ? scrubTime : state.position; }
    void seekTo(float x) {
        const auto track = trackArea();
        const auto fraction = std::clamp((x - track.getX()) / std::max(1.0f, track.getWidth()), 0.0f, 1.0f);
        scrubTime = state.duration * static_cast<double>(fraction);
        if (onSeek) { onSeek(scrubTime); }
        repaint();
    }

    class SpeedChip final : public juce::TextButton {
    public:
        void paintButton(juce::Graphics& g, bool over, bool down) override {
            const auto bounds = getLocalBounds().toFloat();
            const auto changed = getButtonText() != MotionScopeTransport::speedText(1.0);
            g.setColour((changed ? motion::style::accent().withAlpha(.25f) : juce::Colours::white.withAlpha(over || down ? .1f : 0.0f)));
            g.fillRoundedRectangle(bounds, motion::style::radius);
            g.setFont(motion::style::heading());
            g.setColour(changed ? motion::style::accent().brighter(.4f) : osci::Colours::text().withAlpha(over ? .9f : .6f));
            g.drawText(getButtonText(), bounds, juce::Justification::centred, false);
        }
    };

    struct Pointer final : juce::MouseListener {
        explicit Pointer(MotionScopeTransport& owner) : transport(owner) {}
        // JUCE also reports moves when what is under a still pointer changes;
        // only the pointer really moving wakes the transport.
        void mouseMove(const juce::MouseEvent& event) override { moved(event); }
        void mouseDrag(const juce::MouseEvent& event) override { moved(event); }
        void moved(const juce::MouseEvent& event) {
            const auto at = event.getScreenPosition();
            if (!transport.isVisible() || at.getDistanceFrom(last) < 3) { return; }
            last = at;
            transport.wake();
        }
        MotionScopeTransport& transport;
        juce::Point<int> last {-100, -100};
    } pointer {*this};
    juce::Component::SafePointer<juce::Component> watched;
    static constexpr float timeWidth = 52.0f;
    motion::icons::Button start {"To the start", motion::icons::Icon::start}, play {"Play", motion::icons::Icon::play}, end {"To the end", motion::icons::Icon::end};
    SpeedChip speed;
    State state;
    double lastActivity = 0, scrubTime = 0;
    bool scrubbing = false, hovered = false;
};
