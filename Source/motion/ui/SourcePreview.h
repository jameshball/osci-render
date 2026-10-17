#pragma once

#include "MotionStyle.h"
#include "../model/PreparedSource.h"
#include <optional>

namespace motion::ui {
// One frame of a prepared source as strokes in a unit square, centred and
// scaled to fit: dark samples and long jumps break the line, as the beam
// blanks them. `view` places a point on the page (y up), front on unless
// given.
// `area`, when given, is the region drawn whatever the frame holds, so an
// animation's frames keep their places.
inline juce::Path traceSource(const PreparedSource& source, std::size_t frame, int steps, juce::Point<float> (*view)(const osci::Point&) = nullptr, std::optional<juce::Rectangle<float>> area = std::nullopt) {
    juce::Path path;
    if (frame >= source.frameCount()) { return path; }
    const auto pointFrames = source.drawingAt(frame) == nullptr;
    std::vector<juce::Point<float>> points;
    std::vector<bool> lit;
    points.reserve(static_cast<std::size_t>(steps) + 1);
    float left = 1e9f, right = -1e9f, top = 1e9f, bottom = -1e9f;
    for (int index = 0; index <= steps; ++index) {
        const auto point = source.sampleFrame(frame, static_cast<double>(index) / steps, 0);
        if (!std::isfinite(point.x) || !std::isfinite(point.y) || !std::isfinite(point.z)) { continue; }
        points.push_back(view != nullptr ? view(point) : juce::Point<float>(point.x, point.y));
        lit.push_back(point.r > 0 || point.g > 0 || point.b > 0);
        left = std::min(left, points.back().x);
        right = std::max(right, points.back().x);
        top = std::min(top, points.back().y);
        bottom = std::max(bottom, points.back().y);
    }
    if (points.empty()) { return path; }
    if (area.has_value()) {
        left = area->getX();
        right = area->getRight();
        top = area->getY();
        bottom = area->getBottom();
    }
    // A script that never sets a colour leaves every point unlit; draw it all.
    const auto anyLit = std::find(lit.begin(), lit.end(), true) != lit.end();
    const auto size = std::max({right - left, bottom - top, 1e-6f});
    const auto jump = size * .2f;
    const auto map = [&](juce::Point<float> point) {
        return juce::Point<float>(.5f + (point.x - (left + right) * .5f) / size, .5f - (point.y - (top + bottom) * .5f) / size);
    };
    bool open = false;
    for (std::size_t index = 0; index < points.size(); ++index) {
        const auto dark = pointFrames && anyLit && !lit[index];
        const auto jumped = index > 0 && points[index].getDistanceFrom(points[index - 1]) > jump;
        if (dark) { open = false; continue; }
        if (open && !jumped) { path.lineTo(map(points[index])); } else { path.startNewSubPath(map(points[index])); open = true; }
    }
    return path;
}

// What a source's settings will draw, worked out off the message thread
// whenever the settings change: a dark well with the strokes in the beam's
// green, and a caption (points and frames, or what went wrong) under it.
class SourcePreview final : public juce::Component, private juce::Timer {
public:
    struct Result {
        juce::Path path;
        juce::String caption, error;
        // An animation's frames, played in a loop at this rate (`path`
        // unused); each stands for `frameStep` of the source's when only a
        // share of them is traced.
        std::vector<juce::Path> frames;
        double framesPerSecond = 0;
        double frameStep = 1;
    };
    using Work = std::function<Result(const std::atomic<bool>& cancel)>;

    SourcePreview() { setName("Source preview"); }
    ~SourcePreview() override {
        stopTimer();
        cancel();
        pool.removeAllJobs(true, 4000);
    }

    // Runs `work` shortly after the last request, cancelling one in progress.
    void request(Work next) {
        pending = std::move(next);
        startTimer(120);
    }

    // The rate an animation plays at, whatever its result says (0: still);
    // and a caption in place of the result's.
    void setFrameRate(double framesPerSecond) {
        rate = framesPerSecond;
        updatePlayback();
        repaint();
    }
    // Shown at once, with no work to do (a live picture's latest frame).
    void show(Result result) {
        cancel();
        shown = std::move(result);
        busy = false;
        updatePlayback();
        repaint();
    }
    void setCaption(const juce::String& text) {
        caption = text;
        repaint();
    }

