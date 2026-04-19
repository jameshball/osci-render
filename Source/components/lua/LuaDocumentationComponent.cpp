#include "LuaDocumentationComponent.h"

LuaDocumentationComponent::LuaDocumentationComponent() {
    configureLabel(titleLabel, juce::Font(juce::FontOptions(22.0f, juce::Font::bold)), juce::Justification::centred);
    titleLabel.setText("Lua Scripting Reference", juce::NotificationType::dontSendNotification);
    addAndMakeVisible(titleLabel);

    closeButton = std::make_unique<SvgButton>("close", BinaryData::close_svg, juce::Colours::white);
    closeButton->onClick = [this] { dismiss(); };
    addAndMakeVisible(*closeButton);

    viewport.setViewedComponent(&contentComponent, false);
    viewport.setScrollBarsShown(true, false);
    viewport.setColour(juce::ScrollBar::thumbColourId, Colours::grey());
    viewport.setColour(juce::ScrollBar::trackColourId, juce::Colours::transparentBlack);
    addAndMakeVisible(viewport);

    buildDocumentation();
}

void LuaDocumentationComponent::resizeContent(juce::Rectangle<int> contentArea) {
    auto titleArea = contentArea.removeFromTop(32);
    closeButton->setBounds(titleArea.removeFromRight(32).reduced(4));
    titleLabel.setBounds(titleArea);
    contentArea.removeFromTop(4);

    viewport.setBounds(contentArea);
    layoutContent(contentArea.getWidth() - viewport.getScrollBarThickness() - 4);
}

// ── Element builders ──────────────────────────────────────────

void LuaDocumentationComponent::addHeading(const juce::String& text) {
    auto* label = new juce::Label();
    label->setText(text, juce::dontSendNotification);
    label->setFont(juce::Font(juce::FontOptions(18.0f, juce::Font::bold)));
    label->setColour(juce::Label::textColourId, juce::Colours::white);
    label->setJustificationType(juce::Justification::centredLeft);
    label->setInterceptsMouseClicks(false, false);
    elements.add(label);
    contentComponent.addAndMakeVisible(label);
}

void LuaDocumentationComponent::addSubHeading(const juce::String& text) {
    auto* label = new juce::Label();
    label->setText(text, juce::dontSendNotification);
    label->setFont(juce::Font(juce::FontOptions(14.0f, juce::Font::bold)));
    label->setColour(juce::Label::textColourId, juce::Colours::white);
    label->setJustificationType(juce::Justification::centredLeft);
    label->setInterceptsMouseClicks(false, false);
    elements.add(label);
    contentComponent.addAndMakeVisible(label);
}

void LuaDocumentationComponent::addBody(const juce::String& text) {
    auto* label = new juce::Label();
    label->setText(text, juce::dontSendNotification);
    label->setFont(juce::Font(juce::FontOptions(13.5f)));
    label->setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.85f));
    label->setJustificationType(juce::Justification::topLeft);
    label->setInterceptsMouseClicks(false, false);
    label->setMinimumHorizontalScale(1.0f);
    elements.add(label);
    contentComponent.addAndMakeVisible(label);
}

void LuaDocumentationComponent::addCode(const juce::String& code) {
    auto* snippet = new LuaCodeSnippet();
    snippet->setText(code);
    elements.add(snippet);
    contentComponent.addAndMakeVisible(snippet);
}

void LuaDocumentationComponent::addLink(const juce::String& text, const juce::URL& url) {
    auto* link = new juce::HyperlinkButton(text, url);
    link->setFont(juce::Font(juce::FontOptions(13.5f)), false);
    link->setColour(juce::HyperlinkButton::textColourId, Colours::accentColor());
    link->setJustificationType(juce::Justification::centredLeft);
    elements.add(link);
    contentComponent.addAndMakeVisible(link);
}

void LuaDocumentationComponent::addSpacer(int height) {
    auto* spacer = new juce::Component();
    spacer->setSize(1, height);
    elements.add(spacer);
    contentComponent.addAndMakeVisible(spacer);
}

// ── Layout ────────────────────────────────────────────────────

void LuaDocumentationComponent::layoutContent(int width) {
    if (width <= 0) return;

    int y = 0;
    const int pad = 4;

    for (auto* elem : elements) {
        if (auto* snippet = dynamic_cast<LuaCodeSnippet*>(elem)) {
            int h = snippet->getIdealHeight();
            snippet->setBounds(5, y, width - 10, h);
            y += h + pad;
        } else if (auto* label = dynamic_cast<juce::Label*>(elem)) {
            juce::Font font = label->getFont();
            int textWidth = width - 4;
            int numLines = 1;
            if (textWidth > 0) {
                juce::AttributedString as;
                as.append(label->getText(), font);
                as.setWordWrap(juce::AttributedString::WordWrap::byWord);
                juce::TextLayout tl;
                tl.createLayout(as, (float)textWidth);
                numLines = juce::jmax(1, (int)tl.getNumLines());
            }
            int h = (int)(numLines * font.getHeight() + 6);
            label->setBounds(0, y, width, h);
            y += h + pad;
        } else if (auto* link = dynamic_cast<juce::HyperlinkButton*>(elem)) {
            int h = 24;
            link->setBounds(0, y, width, h);
            y += h + pad;
        } else {
            int h = elem->getHeight();
            if (h <= 0) h = 10;
            elem->setBounds(0, y, width, h);
            y += h;
        }
    }

    contentComponent.setSize(width, y + 8);
}

