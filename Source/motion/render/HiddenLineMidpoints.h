#pragma once

#include <JuceHeader.h>
#include <bit>
#include <cstdint>
#include <cstring>
#include <vector>

namespace motion::hidden {
// Each edge's middle, made once. Dicing splits pieces across their longest
// edges, and pieces cut from the same face split many of the same edges from
// either side, so the same middle would be bent through the clip's effects
// two or more times. An edge is its two source points in either order; the
// table is open addressed and starts afresh in O(1), so it can be cleared
// for every face and stay small.
template <typename Value>
class MidpointCache {
public:
    void clear() {
        if (++generation == 0) {
            for (auto& entry : entries) { entry.generation = 0; }
            generation = 1;
        }
        count = 0;
    }

    template <typename Make>
    Value at(const osci::Point& a, const osci::Point& b, Make&& make) {
        if (entries.empty() || 2 * (count + 1) > entries.size()) { grow(); }
        const auto key = keyOf(a, b);
        const auto mask = entries.size() - 1;
        for (auto slot = hash(key) & mask;; slot = (slot + 1) & mask) {
            auto& entry = entries[slot];
            if (entry.generation != generation) {
                entry = {key, generation, make()};
                ++count;
                return entry.value;
            }
            if (entry.key == key) { return entry.value; }
        }
    }

private:
    using Key = std::array<std::uint32_t, 6>;
    struct Entry {
        Key key {};
        std::uint32_t generation = 0;
        Value value {};
    };

    static Key keyOf(const osci::Point& a, const osci::Point& b) {
        const auto bits = [](float value) { return std::bit_cast<std::uint32_t>(value == 0.0f ? 0.0f : value); };
        const std::array<std::uint32_t, 3> first {bits(a.x), bits(a.y), bits(a.z)}, second {bits(b.x), bits(b.y), bits(b.z)};
        // Either way round is the same edge.
        return first < second ? Key {first[0], first[1], first[2], second[0], second[1], second[2]} : Key {second[0], second[1], second[2], first[0], first[1], first[2]};
    }
    static std::size_t hash(const Key& key) {
        std::uint64_t value = 0x9e3779b97f4a7c15ull;
        for (const auto part : key) { value = (value ^ part) * 0xff51afd7ed558ccdull; }
        return static_cast<std::size_t>(value ^ (value >> 29));
    }
    void grow() {
        auto previous = std::move(entries);
        const auto previousGeneration = generation;
        entries.assign(std::max<std::size_t>(1024, previous.size() * 2), Entry {});
        generation = 1;
        count = 0;
        const auto mask = entries.size() - 1;
        for (auto& entry : previous) {
            if (entry.generation != previousGeneration) { continue; }
            auto slot = hash(entry.key) & mask;
            while (entries[slot].generation == generation) { slot = (slot + 1) & mask; }
            entries[slot] = {entry.key, generation, std::move(entry.value)};
            ++count;
        }
    }

    std::vector<Entry> entries;
    std::uint32_t generation = 1;
    std::size_t count = 0;
};
}
