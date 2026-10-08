#include "DrawingEditor.h"

MotionDrawingEditor::MotionDrawingEditor(motion::drawing::Drawing initial, const juce::String& initialName, bool editing) : drawing(std::move(initial)), mode(editing ? "Editing" : "New drawing") {
    setName("Drawing editor");
    setWantsKeyboardFocus(true);
    for (auto [button, value, tip] : {std::tuple {&selectTool, Tool::select, "Select (V): drag shapes, points and handles; double-click a point for smooth or sharp"},
                                      std::tuple {&penTool, Tool::pen, "Pen (P): click for corners, drag for curves, click the first point to close"},
                                      std::tuple {&lineTool, Tool::line, "Line (L): Shift for 15\xc2\xb0 steps"}, std::tuple {&freehandTool, Tool::freehand, "Freehand (F)"},
                                      std::tuple {&rectangleTool, Tool::rectangle, "Rectangle (R): Shift for a square, Alt from the centre"}, std::tuple {&ellipseTool, Tool::ellipse, "Ellipse (E): Shift for a circle, Alt from the centre"}}) {
        button->setTooltip(juce::String::fromUTF8(tip));
        button->onClick = [this, value] { setTool(value); };
    }
    undoButton.setTooltip("Undo (Cmd+Z)");
    redoButton.setTooltip("Redo (Shift+Cmd+Z)");
    clearButton.setTooltip("Clear the drawing");
    undoButton.onClick = [this] { undo(); };
    redoButton.onClick = [this] { redo(); };
    clearButton.onClick = [this] {
        if (drawing.strokes.empty() || drag.has_value()) { return; }
        remember();
        activeStroke = -1;
        drawing.strokes.clear();
        deselect();
        changed();
    };
    tools.setGroups({{&selectTool, &penTool, &lineTool, &freehandTool, &rectangleTool, &ellipseTool}, {&undoButton, &redoButton, &clearButton}});
    addAndMakeVisible(tools);
    // The name sits in the panel's header like a title, editable in place.
    name.setText(initialName, juce::dontSendNotification);
    name.setName("Drawing name");
    name.setTitle("Drawing name");
    name.setFont(motion::style::title());
    name.applyFontToAllText(motion::style::title());
    name.setJustification(juce::Justification::centredLeft);
    name.setIndents(6, 0);
    name.setColour(juce::TextEditor::backgroundColourId, juce::Colours::transparentBlack);
    name.setColour(juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    name.setColour(juce::TextEditor::focusedOutlineColourId, motion::style::accent().withAlpha(.6f));
    name.setTooltip("Name");
    addAndMakeVisible(name);
    cancelButton.setButtonText("Cancel");
    cancelButton.onClick = [this] { if (onCancel) { onCancel(); } };
    doneButton.setButtonText(editing ? "Apply" : "Add");
    doneButton.setTooltip(editing ? "Apply the drawing (Cmd+Return)" : "Add the drawing to the project (Cmd+Return)");
    motion::style::makePrimary(doneButton);
    doneButton.onClick = [this] { finish(); };
    for (auto* button : {&cancelButton, &doneButton}) { addAndMakeVisible(button); }
    setTool(drawing.strokes.empty() ? Tool::pen : Tool::select);
    changed();
}

void MotionDrawingEditor::setTool(Tool value) {
    finishStroke();
    tool = value;
    selectTool.setToggleState(tool == Tool::select, juce::dontSendNotification);
    penTool.setToggleState(tool == Tool::pen, juce::dontSendNotification);
    lineTool.setToggleState(tool == Tool::line, juce::dontSendNotification);
    freehandTool.setToggleState(tool == Tool::freehand, juce::dontSendNotification);
    rectangleTool.setToggleState(tool == Tool::rectangle, juce::dontSendNotification);
    ellipseTool.setToggleState(tool == Tool::ellipse, juce::dontSendNotification);
    setMouseCursor(tool == Tool::select ? juce::MouseCursor::NormalCursor : juce::MouseCursor::CrosshairCursor);
    repaint();
}

void MotionDrawingEditor::resized() {
    titleArea = motion::style::sceneEditor::layoutHeader(getLocalBounds(), doneButton, cancelButton);
    // The name is editable in place, after the mode.
    const auto field = motion::style::sceneEditor::nameArea(titleArea, mode).translated(-6, 0);
    name.setBounds(field.withWidth(std::min(field.getWidth(), 220)));
    canvas = motion::style::sceneEditor::page(getLocalBounds());
    tools.setBounds(canvas.getX() + 8, canvas.getY() + 8, tools.preferredWidth(), tools.preferredHeight());
}

void MotionDrawingEditor::paint(juce::Graphics& g) {
    motion::style::sceneEditor::paint(g, getLocalBounds(), titleArea, {}, mode);
    juce::Graphics::ScopedSaveState state(g);
    g.reduceClipRegion(canvas);
    // A quarter-unit grid across the whole canvas, the axes and the output frame.
    const auto low = toDrawing(canvas.getBottomLeft().toFloat()), high = toDrawing(canvas.getTopRight().toFloat());
    for (int line = static_cast<int>(std::floor(low.x * 4)); line <= static_cast<int>(std::ceil(high.x * 4)); ++line) {
        g.setColour(juce::Colours::white.withAlpha(line == 0 ? .12f : .04f));
        g.drawLine(juce::Line<float>(toScreen({line * .25f, low.y}), toScreen({line * .25f, high.y})), 1.0f);
    }
    for (int line = static_cast<int>(std::floor(low.y * 4)); line <= static_cast<int>(std::ceil(high.y * 4)); ++line) {
        g.setColour(juce::Colours::white.withAlpha(line == 0 ? .12f : .04f));
        g.drawLine(juce::Line<float>(toScreen({low.x, line * .25f}), toScreen({high.x, line * .25f})), 1.0f);
    }
    const auto frame = juce::Rectangle<float>(toScreen({-1, 1}), toScreen({1, -1}));
    const float dashes[] {5.0f, 4.0f};
    g.setColour(juce::Colours::white.withAlpha(.3f));
    for (const auto& edge : {juce::Line<float>(frame.getTopLeft(), frame.getTopRight()), juce::Line<float>(frame.getTopRight(), frame.getBottomRight()), juce::Line<float>(frame.getBottomRight(), frame.getBottomLeft()), juce::Line<float>(frame.getBottomLeft(), frame.getTopLeft())}) {
        g.drawDashedLine(edge, dashes, 2, 1.0f);
    }
    // Strokes glow like the beam.
    const auto transform = screenTransform();
    for (std::size_t index = 0; index < drawing.strokes.size(); ++index) {
        const auto path = motion::drawing::toPath(drawing.strokes[index]);
        const auto selected = static_cast<int>(index) == selectedStroke;
        g.setColour(motion::style::key().withAlpha(.14f));
        g.strokePath(path, juce::PathStrokeType(5.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded), transform);
        g.setColour(selected ? motion::style::selection() : motion::style::key());
        g.strokePath(path, juce::PathStrokeType(selected ? 2.0f : 1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded), transform);
    }
    paintPreview(g);
    if (selectedStroke >= 0 && selectedStroke < static_cast<int>(drawing.strokes.size())) { paintAnchors(g, drawing.strokes[static_cast<std::size_t>(selectedStroke)], true); }
    if (activeStroke >= 0 && activeStroke < static_cast<int>(drawing.strokes.size())) { paintAnchors(g, drawing.strokes[static_cast<std::size_t>(activeStroke)], false); }
    if (snapTarget.has_value()) {
        g.setColour(osci::Colours::accentColor().brighter(.4f));
        g.drawEllipse(juce::Rectangle<float>(14, 14).withCentre(toScreen(*snapTarget)), 1.5f);
    }
}

void MotionDrawingEditor::mouseMove(const juce::MouseEvent& event) {
    pointer = toDrawing(event.position);
    snapTarget = tool != Tool::select && tool != Tool::freehand ? nearestAnchor(event.position, activeStroke) : std::nullopt;
    if (tool == Tool::pen && activeStroke >= 0 && closesActive(event.position)) { snapTarget = drawing.strokes[static_cast<std::size_t>(activeStroke)].anchors.front().point; }
    repaint();
}

void MotionDrawingEditor::mouseExit(const juce::MouseEvent&) {
    pointer.reset();
    snapTarget.reset();
    repaint();
}

void MotionDrawingEditor::mouseDown(const juce::MouseEvent& event) {
    grabKeyboardFocus();
    if (!canvas.contains(event.position.toInt())) { return; }
    auto at = snapped(event.position, event.mods);
    Drag next;
    next.start = next.current = at;
    if (tool == Tool::select) {
        pickForEditing(event, next);
        return;
    } else if (tool == Tool::pen) {
        if (activeStroke >= 0 && closesActive(event.position)) {
            remember();
            drawing.strokes[static_cast<std::size_t>(activeStroke)].closed = true;
            next.kind = Drag::Kind::closingHandle;
            drag = next;
            return;
        }
        remember();
        if (activeStroke < 0) {
            drawing.strokes.push_back({});
            activeStroke = static_cast<int>(drawing.strokes.size()) - 1;
            selectedStroke = -1;
        }
        drawing.strokes[static_cast<std::size_t>(activeStroke)].anchors.push_back(motion::drawing::Anchor::corner(at));
        next.kind = Drag::Kind::penHandle;
    } else if (tool == Tool::freehand) {
        remember();
        next.kind = Drag::Kind::freehand;
        next.trail = {at};
    } else {
        remember();
        next.kind = tool == Tool::line ? Drag::Kind::line : Drag::Kind::box;
        next.ellipse = tool == Tool::ellipse;
    }
    drag = next;
    changed();
}

void MotionDrawingEditor::mouseDrag(const juce::MouseEvent& event) {
    if (!drag.has_value()) { return; }
    auto& state = *drag;
    const auto raw = toDrawing(event.position);
    state.current = state.kind == Drag::Kind::freehand ? raw : snapped(event.position, event.mods, state.start);
    state.moved = state.moved || event.getDistanceFromDragStart() > 2;
    pointer = state.current;
    using Kind = Drag::Kind;
    if (state.kind == Kind::freehand) {
        if (state.trail.empty() || state.trail.back().getDistanceFrom(raw) * scale() > 1.5f) { state.trail.push_back(raw); }
    } else if ((state.kind == Kind::penHandle || state.kind == Kind::closingHandle) && (activeStroke < 0 || activeStroke >= static_cast<int>(drawing.strokes.size()))) {
        return;
    } else if (state.kind == Kind::penHandle && state.moved) {
        auto& anchor = drawing.strokes[static_cast<std::size_t>(activeStroke)].anchors.back();
        anchor.out = raw;
        anchor.in = anchor.point * 2.0f - raw;
        anchor.smooth = true;
    } else if (state.kind == Kind::closingHandle && state.moved) {
        auto& anchor = drawing.strokes[static_cast<std::size_t>(activeStroke)].anchors.front();
        anchor.in = anchor.point * 2.0f - raw;
        anchor.out = raw;
        anchor.smooth = true;
    } else if (state.kind == Kind::moveStroke || state.kind == Kind::moveAnchor || state.kind == Kind::moveHandleIn || state.kind == Kind::moveHandleOut) {
        editSelection(state, event.mods);
    }
    snapTarget = state.kind == Kind::line || state.kind == Kind::moveAnchor ? nearestAnchor(event.position, state.kind == Kind::moveAnchor ? selectedStroke : -1) : std::nullopt;
    changed();
}

void MotionDrawingEditor::mouseUp(const juce::MouseEvent& event) {
    if (!drag.has_value()) { return; }
    auto state = std::move(*drag);
    drag.reset();
    using Kind = Drag::Kind;
    const auto shift = event.mods.isShiftDown(), centre = event.mods.isAltDown();
    if (state.kind == Kind::closingHandle) {
        finishStroke();
    } else if (state.kind == Kind::line) {
        if (state.start.getDistanceFrom(state.current) * scale() > 3) {
            motion::drawing::Stroke stroke;
            stroke.anchors = {motion::drawing::Anchor::corner(state.start), motion::drawing::Anchor::corner(state.current)};
            addStroke(std::move(stroke));
        } else {
            dropHistory();
        }
    } else if (state.kind == Kind::box) {
        auto a = state.start, b = state.current;
        if (shift) {
            const auto size = std::max(std::abs(b.x - a.x), std::abs(b.y - a.y));
            b = {a.x + std::copysign(size, b.x - a.x), a.y + std::copysign(size, b.y - a.y)};
        }
        if (centre) { a = state.start * 2.0f - b; }
        if (std::abs(b.x - a.x) * scale() > 3 && std::abs(b.y - a.y) * scale() > 3) {
            addStroke(!state.ellipse ? motion::drawing::rectangle(a, b) : motion::drawing::ellipse((a + b) * .5f, {std::abs(b.x - a.x) * .5f, std::abs(b.y - a.y) * .5f}));
        } else {
            dropHistory();
        }
    } else if (state.kind == Kind::freehand) {
        if (state.trail.size() > 2) {
            const auto closed = state.trail.size() > 8 && state.trail.front().getDistanceFrom(state.trail.back()) * scale() < 10;
            if (closed) { state.trail.push_back(state.trail.front()); }
            addStroke(motion::drawing::fit(state.trail, 2.0f / scale(), closed));
        } else {
            dropHistory();
        }
    } else if (!state.moved && (state.kind == Kind::moveStroke || state.kind == Kind::moveAnchor || state.kind == Kind::moveHandleIn || state.kind == Kind::moveHandleOut)) {
        dropHistory();
    }
    snapTarget.reset();
    changed();
}

void MotionDrawingEditor::mouseDoubleClick(const juce::MouseEvent& event) {
    if (tool == Tool::pen && activeStroke >= 0) {
        // The double-click's second press added a point on top of the first.
        auto& anchors = drawing.strokes[static_cast<std::size_t>(activeStroke)].anchors;
        if (anchors.size() > 1 && anchors.back().point.getDistanceFrom(anchors[anchors.size() - 2].point) * scale() < 4) { anchors.pop_back(); }
        finishStroke();
        changed();
        return;
    }
    if (tool == Tool::select && selectedStroke >= 0 && selectedAnchor >= 0) {
        remember();
        toggleSmooth(drawing.strokes[static_cast<std::size_t>(selectedStroke)], static_cast<std::size_t>(selectedAnchor));
        changed();
    }
    juce::ignoreUnused(event);
}

bool MotionDrawingEditor::keyPressed(const juce::KeyPress& key) {
    // Edits wait until the mouse is released.
    if (drag.has_value()) { return !name.hasKeyboardFocus(false); }
    const auto command = key.getModifiers().isCommandDown();
    if (command && key.getKeyCode() == 'Z') {
        if (key.getModifiers().isShiftDown()) { redo(); } else { undo(); }
        return true;
    }
    // Other shortcuts would act on the timeline behind the drawing.
    if (command) { return key.getKeyCode() != 'S'; }
    if (key == juce::KeyPress::returnKey) { finishStroke(); changed(); return true; }
    if (key == juce::KeyPress::escapeKey) {
        if (activeStroke >= 0) { finishStroke(); } else { deselect(); }
        changed();
        return true;
    }
    if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey) {
        deleteSelection();
        return true;
    }
    // Single-letter tool keys, as in drawing applications.
    static const std::map<juce::juce_wchar, Tool> keys {{'v', Tool::select}, {'p', Tool::pen}, {'l', Tool::line}, {'f', Tool::freehand}, {'r', Tool::rectangle}, {'e', Tool::ellipse}};
    const auto found = keys.find(juce::CharacterFunctions::toLowerCase(key.getTextCharacter()));
    if (found != keys.end() && !name.hasKeyboardFocus(false)) {
        setTool(found->second);
        return true;
    }
    return false;
}

