#pragma once

#include "MotionIcons.h"
#include "../model/Drawing.h"

// Draw a source by hand in the Scene: Bezier pen, lines, freehand,
// rectangles and ellipses, edited with a select tool. The square is the
// output: what is inside the dashed frame is on screen with the default
// camera, and the Scope shows the drawing live.
class MotionDrawingEditor final : public juce::Component {
public:
    enum class Tool { select, pen, line, freehand, rectangle, ellipse };

    MotionDrawingEditor(motion::drawing::Drawing initial, const juce::String& initialName, bool editing);

    std::function<void(const motion::drawing::Drawing&, const juce::String&)> onDone;
    std::function<void()> onCancel, onChanged;
    const motion::drawing::Drawing& current() const { return drawing; }

    void setTool(Tool value);

    void resized() override;

    void paint(juce::Graphics& g) override;

    void mouseMove(const juce::MouseEvent& event) override;
    void mouseExit(const juce::MouseEvent&) override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;
    void mouseDoubleClick(const juce::MouseEvent& event) override;
    bool keyPressed(const juce::KeyPress& key) override;

private:
    struct Drag {
        enum class Kind { penHandle, closingHandle, line, box, freehand, moveStroke, moveAnchor, moveHandleIn, moveHandleOut } kind = Kind::line;
        bool ellipse = false;
        motion::drawing::Point start, current;
        std::vector<motion::drawing::Point> trail;
        motion::drawing::Stroke original;
        bool moved = false;
    };

    // Screen mapping: the output frame fits the canvas with a margin.
    float scale() const { return std::min(canvas.getWidth(), canvas.getHeight()) / 2.4f; }
    juce::Point<float> toScreen(motion::drawing::Point point) const { return canvas.toFloat().getCentre() + juce::Point<float>(point.x, -point.y) * scale(); }
    motion::drawing::Point toDrawing(juce::Point<float> screen) const;
    juce::AffineTransform screenTransform() const {
        return juce::AffineTransform::scale(scale(), -scale()).translated(canvas.toFloat().getCentre());
    }
    // Snaps to nearby points, and with Shift to 15 degree steps from `from`.
    motion::drawing::Point snapped(juce::Point<float> screen, juce::ModifierKeys mods, std::optional<motion::drawing::Point> from = std::nullopt) const;
    std::optional<motion::drawing::Point> nearestAnchor(juce::Point<float> screen, int except) const;
    bool closesActive(juce::Point<float> screen) const;

    void paintPreview(juce::Graphics& g) const;
    void paintAnchors(juce::Graphics& g, const motion::drawing::Stroke& stroke, bool handles) const;

    // Select tool: handles of the selected stroke, then any point, then a stroke.
    void pickForEditing(const juce::MouseEvent& event, Drag& next);
    void beginEdit(Drag& next, Drag::Kind kind, int anchor);
    void editSelection(const Drag& state, juce::ModifierKeys mods);
    static void toggleSmooth(motion::drawing::Stroke& stroke, std::size_t index);
    void deleteSelection();
    void addStroke(motion::drawing::Stroke stroke);
    // Ends the pen's stroke; a single point is not a shape.
    void finishStroke();
    void deselect();
    void finish();
    void remember();
    void dropHistory() {
        if (!history.empty()) { history.pop_back(); }
    }
    void undo();
    void redo();
    void changed();

    motion::drawing::Drawing drawing;
    std::vector<motion::drawing::Drawing> history, future;
    Tool tool = Tool::pen;
    std::optional<Drag> drag;
    std::optional<motion::drawing::Point> pointer, snapTarget;
    int activeStroke = -1, selectedStroke = -1, selectedAnchor = -1;
    juce::Rectangle<int> canvas;
    using Icon = motion::icons::Icon;
    motion::icons::Button selectTool {"Select tool", Icon::select}, penTool {"Pen tool", Icon::bezier}, lineTool {"Line tool", Icon::line}, freehandTool {"Freehand tool", Icon::pen};
    motion::icons::Button rectangleTool {"Rectangle tool", Icon::rectangle}, ellipseTool {"Ellipse tool", Icon::ellipse};
    motion::icons::Button undoButton {"Undo drawing", Icon::undo}, redoButton {"Redo drawing", Icon::redo}, clearButton {"Clear drawing", Icon::trash};
    motion::icons::ToolStrip tools;
    juce::TextEditor name;
    juce::Rectangle<int> titleArea;
    const juce::String mode;
    juce::TextButton cancelButton, doneButton;
};