    void paint(juce::Graphics& g) override {
        const auto bounds = getLocalBounds().toFloat();
        g.setColour(juce::Colours::black);
        g.fillRoundedRectangle(bounds, style::radius + 1);
        g.setColour(style::hairline());
        g.drawRoundedRectangle(bounds.reduced(.5f), style::radius + 1, 1.0f);
        // The drawing leaves room at the foot for its caption.
        const auto side = std::min(bounds.getWidth(), bounds.getHeight() - 20) - 24;
        const auto square = juce::Rectangle<float>(side, side).withCentre({bounds.getCentreX(), bounds.getY() + 12 + side * .5f});
        const auto& drawn = shown.frames.empty() ? shown.path : shown.frames[frameNow()];
        if (!drawn.isEmpty()) {
            auto path = drawn;
            path.applyTransform(juce::AffineTransform::scale(square.getWidth(), square.getHeight()).translated(square.getX(), square.getY()));
            // A soft halo under a crisp core reads as a beam, not a vector.
            g.setColour(style::key().withAlpha(.18f));
            g.strokePath(path, juce::PathStrokeType(3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.setColour(style::key().brighter(.2f).withAlpha(.95f));
            g.strokePath(path, juce::PathStrokeType(1.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        g.setFont(style::caption());
        if (busy && drawn.isEmpty()) {
            g.setColour(osci::Colours::textMuted().withAlpha(.6f));
            g.drawText("Tracing...", bounds, juce::Justification::centred, false);
        }
        const auto failed = shown.error.isNotEmpty();
        g.setColour(failed ? style::error() : osci::Colours::textMuted().withAlpha(.6f));
        const auto text = failed ? shown.error : busy && !drawn.isEmpty() ? juce::String("Updating...") : caption.isNotEmpty() ? caption : shown.caption;
        g.drawFittedText(text, bounds.reduced(8, 6).toNearestInt(), failed ? juce::Justification::centred : juce::Justification::centredBottom, 3);
    }

private:
    void timerCallback() override {
        stopTimer();
        if (!pending) { return; }
        cancel();
        auto flag = std::make_shared<std::atomic<bool>>(false);
        current = flag;
        const auto generation = ++requested;
        busy = true;
        repaint();
        pool.addJob([work = std::move(pending), flag, generation, owner = juce::Component::SafePointer<SourcePreview>(this)] {
            auto result = work(*flag);
            if (flag->load()) { return; }
            juce::MessageManager::callAsync([owner, generation, result = std::move(result)] {
                if (owner == nullptr || generation != owner->requested) { return; }
                owner->shown = result;
                owner->busy = false;
                owner->updatePlayback();
                owner->repaint();
            });
        });
        pending = nullptr;
    }
    void cancel() {
        if (current != nullptr) { current->store(true); }
    }

    double playbackRate() const { return (rate.has_value() ? *rate : shown.framesPerSecond) / std::max(1.0, shown.frameStep); }
    void updatePlayback() {
        playStart = juce::Time::getMillisecondCounterHiRes();
        if (shown.frames.size() > 1 && playbackRate() > 0) {
            play.startTimerHz(std::clamp(juce::roundToInt(playbackRate()), 1, 60));
        } else {
            play.stopTimer();
        }
    }
    std::size_t frameNow() const {
        if (shown.frames.size() < 2 || playbackRate() <= 0) { return 0; }
        const auto elapsed = (juce::Time::getMillisecondCounterHiRes() - playStart) / 1000.0;
        return static_cast<std::size_t>(elapsed * playbackRate()) % shown.frames.size();
    }
    std::optional<double> rate;
    juce::String caption;
    double playStart = 0;
    juce::TimedCallback play {[this] { repaint(); }};
    juce::ThreadPool pool {1};
    Work pending;
    std::shared_ptr<std::atomic<bool>> current;
    std::uint64_t requested = 0;
    Result shown;
    bool busy = false;
};
}