motion::drawing::Point MotionDrawingEditor::toDrawing(juce::Point<float> screen) const {
    const auto offset = (screen - canvas.toFloat().getCentre()) / scale();
    return {offset.x, -offset.y};
}

motion::drawing::Point MotionDrawingEditor::snapped(juce::Point<float> screen, juce::ModifierKeys mods, std::optional<motion::drawing::Point> from) const {
    auto point = toDrawing(screen);
    if (tool == Tool::freehand || tool == Tool::select) { return point; }
    const auto anchor = nearestAnchor(screen, -1);
    if (anchor.has_value()) { return *anchor; }
    const auto origin = from.has_value() ? from : (activeStroke >= 0 && !drawing.strokes[static_cast<std::size_t>(activeStroke)].anchors.empty() ? std::optional(drawing.strokes[static_cast<std::size_t>(activeStroke)].anchors.back().point) : std::nullopt);
    if (mods.isShiftDown() && origin.has_value() && (tool == Tool::line || tool == Tool::pen)) {
        const auto delta = point - *origin;
        const auto step = juce::MathConstants<float>::pi / 12;
        const auto angle = std::round(std::atan2(delta.y, delta.x) / step) * step;
        const auto length = delta.getDistanceFromOrigin();
        point = *origin + motion::drawing::Point(std::cos(angle), std::sin(angle)) * length;
    }
    return point;
}

