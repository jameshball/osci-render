#pragma once

#include <JuceHeader.h>
#include "EditorTransformFrame.h"
#include "MotionStyle.h"
#include "../model/GeneratorHandles.h"
#include "../model/KeyEdit.h"
#include "../model/PropertySchema.h"

// A selected generator clip's handles in the Scene: where they sit, how its
// own space maps to the world (its transform alone, as the gizmo's handles
// follow, before any effect), and how a drag's values are written, keyed as
// Properties keys them.
namespace motion::ui::generator_handles {
struct Context {
    const GeneratorDefinition* definition = nullptr;
    std::vector<double> values;
    editor::EulerGizmoFrame frame;
    double localTime = 0;

    Vec3 world(Vec3 local) const { return frame.parent.worldOrigin + parentDirection(own().direction(local - frame.evaluatedAnchor)); }
    Vec3 worldDirection(Vec3 local) const { return parentDirection(own().direction(local)); }
    std::optional<Vec3> local(Vec3 world) const {
        const auto delta = frame.parent.positionDelta(world - frame.parent.worldOrigin);
        if (!delta.has_value()) { return std::nullopt; }
        const auto result = own().inverseDirection(*delta) + frame.evaluatedAnchor;
        return result.finite() ? std::optional(result) : std::nullopt;
    }
    std::optional<Vec3> localDirection(Vec3 world) const {
        const auto delta = frame.parent.positionDelta(world);
        if (!delta.has_value()) { return std::nullopt; }
        const auto result = own().inverseDirection(*delta);
        return result.finite() ? std::optional(result) : std::nullopt;
    }

private:
    editor::transform_detail::Affine own() const { return {{}, frame.eulerRadians, frame.evaluatedScale, {}}; }
    Vec3 parentDirection(Vec3 value) const { return frame.parent.parentBasis[0] * value.x + frame.parent.parentBasis[1] * value.y + frame.parent.parentBasis[2] * value.z; }
};

// A generator clip's numbers at a time (keyed values, not what modulation
// adds) and its frame; none for anything else.
template <typename ProjectType>
std::optional<Context> context(const ProjectType& project, Id id, double time) {
    const auto* definition = generatorFor(project, id);
    const auto* clip = findClip(project, id);
    if (definition == nullptr || clip == nullptr) { return std::nullopt; }
    const auto frame = editor::gizmoFrameForClip(project, id, time);
    if (!frame.has_value()) { return std::nullopt; }
    Context result;
    result.definition = definition;
    result.frame = *frame;
    result.localTime = clip->localTime(time, project.tempo());
    for (const auto& spec : definition->parameters) {
        const auto found = clip->properties.find(std::string(spec.id));
        result.values.push_back(found != clip->properties.end() ? found->second.evaluateBase(result.localTime) : spec.defaultValue);
    }
    result.values = generator::settle(*definition, result.values);
    return result;
}

// Writes the numbers that changed, keyed at the clip's time when keyed.
inline bool write(Project& project, Id id, const Context& at, const std::vector<double>& values) {
    bool changed = false;
    for (std::size_t index = 0; index < values.size() && index < at.values.size(); ++index) {
        if (std::abs(values[index] - at.values[index]) <= 1.0e-12) { continue; }
        auto* curve = ensurePropertyCurve(project, id, at.definition->parameters[index].id);
        if (curve == nullptr) { continue; }
        keyedit::setValue(*curve, at.localTime, values[index]);
        changed = true;
    }
    return changed;
}

// What a handle's hint says: what it sets and to what, and the count
// scrolling steps.
inline juce::String hint(const Context& at, const generator::Handle& handle) {
    const auto shown = [&](std::size_t parameter) {
        const auto& spec = at.definition->parameters[parameter];
        return juce::String(at.values[parameter], spec.decimals) + juce::String(spec.unit.data(), spec.unit.size());
    };
    juce::String text(handle.label);
    if (handle.kind == generator::Handle::Kind::count) {
        return text + " " + shown(*handle.count) + motion::style::dot() + "drag sideways or scroll";
    }
    juce::StringArray numbers;
    for (const auto parameter : handle.parameters) { numbers.add(shown(parameter)); }
    text << " " << numbers.joinIntoString(juce::String::fromUTF8(" \xc3\x97 "));
    if (handle.count.has_value()) {
        const auto& spec = at.definition->parameters[*handle.count];
        text << motion::style::dot() << "scroll for " << juce::String(spec.label.data(), spec.label.size()).toLowerCase() << " (" << shown(*handle.count) << ")";
    }
    return text;
}

// A handle as drawn, white ringed in the selection's colour so it stands
// off the lines it sits on: a point a small disc, a count a small rounded
// square. Lit (hovered or dragged), it fills with the colour instead.
inline void paint(juce::Graphics& g, juce::Point<float> at, const generator::Handle& handle, bool lit) {
    const auto colour = motion::style::selection();
    const auto size = lit ? 10.0f : 8.0f;
    const auto bounds = juce::Rectangle<float>(size, size).withCentre(at);
    const auto count = handle.kind == generator::Handle::Kind::count;
    const auto shape = [&](juce::Rectangle<float> area) {
        juce::Path path;
        if (count) {
            path.addRoundedRectangle(area, area.getWidth() * .28f);
        } else {
            path.addEllipse(area);
        }
        return path;
    };
    g.setColour(juce::Colours::black.withAlpha(.5f));
    g.fillPath(shape(bounds.expanded(2.0f)));
    g.setColour(lit ? colour : juce::Colours::white);
    g.fillPath(shape(bounds));
    g.setColour(lit ? juce::Colours::white : colour);
    g.strokePath(shape(bounds.reduced(.75f)), juce::PathStrokeType(1.5f));
}

// The hint beside a handle, in a pill clear of it.
inline void paintHint(juce::Graphics& g, juce::Point<float> at, const juce::String& text, juce::Rectangle<float> within, juce::Rectangle<float> avoid) {
    const auto width = juce::TextLayout::getStringWidth(motion::style::caption(), text) + 16.0f;
    auto pill = juce::Rectangle<float>(width, 22.0f).withPosition(at.x + 12.0f, at.y - 30.0f);
    if (pill.getRight() > within.getRight() - 4) { pill.setX(at.x - 12.0f - width); }
    if (pill.getY() < within.getY() + 4 || pill.intersects(avoid)) { pill.setY(at.y + 10.0f); }
    g.setColour(osci::Colours::surfaceRaised().withAlpha(.94f));
    g.fillRoundedRectangle(pill, 6.0f);
    g.setColour(juce::Colours::white.withAlpha(.08f));
    g.drawRoundedRectangle(pill.reduced(.5f), 6.0f, 1.0f);
    g.setColour(osci::Colours::text());
    g.setFont(motion::style::caption());
    g.drawText(text, pill, juce::Justification::centred, false);
}
}