// ── Documentation content ─────────────────────────────────────

void LuaDocumentationComponent::buildDocumentation() {

    // ── Script Structure ──────────────────────────────────────

    addHeading("Script Structure");
    addBody(
        "Your script's top-level code runs once per audio sample. "
        "Return a table to output a point to the oscilloscope.");
    addCode(
        "return { x, y }              -- 2D point\n"
        "return { x, y, z }           -- 3D point (z = depth)\n"
        "return { x, y, z, r, g, b }  -- 3D point with colour");
    addBody("Colour values (r, g, b) range from 0.0 to 1.0.");

    addSpacer(6);
    addSubHeading("One-Time Setup");
    addBody(
        "Globals persist across samples, so you can guard "
        "expensive setup with a nil check to run it only once.");
    addCode(
        "if not counter then\n"
        "    counter = 0\n"
        "end\n"
        "\n"
        "counter = counter + 1\n"
        "return { math.sin(counter * 0.01), 0 }");

    addSpacer(12);

    // ── Global Variables ──────────────────────────────────────

    addHeading("Global Variables");
    addBody("These are set automatically before each sample and are read-only.");

    addSpacer(4);
    addSubHeading("Audio Context");
    addCode(
        "step          -- sample counter (increments each call)\n"
        "phase         -- phase in radians [0, 2*pi)\n"
        "sample_rate   -- audio sample rate (e.g. 192000)\n"
        "frequency     -- current playback frequency in Hz\n"
        "cycle_count   -- number of completed phase cycles");

    addSpacer(4);
    addSubHeading("Sliders");
    addBody(
        "26 user-controlled sliders, each ranging from 0.0 to 1.0. "
        "Adjust them in the Lua Sliders panel.");
    addCode("slider_a, slider_b, ... slider_z");

    addSpacer(4);
    addSubHeading("External Audio Input");
    addCode(
        "ext_x   -- external audio channel 1 sample\n"
        "ext_y   -- external audio channel 2 sample");

    addSpacer(4);
    addSubHeading("MIDI");
    addCode(
        "midi_note     -- MIDI note number (0-127)\n"
        "velocity      -- note velocity (0.0 to 1.0)\n"
        "voice_index   -- polyphonic voice index (0-based)\n"
        "note_on       -- true on the first sample of a note");

    addSpacer(4);
    addSubHeading("DAW Transport");
    addCode(
        "bpm              -- tempo in beats per minute\n"
        "play_time        -- playhead position in seconds\n"
        "play_time_beats  -- playhead position in beats\n"
        "is_playing       -- true if the DAW is playing\n"
        "time_sig_num     -- time signature numerator\n"
        "time_sig_den     -- time signature denominator");

    addSpacer(4);
    addSubHeading("Envelope (DAHDSR)");
    addCode(
        "envelope        -- current envelope value (0.0-1.0)\n"
        "envelope_stage  -- 0=delay 1=attack 2=hold\n"
        "                -- 3=decay 4=sustain 5=release 6=done");

    addSpacer(4);
    addSubHeading("Effect Script Only");
    addBody("Available when using Lua as an audio effect.");
    addCode("x, y, z   -- input point coordinates");

    addSpacer(12);

    // ── Shape / Geometry Functions ────────────────────────────

    addHeading("Shape / Geometry Functions");
    addBody(
        "All shape functions take a phase argument in radians "
        "(typically 0 to 2*pi) and return a point table {x, y} or {x, y, z}.");

    addSpacer(4);
    addCode(
        "osci_line(phase, {x1,y1}, {x2,y2})\n"
        "  -- Line segment between two points\n"
        "\n"
        "osci_rect(phase, width, height)\n"
        "  -- Rectangle outline (default 1 x 1.5)\n"
        "\n"
        "osci_square(phase, size)\n"
        "  -- Square outline (default size 1)\n"
        "\n"
        "osci_circle(phase, radius)\n"
        "  -- Circle (default radius 0.8)\n"
        "\n"
        "osci_ellipse(phase, radiusX, radiusY)\n"
        "  -- Ellipse (defaults 0.6, 0.8)\n"
        "\n"
        "osci_arc(phase, radiusX, radiusY, startAngle, endAngle)\n"
        "  -- Arc segment\n"
        "\n"
        "osci_polygon(phase, numSides)\n"
        "  -- Regular polygon (default 5 sides)\n"
        "\n"
        "osci_bezier(phase, {p1}, {p2}, {p3})\n"
        "osci_bezier(phase, {p1}, {p2}, {p3}, {p4})\n"
        "  -- Quadratic (3 pts) or cubic (4 pts) Bezier\n"
        "\n"
        "osci_lissajous(phase, radius, ratioA, ratioB, theta)\n"
        "  -- Lissajous figure (defaults r=1,a=1,b=5,t=0)");

    addSpacer(12);

    // ── Shape Chaining ────────────────────────────────────────

    addHeading("Shape Chaining");
    addBody(
        "osci_chain evenly distributes the phase across a table "
        "of shape functions, drawing them all in sequence within "
        "a single cycle.");
    addCode(
        "if not shapes then\n"
        "    shapes = {}\n"
        "    shapes[1] = function(p)\n"
        "        return osci_circle(p, 0.5)\n"
        "    end\n"
        "    shapes[2] = function(p)\n"
        "        return osci_square(p, 0.3)\n"
        "    end\n"
        "end\n"
        "\n"
        "return osci_chain(phase, shapes)");

    addSpacer(12);

    // ── Waveform Functions ────────────────────────────────────

    addHeading("Waveform Functions");
    addBody("All waveform functions return a value from 0.0 to 1.0.");
    addCode(
        "osci_square_wave(phase)          -- 1 first half, 0 second\n"
        "osci_saw_wave(phase)             -- ramps 0 to 1\n"
        "osci_triangle_wave(phase)        -- ramps 0 to 1 to 0\n"
        "osci_pulse_wave(phase, duty)     -- configurable duty (default 0.5)");

    addSpacer(12);

    // ── Transform Functions ───────────────────────────────────

    addHeading("Transform Functions");
    addBody("Transform point tables in-place or return new ones.");
    addCode(
        "osci_translate(point, {dx, dy, dz})\n"
        "  -- move a point\n"
        "\n"
        "osci_scale(point, {sx, sy, sz})\n"
        "  -- scale a point\n"
        "\n"
        "osci_rotate(point, rx, ry, rz)\n"
        "  -- 3D rotation (radians per axis)\n"
        "\n"
        "osci_rotate(point, angle)\n"
        "  -- 2D rotation (radians)\n"
        "\n"
        "osci_mix(point1, point2, weight)\n"
        "  -- lerp between two points (0=p1, 1=p2)");

    addSpacer(6);
    addSubHeading("Example");
    addCode(
        "local pt = osci_circle(phase, 0.5)\n"
        "pt = osci_rotate(pt, 0, 0, step * 0.001)\n"
        "pt = osci_translate(pt, {0.3, 0, 0})\n"
        "return pt");

    addSpacer(12);

    // ── Math / DSP Utilities ──────────────────────────────────

    addHeading("Math / DSP Utilities");
    addCode(
        "osci_noise()                         -- random in [-1, 1]\n"
        "osci_lerp(a, b, t)                   -- linear interp\n"
        "osci_smoothstep(edge0, edge1, x)     -- Hermite smoothstep\n"
        "osci_clamp(x, lo, hi)                -- clamp to [lo, hi]\n"
        "osci_map(x, inMin, inMax, outMin, outMax)\n"
        "  -- remap x from one range to another");

    addSpacer(12);

    // ── Vector Math ───────────────────────────────────────────

    addHeading("Vector Math");
    addCode(
        "osci_dot(a, b)        -- dot product (returns number)\n"
        "osci_cross(a, b)      -- cross product (returns {x,y,z})\n"
        "osci_length(v)        -- euclidean length\n"
        "osci_normalize(v)     -- unit vector");

    addSpacer(12);

    // ── Console ───────────────────────────────────────────────

    addHeading("Console");
    addBody(
        "Output text to the Lua console panel below the editor. "
        "Be careful: print() runs every sample and can flood the output.");
    addCode(
        "print(\"hello\")   -- print to console\n"
        "clear()                  -- clear console output");

    addSpacer(12);

    // ── Tips ──────────────────────────────────────────────────

    addHeading("Tips");
    addBody(juce::CharPointer_UTF8(
        "\xe2\x80\xa2 Guard expensive setup with `if not myVar then ... end` so it runs only once."));
    addBody(juce::CharPointer_UTF8(
        "\xe2\x80\xa2 Globals persist between samples automatically."));
    addBody(juce::CharPointer_UTF8(
        "\xe2\x80\xa2 The standard Lua math library is available (math.sin, math.cos, math.pi, etc.)."));
    addBody(juce::CharPointer_UTF8(
        "\xe2\x80\xa2 Use osci_chain() to draw multiple shapes per frame - it divides phase evenly across all shape functions."));
    addBody(juce::CharPointer_UTF8(
        "\xe2\x80\xa2 Draw the same shape multiple times to make it brighter on the oscilloscope."));
    addBody(juce::CharPointer_UTF8(
        "\xe2\x80\xa2 For MIDI instruments, use midi_note and frequency to change pitch, velocity for dynamics, and envelope for amplitude shaping."));

    addSpacer(12);
    addBody("osci-render uses LuaJIT (Lua 5.1). For the full Lua language reference, see:");
    addLink("Lua 5.1 Reference Manual", juce::URL("https://www.lua.org/manual/5.1/"));
}