std::optional<motion::drawing::Point> MotionDrawingEditor::nearestAnchor(juce::Point<float> screen, int except) const {
    std::optional<motion::drawing::Point> best;
    float distance = 8;
    for (std::size_t index = 0; index < drawing.strokes.size(); ++index) {
        if (static_cast<int>(index) == except) { continue; }
        for (const auto& anchor : drawing.strokes[index].anchors) {
            const auto d = toScreen(anchor.point).getDistanceFrom(screen);
            if (d < distance) {
                distance = d;
                best = anchor.point;
            }
        }
    }
    return best;
}

bool MotionDrawingEditor::closesActive(juce::Point<float> screen) const {
    if (activeStroke < 0) { return false; }
    const auto& anchors = drawing.strokes[static_cast<std::size_t>(activeStroke)].anchors;
    return anchors.size() > 2 && toScreen(anchors.front().point).getDistanceFrom(screen) < 9;
}

void MotionDrawingEditor::paintPreview(juce::Graphics& g) const {
    if (drag.has_value()) {
        const auto& state = *drag;
        juce::Path path;
        if (state.kind == Drag::Kind::line) {
            path.startNewSubPath(state.start);
            path.lineTo(state.current);
        } else if (state.kind == Drag::Kind::box) {
            auto a = state.start, b = state.current;
            const auto mods = juce::ModifierKeys::getCurrentModifiers();
            if (mods.isShiftDown()) {
                const auto size = std::max(std::abs(b.x - a.x), std::abs(b.y - a.y));
                b = {a.x + std::copysign(size, b.x - a.x), a.y + std::copysign(size, b.y - a.y)};
            }
            if (mods.isAltDown()) { a = state.start * 2.0f - b; }
            path = motion::drawing::toPath(!state.ellipse ? motion::drawing::rectangle(a, b) : motion::drawing::ellipse((a + b) * .5f, {std::abs(b.x - a.x) * .5f, std::abs(b.y - a.y) * .5f}));
        } else if (state.kind == Drag::Kind::freehand && state.trail.size() > 1) {
            path.startNewSubPath(state.trail.front());
            for (const auto& point : state.trail) { path.lineTo(point); }
        }
        g.setColour(motion::style::key().withAlpha(.9f));
        g.strokePath(path, juce::PathStrokeType(1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded), screenTransform());
    }
    // The pen's next segment follows the pointer.
    if (tool == Tool::pen && activeStroke >= 0 && pointer.has_value() && !drag.has_value()) {
        const auto& last = drawing.strokes[static_cast<std::size_t>(activeStroke)].anchors.back();
        const auto target = snapTarget.value_or(*pointer);
        juce::Path path;
        path.startNewSubPath(last.point);
        path.cubicTo(last.out, target, target);
        const float dashes[] {4.0f, 3.0f};
        juce::Path dashed;
        juce::PathStrokeType(1.2f).createDashedStroke(dashed, path, dashes, 2, screenTransform());
        g.setColour(motion::style::key().withAlpha(.6f));
        g.fillPath(dashed);
    }
}

