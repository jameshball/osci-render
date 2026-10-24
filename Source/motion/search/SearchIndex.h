#pragma once

#include "FuzzyMatch.h"
#include "Synonyms.h"
#include <functional>

namespace motion::search {
// What kind of thing an entry is, in the order the full list shows them.
enum class Kind { action, add, property, setting, place, help };

inline juce::String kindName(Kind kind) {
    switch (kind) {
        case Kind::action: return "Action";
        case Kind::add: return "Add";
        case Kind::property: return "Property";
        case Kind::setting: return "Setting";
        case Kind::place: return "Go to";
        case Kind::help: return "Help";
    }
    return {};
}

// One thing a search can find and do: a command, something to add, a
// property or setting to go to, a place in the app.
struct Entry {
    juce::String id;               // stable across launches, for the recent list
    juce::String title;            // what it does, as the menus say it
    Kind kind = Kind::action;
    juce::String path;             // where it lives: "Clip menu", "Properties › Surface"
    juce::StringArray keywords;    // other words for it
    juce::String shortcut;         // its keys, as the menus show them
    std::function<juce::String()> unavailable; // why it can't run now, or empty
    std::function<int()> toggled;  // a switch's state: 1 on, 0 off, -1 not a switch
    std::function<void()> run;
    int relevance = 0;             // extra for what's in front of the user
};

// An entry found by a search, with what to show about why.
struct Result {
    const Entry* entry = nullptr;
    int score = 0;
    std::vector<int> highlights;   // the title's matched characters
    juce::String via;              // the word that matched when the title didn't
    juce::String unavailable;      // why it can't run now
};

// Everything a search can find, with the ones picked lately first.
class Index {
public:
    std::vector<Entry> entries;
    juce::StringArray recent;      // entry ids, most recent first

    static constexpr int mostRecent = 20, mostResults = 80;

    // Moves `id` to the front of the recent list.
    void used(const juce::String& id) {
        recent.removeString(id);
        recent.insert(0, id);
        recent.removeRange(mostRecent, recent.size());
    }

    // Entries matching every word of `query`, best first. An empty query
    // lists the recent ones, then everything in kind order.
    std::vector<Result> search(const juce::String& query) const {
        const auto words = wordsOf(query);
        std::vector<Result> results;
        if (words.isEmpty()) {
            for (const auto& id : recent) {
                const auto found = std::find_if(entries.begin(), entries.end(), [&](const Entry& entry) { return entry.id == id; });
                if (found != entries.end()) {
                    results.push_back(resultFor(*found, 0));
                }
            }
            for (const auto kind : {Kind::action, Kind::add, Kind::property, Kind::setting, Kind::place, Kind::help}) {
                for (const auto& entry : entries) {
                    if (entry.kind == kind && !recent.contains(entry.id)) {
                        results.push_back(resultFor(entry, 0));
                    }
                }
            }
            return results;
        }
        std::vector<juce::StringArray> alternatives;
        for (const auto& word : words) {
            alternatives.push_back(synonymsOf(word));
        }
        const auto whole = words.joinIntoString(" ");
        for (const auto& entry : entries) {
            auto result = resultFor(entry, 0);
            bool all = true, allInTitle = true;
            for (int index = 0; index < words.size() && all; ++index) {
                const auto found = bestFor(words[index], alternatives[static_cast<size_t>(index)], entry);
                if (!found.has_value()) {
                    all = false;
                    break;
                }
                result.score += found->score;
                if (found->inTitle) {
                    result.highlights.insert(result.highlights.end(), found->positions.begin(), found->positions.end());
                } else {
                    allInTitle = false;
                    if (result.via.isEmpty()) {
                        result.via = found->via;
                    }
                }
            }
            if (!all) {
                continue;
            }
            const auto title = entry.title.toLowerCase();
            if (title == whole) {
                result.score += 80;
            } else if (title.startsWith(whole)) {
                result.score += 40;
            }
            if (allInTitle) {
                result.score += 20;
            }
            result.score += entry.relevance - entry.title.length() / 3;
            const auto rank = recent.indexOf(entry.id);
            if (rank >= 0) {
                result.score += std::max(0, 40 - 4 * rank);
            }
            if (result.unavailable.isNotEmpty()) {
                result.score -= 25;
            }
            std::sort(result.highlights.begin(), result.highlights.end());
            result.highlights.erase(std::unique(result.highlights.begin(), result.highlights.end()), result.highlights.end());
            results.push_back(std::move(result));
        }
        // Ties keep the order entries were listed in.
        std::stable_sort(results.begin(), results.end(), [](const Result& a, const Result& b) { return a.score > b.score; });
        if (static_cast<int>(results.size()) > mostResults) {
            results.resize(static_cast<size_t>(mostResults));
        }
        return results;
    }

