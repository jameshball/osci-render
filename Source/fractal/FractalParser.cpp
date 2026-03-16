#include "FractalParser.h"
#include "../modules/osci_render_core/shape/osci_Line.h"
#include <cmath>
#include <algorithm>

// ==== FractalPath implementation ====

FractalPath::FractalPath(std::shared_ptr<const std::vector<FractalCmd>> commands,
                         int numDraws, float baseAngle,
                         float normCX, float normCY, float normScale)
    : commands(std::move(commands)), numDraws(numDraws), baseAngle(baseAngle),
      normCX(normCX), normCY(normCY), normScale(normScale) {
    cursorStack.reserve(64);
}

float FractalPath::length() {
    if (len < 0) len = (float)numDraws;
    return len;
}

std::unique_ptr<osci::Shape> FractalPath::clone() {
    return std::make_unique<FractalPath>(commands, numDraws, baseAngle,
                                         normCX, normCY, normScale);
}

void FractalPath::resetCursor(float angle) const {
    cursorCmdPos = 0;
    cursorDrawCount = 0;
    cursorX = cursorY = 0.0f;
    cursorHeading = 90.0f;
    cursorStack.clear();

    // Process any non-draw commands before the first Draw
    const auto& cmds = *commands;
    while (cursorCmdPos < (int)cmds.size() && cmds[cursorCmdPos] != FractalCmd::Draw) {
        FractalCmd cmd = cmds[cursorCmdPos++];
        switch (cmd) {
            case FractalCmd::Move: {
                float rad = cursorHeading * (juce::MathConstants<float>::pi / 180.0f);
                cursorX += std::cos(rad);
                cursorY += std::sin(rad);
                break;
            }
            case FractalCmd::TurnRight: cursorHeading += angle; break;
            case FractalCmd::TurnLeft:  cursorHeading -= angle; break;
            case FractalCmd::Push: cursorStack.push_back({cursorX, cursorY, cursorHeading}); break;
            case FractalCmd::Pop:
                if (!cursorStack.empty()) {
                    cursorX = cursorStack.back().x;
                    cursorY = cursorStack.back().y;
                    cursorHeading = cursorStack.back().heading;
                    cursorStack.pop_back();
                }
                break;
            default: break;
        }
    }
}

void FractalPath::advancePastOneDraw(float angle) const {
    const auto& cmds = *commands;
    if (cursorCmdPos >= (int)cmds.size()) return;

    // Execute the Draw command at the current position
    if (cmds[cursorCmdPos] == FractalCmd::Draw) {
        float rad = cursorHeading * (juce::MathConstants<float>::pi / 180.0f);
        cursorX += std::cos(rad);
        cursorY += std::sin(rad);
        cursorCmdPos++;
        cursorDrawCount++;
    }

    // Process non-draw commands until the next Draw (or end)
    while (cursorCmdPos < (int)cmds.size() && cmds[cursorCmdPos] != FractalCmd::Draw) {
        FractalCmd cmd = cmds[cursorCmdPos++];
        switch (cmd) {
            case FractalCmd::Move: {
                float rad = cursorHeading * (juce::MathConstants<float>::pi / 180.0f);
                cursorX += std::cos(rad);
                cursorY += std::sin(rad);
                break;
            }
            case FractalCmd::TurnRight: cursorHeading += angle; break;
            case FractalCmd::TurnLeft:  cursorHeading -= angle; break;
            case FractalCmd::Push: cursorStack.push_back({cursorX, cursorY, cursorHeading}); break;
            case FractalCmd::Pop:
                if (!cursorStack.empty()) {
                    cursorX = cursorStack.back().x;
                    cursorY = cursorStack.back().y;
                    cursorHeading = cursorStack.back().heading;
                    cursorStack.pop_back();
                }
                break;
            default: break;
        }
    }
}

osci::Point FractalPath::nextVector(float drawingProgress) {
    if (numDraws == 0) return osci::Point();

    float angle = baseAngle;

    float segFloat = drawingProgress * numDraws;
    int targetDraw = (int)segFloat;
    float localT = segFloat - targetDraw;
    targetDraw = juce::jlimit(0, numDraws - 1, targetDraw);

    // Reset cursor on frame restart (progress went backwards)
    if (targetDraw < cursorDrawCount) {
        resetCursor(angle);
    }

    // Advance cursor to the target draw segment
    while (cursorDrawCount < targetDraw) {
        advancePastOneDraw(angle);
    }

    // Interpolate within the current draw segment
    float rad = cursorHeading * (juce::MathConstants<float>::pi / 180.0f);
    float px = cursorX + localT * std::cos(rad);
    float py = cursorY + localT * std::sin(rad);

    // Apply smoothed normalisation
    px = (px - normCX) * normScale;
    py = (py - normCY) * normScale;

    return osci::Point(px, py, 0.0f);
}