void MotionDrawingEditor::paintAnchors(juce::Graphics& g, const motion::drawing::Stroke& stroke, bool handles) const {
    for (std::size_t index = 0; index < stroke.anchors.size(); ++index) {
        const auto& anchor = stroke.anchors[index];
        const auto centre = toScreen(anchor.point);
        const auto selected = handles && static_cast<int>(index) == selectedAnchor;
        if (handles || static_cast<int>(index) == static_cast<int>(stroke.anchors.size()) - 1) {
            for (const auto handle : {anchor.in, anchor.out}) {
                if (handle == anchor.point) { continue; }
                g.setColour(juce::Colours::white.withAlpha(.45f));
                g.drawLine(juce::Line<float>(centre, toScreen(handle)), 1.0f);
                g.setColour(juce::Colours::white);
                g.fillEllipse(juce::Rectangle<float>(6, 6).withCentre(toScreen(handle)));
            }
        }
        const auto box = juce::Rectangle<float>(8, 8).withCentre(centre);
        g.setColour(selected ? osci::Colours::accentColor().brighter(.4f) : juce::Colours::black);
        if (anchor.smooth) { g.fillEllipse(box); } else { g.fillRect(box); }
        g.setColour(juce::Colours::white);
        if (anchor.smooth) { g.drawEllipse(box, 1.2f); } else { g.drawRect(box, 1.2f); }
    }
}

