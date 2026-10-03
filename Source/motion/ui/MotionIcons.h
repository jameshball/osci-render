#pragma once

#include <JuceHeader.h>
#include "MotionStyle.h"

// osci-motion's icons: Material Design glyphs (Apache 2.0) on a 24 px grid,
// drawn at 18 px so their 2 px strokes land on whole device pixels on
// high-density displays. One button draws them all, so toolbars match.
namespace motion::icons {
enum class Icon { move, rotate, scale, path, fly, frame, play, pause, start, end, loop, add, select, slip, stretch, ripple, magnet, pen, line, freehand, rectangle, ellipse, erase, trash, undo, redo, close, check };

inline const juce::Path& path(Icon icon) {
    static const auto paths = [] {
        std::map<Icon, juce::Path> result;
        const auto add = [&result](Icon key, const char* data) { result[key] = juce::Drawable::parseSVGPath(data); };
        add(Icon::move, "M10 9h4V6h3l-5-5-5 5h3v3zm-1 1H6V7l-5 5 5 5v-3h3v-4zm14 2l-5-5v3h-3v4h3v3l5-5zm-9 3h-4v3H7l5 5 5-5h-3v-3z");
        add(Icon::rotate, "M17.65 6.35C16.2 4.9 14.21 4 12 4c-4.42 0-7.99 3.58-7.99 8s3.57 8 7.99 8c3.73 0 6.84-2.55 7.73-6h-2.08c-.82 2.33-3.04 4-5.65 4-3.31 0-6-2.69-6-6s2.69-6 6-6c1.66 0 3.14.69 4.22 1.78L13 11h7V4l-2.35 2.35z");
        add(Icon::scale, "M21 11V3h-8l3.29 3.29-10 10L3 13v8h8l-3.29-3.29 10-10z");
        add(Icon::path, "M23 8c0 1.1-.9 2-2 2-.18 0-.35-.02-.51-.07l-3.56 3.55c.05.16.07.34.07.52 0 1.1-.9 2-2 2s-2-.9-2-2c0-.18.02-.36.07-.52l-2.55-2.55c-.16.05-.34.07-.52.07s-.36-.02-.52-.07l-4.55 4.56c.05.16.07.33.07.51 0 1.1-.9 2-2 2s-2-.9-2-2 .9-2 2-2c.18 0 .35.02.51.07l4.56-4.55C8.02 9.36 8 9.18 8 9c0-1.1.9-2 2-2s2 .9 2 2c0 .18-.02.36-.07.52l2.55 2.55c.16-.05.34-.07.52-.07s.36.02.52.07l3.55-3.56C19.02 8.35 19 8.18 19 8c0-1.1.9-2 2-2s2 .9 2 2z");
        add(Icon::fly, "M21 3L3 10.53v.98l6.84 2.65L12.48 21h.98L21 3z");
        add(Icon::frame, "M3 5v4h2V5h4V3H5c-1.1 0-2 .9-2 2zm2 10H3v4c0 1.1.9 2 2 2h4v-2H5v-4zm14 4h-4v2h4c1.1 0 2-.9 2-2v-4h-2v4zm0-16h-4v2h4v4h2V5c0-1.1-.9-2-2-2z");
        add(Icon::play, "M8 5v14l11-7z");
        add(Icon::pause, "M6 19h4V5H6v14zm8-14v14h4V5h-4z");
        add(Icon::start, "M6 6h2v12H6zm3.5 6l8.5 6V6z");
        add(Icon::end, "M6 18l8.5-6L6 6v12zM16 6v12h2V6h-2z");
        add(Icon::loop, "M7 7h10v3l4-4-4-4v3H5v6h2V7zm10 10H7v-3l-4 4 4 4v-3h12v-6h-2v4z");
        add(Icon::add, "M19 13h-6v6h-2v-6H5v-2h6V5h2v6h6v2z");
        // Timeline editing tools.
        add(Icon::select, "M7 2l12 11.2-5.8.5 3.3 7.3-2.2 1-3.2-7.4L7 18.5V2z");
        add(Icon::slip, "M3 5h18v14H3V5zm2 2v10h14V7H5zm3 5l3-3v2h2V9l3 3-3 3v-2h-2v2l-3-3z");
        add(Icon::stretch, "M9 11h6V8l4 4-4 4v-3H9v3l-4-4 4-4v3zM2 20V4h2v16H2zm18 0V4h2v16h-2z");
        add(Icon::ripple, "M2 11h8V8l4 4-4 4v-3H2v-2zm13-7h2v16h-2V4zm4 4h3v8h-3V8z");
        add(Icon::magnet, "M3 7v6a9 9 0 0 0 18 0V7h-4v6a5 5 0 0 1-10 0V7H3zm0-4v3h4V3H3zm14 0v3h4V3h-4z");
        // Drawing tools.
        add(Icon::pen, "M3 17.25V21h3.75L17.81 9.94l-3.75-3.75L3 17.25zM20.71 7.04a.996.996 0 0 0 0-1.41l-2.34-2.34a.996.996 0 0 0-1.41 0l-1.83 1.83 3.75 3.75 1.83-1.83z");
        add(Icon::line, "M4 18.6L18.6 4 20 5.4 5.4 20 4 18.6zM3 21v-4h4v4H3zM17 7V3h4v4h-4z");
        add(Icon::freehand, "M4.6 17.9c-.8-.8-.8-2 .1-3 1.6-1.6 4.4-1.3 6.2-3 1.4-1.4.8-3.1-.8-3.2-1.6 0-3 1.5-4.5 1.2-1-.2-1.4-1.2-.8-2C6 6.5 8 5 10.5 5c3.2 0 5 2.4 3.4 5.2-1.4 2.4-4.8 2.6-6.4 4-.6.5-.4 1.2.3 1.2 1.6 0 3.5-1.6 6-1.6 2.2 0 4.2 1.3 4.2 3.3 0 .6-.5 1-1 .9-.5 0-.8-.4-.9-.9-.2-1-1.3-1.4-2.4-1.3-2 .2-3.7 1.9-6.1 2.4-1.2.2-2.3.2-3-.3z");
        add(Icon::rectangle, "M3 5h18v14H3V5zm2 2v10h14V7H5z");
        add(Icon::ellipse, "M12 4C6.48 4 2 7.58 2 12s4.48 8 10 8 10-3.58 10-8-4.48-8-10-8zm0 14c-4.41 0-8-2.69-8-6s3.59-6 8-6 8 2.69 8 6-3.59 6-8 6z");
        add(Icon::erase, "M15.14 3c-.51 0-1.02.2-1.41.59L2.59 14.73c-.78.77-.78 2.04 0 2.83L5.03 20h7.66l8.72-8.73c.79-.77.79-2.04 0-2.83l-4.85-4.85c-.39-.39-.91-.59-1.42-.59zM6.44 18.03l-2.44-2.44 6.2-6.2 4.85 4.85-3.77 3.79H6.44z");
        add(Icon::trash, "M6 19c0 1.1.9 2 2 2h8c1.1 0 2-.9 2-2V7H6v12zM19 4h-3.5l-1-1h-5l-1 1H5v2h14V4z");
        add(Icon::undo, "M12.5 8c-2.65 0-5.05.99-6.9 2.6L2 7v9h9l-3.62-3.62c1.39-1.16 3.16-1.88 5.12-1.88 3.54 0 6.55 2.31 7.6 5.5l2.37-.78C21.08 11.03 17.15 8 12.5 8z");
        add(Icon::redo, "M18.4 10.6C16.55 8.99 14.15 8 11.5 8c-4.65 0-8.58 3.03-9.96 7.22L3.9 16c1.05-3.19 4.05-5.5 7.6-5.5 1.95 0 3.73.72 5.12 1.88L13 16h9V7l-3.6 3.6z");
        add(Icon::close, "M19 6.41L17.59 5 12 10.59 6.41 5 5 6.41 10.59 12 5 17.59 6.41 19 12 13.41 17.59 19 19 17.59 13.41 12z");
        add(Icon::check, "M9 16.17L4.83 12l-1.42 1.41L9 19 21 7l-1.41-1.41z");
        return result;
    }();
    return paths.at(icon);
}

// Draws the icon's 24 px grid into `area` at `size` px, on whole pixels.
inline void draw(juce::Graphics& g, Icon icon, juce::Rectangle<float> area, juce::Colour colour, float size = 18.0f) {
    const auto box = area.withSizeKeepingCentre(size, size);
    const auto origin = juce::Point<float>(std::round(box.getX()), std::round(box.getY()));
    g.setColour(colour);
    g.fillPath(path(icon), juce::AffineTransform::scale(size / 24.0f).translated(origin));
}

// A square icon button: toggles light up in the accent, hover fades in.
class Button final : public juce::Button {
public:
    Button(const juce::String& name, Icon glyph) : juce::Button(name), icon(glyph) {
        setTitle(name);
        setWantsKeyboardFocus(false);
        setMouseClickGrabsKeyboardFocus(false);
    }
    void setIcon(Icon glyph) {
        if (icon != glyph) {
            icon = glyph;
            repaint();
        }
    }
    float iconSize = 18.0f;
    void paintButton(juce::Graphics& g, bool highlighted, bool down) override {
        fade.setTarget(highlighted && isEnabled());
        const auto bounds = getLocalBounds().toFloat().reduced(1.0f);
        const auto on = getToggleState();
        if (on) {
            g.setColour(motion::style::accent().withAlpha(down ? .45f : .35f));
            g.fillRoundedRectangle(bounds, motion::style::radius + 1);
        } else if (fade.value() > 0.0f || down) {
            g.setColour(juce::Colours::white.withAlpha(down ? .14f : .08f * fade.value()));
            g.fillRoundedRectangle(bounds, motion::style::radius + 1);
        }
        const auto idle = motion::style::text().withAlpha(.72f);
        const auto colour = on ? juce::Colours::white : idle.interpolatedWith(motion::style::text(), fade.value());
        draw(g, icon, bounds, colour.withMultipliedAlpha(isEnabled() ? 1.0f : .35f), iconSize);
    }
private:
    Icon icon;
    motion::style::Fade fade {*this};
};
}
