#pragma once

#include <juce_core/juce_core.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <optional>
#include <string>
#include <vector>

namespace osci::fractal {
// Single-depth preparation for immutable consumers. Render's live multi-level
// cache remains separate; no parser or mutable turtle state crosses threads.
struct Prepared {
    std::vector<std::array<double, 4>> segments;
    juce::String error;
    explicit operator bool() const { return error.isEmpty(); }
};

inline Prepared prepare(const juce::String& source, int depth, const std::atomic<bool>* cancel = nullptr) {
    constexpr std::size_t maxSymbols = 262144, maxSegments = 32768, maxStack = 4096;
    const auto cancelled = [cancel] { return cancel != nullptr && cancel->load(std::memory_order_relaxed); };
    const auto fail = [](juce::String message) { return Prepared { {}, std::move(message) }; };
    if (cancelled()) { return fail("Fractal preparation cancelled."); }
    if (depth < 0 || depth > 15) { return fail("Choose a fractal depth from 0 to 15."); }
    if (source.getNumBytesAsUTF8() > maxSymbols) { return fail("Fractal source must be no larger than 256 KiB."); }
    // Bound recursion before entering JUCE's recursive JSON parser.
    int nesting = 0;
    bool quoted = false, escaped = false;
    for (auto character = source.getCharPointer(); !character.isEmpty(); ++character) {
        const auto value = *character;
        if (quoted) {
            if (escaped) { escaped = false; } else if (value == '\\') { escaped = true; } else if (value == '"') { quoted = false; }
        } else if (value == '\'') {
            return fail("Fractal source must use standard JSON double-quoted strings.");
        } else if (value == '"') {
            quoted = true;
        } else if (value == '{' || value == '[') {
            if (++nesting > 16) { return fail("Fractal JSON exceeds 16 nesting levels."); }
        } else if (value == '}' || value == ']') {
            if (--nesting < 0) { return fail("Fractal JSON has unmatched closing brackets."); }
        }
    }
    juce::var json;
    const auto parsed = juce::JSON::parse(source, json);
    if (parsed.failed() || !json.isObject()) { return fail("Fractal source must be a JSON object."); }
    const auto axiom = json.getProperty("axiom", {});
    const auto angle = json.getProperty("angle", 60.0);
    const auto ruleList = json.getProperty("rules", juce::var(juce::Array<juce::var>()));
    const auto ascii = [](const juce::String& text) {
        for (auto p = text.getCharPointer(); !p.isEmpty(); ++p) {
            if (*p == 0 || *p > 127) { return false; }
        }
        return true;
    };
    if (!axiom.isString() || axiom.toString().isEmpty() || !ascii(axiom.toString())) { return fail("Use a non-empty ASCII axiom."); }
    if (!(angle.isDouble() || angle.isInt() || angle.isInt64()) || !std::isfinite(static_cast<double>(angle))) { return fail("Fractal angle must be a finite number."); }
    if (!ruleList.isArray() || ruleList.size() > 128) { return fail("Use an array of at most 128 fractal rules."); }
    std::array<std::optional<std::string>, 128> rules;
    for (const auto& rule : *ruleList.getArray()) {
        const auto variable = rule.getProperty("variable", {}), replacement = rule.getProperty("replacement", {});
        const auto key = variable.toString();
        if (!rule.isObject() || !variable.isString() || key.length() != 1 || !ascii(key) || !replacement.isString() || !ascii(replacement.toString())) {
            return fail("Each rule needs one ASCII variable and an ASCII replacement string.");
        }
        const auto index = static_cast<unsigned char>(key[0]);
        if (rules[index].has_value()) { return fail("Each fractal variable may have only one rule."); }
        rules[index] = replacement.toString().toStdString();
    }
    auto symbols = axiom.toString().toStdString();
    for (int level = 0; level < depth; ++level) {
        std::string next;
        next.reserve(std::min(maxSymbols, symbols.size()));
        for (std::size_t index = 0; index < symbols.size(); ++index) {
            if ((index & 255) == 0 && cancelled()) { return fail("Fractal preparation cancelled."); }
            const auto& replacement = rules[static_cast<unsigned char>(symbols[index])];
            const auto size = replacement.has_value() ? replacement->size() : 1;
            if (size > maxSymbols - next.size()) { return fail("Fractal exceeds 262,144 symbols. Choose a lower depth."); }
            if (replacement.has_value()) { next += *replacement; } else { next += symbols[index]; }
        }
        symbols = std::move(next);
    }
    struct Turtle { double x = 0, y = 0, heading = 90; };
    Turtle turtle;
    std::vector<Turtle> stack;
    Prepared result;
    result.segments.reserve(std::min(symbols.size(), maxSegments));
    const auto turn = std::remainder(static_cast<double>(angle), 360.0);
    double minX = 0, maxX = 0, minY = 0, maxY = 0;
    for (std::size_t index = 0; index < symbols.size(); ++index) {
        if ((index & 255) == 0 && cancelled()) { return fail("Fractal preparation cancelled."); }
        const auto symbol = symbols[index];
        const auto draws = symbol >= 'A' && symbol <= 'Z' && symbol != 'X' && symbol != 'Y';
        if (draws || symbol == 'f') {
            const auto radians = turtle.heading * juce::MathConstants<double>::pi / 180;
            const auto x = turtle.x + std::cos(radians), y = turtle.y + std::sin(radians);
            if (draws) {
                if (result.segments.size() == maxSegments) { return fail("Fractal exceeds 32,768 drawing segments. Choose a lower depth."); }
                result.segments.push_back({ turtle.x, turtle.y, x, y });
                minX = std::min({minX, turtle.x, x}); maxX = std::max({maxX, turtle.x, x});
                minY = std::min({minY, turtle.y, y}); maxY = std::max({maxY, turtle.y, y});
            }
            turtle.x = x; turtle.y = y;
        } else if (symbol == '+') {
            turtle.heading = std::remainder(turtle.heading + turn, 360.0);
        } else if (symbol == '-') {
            turtle.heading = std::remainder(turtle.heading - turn, 360.0);
        } else if (symbol == '[') {
            if (stack.size() == maxStack) { return fail("Fractal exceeds 4,096 branch levels."); }
            stack.push_back(turtle);
        } else if (symbol == ']') {
            if (stack.empty()) { return fail("Fractal has an unmatched closing bracket."); }
            turtle = stack.back(); stack.pop_back();
        }
    }
    if (!stack.empty()) { return fail("Fractal has an unmatched opening bracket."); }
    if (result.segments.empty()) { return fail("This fractal depth produces no drawing segments."); }
    const auto span = std::max(maxX - minX, maxY - minY);
    const auto scale = span > 1e-8 ? 2.0 / span : 1.0;
    for (std::size_t index = 0; index < result.segments.size(); ++index) {
        if ((index & 255) == 0 && cancelled()) { return fail("Fractal preparation cancelled."); }
        auto& segment = result.segments[index];
        for (int point = 0; point < 4; point += 2) {
            segment[point] = (segment[point] - (minX + maxX) * .5) * scale;
            segment[point + 1] = (segment[point + 1] - (minY + maxY) * .5) * scale;
        }
    }
    return result;
}
}
