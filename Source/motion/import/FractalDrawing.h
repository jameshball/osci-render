#pragma once

#include "FractalPreparation.h"
#include "../model/DrawingLru.h"
#include "../model/PreparedSource.h"
#include <cmath>
#include <complex>
#include <map>
#include <unordered_map>

namespace motion::fractal {
using Segments = std::vector<std::array<double, 4>>;

// An L-system at a depth that need not be whole: between two depths the
// deeper drawing grows out of the shallower. Each point of the deeper one is
// matched to the point as far along the shallower one's drawn length, and
// the two blended; the deeper one is first fitted onto the shallower (turned,
// sized and moved as little as it can be), and that fit is undone as the
// blend goes, so a curve that turns as it grows (a dragon curve turns by 45
// degrees a generation) turns smoothly rather than folding through itself.
inline Segments between(const Segments& from, const Segments& to, double amount) {
    if (from.empty() || amount >= 1) {
        return to;
    }
    if (to.empty() || amount <= 0) {
        return from;
    }
    using Complex = std::complex<double>;
    // Where along the shallower drawing's drawn length a fraction falls.
    std::vector<double> reach(from.size() + 1, 0.0);
    for (std::size_t index = 0; index < from.size(); ++index) {
        const auto& s = from[index];
        reach[index + 1] = reach[index] + std::hypot(s[2] - s[0], s[3] - s[1]);
    }
    const auto fromLength = reach.back();
    const auto along = [&](double fraction) {
        const auto distance = std::clamp(fraction, 0.0, 1.0) * fromLength;
        const auto found = std::upper_bound(reach.begin() + 1, reach.end(), distance);
        const auto index = static_cast<std::size_t>(std::min<std::ptrdiff_t>(std::distance(reach.begin() + 1, found), static_cast<std::ptrdiff_t>(from.size()) - 1));
        const auto& s = from[index];
        const auto span = reach[index + 1] - reach[index];
        const auto t = span > 0 ? std::clamp((distance - reach[index]) / span, 0.0, 1.0) : 0.0;
        return Complex(s[0] + (s[2] - s[0]) * t, s[1] + (s[3] - s[1]) * t);
    };
    double toLength = 0;
    for (const auto& s : to) { toLength += std::hypot(s[2] - s[0], s[3] - s[1]); }
    if (fromLength <= 0 || toLength <= 0) {
        return amount < .5 ? from : to;
    }
    // Each deeper segment's ends, and where they fall on the shallower.
    std::vector<std::array<Complex, 4>> pairs;
    pairs.reserve(to.size());
    double travelled = 0;
    for (const auto& s : to) {
        const auto length = std::hypot(s[2] - s[0], s[3] - s[1]);
        const auto start = along(travelled / toLength);
        travelled += length;
        pairs.push_back({start, along(travelled / toLength), Complex(s[0], s[1]), Complex(s[2], s[3])});
    }
    // The fit: least squares, z -> c (z - deeperMiddle) + shallowerMiddle.
    Complex shallowerMiddle, deeperMiddle;
    for (const auto& pair : pairs) {
        shallowerMiddle += pair[0] + pair[1];
        deeperMiddle += pair[2] + pair[3];
    }
    shallowerMiddle /= static_cast<double>(pairs.size() * 2);
    deeperMiddle /= static_cast<double>(pairs.size() * 2);
    Complex cross;
    double spread = 0;
    for (const auto& pair : pairs) {
        for (int end = 0; end < 2; ++end) {
            const auto deeper = pair[static_cast<std::size_t>(2 + end)] - deeperMiddle;
            cross += std::conj(deeper) * (pair[static_cast<std::size_t>(end)] - shallowerMiddle);
            spread += std::norm(deeper);
        }
    }
    const auto fit = spread > 0 && std::abs(cross) > 0 ? cross / spread : Complex(1);
    // Its undoing, a share of the way: w -> a^share w + share b.
    const auto undoTurn = 1.0 / fit;
    const auto undoShift = deeperMiddle - shallowerMiddle / fit;
    const auto turned = std::polar(std::pow(std::abs(undoTurn), amount), std::arg(undoTurn) * amount);
    const auto place = [&](Complex shallower, Complex deeper) {
        const auto blended = shallower * (1 - amount) + (fit * (deeper - deeperMiddle) + shallowerMiddle) * amount;
        return turned * blended + undoShift * amount;
    };
    Segments result;
    result.reserve(pairs.size());
    double minX = 0, maxX = 0, minY = 0, maxY = 0;
    for (const auto& pair : pairs) {
        const auto a = place(pair[0], pair[2]), b = place(pair[1], pair[3]);
        if (result.empty()) {
            minX = maxX = a.real();
            minY = maxY = a.imag();
        }
        minX = std::min({minX, a.real(), b.real()});
        maxX = std::max({maxX, a.real(), b.real()});
        minY = std::min({minY, a.imag(), b.imag()});
        maxY = std::max({maxY, a.imag(), b.imag()});
        result.push_back({a.real(), a.imag(), b.real(), b.imag()});
    }
    // Fitted to the frame as each whole depth is, so growing never
    // overflows it.
    const auto span = std::max(maxX - minX, maxY - minY);
    const auto scale = span > 1e-8 ? 2.0 / span : 1.0;
    for (auto& segment : result) {
        for (int point = 0; point < 4; point += 2) {
            segment[static_cast<std::size_t>(point)] = (segment[static_cast<std::size_t>(point)] - (minX + maxX) * .5) * scale;
            segment[static_cast<std::size_t>(point + 1)] = (segment[static_cast<std::size_t>(point + 1)] - (minY + maxY) * .5) * scale;
        }
    }
    return result;
}

inline std::shared_ptr<const PreparedDrawing> drawingOf(const Segments& segments) {
    std::vector<std::unique_ptr<osci::Shape>> shapes;
    shapes.reserve(segments.size());
    for (const auto& s : segments) {
        shapes.push_back(std::make_unique<osci::Line>(osci::Point(static_cast<float>(s[0]), static_cast<float>(s[1]), 0), osci::Point(static_cast<float>(s[2]), static_cast<float>(s[3]), 0)));
    }
    return std::make_shared<PreparedDrawing>(std::move(shapes));
}

// Drawings already made, by source and depth, shared by every thread that
// prepares compositions; whole depths are kept for the blends between them.
// The least recently used blends are let go past a budget of memory.
class DrawingCache {
public:
    static DrawingCache& shared() {
        static DrawingCache cache;
        return cache;
    }
    // Blends are made at up to this many steps a depth.
    static constexpr double steps = 240;
    // A depth as a blend is made of it: fewer steps between deep levels, so
    // the blends across one fit a quarter of the budget. A depth keyed across
    // a level then keeps its blends from one preparation to the next (the
    // same drawings, which bakes know as the same) rather than evicting them
    // while they are still being sampled.
    double settle(const juce::String& source, double depth) {
        depth = std::isfinite(depth) ? std::clamp(depth, 0.0, 15.0) : 0.0;
        const auto whole = std::floor(depth);
        if (depth == whole) { return depth; }
        const auto deeper = at(source, whole + 1);
        const auto fit = deeper != nullptr ? static_cast<double>(budget / 4) / static_cast<double>(std::max<std::size_t>(deeper->bytes(), 1)) : steps;
        const auto levelSteps = std::clamp(std::floor(fit), 4.0, steps);
        return std::round(depth * levelSteps) / levelSteps;
    }

