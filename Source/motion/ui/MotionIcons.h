#pragma once

#include <JuceHeader.h>
#include "MotionStyle.h"
#include <optional>

// osci-motion's icons: Material Design glyphs (Apache 2.0) on a 24 px grid,
// drawn at 18 px so their 2 px strokes land on whole device pixels on
// high-density displays. One button draws them all, so toolbars match.
namespace motion::icons {
enum class Icon { bezier, wave, videocam, visibility, visibilityOff, move, rotate, scale, path, fly, frame, play, pause, start, end, loop, add, select, slip, stretch, ripple, magnet, pen, line, freehand, rectangle, ellipse, erase, lock, trash, undo, redo, check, record, settings, openInNew, fullscreen, fullscreenExit, aspectRatio, cast, bold, italic, alignLeft, alignCentre, alignRight, lineSpacing, keyframe, close, parts, box, lasso, connected };

inline const juce::Path& path(Icon icon) {
    static const auto paths = [] {
        std::map<Icon, juce::Path> result;
        const auto add = [&result](Icon key, const char* data) { result[key] = juce::Drawable::parseSVGPath(data); };
        // A keyframe: an outlined diamond.
        // Ways of picking parts: a dashed box, a lasso, separate pieces.
        add(Icon::box, "M3 5h2V3c-1.1 0-2 .9-2 2zm0 8h2v-2H3v2zm4 8h2v-2H7v2zM3 9h2V7H3v2zm10-6h-2v2h2V3zm6 0v2h2c0-1.1-.9-2-2-2zM5 21v-2H3c0 1.1.9 2 2 2zm-2-4h2v-2H3v2zM9 3H7v2h2V3zm2 18h2v-2h-2v2zm8-8h2v-2h-2v2zm0 8c1.1 0 2-.9 2-2h-2v2zm0-12h2V7h-2v2zm0 8h2v-2h-2v2zm-4 4h2v-2h-2v2zm0-16h2V3h-2v2z");
        add(Icon::connected, "M12 2l-5.5 9h11L12 2zm0 3.84L13.93 9h-3.87L12 5.84zM17.5 13c-2.49 0-4.5 2.01-4.5 4.5s2.01 4.5 4.5 4.5 4.5-2.01 4.5-4.5-2.01-4.5-4.5-4.5zm0 7c-1.38 0-2.5-1.12-2.5-2.5s1.12-2.5 2.5-2.5 2.5 1.12 2.5 2.5-1.12 2.5-2.5 2.5zM3 21.5h8v-8H3v8zm2-6h4v4H5v-4z");
        {
            juce::Path loop, rope, lasso, tail;
            loop.addEllipse(3.0f, 3.0f, 18.0f, 11.5f);
            juce::PathStrokeType(2.0f).createStrokedPath(lasso, loop);
            rope.startNewSubPath(8.5f, 13.8f);
            rope.cubicTo(6.0f, 16.0f, 10.0f, 18.5f, 7.0f, 21.5f);
            juce::PathStrokeType(2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded).createStrokedPath(tail, rope);
            lasso.addPath(tail);
            result[Icon::lasso] = lasso;
        }
        // A dashed marquee with a pointer: picking parts.
        add(Icon::parts, "M17 5h-2V3h2v2zm-2 16h2v-2.59L19.59 21 21 19.59 18.41 17H21v-2h-6v6zm4-12h2V7h-2v2zm0 4h2v-2h-2v2zm-8 8h2v-2h-2v2zM7 5h2V3H7v2zM3 17h2v-2H3v2zm2 4v-2H3c0 1.1.9 2 2 2zM19 3v2h2c0-1.1-.9-2-2-2zm-8 2h2V3h-2v2zM3 9h2V7H3v2zm4 12h2v-2H7v2zm-4-8h2v-2H3v2zm0-8h2V3c-1.1 0-2 .9-2 2z");
        add(Icon::close, "M19 6.41L17.59 5 12 10.59 6.41 5 5 6.41 10.59 12 5 17.59 6.41 19 12 13.41 17.59 19 19 17.59 13.41 12z");
        add(Icon::keyframe, "M12 3l9 9-9 9-9-9 9-9zm0 3.1L6.1 12 12 17.9 17.9 12 12 6.1z");
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
        add(Icon::visibility, "M12 4.5C7 4.5 2.73 7.61 1 12c1.73 4.39 6 7.5 11 7.5s9.27-3.11 11-7.5c-1.73-4.39-6-7.5-11-7.5zM12 17c-2.76 0-5-2.24-5-5s2.24-5 5-5 5 2.24 5 5-2.24 5-5 5zm0-8c-1.66 0-3 1.34-3 3s1.34 3 3 3 3-1.34 3-3-1.34-3-3-3z");
        add(Icon::visibilityOff, "M12 7c2.76 0 5 2.24 5 5 0 .65-.13 1.26-.36 1.83l2.92 2.92c1.51-1.26 2.7-2.89 3.43-4.75-1.73-4.39-6-7.5-11-7.5-1.4 0-2.74.25-3.98.7l2.16 2.16C10.74 7.13 11.35 7 12 7zM2 4.27l2.28 2.28.46.46C3.08 8.3 1.78 10.02 1 12c1.73 4.39 6 7.5 11 7.5 1.55 0 3.03-.3 4.38-.84l.42.42L19.73 22 21 20.73 3.27 3 2 4.27zM7.53 9.8l1.55 1.55c-.05.21-.08.43-.08.65 0 1.66 1.34 3 3 3 .22 0 .44-.03.65-.08l1.55 1.55c-.67.33-1.41.53-2.2.53-2.76 0-5-2.24-5-5 0-.79.2-1.53.53-2.2zm4.31-.78l3.15 3.15.02-.16c0-1.66-1.34-3-3-3l-.17.01z");
        add(Icon::videocam, "M17 10.5V7c0-.55-.45-1-1-1H4c-.55 0-1 .45-1 1v10c0 .55.45 1 1 1h12c.55 0 1-.45 1-1v-3.5l4 4v-11l-4 4z");
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
        add(Icon::lock, "M18 8h-1V6c0-2.76-2.24-5-5-5S7 3.24 7 6v2H6c-1.1 0-2 .9-2 2v10c0 1.1.9 2 2 2h12c1.1 0 2-.9 2-2V10c0-1.1-.9-2-2-2zm-6 9c-1.1 0-2-.9-2-2s.9-2 2-2 2 .9 2 2-.9 2-2 2zm3.1-9H8.9V6c0-1.71 1.39-3.1 3.1-3.1 1.71 0 3.1 1.39 3.1 3.1v2z");
        add(Icon::trash, "M6 19c0 1.1.9 2 2 2h8c1.1 0 2-.9 2-2V7H6v12zM19 4h-3.5l-1-1h-5l-1 1H5v2h14V4z");
        add(Icon::undo, "M12.5 8c-2.65 0-5.05.99-6.9 2.6L2 7v9h9l-3.62-3.62c1.39-1.16 3.16-1.88 5.12-1.88 3.54 0 6.55 2.31 7.6 5.5l2.37-.78C21.08 11.03 17.15 8 12.5 8z");
        add(Icon::redo, "M18.4 10.6C16.55 8.99 14.15 8 11.5 8c-4.65 0-8.58 3.03-9.96 7.22L3.9 16c1.05-3.19 4.05-5.5 7.6-5.5 1.95 0 3.73.72 5.12 1.88L13 16h9V7l-3.6 3.6z");
        add(Icon::check, "M9 16.17L4.83 12l-1.42 1.41L9 19 21 7l-1.41-1.41z");
        // The Scope's controls.
        add(Icon::record, "M12 6a6 6 0 1 0 0 12a6 6 0 1 0 0-12z");
        add(Icon::settings, "M19.14 12.94c.04-.3.06-.61.06-.94 0-.32-.02-.64-.07-.94l2.03-1.58c.18-.14.23-.41.12-.61l-1.92-3.32c-.12-.22-.37-.29-.59-.22l-2.39.96c-.5-.38-1.03-.7-1.62-.94l-.36-2.54c-.04-.24-.24-.41-.48-.41h-3.84c-.24 0-.43.17-.47.41l-.36 2.54c-.59.24-1.13.57-1.62.94l-2.39-.96c-.22-.08-.47 0-.59.22L2.74 8.87c-.12.21-.08.47.12.61l2.03 1.58c-.05.3-.09.63-.09.94s.02.64.07.94l-2.03 1.58c-.18.14-.23.41-.12.61l1.92 3.32c.12.22.37.29.59.22l2.39-.96c.5.38 1.03.7 1.62.94l.36 2.54c.05.24.24.41.48.41h3.84c.24 0 .44-.17.47-.41l.36-2.54c.59-.24 1.13-.56 1.62-.94l2.39.96c.22.08.47 0 .59-.22l1.92-3.32c.12-.22.07-.47-.12-.61l-2.01-1.58zM12 15.6c-1.98 0-3.6-1.62-3.6-3.6s1.62-3.6 3.6-3.6 3.6 1.62 3.6 3.6-1.62 3.6-3.6 3.6z");
        add(Icon::openInNew, "M19 19H5V5h7V3H5c-1.11 0-2 .9-2 2v14c0 1.1.89 2 2 2h14c1.1 0 2-.9 2-2v-7h-2v7zM14 3v2h3.59l-9.83 9.83 1.41 1.41L19 6.41V10h2V3h-7z");
        add(Icon::fullscreen, "M7 14H5v5h5v-2H7v-3zm-2-4h2V7h3V5H5v5zm12 7h-3v2h5v-5h-2v3zM14 5v2h3v3h2V5h-5z");
        add(Icon::fullscreenExit, "M5 16h3v3h2v-5H5v2zm3-8H5v2h5V5H8v3zm6 11h2v-3h3v-2h-5v5zm2-11V5h-2v5h5V8h-3z");
        add(Icon::aspectRatio, "M19 12h-2v3h-3v2h5v-5zM7 9h3V7H5v5h2V9zm14-6H3c-1.1 0-2 .9-2 2v14c0 1.1.9 2 2 2h18c1.1 0 2-.9 2-2V5c0-1.1-.9-2-2-2zm0 16.01H3V4.99h18v14.02z");
        add(Icon::cast, "M21 3H3c-1.1 0-2 .9-2 2v3h2V5h18v14h-7v2h7c1.1 0 2-.9 2-2V5c0-1.1-.9-2-2-2zM1 18v3h3c0-1.66-1.34-3-3-3zm0-4v2c2.76 0 5 2.24 5 5h2c0-3.87-3.13-7-7-7zm0-4v2c4.97 0 9 4.03 9 9h2c0-6.08-4.93-11-11-11z");
        // Typography.
        add(Icon::bold, "M15.6 10.79c.97-.67 1.65-1.77 1.65-2.79 0-2.26-1.75-4-4-4H7v14h7.04c2.09 0 3.71-1.7 3.71-3.79 0-1.52-.86-2.82-2.15-3.42zM10 6.5h3c.83 0 1.5.67 1.5 1.5s-.67 1.5-1.5 1.5h-3v-3zm3.5 9H10v-3h3.5c.83 0 1.5.67 1.5 1.5s-.67 1.5-1.5 1.5z");
        add(Icon::italic, "M10 4v3h2.21l-3.42 8H6v3h8v-3h-2.21l3.42-8H18V4z");
        add(Icon::alignLeft, "M15 15H3v2h12v-2zm0-8H3v2h12V7zM3 13h18v-2H3v2zm0 8h18v-2H3v2zM3 3v2h18V3H3z");
        add(Icon::alignCentre, "M7 15v2h10v-2H7zm-4 6h18v-2H3v2zm0-8h18v-2H3v2zm4-6v2h10V7H7zM3 3v2h18V3H3z");
        add(Icon::alignRight, "M3 21h18v-2H3v2zm6-4h12v-2H9v2zm-6-4h18v-2H3v2zm6-4h12V7H9v2zM3 3v2h18V3H3z");
        add(Icon::lineSpacing, "M6 7h2.5L5 3.5 1.5 7H4v10H1.5L5 20.5 8.5 17H6V7zm4-2v2h12V5H10zm0 14h12v-2H10v2zm0-6h12v-2H10v2z");
        // The pen: a curve between two points with their handles.
        juce::Path curve, bezier;
        curve.startNewSubPath(4, 19);
        curve.cubicTo(8, 7, 16, 7, 20, 19);
        curve.startNewSubPath(4, 19);
        curve.lineTo(8, 7);
        curve.startNewSubPath(20, 19);
        curve.lineTo(16, 7);
        juce::PathStrokeType(1.6f).createStrokedPath(bezier, curve);
        bezier.addRectangle(2, 17, 4, 4);
        bezier.addRectangle(18, 17, 4, 4);
        bezier.addEllipse(6.5f, 5.5f, 3, 3);
        bezier.addEllipse(14.5f, 5.5f, 3, 3);
        bezier.setUsingNonZeroWinding(true);
        result[Icon::bezier] = bezier;
        // Modulation: one cycle of a sine.
        juce::Path sine, wave;
        for (int step = 0; step <= 32; ++step) {
            const auto x = 3.0f + 18.0f * static_cast<float>(step) / 32;
            const auto y = 12.0f - 6.0f * std::sin(static_cast<float>(step) / 32 * juce::MathConstants<float>::twoPi);
            if (step == 0) { sine.startNewSubPath(x, y); } else { sine.lineTo(x, y); }
        }
        juce::PathStrokeType(2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded).createStrokedPath(wave, sine);
        result[Icon::wave] = wave;
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
    void buttonStateChanged() override { fade.follow(*this); }
    void paintButton(juce::Graphics& g, bool, bool down) override {
        const auto bounds = getLocalBounds().toFloat().reduced(1.0f);
        const auto on = getToggleState();
        if (on) {
            g.setColour(onColour.withMultipliedAlpha(down ? 1.3f : 1.0f));
            g.fillRoundedRectangle(bounds, motion::style::radius + 1);
        } else if (fade.value() > 0.0f || down) {
            g.setColour(juce::Colours::white.withAlpha(down ? .14f : .08f * fade.value()));
            g.fillRoundedRectangle(bounds, motion::style::radius + 1);
        }
        const auto idle = osci::Colours::text().withAlpha(.72f);
        const auto colour = tint.has_value() ? *tint : on ? juce::Colours::white : idle.interpolatedWith(osci::Colours::text(), fade.value());
        draw(g, icon, bounds, colour.withMultipliedAlpha(isEnabled() ? 1.0f : .35f), iconSize);
    }
    // A glyph that keeps its own colour (the red record dot), and the fill
    // behind a button that is on.
    std::optional<juce::Colour> tint;
    juce::Colour onColour = motion::style::onFill();
private:
    Icon icon;
    motion::style::Fade fade {*this};
};

// A quiet text action with a leading icon (Add track), hover fades in.
class LabelButton final : public juce::Button {
public:
    LabelButton(const juce::String& name, Icon glyph) : juce::Button(name), icon(glyph) {
        setTitle(name);
        setButtonText(name);
        setWantsKeyboardFocus(false);
        setMouseClickGrabsKeyboardFocus(false);
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
    }
    // The natural width: icon, gap, text and side padding.
    int idealWidth() const { return juce::roundToInt(leading + iconSize + gap + juce::TextLayout::getStringWidth(motion::style::body(), getButtonText()) + trailing); }
    void buttonStateChanged() override { fade.follow(*this); }
    void paintButton(juce::Graphics& g, bool, bool down) override {
        const auto bounds = getLocalBounds().toFloat();
        if (fade.value() > 0.0f || down) {
            g.setColour(juce::Colours::white.withAlpha(down ? .12f : .06f * fade.value()));
            g.fillRoundedRectangle(bounds, motion::style::radius + 1);
        }
        const auto colour = osci::Colours::textMuted().interpolatedWith(osci::Colours::text(), fade.value()).withMultipliedAlpha(isEnabled() ? 1.0f : .35f);
        auto area = getLocalBounds().withTrimmedLeft(leading);
        draw(g, icon, area.removeFromLeft(static_cast<int>(iconSize)).toFloat(), colour, iconSize);
        area.removeFromLeft(gap);
        g.setColour(colour);
        g.setFont(motion::style::body());
        g.drawText(getButtonText(), area, juce::Justification::centredLeft, false);
    }
    float iconSize = 16.0f;
    // Space before the icon, between it and the text, and after the text.
    int leading = 6, gap = 5, trailing = 8;
private:
    Icon icon;
    motion::style::Fade fade {*this};
};

// A floating vertical strip of tool buttons in groups, with the same 4 px
// margin on every side of each button's highlight. Hidden buttons take no room.
class ToolStrip : public juce::Component {
public:
    static constexpr int cell = 28, inset = 3, groupGap = 9;
    void setGroups(std::vector<std::vector<juce::Button*>> buttons) {
        groups = std::move(buttons);
        for (auto& group : groups) {
            for (auto* button : group) { addAndMakeVisible(button); }
        }
        resized();
    }
    // A strip runs down, or across when horizontal.
    bool horizontal = false;
    int preferredWidth() const { return horizontal ? length() : cell + 2 * inset; }
    int preferredHeight() const { return horizontal ? cell + 2 * inset : length(); }
    void paint(juce::Graphics& g) override {
        const auto bounds = getLocalBounds().toFloat();
        // Opaque, so strokes behind it never show through the tools.
        motion::style::fillFloatingPanel(g, bounds, osci::Colours::veryDark());
        g.setColour(juce::Colours::white.withAlpha(.1f));
        for (const auto at : separators) {
            const auto across = static_cast<float>(inset + 4), along = static_cast<float>(at), extent = static_cast<float>(cell - 8);
            if (horizontal) {
                g.fillRect(along, across, 1.0f, extent);
            } else {
                g.fillRect(across, along, extent, 1.0f);
            }
        }
    }
    void resized() override {
        separators.clear();
        int position = inset, shown = 0;
        for (const auto& group : groups) {
            if (std::none_of(group.begin(), group.end(), [](const auto* button) { return button->isVisible(); })) { continue; }
            if (shown++ > 0) {
                separators.push_back(position + groupGap / 2);
                position += groupGap;
            }
            for (auto* button : group) {
                if (!button->isVisible()) { continue; }
                button->setBounds(horizontal ? position : inset, horizontal ? inset : position, cell, cell);
                position += cell;
            }
        }
        repaint();
    }
private:
    int length() const {
        int total = inset * 2, shown = 0;
        for (const auto& group : groups) {
            const auto count = std::count_if(group.begin(), group.end(), [](const auto* button) { return button->isVisible(); });
            if (count == 0) { continue; }
            total += static_cast<int>(count) * cell + (shown++ > 0 ? groupGap : 0);
        }
        return total;
    }
    std::vector<std::vector<juce::Button*>> groups;
    std::vector<int> separators;
};
}
