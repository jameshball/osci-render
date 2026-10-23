#pragma once

#include "PreparedDrawing.h"
#include <list>
#include <memory>
#include <vector>

namespace motion {
// Drawings already made, by key, shared by every thread that prepares
// compositions (never the audio thread): the least recently used are let go
// past a budget of memory.
template <typename Key, template <class...> class Map>
class DrawingLru {
public:
    explicit DrawingLru(std::size_t budget) : budget(budget) {}

    // The drawing kept for `key`, now the most recently used; null if none is.
    std::shared_ptr<const PreparedDrawing> find(const Key& key) {
        const juce::SpinLock::ScopedLockType lock(guard);
        const auto found = entries.find(key);
        if (found == entries.end()) { return nullptr; }
        order.splice(order.begin(), order, found->second.place);
        return found->second.drawing;
    }
    // Keeps `made` for `key`, or, when another thread kept one for it first,
    // returns that one instead.
    std::shared_ptr<const PreparedDrawing> insert(Key key, std::shared_ptr<const PreparedDrawing> made) {
        const auto size = made->bytes();
        // Those let go are freed once the lock is released.
        std::vector<std::shared_ptr<const PreparedDrawing>> released;
        const juce::SpinLock::ScopedLockType lock(guard);
        const auto found = entries.find(key);
        if (found != entries.end()) { return found->second.drawing; }
        order.push_front(key);
        entries.emplace(std::move(key), Entry {made, order.begin(), size});
        held += size;
        while (held > budget && entries.size() > 1) {
            const auto oldest = entries.find(order.back());
            held -= oldest->second.bytes;
            released.push_back(std::move(oldest->second.drawing));
            entries.erase(oldest);
            order.pop_back();
        }
        return made;
    }

private:
    struct Entry {
        std::shared_ptr<const PreparedDrawing> drawing;
        typename std::list<Key>::iterator place;
        std::size_t bytes = 0;
    };
    juce::SpinLock guard;
    Map<Key, Entry> entries;
    std::list<Key> order;
    std::size_t held = 0;
    const std::size_t budget;
};
}
