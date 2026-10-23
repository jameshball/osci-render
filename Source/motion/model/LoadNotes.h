#pragma once

#include <JuceHeader.h>
#include <vector>

namespace motion {
// What loading a project left out or put right, so a project saved by
// another version opens with everything this one can use, and whoever opened
// it is told what is missing. Notes of one kind are counted together.
class LoadNotes {
public:
    // `what` names the kind of thing ("Clip property this version doesn't
    // have"); `detail` which one ("hilbert").
    void note(const juce::String& what, const juce::String& detail = {}) {
        auto found = std::find_if(kinds.begin(), kinds.end(), [&](const Kind& kind) { return kind.what == what; });
        if (found == kinds.end()) { found = kinds.insert(kinds.end(), Kind {what, 0, {}}); }
        ++found->count;
        if (detail.isNotEmpty()) { found->details.addIfNotAlreadyThere(detail); }
    }
    bool empty() const { return kinds.empty(); }
    int count() const {
        int total = 0;
        for (const auto& kind : kinds) { total += kind.count; }
        return total;
    }
    // A line for each kind: "Clip property this version doesn't have (×5):
    // hilbert, sierpinski".
    juce::StringArray lines() const {
        juce::StringArray result;
        for (const auto& kind : kinds) {
            auto line = kind.what + (kind.count > 1 ? juce::String(juce::CharPointer_UTF8(" (\xc3\x97")) + juce::String(kind.count) + ")" : juce::String());
            if (!kind.details.isEmpty()) {
                constexpr int shown = 4;
                juce::StringArray named;
                for (int index = 0; index < std::min(shown, kind.details.size()); ++index) { named.add(kind.details[index]); }
                line << ": " << named.joinIntoString(", ") << (kind.details.size() > shown ? juce::String(juce::CharPointer_UTF8(", \xe2\x80\xa6")) : juce::String());
            }
            result.add(line);
        }
        return result;
    }

private:
    struct Kind {
        juce::String what;
        int count = 0;
        juce::StringArray details;
    };
    std::vector<Kind> kinds;
};
}