void MotionDrawingEditor::pickForEditing(const juce::MouseEvent& event, Drag& next) {
    const auto screen = event.position;
    using Kind = Drag::Kind;
    if (selectedStroke >= 0) {
        const auto& stroke = drawing.strokes[static_cast<std::size_t>(selectedStroke)];
        for (std::size_t index = 0; index < stroke.anchors.size(); ++index) {
            const auto& anchor = stroke.anchors[index];
            for (const auto [handle, kind] : {std::pair {anchor.in, Kind::moveHandleIn}, std::pair {anchor.out, Kind::moveHandleOut}}) {
                if (handle != anchor.point && toScreen(handle).getDistanceFrom(screen) < 7) {
                    beginEdit(next, kind, static_cast<int>(index));
                    return;
                }
            }
        }
    }
    for (std::size_t stroke = 0; stroke < drawing.strokes.size(); ++stroke) {
        const auto& anchors = drawing.strokes[stroke].anchors;
        for (std::size_t index = 0; index < anchors.size(); ++index) {
            if (toScreen(anchors[index].point).getDistanceFrom(screen) < 7) {
                selectedStroke = static_cast<int>(stroke);
                beginEdit(next, Kind::moveAnchor, static_cast<int>(index));
                return;
            }
        }
    }
    const auto drawn = toDrawing(screen);
    for (int stroke = static_cast<int>(drawing.strokes.size()) - 1; stroke >= 0; --stroke) {
        const auto path = motion::drawing::toPath(drawing.strokes[static_cast<std::size_t>(stroke)]);
        juce::Point<float> nearest;
        path.getNearestPoint(drawn, nearest, {}, .5f / scale());
        if (nearest.getDistanceFrom(drawn) * scale() < 6) {
            selectedStroke = stroke;
            beginEdit(next, Kind::moveStroke, -1);
            return;
        }
    }
    deselect();
    repaint();
}

