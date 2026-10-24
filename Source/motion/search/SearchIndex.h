#pragma once

#include "FuzzyMatch.h"
#include "Synonyms.h"
#include <cmath>
#include <functional>
#include <map>
#include <set>

namespace motion::search {
// What kind of thing an entry is, in the order the full list shows them.
enum class Kind { action, add, property, setting, object, place, help };

// Between the parts of where an entry lives: "File › Export audio".
inline const juce::String pathSeparator = juce::String::fromUTF8(" \xe2\x80\xba ");

// The word a kind goes by, so typing it narrows to it ("add", "setting").
inline juce::String kindName(Kind kind) {
    switch (kind) {
        case Kind::action: return "Action";
        case Kind::add: return "Add";
        case Kind::property: return "Property";
        case Kind::setting: return "Setting";
        case Kind::object: return "Object";
        case Kind::place: return "Go to";
        case Kind::help: return "Help";
    }
    return {};
}

// A kind's heading when browsing.
inline juce::String kindHeading(Kind kind) {
    switch (kind) {
        case Kind::action: return "Actions";
        case Kind::add: return "Add";
        case Kind::property: return "Properties";
        case Kind::setting: return "Settings";
        case Kind::object: return "In this project";
        case Kind::place: return "Go to";
        case Kind::help: return "Help";
    }
    return {};
}

// A switch's state, or a choice's: the one chosen or another.
enum class State { none, off, on, chosen, other };

// One thing a search can find and do: a command, something to add, a
// property or setting to go to, an object of the project, a place.
struct Entry {
    juce::String id;               // stable across launches, for the recent list
    juce::String title;            // what it does, as the app says it
    Kind kind = Kind::action;
    juce::String path;             // where it lives: "File › Export audio", "Road › Transform"
    juce::StringArray keywords;    // other words for it
    juce::String shortcut;         // its keys, as the menus show them
    std::function<juce::String()> unavailable; // why it can't run now, or empty
    std::function<State()> state;  // asked as it is shown, so always as it is now
    std::function<void()> run;
    int relevance = 0;             // extra for what's in front of the user
    // A name (an example, a recent project, an object): found by its own
    // words only, never by another word for them or a near miss.
    bool literal = false;
    // Entries alike but for a word ("Meter: 3/4", another object's Colour):
    // a search shows a few of them, and says how to find the rest.
    juce::String family;
};

// An entry found by a search, with what to show about why.
struct Result {
    const Entry* entry = nullptr;
    int score = 0;
    std::vector<int> highlights;   // the title's matched characters
    juce::String via;              // the word that matched when the title didn't
    juce::String unavailable;      // why it can't run now
};

// What a search found, and what to say about it: that a near miss or only
// some of the words found it, or that alike entries were left out.
struct Found {
    std::vector<Result> results;
    juce::String note;
    int folded = 0;                     // alike entries left out
    const Entry* foldedExample = nullptr; // the first of them
};

// Everything a search can find, with the ones picked lately first.
class Index {
public:
    std::vector<Entry> entries;
    juce::StringArray recent;      // entry ids, most recent first

    static constexpr int mostRecent = 20, mostResults = 80, perFamily = 3;

    // Moves `id` to the front of the recent list.
    void used(const juce::String& id) {
        recent.removeString(id);
        recent.insert(0, id);
        recent.removeRange(mostRecent, recent.size());
    }

    // Says the entries have changed, so the next search reads them afresh.
    void changed() { prepared.clear(); }

    // Entries matching the query's words, best first: every word, else
    // every word allowing a typing mistake, else the most telling words
    // (saying so). An empty query lists the recent ones, then everything
    // by kind.
    Found search(const juce::String& query) const {
        Found found;
        const auto typed = wordsOf(query);
        if (typed.isEmpty()) {
            for (const auto& id : recent) {
                const auto entry = std::find_if(entries.begin(), entries.end(), [&](const Entry& candidate) { return candidate.id == id; });
                if (entry != entries.end()) {
                    found.results.push_back(resultFor(*entry));
                }
            }
            for (const auto kind : {Kind::action, Kind::add, Kind::property, Kind::setting, Kind::object, Kind::place, Kind::help}) {
                for (const auto& entry : entries) {
                    if (entry.kind == kind && !recent.contains(entry.id)) {
                        found.results.push_back(resultFor(entry));
                    }
                }
            }
            return found;
        }
        prepare();
        std::vector<Word> words;
        for (const auto& word : typed) {
            words.push_back(wordFor(word, &vocabulary));
        }
        found.results = matching(words, Tolerance::exact, found);
        if (found.results.empty()) {
            found.results = matching(words, Tolerance::typos, found);
        }
        if (found.results.empty() && words.size() > 1) {
            found.results = matching(words, Tolerance::someWords, found);
            if (!found.results.empty()) {
                found.note = "Nothing matches every word; these match some";
            }
        }
        return found;
    }