// ==== FractalParser implementation ====

FractalParser::FractalParser(const juce::String& jsonContent) {
    parse(jsonContent);
}

void FractalParser::parse(const juce::String& jsonContent) {
    axiom = "";
    baseAngleDegrees = 60.0f;
    rules.clear();

    auto result = juce::JSON::parse(jsonContent);

    if (auto* obj = result.getDynamicObject()) {
        axiom = obj->getProperty("axiom").toString();
        baseAngleDegrees = (float)obj->getProperty("angle");

        if (auto* rulesArray = obj->getProperty("rules").getArray()) {
            for (const auto& ruleVar : *rulesArray) {
                if (auto* ruleObj = ruleVar.getDynamicObject()) {
                    FractalRule rule;
                    rule.variable = ruleObj->getProperty("variable").toString();
                    rule.replacement = ruleObj->getProperty("replacement").toString();
                    rules.push_back(rule);
                }
            }
        }
    }

    if (axiom.isEmpty()) {
        axiom = "F";
    }

    commandsDirty = true;
}

juce::String FractalParser::toJson() const {
    auto* obj = new juce::DynamicObject();
    obj->setProperty("axiom", axiom);
    obj->setProperty("angle", (double)baseAngleDegrees);

    juce::Array<juce::var> rulesArray;
    for (const auto& rule : rules) {
        auto* ruleObj = new juce::DynamicObject();
        ruleObj->setProperty("variable", rule.variable);
        ruleObj->setProperty("replacement", rule.replacement);
        rulesArray.add(juce::var(ruleObj));
    }
    obj->setProperty("rules", rulesArray);

    return juce::JSON::toString(juce::var(obj), false);
}

juce::String FractalParser::defaultJson() {
    auto* obj = new juce::DynamicObject();
    obj->setProperty("axiom", "F-G-G");
    obj->setProperty("angle", 120.0);

    juce::Array<juce::var> rulesArray;

    auto* rule1 = new juce::DynamicObject();
    rule1->setProperty("variable", "F");
    rule1->setProperty("replacement", "F-G+F+G-F");
    rulesArray.add(juce::var(rule1));

    auto* rule2 = new juce::DynamicObject();
    rule2->setProperty("variable", "G");
    rule2->setProperty("replacement", "GG");
    rulesArray.add(juce::var(rule2));

    obj->setProperty("rules", rulesArray);

    return juce::JSON::toString(juce::var(obj), false);
}

void FractalParser::setIterations(int iterations) {
    currentIterations.store(juce::jlimit(0, MAX_ITERATIONS, iterations), std::memory_order_relaxed);
}

// ---- String rewriting ----

std::string FractalParser::applyRules(const std::string& input) const {
    std::string result;
    result.reserve(input.size() * 4);

    for (char ch : input) {
        bool replaced = false;
        for (const auto& rule : rules) {
            if (rule.variable.isNotEmpty() && ch == (char)rule.variable[0]) {
                auto rep = rule.replacement.toStdString();
                result.append(rep);
                replaced = true;
                break;
            }
        }
        if (!replaced) {
            result.push_back(ch);
        }
        if ((int)result.size() > MAX_STRING_LENGTH)
            break;
    }

    return result;
}

// ---- Convert L-system string to compact command list (shared_ptr for zero-copy) ----

std::shared_ptr<std::vector<FractalCmd>> FractalParser::stringToCommands(const std::string& lsystemString) const {
    auto commands = std::make_shared<std::vector<FractalCmd>>();
    commands->reserve(lsystemString.size());

    for (char ch : lsystemString) {
        if ((ch >= 'A' && ch <= 'Z') && ch != 'X' && ch != 'Y') {
            commands->push_back(FractalCmd::Draw);
        } else if (ch == 'f') {
            commands->push_back(FractalCmd::Move);
        } else if (ch == '+') {
            commands->push_back(FractalCmd::TurnRight);
        } else if (ch == '-') {
            commands->push_back(FractalCmd::TurnLeft);
        } else if (ch == '[') {
            commands->push_back(FractalCmd::Push);
        } else if (ch == ']') {
            commands->push_back(FractalCmd::Pop);
        }
    }

    return commands;
}

// ---- Rebuild all level caches ----

