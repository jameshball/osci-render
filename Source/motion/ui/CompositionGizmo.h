#pragma once

#include "EditorTransformFrame.h"
#include <JuceHeader.h>
#include "MotionStyle.h"

// The anchor tool moves an object's anchor (the point it rotates and scales
// about) with the move handles, keeping the object where it is.
enum class MotionTransformTool { move, rotate, scale, anchor };

// Stateless, screen-sized handles. Editing and undo remain in the composition view.
struct MotionCompositionGizmo {
    struct Handle {
        std::vector<juce::Point<float>> points;
        motion::Vec3 direction;
    };
    MotionTransformTool tool = MotionTransformTool::move;
    juce::Point<float> origin;
    std::array<Handle, 3> axes;
    bool visible = false;

    template <typename ProjectPoint>
    static MotionCompositionGizmo layout(const motion::editor::EulerGizmoFrame& frame, const motion::editor::Camera& camera, MotionTransformTool tool, double pixelRadius, double viewportHeight, ProjectPoint project) {
        MotionCompositionGizmo result;
        result.tool = tool;
        const auto origin = project(frame.parent.worldOrigin);
        const auto depth = (frame.parent.worldOrigin - camera.position).dot(camera.forward());
        if (!origin.has_value() || depth <= motion::editor::Camera::nearPlane || viewportHeight <= 0) { return result; }
        result.visible = true;
        result.origin = *origin;
        const auto radius = pixelRadius * depth * std::tan(camera.fovDegrees * std::numbers::pi / 360) * 2 / viewportHeight;
        double parentSize = 0;
        for (const auto& basis : frame.parent.parentBasis) { parentSize = std::max(parentSize, basis.length()); }
        if (parentSize <= 0 || !std::isfinite(radius)) { return {}; }
        for (int axis = 0; axis < 3; ++axis) {
            auto& handle = result.axes[axis];
            const auto moves = tool == MotionTransformTool::move || tool == MotionTransformTool::anchor;
            const auto direction = moves ? std::optional(frame.parent.parentBasis[axis]) : frame.axisDirection(axis, false);
            if (!direction.has_value()) { continue; }
            const std::array<double, 3> scale { frame.evaluatedScale.x, frame.evaluatedScale.y, frame.evaluatedScale.z };
            const auto sign = tool == MotionTransformTool::scale && scale[axis] < 0 ? -1.0 : 1.0;
            const auto unit = (*direction * sign).normalized();
            if (!unit.has_value()) { continue; }
            handle.direction = *unit;
            if (tool != MotionTransformTool::rotate) {
                const auto tip = project(frame.parent.worldOrigin + *unit * radius);
                if (tip.has_value() && tip->getDistanceFrom(*origin) >= 18) { handle.points = { *origin, *tip }; }
                continue;
            }
            for (int step = 0; step <= 96; ++step) {
                const auto world = frame.rotationRingPoint(axis, step * 2 * std::numbers::pi / 96, radius / parentSize);
                const auto point = world.has_value() ? project(*world) : std::nullopt;
                if (!point.has_value()) { handle.points.clear(); break; }
                handle.points.push_back(*point);
            }
            // A ring viewed edge-on cannot be manipulated reliably by ray/plane picking.
            double twiceArea = 0;
            for (std::size_t i = 1; i < handle.points.size(); ++i) {
                const auto a = handle.points[i - 1] - *origin, b = handle.points[i] - *origin;
                twiceArea += a.x * b.y - b.x * a.y;
            }
            if (std::abs(twiceArea) < 100) { handle.points.clear(); }
        }
        return result;
    }

    int hitTest(juce::Point<float> point) const {
        if (!visible) { return -1; }
        if (tool != MotionTransformTool::rotate && point.getDistanceFrom(origin) <= 9) { return 3; }
        float nearest = 7;
        int hit = -1;
        for (int axis = 0; axis < 3; ++axis) {
            const auto& points = axes[axis].points;
            if (tool != MotionTransformTool::rotate && point.getDistanceFrom(origin) < 18) { continue; }
            for (std::size_t i = 1; i < points.size(); ++i) {
                juce::Point<float> closest;
                const auto distance = juce::Line<float>(points[i - 1], points[i]).getDistanceFromPoint(point, closest);
                if (distance < nearest) { nearest = distance; hit = axis; }
            }
        }
        return hit;
    }

    void paint(juce::Graphics& g, int hover) const {
        if (!visible) { return; }
        const std::array<juce::Colour, 3> colours {motion::style::axisX(), motion::style::axisY(), motion::style::axisZ()};
        for (int axis = 0; axis < 3; ++axis) {
            const auto& points = axes[axis].points;
            if (points.empty()) { continue; }
            g.setColour(hover == axis ? colours[axis].interpolatedWith(juce::Colours::white, 0.4f) : colours[axis]);
            if (tool == MotionTransformTool::rotate) {
                juce::Path ring;
                ring.startNewSubPath(points.front());
                for (std::size_t index = 1; index < points.size(); ++index) { ring.lineTo(points[index]); }
                g.strokePath(ring, juce::PathStrokeType(hover == axis ? 2.8f : 1.8f));
                const auto offset = points.front() - origin;
                g.setFont(motion::style::body());
                g.drawText(juce::String::charToString("XYZ"[axis]), juce::Rectangle<float>(20, 18).withCentre(points.front() + offset * (14 / std::max(1.0f, offset.getDistanceFromOrigin()))), juce::Justification::centred);
            } else {
                const juce::Line<float> line(origin, points.back());
                if (tool == MotionTransformTool::move || tool == MotionTransformTool::anchor) {
                    g.drawArrow(line, 2.0f, 8.0f, 8.0f);
                } else {
                    g.drawLine(line, 2.0f);
                    g.fillRect(juce::Rectangle<float>(8, 8).withCentre(points.back()));
                }
                g.setFont(motion::style::body());
                g.drawText(juce::String::charToString("XYZ"[axis]), juce::Rectangle<float>(20, 18).withCentre(points.back() + (points.back() - origin) * (14 / points.back().getDistanceFrom(origin))), juce::Justification::centred);
            }
        }
        g.setColour(hover == 3 ? juce::Colours::white : osci::Colours::text());
        if (tool == MotionTransformTool::anchor) {
            paintAnchor(g, 7.0f, 2.0f);
        } else if (tool == MotionTransformTool::rotate) {
            // The point the rings turn about.
            g.setColour(osci::Colours::text().withAlpha(.8f));
            paintAnchor(g, 4.5f, 1.5f);
        } else {
            g.fillRect(juce::Rectangle<float>(8, 8).withCentre(origin));
        }
    }

private:
    // A ring with a crosshair through it, as anchors are drawn elsewhere.
    void paintAnchor(juce::Graphics& g, float radius, float thickness) const {
        g.drawEllipse(juce::Rectangle<float>(radius * 2, radius * 2).withCentre(origin), thickness);
        const auto reach = radius + 4.0f;
        g.drawLine({origin.translated(-reach, 0), origin.translated(reach, 0)}, thickness * .75f);
        g.drawLine({origin.translated(0, -reach), origin.translated(0, reach)}, thickness * .75f);
    }
};