    // The source at `depth`; past the deepest it can be drawn (too many
    // segments), the deepest. Null if it can't be drawn at all.
    std::shared_ptr<const PreparedDrawing> at(const juce::String& source, double depth) {
        depth = settle(source, depth);
        const auto key = std::make_pair(source.hashCode64(), depth);
        const auto found = blends.find(key);
        if (found != nullptr) { return found; }
        const auto whole = static_cast<int>(std::floor(depth));
        const auto shallower = segmentsAt(source, whole);
        if (shallower == nullptr) {
            return nullptr;
        }
        const auto deeper = depth > whole ? segmentsAt(source, whole + 1) : nullptr;
        return blends.insert(key, drawingOf(deeper != nullptr ? between(*shallower, *deeper, depth - whole) : *shallower));
    }

private:
    using Key = std::pair<juce::int64, double>;
    // A whole depth's segments; the deepest that can be drawn in place of
    // one too deep. Null when none can.
    std::shared_ptr<const Segments> segmentsAt(const juce::String& source, int depth) {
        const auto key = std::make_pair(source.hashCode64(), depth);
        {
            const juce::SpinLock::ScopedLockType lock(depthsGuard);
            const auto found = depths.find(key);
            if (found != depths.end()) { return found->second; }
        }
        std::shared_ptr<const Segments> made;
        for (auto tried = depth; tried >= 0 && made == nullptr; --tried) {
            auto prepared = prepare(source, tried);
            if (prepared) { made = std::make_shared<const Segments>(std::move(prepared.segments)); }
        }
        const juce::SpinLock::ScopedLockType lock(depthsGuard);
        depths.emplace(key, made);
        return made;
    }

    static constexpr std::size_t budget = 64 * 1024 * 1024;
    DrawingLru<Key, std::map> blends {budget};
    juce::SpinLock depthsGuard;
    std::map<std::pair<juce::int64, int>, std::shared_ptr<const Segments>> depths;
};

// A clip's drawing of an L-system, a frame per frame of its content (fewer
// over long content: contentFrames) where its depth changes; frames of the
// same depth share a drawing.
template <typename DepthAt>
std::shared_ptr<const PreparedSource> clipSource(const juce::String& source, double contentEnd, double frameRate, DepthAt&& depthAt) {
    auto& cache = DrawingCache::shared();
    return sampledSource(contentEnd, frameRate, [&](double seconds) { return cache.settle(source, depthAt(seconds)); }, [&](double depth) { return cache.at(source, depth); });
}
}
