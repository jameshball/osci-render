#include "FractalParser.h"
#include "../../../modules/osci_render_core/shape/osci_Line.h"
#include <cmath>
#include <algorithm>

// ==== FractalPath implementation ====

FractalPath::FractalPath(std::shared_ptr<const std::vector<DrawSegment>> segments)
    : segments(std::move(segments)) {}

float FractalPath::length() {
    if (len < 0) len = (float)segments->size();
    return len;
}

std::unique_ptr<osci::Shape> FractalPath::clone() {
    return std::make_unique<FractalPath>(segments);
}

osci::Point FractalPath::nextVector(float drawingProgress) {
    const int numDraws = (int)segments->size();
    if (numDraws == 0) return osci::Point();

    float segFloat = drawingProgress * numDraws;
    int idx = (int)segFloat;
    float t = segFloat - idx;
    idx = juce::jlimit(0, numDraws - 1, idx);

    const auto& seg = (*segments)[idx];
    return osci::Point(seg.x + t * seg.dx, seg.y + t * seg.dy, 0.0f);
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
        auto angleVar = obj->getProperty("angle");
        if (angleVar.isDouble() || angleVar.isInt())
            baseAngleDegrees = (float) angleVar;

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
    frameDirty.store(true, std::memory_order_release);
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
    int clamped = juce::jlimit(0, MAX_ITERATIONS, iterations);
    int prev = currentIterations.exchange(clamped, std::memory_order_relaxed);
    if (prev != clamped) {
        frameDirty.store(true, std::memory_order_release);
    }
}

bool FractalParser::consumeDirty() {
    return frameDirty.exchange(false, std::memory_order_acq_rel);
}

// ---- String rewriting ----

std::string FractalParser::applyRules(const std::string& input) const {
    std::string result;
    result.reserve(input.size() * 4);

    for (char ch : input) {
        bool replaced = false;
        for (const auto& rule : rules) {
            // Only match rules whose variable is exactly one character long.
            if (rule.variable.length() == 1 && ch == (char)rule.variable[0]) {
                auto rep = rule.replacement.toStdString();
                result.append(rep);
                replaced = true;
                break;
            }
        }
        if (!replaced) {
            result.push_back(ch);
        }
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
        levels[level].norm = traceNormAndBuildSegments(*levels[level].commands, baseAngleDegrees, levels[level].segments);
        maxCachedLevel = level;

        if (level < MAX_ITERATIONS) {
            std::string next = applyRules(current);
            if ((int)next.size() > MAX_STRING_LENGTH) {
                // String exceeded limit — keep this clean level as the cap
                for (int rest = level + 1; rest <= MAX_ITERATIONS; ++rest) {
                    levels[rest].commands = levels[level].commands;
                    levels[rest].numDraws = levels[level].numDraws;
                    levels[rest].norm = levels[level].norm;
                    levels[rest].segments = levels[level].segments;
                }
                maxCachedLevel = MAX_ITERATIONS;
                break;
            }
            current = std::move(next);
        }
    }

    commandsDirty = false;
}

// ---- Single-pass turtle walk: compute bounding box and build normalised segments ----
//
// Walks the command list once, collecting raw (unnormalised) draw segments
// and tracking the bounding box simultaneously.  After the walk the segments
// are normalised in-place.

FractalParser::NormParams FractalParser::traceNormAndBuildSegments(
        const std::vector<FractalCmd>& cmds, float angleDeg,
        std::shared_ptr<const std::vector<FractalPath::DrawSegment>>& outSegments) {
    const int cmdCount = static_cast<int>(cmds.size());

    auto segs = std::make_shared<std::vector<FractalPath::DrawSegment>>();
    // Rough upper-bound reserve — most commands are draws in typical fractals
    segs->reserve(cmdCount);

    float x = 0.0f, y = 0.0f, heading = 90.0f;
    float minX = 0.0f, maxX = 0.0f, minY = 0.0f, maxY = 0.0f;

    struct StackEntry { float x, y, heading; };
    std::vector<StackEntry> stack;
    stack.reserve(64);

    for (int i = 0; i < cmdCount; ++i) {
        switch (cmds[i]) {
            case FractalCmd::Draw: {
                minX = std::min(minX, x); maxX = std::max(maxX, x);
                minY = std::min(minY, y); maxY = std::max(maxY, y);
                float rad = heading * (juce::MathConstants<float>::pi / 180.0f);
                float cosVal = std::cos(rad);
                float sinVal = std::sin(rad);
                // Store raw (unnormalised) segment — will be normalised below
                segs->push_back({x, y, cosVal, sinVal});
                x += cosVal;
                y += sinVal;
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

    // Compute norm params from bounding box
    float w = maxX - minX;
    float h = maxY - minY;
    float maxDim = std::max(w, h);

    NormParams norm;
    if (maxDim < 1e-8f) {
        norm.cx = norm.cy = 0;
        norm.scale = 1.0f;
    } else {
        norm.cx = (minX + maxX) * 0.5f;
        norm.cy = (minY + maxY) * 0.5f;
        norm.scale = 2.0f / maxDim;
    }

    // Normalise segments in-place
    for (auto& seg : *segs) {
        seg.x  = (seg.x - norm.cx) * norm.scale;
        seg.y  = (seg.y - norm.cy) * norm.scale;
        seg.dx *= norm.scale;
        seg.dy *= norm.scale;
    }

    outSegments = std::move(segs);
    return norm;
}

// ---- Main draw: returns a single FractalPath shape ----

std::vector<std::unique_ptr<osci::Shape>> FractalParser::draw() {
    int iters = currentIterations.load(std::memory_order_relaxed);

    if (commandsDirty) {
        rebuildAllLevels();
    }

    int level = juce::jlimit(0, maxCachedLevel, iters);
    auto& data = levels[level];

    std::vector<std::unique_ptr<osci::Shape>> shapes;
    if (data.numDraws > 0) {
        shapes.push_back(std::make_unique<FractalPath>(data.segments));
    }
    return shapes;
}