    // The query's words, lower case, without the filler of a sentence
    // ("how do I make it spin" is "spin"); all of them if that is all it is.
    static juce::StringArray wordsOf(const juce::String& query) {
        auto words = juce::StringArray::fromTokens(query.toLowerCase().trim(), " \t", "");
        words.removeEmptyStrings();
        static const juce::StringArray filler {"a", "an", "the", "it", "its", "my", "this", "that", "to", "of", "on", "in", "into", "for", "with", "and",
                                               "how", "do", "does", "i", "me", "can", "could", "please", "want", "make", "change", "is", "be", "get",
                                               "where", "did", "whole", "thing", "things", "some", "go"};
        juce::StringArray meant;
        for (const auto& word : words) {
            if (!filler.contains(word)) {
                meant.add(word);
            }
        }
        return meant.isEmpty() ? words : meant;
    }

    // The other forms a typed word may stand for: "trails" for trail,
    // "brighter" for bright, "spinning" for spin, "fading" for fade.
    static juce::StringArray stemsOf(const juce::String& word) {
        juce::StringArray stems;
        const auto add = [&](const juce::String& stem) {
            if (stem.length() < 3 || stem == word) {
                return;
            }
            stems.addIfNotAlreadyThere(stem);
            // A doubled last letter (spinn, bigg), but not a word's own (small).
            const auto last = stem.getLastCharacter();
            if (stem.length() >= 4 && last == stem[stem.length() - 2] && !juce::String("lsfz").containsChar(last)) {
                stems.addIfNotAlreadyThere(stem.dropLastCharacters(1));
            }
        };
        const auto n = word.length();
        if (n >= 5 && word.endsWith("ies")) {
            add(word.dropLastCharacters(3) + "y");
        }
        if (n >= 5 && word.endsWith("es")) {
            add(word.dropLastCharacters(2));
        }
        if (n >= 4 && word.endsWith("s") && !word.endsWith("ss")) {
            add(word.dropLastCharacters(1));
        }
        if (n >= 6 && word.endsWith("ing")) {
            add(word.dropLastCharacters(3));
            add(word.dropLastCharacters(3) + "e");
        }
        if (n >= 5 && word.endsWith("ed")) {
            add(word.dropLastCharacters(2));
            add(word.dropLastCharacters(1));
        }
        if (n >= 6 && word.endsWith("est")) {
            add(word.dropLastCharacters(3));
        }
        if (n >= 6 && word.endsWith("er")) {
            add(word.dropLastCharacters(2));
            add(word.dropLastCharacters(1));
        }
        if (n >= 6 && word.endsWith("ly")) {
            add(word.dropLastCharacters(2));
        }
        return stems;
    }

    // Whether `text` (with its `keywords`) matches every word of `query`,
    // as a list's own search field asks.
    static bool matches(const juce::String& query, const juce::String& text, const juce::StringArray& keywords = {}) {
        const auto typed = wordsOf(query);
        if (typed.isEmpty()) {
            return true;
        }
        Entry entry;
        entry.title = text;
        entry.keywords = keywords;
        const auto lower = preparedFor(entry);
        for (const auto& word : typed) {
            if (!bestFor(wordFor(word, nullptr), entry, lower, Tolerance::typos).has_value()) {
                return false;
            }
        }
        return true;
    }

private:
    // How loosely the words may match: as typed (or another word for them),
    // with a typing mistake, or only some of them.
    enum class Tolerance { exact, typos, someWords };

    // A typed word with its other forms and other words for it, and whether
    // the app knows it (a word it knows isn't a typing mistake for another).
    struct Word {
        juce::String typed;
        juce::StringArray stems, synonyms;
        bool known = false;
    };

    // An entry's words in lower case, made once rather than each search.
    struct Lower {
        juce::String title, path, kind;
        juce::StringArray keywords;
    };

    struct Match {
        int score = 0;
        bool inTitle = false;
        std::vector<int> positions;
        juce::String via;
    };

    mutable std::vector<Lower> prepared;
    mutable std::set<juce::String> vocabulary;

