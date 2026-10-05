#pragma once

#include <JuceHeader.h>
#include <unordered_map>

namespace motion {
// Collects line segments and strokes them with one draw call per colour,
// instead of one per segment: a renderer's per-call state work dominates
// when thousands of short segments are drawn. Opacity is rounded to 1/64,
// which is invisible but keeps the number of batches small.
class LineBatch {
public:
    void add(juce::Line<float> line, juce::Colour colour) {
        const auto alpha = static_cast<juce::uint8>(juce::roundToInt(colour.getFloatAlpha() * 63.0f) * 255 / 63);
        auto& path = paths[colour.withAlpha(alpha).getARGB()];
        path.startNewSubPath(line.getStart());
        path.lineTo(line.getEnd());
    }

    // Strokes and empties the batch. A colour's path keeps its storage while
    // that colour is still drawn, so steady redraws do not reallocate.
    void stroke(juce::Graphics& g, float thickness) {
        const juce::PathStrokeType stroke(thickness);
        for (auto entry = paths.begin(); entry != paths.end();) {
            if (entry->second.isEmpty()) {
                entry = paths.erase(entry);
                continue;
            }
            g.setColour(juce::Colour(entry->first));
            g.strokePath(entry->second, stroke);
            entry->second.clear();
            ++entry;
        }
    }

private:
    std::unordered_map<juce::uint32, juce::Path> paths;
};
}
