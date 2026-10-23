#pragma once

#include "Document.h"
#include <map>
#include <vector>

// What the Document's edits share across Document.cpp, DocumentClips.cpp,
// DocumentSources.cpp and DocumentRig.cpp; nothing else includes it.
namespace motion {
// Every edit that adds things checks it has identities for them: a project
// would need trillions of edits to run out (loading leaves that many free).
inline juce::Result noIdentitiesLeft() { return juce::Result::fail("This project can't take any more items."); }

// Gives each item the next identity and records old -> new, so the routes and
// links of a copy can follow it.
template <typename Item>
void renumber(Item& item, Id& highest, std::map<Id, Id>& owners) {
    const auto old = item.id;
    item.id = ++highest;
    owners.emplace(old, item.id);
}
template <typename Item>
void renumber(std::vector<Item>& items, Id& highest, std::map<Id, Id>& owners) {
    for (auto& item : items) { renumber(item, highest, owners); }
}
}
