#pragma once

#include <JuceHeader.h>
#include <vector>
#include <memory>
#include <string>
#include "../../../modules/osci_render_core/shape/osci_Shape.h"
#include "../../../modules/osci_render_core/shape/osci_Point.h"

namespace osci { class Line; }

struct FractalRule {
    juce::String variable;
    juce::String replacement;
};

// Compact turtle command
enum class FractalCmd : uint8_t {
    Draw,      // Move forward and draw line
    Move,      // Move forward without drawing
    TurnRight, // +
    TurnLeft,  // -
    Push,      // [
    Pop        // ]
};

// A single shape representing an entire fractal turtle path.
// All draw positions are precomputed at construction time, so
// nextVector() is an O(1) table lookup with no trig.
class FractalPath : public osci::Shape {
public:
    struct DrawSegment {
        float x, y;   // normalized start position
        float dx, dy;  // normalized direction vector
    };

    FractalPath(std::shared_ptr<const std::vector<DrawSegment>> segments);

    osci::Point nextVector(float drawingProgress) override;
    float length() override;
    void scale(float x, float y, float z) override {}
    void translate(float x, float y, float z) override {}
    std::unique_ptr<Shape> clone() override;
    std::string type() override { return "FractalPath"; }

private:
    std::shared_ptr<const std::vector<DrawSegment>> segments;
};

class FractalParser {
public:
    FractalParser(const juce::String& jsonContent);

    std::vector<std::unique_ptr<osci::Shape>> draw();

    void setIterations(int iterations);

    // Returns true (and atomically clears) if parameters have changed
    // since the last call.  Drives the queue-flush path in FrameProducer.
    bool consumeDirty();

    juce::String getAxiom() const { return axiom; }
    float getBaseAngle() const { return baseAngleDegrees; }
    const std::vector<FractalRule>& getRules() const { return rules; }

    void parse(const juce::String& jsonContent);
    juce::String toJson() const;
    static juce::String defaultJson();

private:
    juce::String axiom;
    float baseAngleDegrees = 60.0f;
    std::vector<FractalRule> rules;

    std::atomic<int> currentIterations{3};
    std::atomic<bool> frameDirty{false};

    struct NormParams { float cx, cy, scale; };

    // Per-level cached data
    struct LevelData {
        std::shared_ptr<std::vector<FractalCmd>> commands;
        int numDraws = 0;
        NormParams norm{0, 0, 1.0f};
        std::shared_ptr<const std::vector<FractalPath::DrawSegment>> segments;
    };

    std::vector<LevelData> levels;
    int maxCachedLevel = -1;
    bool commandsDirty = true;

    static NormParams traceNormAndBuildSegments(
        const std::vector<FractalCmd>& cmds, float angleDeg,
        std::shared_ptr<const std::vector<FractalPath::DrawSegment>>& outSegments);

    std::string applyRules(const std::string& input) const;
    std::shared_ptr<std::vector<FractalCmd>> stringToCommands(const std::string& lsystemString) const;
    void rebuildAllLevels();

    static constexpr int MAX_ITERATIONS = 16;
    static constexpr int MAX_STRING_LENGTH = 3000000;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FractalParser)
};