    static Lower preparedFor(const Entry& entry) {
        Lower lower {entry.title.toLowerCase(), entry.path.toLowerCase(), kindName(entry.kind).toLowerCase(), {}};
        for (const auto& keyword : entry.keywords) {
            lower.keywords.add(keyword.toLowerCase());
        }
        return lower;
    }

    // The entries in lower case, and every word the app uses.
    void prepare() const {
        if (prepared.size() == entries.size()) {
            return;
        }
        prepared.clear();
        vocabulary.clear();
        const auto learn = [this](const juce::String& text) {
            for (const auto& token : juce::StringArray::fromTokens(text, " -_/.:(),&+'", "")) {
                if (token.length() >= 2) {
                    vocabulary.insert(token);
                }
            }
        };
        for (const auto& entry : entries) {
            prepared.push_back(preparedFor(entry));
            learn(prepared.back().title);
            for (const auto& keyword : prepared.back().keywords) {
                learn(keyword);
            }
        }
        for (const auto& group : synonymGroups()) {
            for (const auto& member : group) {
                learn(member);
            }
        }
    }

    static Word wordFor(const juce::String& typed, const std::set<juce::String>* known) {
        Word word {typed, stemsOf(typed), synonymsOf(typed), false};
        for (const auto& stem : word.stems) {
            for (const auto& synonym : synonymsOf(stem)) {
                if (synonym != typed && !word.stems.contains(synonym)) {
                    word.synonyms.addIfNotAlreadyThere(synonym);
                }
            }
        }
        if (known != nullptr) {
            word.known = known->contains(typed) || std::any_of(word.stems.begin(), word.stems.end(), [known](const juce::String& stem) { return known->contains(stem); });
        }
        return word;
    }

    static Result resultFor(const Entry& entry) {
        Result result;
        result.entry = &entry;
        if (entry.unavailable != nullptr) {
            result.unavailable = entry.unavailable();
        }
        return result;
    }

    // A word too common to tell entries apart: a number, or a letter or two.
    static bool weak(const juce::String& word) { return word.length() <= 2 || word.containsOnly("0123456789/.:x"); }

    std::vector<Result> matching(const std::vector<Word>& words, Tolerance tolerance, Found& found) const {
        found.folded = 0;
        found.foldedExample = nullptr;
        const auto count = words.size();
        // Each entry's match for each word.
        std::vector<std::optional<Match>> matched(entries.size() * count);
        std::vector<int> finding(count, 0);
        for (std::size_t index = 0; index < entries.size(); ++index) {
            for (std::size_t word = 0; word < count; ++word) {
                auto& match = matched[index * count + word];
                match = bestFor(words[word], entries[index], prepared[index], tolerance);
                finding[word] += match.has_value() ? 1 : 0;
            }
        }
        // When only some words match, the rarer ones tell more.
        std::vector<float> weights(count, 1.0f);
        if (tolerance == Tolerance::someWords) {
            const auto total = std::log(1.0f + static_cast<float>(entries.size()));
            for (std::size_t word = 0; word < count; ++word) {
                weights[word] = weak(words[word].typed) ? .1f : std::max(.15f, 1.0f - std::log(1.0f + static_cast<float>(finding[word])) / total);
            }
        }
        std::vector<Result> results;
        juce::StringArray typed;
        for (const auto& word : words) {
            typed.add(word.typed);
        }
        const auto whole = typed.joinIntoString(" ");
        for (std::size_t index = 0; index < entries.size(); ++index) {
            const auto& entry = entries[index];
            Result result;
            result.entry = &entry;
            std::size_t hits = 0;
            float weight = 0, score = 0;
            bool allInTitle = true;
            for (std::size_t word = 0; word < count; ++word) {
                const auto& match = matched[index * count + word];
                if (!match.has_value()) {
                    allInTitle = false;
                    continue;
                }
                ++hits;
                weight += weights[word];
                score += static_cast<float>(match->score) * weights[word];
                if (match->inTitle) {
                    result.highlights.insert(result.highlights.end(), match->positions.begin(), match->positions.end());
                } else {
                    allInTitle = false;
                    // The word that found it, unless the title says it already.
                    if (result.via.isEmpty() && !entry.title.containsIgnoreCase(match->via)) {
                        result.via = match->via;
                    }
                }
            }
            if (tolerance == Tolerance::someWords ? weight < .3f : hits < count) {
                continue;
            }
            result.score = juce::roundToInt(score);
            const auto& title = prepared[index].title;
            if (title == whole) {
                result.score += 80;
            } else if (title.startsWith(whole)) {
                // More when it ends where a word does ("export" in Export
                // video) than inside one ("star" in Start screen).
                result.score += detail::isSeparator(title[whole.length()]) ? 40 : 15;
            } else if (count > 1 && prepared[index].keywords.contains(whole)) {
                // Several words that are one of its keywords ("add music"):
                // what they mean together outweighs where each word lands.
                result.score += 120;
            }
            if (allInTitle) {
                result.score += 20;
            }
            result.score += entry.relevance;
            const auto rank = recent.indexOf(entry.id);
            if (rank >= 0) {
                result.score += std::max(0, 30 - 3 * rank);
            }
            if (entry.unavailable != nullptr) {
                result.unavailable = entry.unavailable();
            }
            if (result.unavailable.isNotEmpty()) {
                result.score -= 25;
            }
            std::sort(result.highlights.begin(), result.highlights.end());
            result.highlights.erase(std::unique(result.highlights.begin(), result.highlights.end()), result.highlights.end());
            results.push_back(std::move(result));
        }
        // Ties keep the order entries were listed in (the menus' order); what
        // can run comes before what can't, so Return on the first row acts.
        std::stable_sort(results.begin(), results.end(), [](const Result& a, const Result& b) { return a.score > b.score; });
        std::stable_partition(results.begin(), results.end(), [](const Result& result) { return result.unavailable.isEmpty(); });
        // A few of each family, the best; the rest are counted.
        std::map<juce::String, int> shown;
        std::erase_if(results, [&](const Result& result) {
            const auto& family = result.entry->family;
            if (family.isEmpty() || ++shown[family] <= perFamily) {
                return false;
            }
            ++found.folded;
            if (found.foldedExample == nullptr) {
                found.foldedExample = result.entry;
            }
            return true;
        });
        if (static_cast<int>(results.size()) > mostResults) {
            results.resize(static_cast<size_t>(mostResults));
        }
        return results;
    }

