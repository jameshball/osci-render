#pragma once

#include <JuceHeader.h>
#include <vector>
#include <memory>
#include <string>
#include "../modules/osci_render_core/shape/osci_Shape.h"
#include "../modules/osci_render_core/shape/osci_Point.h"

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
// Uses a streaming cursor for O(1) per-sample sequential access.
class FractalPath : public osci::Shape {
public:
    FractalPath(std::shared_ptr<const std::vector<FractalCmd>> commands,
                int numDraws, float baseAngle,
                float normCX, float normCY, float normScale);

    osci::Point nextVector(float drawingProgress) override;
    float length() override;
    void scale(float x, float y, float z) override {}
    void translate(float x, float y, float z) override {}
    std::unique_ptr<Shape> clone() override;
    std::string type() override { return "FractalPath"; }

private:
    std::shared_ptr<const std::vector<FractalCmd>> commands;
    int numDraws;
    float baseAngle;
    float normCX, normCY, normScale;

    // Mutable streaming cursor state (O(1) for sequential access)
    struct SavedState { float x, y, heading; };
    mutable int cursorCmdPos = 0;
    mutable int cursorDrawCount = 0;
    mutable float cursorX = 0.0f, cursorY = 0.0f, cursorHeading = 90.0f;
    mutable std::vector<SavedState> cursorStack;

    void resetCursor(float angle) const;
    void advancePastOneDraw(float angle) const;
};

class FractalParser {
public:
    FractalParser(const juce::String& jsonContent);

    std::vector<std::unique_ptr<osci::Shape>> draw();

    void setIterations(int iterations);

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

    // Per-level cached data
    struct LevelData {
        std::shared_ptr<std::vector<FractalCmd>> commands;
        int numDraws = 0;
    };

    std::vector<LevelData> levels;
    int maxCachedLevel = -1;
    bool commandsDirty = true;

    // Smoothed normalisation state (updated each frame in draw())
    float smoothCX = 0.0f, smoothCY = 0.0f, smoothScale = 1.0f;
    bool smoothInitialised = false;
    int smoothLevel = -1; // tracks which level the smooth state was computed for

    struct NormParams { float cx, cy, scale; };
    static NormParams traceNorm(const std::vector<FractalCmd>& cmds, float angleDeg);

    std::string applyRules(const std::string& input) const;
    std::shared_ptr<std::vector<FractalCmd>> stringToCommands(const std::string& lsystemString) const;
    void rebuildAllLevels();

    static constexpr int MAX_ITERATIONS = 16;
    static constexpr int MAX_STRING_LENGTH = 2000000;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FractalParser)
};