void FractalParser::rebuildAllLevels() {
    levels.clear();
    levels.resize(MAX_ITERATIONS + 1);
    maxCachedLevel = -1;

    std::string current = axiom.toStdString();

    for (int level = 0; level <= MAX_ITERATIONS; ++level) {
        levels[level].commands = stringToCommands(current);

        int drawCount = 0;
        for (FractalCmd cmd : *levels[level].commands) {
            if (cmd == FractalCmd::Draw) drawCount++;
        }
        levels[level].numDraws = drawCount;
        maxCachedLevel = level;

        if (level < MAX_ITERATIONS && (int)current.size() <= MAX_STRING_LENGTH) {
            current = applyRules(current);
        } else if (level < MAX_ITERATIONS) {
            for (int rest = level + 1; rest <= MAX_ITERATIONS; ++rest) {
                levels[rest].commands = levels[level].commands;
                levels[rest].numDraws = levels[level].numDraws;
            }
            maxCachedLevel = MAX_ITERATIONS;
            break;
        }
    }

    commandsDirty = false;
}

// ---- Trace bounding box at a given angle and return norm params ----

FractalParser::NormParams FractalParser::traceNorm(const std::vector<FractalCmd>& cmds, float angleDeg) {
    float x = 0, y = 0, heading = 90.0f;
    float minX = 0, maxX = 0, minY = 0, maxY = 0;

    struct State { float x, y, heading; };
    std::vector<State> stack;
    stack.reserve(64);

    for (FractalCmd cmd : cmds) {
        switch (cmd) {
            case FractalCmd::Draw: {
                minX = std::min(minX, x); maxX = std::max(maxX, x);
                minY = std::min(minY, y); maxY = std::max(maxY, y);
                float rad = heading * (juce::MathConstants<float>::pi / 180.0f);
                x += std::cos(rad);
                y += std::sin(rad);
                minX = std::min(minX, x); maxX = std::max(maxX, x);
                minY = std::min(minY, y); maxY = std::max(maxY, y);
                break;
            }
            case FractalCmd::Move: {
                float rad = heading * (juce::MathConstants<float>::pi / 180.0f);
                x += std::cos(rad);
                y += std::sin(rad);
                break;
            }
            case FractalCmd::TurnRight: heading += angleDeg; break;
            case FractalCmd::TurnLeft:  heading -= angleDeg; break;
            case FractalCmd::Push: stack.push_back({x, y, heading}); break;
            case FractalCmd::Pop:
                if (!stack.empty()) {
                    x = stack.back().x; y = stack.back().y; heading = stack.back().heading;
                    stack.pop_back();
                }
                break;
        }
    }

    float w = maxX - minX;
    float h = maxY - minY;
    float maxDim = std::max(w, h);

    NormParams p;
    if (maxDim < 1e-8f) {
        p.cx = p.cy = 0;
        p.scale = 1.0f;
    } else {
        p.cx = (minX + maxX) * 0.5f;
        p.cy = (minY + maxY) * 0.5f;
        p.scale = 2.0f / maxDim;
    }
    return p;
}

// ---- Main draw: returns a single FractalPath shape ----

std::vector<std::unique_ptr<osci::Shape>> FractalParser::draw() {
    int iters = currentIterations.load(std::memory_order_relaxed);

    if (commandsDirty) {
        rebuildAllLevels();
    }

    int level = juce::jlimit(0, maxCachedLevel, iters);
    auto& data = levels[level];

    // Reset smooth state when switching iteration levels
    if (level != smoothLevel) {
        smoothInitialised = false;
        smoothLevel = level;
    }

    // Compute ideal norm at the current base angle
    auto ideal = traceNorm(*data.commands, baseAngleDegrees);

    if (!smoothInitialised) {
        smoothCX = ideal.cx;
        smoothCY = ideal.cy;
        smoothScale = ideal.scale;
        smoothInitialised = true;
    } else {
        // Asymmetric smoothing: fast when scaling down (prevent clipping),
        // slower when scaling up (aesthetic expansion)
        constexpr float alphaDown = 0.4f;
        constexpr float alphaUp   = 0.1f;
        float scaleAlpha = (ideal.scale < smoothScale) ? alphaDown : alphaUp;
        smoothCX    += 0.2f * (ideal.cx - smoothCX);
        smoothCY    += 0.2f * (ideal.cy - smoothCY);
        smoothScale += scaleAlpha * (ideal.scale - smoothScale);
    }

    std::vector<std::unique_ptr<osci::Shape>> shapes;
    if (data.numDraws > 0) {
        shapes.push_back(std::make_unique<FractalPath>(
            data.commands, data.numDraws, baseAngleDegrees,
            smoothCX, smoothCY, smoothScale));
    }
    return shapes;
}