    // A fuzzy match good enough to show. Each letter after the first
    // follows the one before or starts a word (so initials work, as "sap"
    // for Split at playhead), and past three letters each jump lands on two
    // or more together ("stop" isn't sTart TO Playhead). One starting inside
    // a word must run together for four letters or more, and ranks below
    // any that starts a word.
    static std::optional<motion::search::Match> sensible(const juce::String& word, const juce::String& text, const juce::String& lower) {
        auto match = fuzzyIn(word, text, lower);
        if (!match.has_value() || match->positions.empty()) {
            return std::nullopt;
        }
        if (followsRules(word, text, *match)) {
            return match;
        }
        // The best-scored letters break the rules, but the word may still
        // be there whole, starting a word ("ease" in Easy ease in).
        const auto at = startsWord(word, lower);
        if (!at.has_value() || at->empty()) {
            return std::nullopt;
        }
        const auto from = at->front();
        auto whole = fuzzyIn(word, text.substring(from), lower.substring(from));
        if (!whole.has_value()) {
            return std::nullopt;
        }
        for (auto& position : whole->positions) {
            position += from;
        }
        whole->score -= std::min(from, 12);
        return whole;
    }

    static bool followsRules(const juce::String& word, const juce::String& text, motion::search::Match& match) {
        const auto& positions = match.positions;
        int run = 1;
        for (std::size_t index = 1; index < positions.size(); ++index) {
            if (positions[index] == positions[index - 1] + 1) {
                ++run;
                continue;
            }
            if (detail::boundaryBonus(text, positions[index]) < 6) {
                return false;
            }
            if (word.length() > 3 && run == 1 && index > 1) {
                return false;
            }
            run = 1;
        }
        if (word.length() > 3 && run == 1 && positions.size() > 1) {
            return false;
        }
        if (detail::boundaryBonus(text, positions.front()) < 6) {
            const auto together = positions.back() - positions.front() + 1 == static_cast<int>(positions.size());
            if (word.length() < 4 || !together) {
                return false;
            }
            match.score -= 40;
        }
        return true;
    }

    // Extra for letters that are each a word's first, as initials are
    // ("sap" for Split at playhead, beside the run in Save project).
    static int initials(const std::vector<int>& positions, const juce::String& text) {
        if (positions.size() < 2) {
            return 0;
        }
        for (const auto position : positions) {
            if (detail::boundaryBonus(text, position) < 6) {
                return 0;
            }
        }
        return 8 * static_cast<int>(positions.size());
    }