void MotionDrawingEditor::beginEdit(Drag& next, Drag::Kind kind, int anchor) {
    remember();
    selectedAnchor = anchor;
    next.kind = kind;
    next.original = drawing.strokes[static_cast<std::size_t>(selectedStroke)];
    drag = next;
}

void MotionDrawingEditor::editSelection(const Drag& state, juce::ModifierKeys mods) {
    if (selectedStroke < 0) { return; }
    auto& stroke = drawing.strokes[static_cast<std::size_t>(selectedStroke)];
    auto delta = state.current - state.start;
    using Kind = Drag::Kind;
    if (state.kind == Kind::moveStroke) {
        stroke = state.original;
        for (auto& anchor : stroke.anchors) {
            anchor.point += delta;
            anchor.in += delta;
            anchor.out += delta;
        }
        return;
    }
    const auto index = static_cast<std::size_t>(selectedAnchor);
    const auto& before = state.original.anchors[index];
    auto& anchor = stroke.anchors[index];
    if (state.kind == Kind::moveAnchor) {
        const auto snap = nearestAnchor(toScreen(before.point + delta), selectedStroke);
        if (snap.has_value()) { delta = *snap - before.point; }
        anchor.point = before.point + delta;
        anchor.in = before.in + delta;
        anchor.out = before.out + delta;
        return;
    }
    // Smooth points keep their handles in line; Alt breaks them apart.
    const auto in = state.kind == Kind::moveHandleIn;
    auto& moved = in ? anchor.in : anchor.out;
    auto& opposite = in ? anchor.out : anchor.in;
    moved = (in ? before.in : before.out) + delta;
    if (mods.isAltDown()) { anchor.smooth = false; }
    if (anchor.smooth) {
        const auto length = (in ? before.out : before.in).getDistanceFrom(before.point);
        const auto direction = anchor.point - moved;
        const auto span = direction.getDistanceFromOrigin();
        opposite = span > 0 ? anchor.point + direction / span * length : anchor.point;
    }
}

