#pragma once

#include "../MotionProcessor.h"

class MotionCompositionView : public juce::Component {
public:
    explicit MotionCompositionView(MotionProcessor& processor) : processor(processor) {
        setName("Composition preview");
        setWantsKeyboardFocus(true);
    }
    motion::Id selected = 0;
    std::function<void(motion::Id)> onSelection;
    void refresh() { prepared = std::make_unique<motion::PreparedComposition>(processor.document.project()); repaint(); }
    void preview(const motion::Project& project) {
        prepared = std::make_unique<motion::PreparedComposition>(project);
        repaint();
    }

    void paint(juce::Graphics& g) override {
        g.fillAll(osci::Colours::veryDark());
        const auto frame = outputFrame();
        g.setColour(juce::Colours::white.withAlpha(0.07f));
        g.drawLine(frame.getCentreX(), frame.getY(), frame.getCentreX(), frame.getBottom());
        g.drawLine(frame.getX(), frame.getCentreY(), frame.getRight(), frame.getCentreY());
        g.setColour(juce::Colours::white.withAlpha(0.2f));
        g.drawRect(frame, 1.0f);
        if (prepared == nullptr || prepared->clips.empty()) {
            g.setColour(osci::Colours::text().withAlpha(0.5f));
            g.setFont(14);
            g.drawText("Drop an object here", getLocalBounds(), juce::Justification::centred);
            return;
        }
        const auto time = processor.position.load();
        for (const auto& clip : prepared->clips) {
            if (!clip.active(time)) {
                continue;
            }
            auto previous = projected(clip.sample(time, 0), time);
            for (int i = 1; i <= 512; ++i) {
                const auto point = clip.sample(time, static_cast<double>(i) / 512);
                const auto next = projected(point, time);
                const auto distance = previous.getDistanceFrom(next);
                const auto alpha = std::min(1.0f, 12.0f / std::max(1.0f, distance));
                const auto colour = clip.id == selected ? juce::Colour(0xff9affb3) : juce::Colour::fromFloatRGBA(point.r, point.g, point.b, 1);
                g.setColour(colour.withAlpha(alpha * 0.8f));
                g.drawLine({ previous, next }, clip.id == selected ? 1.4f : 1.0f);
                previous = next;
            }
        }
    }

    void mouseDown(const juce::MouseEvent& event) override {
        grabKeyboardFocus();
        before.reset();
        if (prepared == nullptr) {
            return;
        }
        float nearest = 18;
        motion::Id hit = 0;
        const auto time = processor.position.load();
        for (const auto& clip : prepared->clips) {
            if (!clip.active(time)) {
                continue;
            }
            for (int i = 0; i < 256; ++i) {
                const auto distance = projected(clip.sample(time, i / 256.0), time).getDistanceFrom(event.position);
                if (distance < nearest) {
                    nearest = distance;
                    hit = clip.id;
                }
            }
        }
        selected = hit;
        if (onSelection) {
            onSelection(hit);
        }
        if (hit != 0) {
            processor.playing.store(false);
            editTime = time;
            before = processor.document.project();
            editRevision = processor.document.revision();
            down = event.position;
            changed = false;
        }
        repaint();
    }

    void mouseDrag(const juce::MouseEvent& event) override {
        if (!validGesture()) {
            return;
        }
        auto project = *before;
        const auto delta = (event.position - down) / (outputFrame().getWidth() / 2);
        for (auto& track : project.tracks) {
            for (auto& clip : track.clips) {
                if (clip.id == selected) {
                    const auto time = clip.localTime(editTime);
                    for (const auto& [name, offset] : { std::pair { "position.x", delta.x }, std::pair { "position.y", -delta.y } }) {
                        auto& curve = clip.properties[name];
                        const auto value = curve.evaluate(time) + offset;
                        if (curve.animated()) {
                            curve.setKeyValue(time, value);
                        } else {
                            curve.base = value;
                        }
                    }
                }
            }
        }
        changed = delta.getDistanceFromOrigin() > 0;
        processor.document.preview(std::move(project));
        editRevision = processor.document.revision();
    }

    void mouseUp(const juce::MouseEvent&) override {
        if (validGesture()) {
            if (changed) {
                processor.document.commit("Move object", std::move(*before));
            }
            before.reset();
        }
    }

    bool keyPressed(const juce::KeyPress& key) override {
        if (key == juce::KeyPress::escapeKey && validGesture()) {
            processor.document.preview(std::move(*before));
            before.reset();
            return true;
        }
        return false;
    }

private:
    bool validGesture() {
        if (before.has_value() && editRevision != processor.document.revision()) {
            before.reset();
        }
        return before.has_value();
    }
    juce::Rectangle<float> outputFrame() const {
        const auto size = std::max(10, std::min(getWidth(), getHeight()) - 48);
        return getLocalBounds().toFloat().withSizeKeepingCentre(size, size);
    }
    juce::Point<float> projected(osci::Point point, double time) const {
        if (prepared != nullptr) { point = prepared->applyCompositionEffects(point, time); }
        const auto scale = 4.0f / std::max(0.05f, 4.0f - point.z);
        const auto frame = outputFrame();
        return { frame.getCentreX() + point.x * scale * frame.getWidth() / 2,
            frame.getCentreY() - point.y * scale * frame.getHeight() / 2 };
    }
    MotionProcessor& processor;
    std::unique_ptr<motion::PreparedComposition> prepared;
    std::optional<motion::Project> before;
    juce::Point<float> down;
    bool changed = false;
    double editTime = 0;
    std::uint64_t editRevision = 0;
};