    // Whether `positions` are a whole word of `lower`, not just its start
    // ("star" in Add Star rather than Start screen).
    static bool wholeWord(const std::vector<int>& positions, const juce::String& lower) {
        if (positions.empty() || positions.back() - positions.front() + 1 != static_cast<int>(positions.size())) {
            return false;
        }
        const auto after = positions.back() + 1;
        const auto startsOne = positions.front() == 0 || detail::isSeparator(lower[positions.front() - 1]);
        return startsOne && (after >= lower.length() || detail::isSeparator(lower[after]));
    }

    // Where `word` (or a phrase) begins a word of `lower`, as positions to mark.
    static std::optional<std::vector<int>> startsWord(const juce::String& word, const juce::String& lower) {
        for (int at = lower.indexOf(word); at >= 0; at = lower.indexOf(at + 1, word)) {
            if (at == 0 || detail::isSeparator(lower[at - 1])) {
                std::vector<int> positions;
                for (int index = at; index < at + word.length(); ++index) {
                    positions.push_back(index);
                }
                return positions;
            }
        }
        return std::nullopt;
    }

    // The best way `word` (or another form of it, or another word for it,
    // or a near miss when tolerated) matches `entry`: its title counts
    // most, then its keywords, then where it lives.
    static std::optional<Match> bestFor(const Word& word, const Entry& entry, const Lower& lower, Tolerance tolerance) {
        std::optional<Match> best;
        const auto consider = [&best](int score, bool inTitle, std::vector<int> positions, const juce::String& via) {
            if (!best.has_value() || score > best->score) {
                best = Match {score, inTitle, std::move(positions), via};
            }
        };
        const auto& typed = word.typed;
        // The word itself, or another form of it ("trails"), in the title
        // most, then a keyword, where it lives, and its kind.
        const auto named = sensible(typed, entry.title, lower.title);
        if (named.has_value()) {
            consider(named->score + 10 + (wholeWord(named->positions, lower.title) ? 40 : 0) + initials(named->positions, entry.title), true, named->positions, {});
        }
        for (const auto& stem : word.stems) {
            const auto at = startsWord(stem, lower.title);
            if (at.has_value()) {
                consider(14 * stem.length() * 85 / 100, true, *at, {});
            }
        }
        for (int index = 0; index < lower.keywords.size(); ++index) {
            const auto match = sensible(typed, entry.keywords[index], lower.keywords[index]);
            if (match.has_value()) {
                // A keyword that is the word itself beats one it only starts,
                // and one word of a phrase ("bigger" of "bigger text") counts
                // for less.
                const auto exactly = lower.keywords[index] == typed ? 10 : 0;
                const auto share = lower.keywords[index].containsChar(' ') ? 6 : 8;
                consider(match->score * share / 10 + exactly, false, {}, entry.keywords[index]);
            }
            for (const auto& stem : word.stems) {
                if (startsWord(stem, lower.keywords[index]).has_value()) {
                    consider(14 * stem.length() * 6 / 10, false, {}, entry.keywords[index]);
                }
            }
        }
        // Where it lives and its kind narrow a search by whole words only
        // (a clip's name in the path is no hint).
        if (startsWord(typed, lower.path).has_value()) {
            consider(9 * typed.length(), false, {}, {});
        }
        if (startsWord(typed, lower.kind).has_value()) {
            consider(6 * typed.length(), false, {}, {});
        }
        if (entry.literal) {
            return best;
        }
        // Another word for it counts only as the start of a word, below the
        // word itself; the title isn't marked, as none of it was typed.
        // Only a whole word of the title or a whole keyword: "turn" (for
        // spin) isn't the start of "turn off".
        for (const auto& synonym : word.synonyms) {
            const auto inTitle = startsWord(synonym, lower.title);
            if (inTitle.has_value() && wholeWord(*inTitle, lower.title)) {
                consider(14 * synonym.length() * 6 / 10, false, {}, typed);
            }
            for (const auto& keyword : lower.keywords) {
                if (keyword == synonym) {
                    consider(14 * synonym.length() * 5 / 10, false, {}, typed);
                }
            }
        }
        // Near misses only on the pass for them, and only for a word the
        // app doesn't know (a word it knows is meant as typed).
        if (best.has_value() || tolerance != Tolerance::typos || word.known) {
            return best;
        }
        const auto nearMiss = approximateIn(typed, lower.title);
        if (nearMiss.has_value()) {
            consider(nearMiss->score * 7 / 10, true, nearMiss->positions, {});
        }
        for (int index = 0; index < lower.keywords.size(); ++index) {
            const auto match = approximateIn(typed, lower.keywords[index]);
            if (match.has_value()) {
                consider(match->score * 5 / 10, false, {}, entry.keywords[index]);
            }
        }
        return best;
    }
};
}