void MotionDrawingEditor::toggleSmooth(motion::drawing::Stroke& stroke, std::size_t index) {
    auto& anchor = stroke.anchors[index];
    anchor.smooth = !anchor.smooth;
    if (!anchor.smooth) {
        anchor.in = anchor.out = anchor.point;
        return;
    }
    const auto count = stroke.anchors.size();
    const auto previous = index > 0 ? stroke.anchors[index - 1].point : (stroke.closed ? stroke.anchors[count - 1].point : anchor.point);
    const auto next = index + 1 < count ? stroke.anchors[index + 1].point : (stroke.closed ? stroke.anchors[0].point : anchor.point);
    const auto tangent = (next - previous) / 6.0f;
    anchor.in = anchor.point - tangent;
    anchor.out = anchor.point + tangent;
}

void MotionDrawingEditor::deleteSelection() {
    if (activeStroke >= 0) {
        remember();
        auto& anchors = drawing.strokes[static_cast<std::size_t>(activeStroke)].anchors;
        if (!anchors.empty()) { anchors.pop_back(); }
        if (anchors.empty()) {
            drawing.strokes.erase(drawing.strokes.begin() + activeStroke);
            activeStroke = -1;
        }
        changed();
        return;
    }
    if (selectedStroke < 0) { return; }
    remember();
    auto& stroke = drawing.strokes[static_cast<std::size_t>(selectedStroke)];
    if (selectedAnchor >= 0 && stroke.anchors.size() > 2) {
        stroke.anchors.erase(stroke.anchors.begin() + selectedAnchor);
        selectedAnchor = -1;
    } else {
        drawing.strokes.erase(drawing.strokes.begin() + selectedStroke);
        deselect();
    }
    changed();
}

void MotionDrawingEditor::addStroke(motion::drawing::Stroke stroke) {
    drawing.strokes.push_back(std::move(stroke));
    selectedStroke = -1;
}

void MotionDrawingEditor::finishStroke() {
    if (activeStroke < 0) { return; }
    if (drawing.strokes[static_cast<std::size_t>(activeStroke)].anchors.size() < 2) { drawing.strokes.erase(drawing.strokes.begin() + activeStroke); }
    activeStroke = -1;
    repaint();
}

void MotionDrawingEditor::deselect() {
    selectedStroke = -1;
    selectedAnchor = -1;
}

void MotionDrawingEditor::finish() {
    finishStroke();
    if (drawing.empty()) { return; }
    auto text = name.getText().trim();
    if (text.isEmpty()) { text = "Drawing"; }
    if (onDone) { onDone(drawing, text); }
}

void MotionDrawingEditor::remember() {
    history.push_back(drawing);
    if (history.size() > 200) { history.erase(history.begin()); }
    future.clear();
}

void MotionDrawingEditor::undo() {
    finishStroke();
    if (history.empty()) { return; }
    future.push_back(std::exchange(drawing, history.back()));
    history.pop_back();
    deselect();
    changed();
}

void MotionDrawingEditor::redo() {
    if (future.empty()) { return; }
    history.push_back(std::exchange(drawing, future.back()));
    future.pop_back();
    deselect();
    changed();
}

void MotionDrawingEditor::changed() {
    if (selectedStroke >= static_cast<int>(drawing.strokes.size())) { deselect(); }
    undoButton.setEnabled(!history.empty());
    redoButton.setEnabled(!future.empty());
    clearButton.setEnabled(!drawing.strokes.empty());
    doneButton.setEnabled(!drawing.empty());
    if (onChanged) { onChanged(); }
    repaint();
}