    // The query's words, lower case.
    static juce::StringArray wordsOf(const juce::String& query) {
        auto words = juce::StringArray::fromTokens(query.toLowerCase().trim(), " \t", "");
        words.removeEmptyStrings();
        return words;
    }

private:
    struct Found {
        int score = 0;
        bool inTitle = false;
        std::vector<int> positions;
        juce::String via;
    };

    static Result resultFor(const Entry& entry, int score) {
        Result result;
        result.entry = &entry;
        result.score = score;
        if (entry.unavailable != nullptr) {
            result.unavailable = entry.unavailable();
        }
        return result;
    }

    // A fuzzy match good enough to show: a one or two letter word must
    // start a word of the text, and a longer one mustn't be scattered.
    static std::optional<Match> sensible(const juce::String& word, const juce::String& text) {
        auto match = fuzzy(word, text);
        if (!match.has_value() || match->positions.empty()) {
            return std::nullopt;
        }
        if (word.length() <= 2 && detail::boundaryBonus(text, match->positions.front()) < 6) {
            return std::nullopt;
        }
        if (match->score < 10 * word.length()) {
            return std::nullopt;
        }
        return match;
    }

    // The best way `word` (or one of its `synonyms`, or a near miss) matches
    // `entry`: its title counts most, then its keywords, then where it lives.
    static std::optional<Found> bestFor(const juce::String& word, const juce::StringArray& synonyms, const Entry& entry) {
        std::optional<Found> best;
        const auto consider = [&best](int score, bool inTitle, std::vector<int> positions, const juce::String& via) {
            if (!best.has_value() || score > best->score) {
                best = Found {score, inTitle, std::move(positions), via};
            }
        };
        const auto direct = [&](const juce::String& candidate, int weight, bool synonym) {
            const auto viaWord = synonym ? candidate : juce::String();
            auto title = sensible(candidate, entry.title);
            if (title.has_value()) {
                consider(title->score * weight / 10, true, title->positions, viaWord);
            }
            for (const auto& keyword : entry.keywords) {
                const auto match = sensible(candidate, keyword);
                if (match.has_value()) {
                    consider(match->score * weight * 8 / 100, false, {}, keyword);
                }
            }
            const auto path = sensible(candidate, entry.path);
            if (path.has_value()) {
                consider(path->score * weight * 6 / 100, false, {}, {});
            }
            const auto kind = sensible(candidate, kindName(entry.kind));
            if (kind.has_value()) {
                consider(kind->score * weight * 4 / 100, false, {}, {});
            }
        };
        direct(word, 10, false);
        for (const auto& synonym : synonyms) {
            if (synonym.containsChar(' ')) {
                // A phrase is found whole.
                const auto at = entry.title.toLowerCase().indexOf(synonym);
                if (at >= 0) {
                    std::vector<int> positions;
                    for (int index = at; index < at + synonym.length(); ++index) {
                        positions.push_back(index);
                    }
                    consider(12 * synonym.length() * 7 / 10, true, positions, synonym);
                }
                for (const auto& keyword : entry.keywords) {
                    if (keyword.containsIgnoreCase(synonym)) {
                        consider(12 * synonym.length() * 6 / 10, false, {}, keyword);
                    }
                }
                continue;
            }
            direct(synonym, 7, true);
        }
        if (best.has_value()) {
            return best;
        }
        // Typing mistakes, last.
        const auto title = approximate(word, entry.title);
        if (title.has_value()) {
            consider(title->score * 7 / 10, true, title->positions, {});
        }
        for (const auto& keyword : entry.keywords) {
            const auto match = approximate(word, keyword);
            if (match.has_value()) {
                consider(match->score * 5 / 10, false, {}, keyword);
            }
        }
        return best;
    }
};
}
