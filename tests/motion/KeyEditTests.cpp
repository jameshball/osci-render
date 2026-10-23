#include "../../Source/motion/model/KeyEdit.h"
#include <cassert>
#include <iostream>

namespace {
bool close(double a, double b) { return std::abs(a - b) <= 1.0e-10 * std::max({ 1.0, std::abs(a), std::abs(b) }); }

motion::PropertyMap axes() {
    motion::PropertyMap curves;
    curves["position.x"].setKey({ 0, 0 });
    curves["position.x"].setKey({ 1, 10, motion::Interpolation::cubic, 0.5, 2.0, 0.25, 0.75 });
    curves["position.x"].setKey({ 2, 20 });
    curves["position.y"].setKey({ 0.5, 5 });
    curves["position.y"].setKey({ 1.5, 15 });
    return curves;
}

std::vector<double> times(const motion::Curve& curve) {
    std::vector<double> result;
    for (const auto& key : curve.keyframes()) { result.push_back(key.time); }
    return result;
}
}

int main() {
    using motion::keyedit::moveKeys;
    using motion::keyedit::scaleKeyTimes;
    const auto curves = axes();

    // A cross-curve move shifts every selected key by the same time and value delta.
    const std::vector<motion::CurveKey> pair { { "position.x", 1 }, { "position.y", 0.5 } };
    const auto moved = moveKeys(curves, pair, 0.25, 1.0, 0, 3);
    assert(moved.has_value());
    assert((times(moved->curves.at("position.x")) == std::vector<double> { 0, 1.25, 2 }));
    assert((times(moved->curves.at("position.y")) == std::vector<double> { 0.75, 1.5 }));
    assert(close(moved->curves.at("position.x").keyframes()[1].value, 11));
    assert(close(moved->curves.at("position.y").keyframes()[0].value, 6));
    // Interpolation and authored tangents travel with the key.
    const auto& carried = moved->curves.at("position.x").keyframes()[1];
    assert(carried.interpolation == motion::Interpolation::cubic && carried.outgoingSlope == 2.0 && carried.outgoingInfluence == 0.75);
    assert((moved->selection == std::vector<motion::CurveKey> { { "position.x", 1.25 }, { "position.y", 0.75 } }));
    // Unselected keys and curves are untouched.
    assert(close(moved->curves.at("position.y").keyframes()[1].value, 15));

    // Landing on an unselected key of the same curve rejects the whole move...
    assert(!moveKeys(curves, pair, 1.0, 0, 0, 3).has_value());
    // ...but a key of another curve at that time is not a collision.
    assert(moveKeys(curves, { { "position.x", 1 } }, 0.5, 0, 0, 3).has_value());
    // Keys moving together may pass through each other's old times.
    const auto chain = moveKeys(curves, { { "position.x", 0 }, { "position.x", 1 } }, 1.0, 0, 0, 3);
    assert(!chain.has_value());
    const auto train = moveKeys(curves, { { "position.x", 1 }, { "position.x", 2 } }, 0.5, 0, 0, 3);
    assert(train.has_value() && (times(train->curves.at("position.x")) == std::vector<double> { 0, 1.5, 2.5 }));
    // Bounds and constraints.
    assert(!moveKeys(curves, pair, -0.75, 0, 0, 3).has_value());
    assert(!moveKeys(curves, pair, 2.1, 0, 0, 3).has_value());
    const auto clamped = moveKeys(curves, pair, 0, 100, 0, 3, [](const std::string& property, double value) { return property == "position.y" ? std::min(value, 8.0) : value; });
    assert(clamped.has_value() && close(clamped->curves.at("position.y").keyframes()[0].value, 8) && close(clamped->curves.at("position.x").keyframes()[1].value, 110));
    // A key that no longer exists rejects the edit.
    assert(!moveKeys(curves, { { "position.z", 0 } }, 0.1, 0, 0, 3).has_value());
    assert(!moveKeys(curves, { { "position.x", 0.3 } }, 0.1, 0, 0, 3).has_value());

    // Scaling about the left edge stretches times and keeps values.
    const std::vector<motion::CurveKey> span { { "position.x", 1 }, { "position.y", 0.5 }, { "position.y", 1.5 } };
    const auto stretched = scaleKeyTimes(curves, span, 0.5, 1.5, 2.5, 0, 3);
    assert(stretched.has_value());
    assert((times(stretched->curves.at("position.y")) == std::vector<double> { 0.5, 2.5 }));
    assert(close(stretched->curves.at("position.x").keyframes()[1].time, 1.5));
    assert(close(stretched->curves.at("position.x").keyframes()[1].value, 10));
    // Scaling about the right edge compresses towards it.
    const auto squeezed = scaleKeyTimes(curves, span, 1.5, 0.5, 1.0, 0, 3);
    assert(squeezed.has_value() && close(squeezed->curves.at("position.x").keyframes()[1].time, 1.25));
    // Crossing or collapsing onto the pivot is rejected, as is a collision.
    assert(!scaleKeyTimes(curves, span, 0.5, 1.5, 0.5, 0, 3).has_value());
    assert(!scaleKeyTimes(curves, span, 0.5, 1.5, 0.25, 0, 3).has_value());
    assert(!scaleKeyTimes(curves, span, 0.5, 1.5, 3.5, 0, 3).has_value());
    assert(!scaleKeyTimes(curves, { { "position.x", 1 }, { "position.y", 0.5 } }, 0.5, 1, 2, 0, 3).has_value());
    // Switching a smooth key to Bezier starts its handles on the automatic
    // tangents with default influence; a second switch changes nothing.
    auto curve = curves.at("position.y");
    assert(motion::keyedit::setInterpolation(curve, 0.5, motion::Interpolation::cubic));
    const auto& bezier = curve.keyframes()[0];
    assert(bezier.interpolation == motion::Interpolation::cubic && bezier.outgoingInfluence == motion::Keyframe::defaultInfluence);
    assert(close(bezier.outgoingSlope, curves.at("position.y").automaticSlope(0)));
    assert(!motion::keyedit::setInterpolation(curve, 0.5, motion::Interpolation::cubic));
    assert(!motion::keyedit::setInterpolation(curve, 0.75, motion::Interpolation::linear));

    // A straight segment stays straight as a Bezier: both handles take its slope.
    motion::Curve line;
    line.setKey({0, 0, motion::Interpolation::linear});
    line.setKey({1, 2, motion::Interpolation::linear});
    line.setKey({2, 0, motion::Interpolation::linear});
    assert(motion::keyedit::setInterpolation(line, 0, motion::Interpolation::cubic));
    for (const auto time : {0.25, 0.5, 0.75}) { assert(close(line.evaluate(time), 2 * time)); }
    std::cout << "KeyEditTests passed\n";
    return 0;
}
