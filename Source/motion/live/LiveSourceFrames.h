#pragma once

#include "../model/LiveSourceIdentity.h"
#include "../model/PreparedSource.h"
#include <algorithm>
#include <functional>
#include <stdexcept>

namespace motion {
// Build on the publisher thread, then publish one immutable set for an entire
// audio block. The editor retains a separate shared snapshot of the same data;
// it must never acquire from the audio thread's PreparedState exchange.
class LiveSourceFrames {
public:
    struct Entry {
        std::shared_ptr<const LiveSourceIdentity> identity;
        std::shared_ptr<const PreparedSource> source; // Null explicitly blanks.
    };
    static constexpr std::size_t maximumSources = 128;
    explicit LiveSourceFrames(std::vector<Entry> entries) : entries(std::move(entries)) {
        if (this->entries.size() > maximumSources) { throw std::invalid_argument("Too many live sources."); }
        for (const auto& entry : this->entries) {
            if (entry.identity == nullptr) { throw std::invalid_argument("Missing live source identity."); }
        }
        std::sort(this->entries.begin(), this->entries.end(), [](const auto& a, const auto& b) {
            return std::less<const LiveSourceIdentity*>{}(a.identity.get(), b.identity.get());
        });
        for (std::size_t index = 1; index < this->entries.size(); ++index) {
            if (this->entries[index - 1].identity == this->entries[index].identity) {
                throw std::invalid_argument("Duplicate live source identity.");
            }
        }
    }
    const PreparedSource* resolve(const LiveSourceIdentity* identity) const noexcept {
        const auto found = std::lower_bound(entries.begin(), entries.end(), identity, [](const auto& entry, const auto* key) {
            return std::less<const LiveSourceIdentity*>{}(entry.identity.get(), key);
        });
        return found != entries.end() && found->identity.get() == identity ? found->source.get() : nullptr;
    }
private:
    std::vector<Entry> entries;
};

}
