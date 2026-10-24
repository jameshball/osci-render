#pragma once

#include <JuceHeader.h>
#include <algorithm>
#include <climits>
#include <optional>
#include <vector>

namespace motion::search {
// How well one typed word matches a piece of text. The word's letters must
// appear in the text in order; the score rewards letters at the start of the
// text or of a word, letters in a run, and matches that begin early, as an
// editor's quick open does. `positions` are the matched characters, to show.
struct Match {
    int score = 0;
    std::vector<int> positions;
};

namespace detail {
inline bool isSeparator(juce::juce_wchar c) {
    return c == ' ' || c == '-' || c == '_' || c == '/' || c == '.' || c == ':' || c == '(' || c == ')' || c == ',' || c == '&' || c == '+' || c == '\'' || c == 0x203A;
}

// Extra for matching the text's character at `index`: most at the start of
// the text or of a word, some at a capital inside a word or a digit after a
// letter.
inline int boundaryBonus(juce::juce_wchar previous, juce::juce_wchar current, int index) {
    if (index == 0) {
        return 10;
    }
    if (isSeparator(previous)) {
        return 9;
    }
    if (juce::CharacterFunctions::isLowerCase(previous) && juce::CharacterFunctions::isUpperCase(current)) {
        return 6;
    }
    if (juce::CharacterFunctions::isLetter(previous) && juce::CharacterFunctions::isDigit(current)) {
        return 4;
    }
    return 0;
}
inline int boundaryBonus(const juce::String& text, int index) {
    return boundaryBonus(index > 0 ? text[index - 1] : 0, text[index], index);
}

// Whether `word` (lower case) appears in `lower` in order, letter by letter.
inline bool isSubsequence(const juce::String& word, const juce::String& lower) {
    int at = 0;
    for (auto c : word) {
        at = lower.indexOfChar(at, c);
        if (at < 0) {
            return false;
        }
        ++at;
    }
    return true;
}

// The Damerau-Levenshtein distance between `a` and `b`, or more than
// `limit` when it is over it. Three rows, reused between calls.
inline int editDistance(const juce::String& a, const juce::String& b, int limit) {
    const auto n = a.length(), m = b.length();
    if (std::abs(n - m) > limit) {
        return limit + 1;
    }
    thread_local std::vector<int> rows;
    rows.assign(static_cast<size_t>(3 * (m + 1)), 0);
    const auto at = [m](int i, int j) { return static_cast<size_t>((i % 3) * (m + 1) + j); };
    for (int j = 0; j <= m; ++j) {
        rows[at(0, j)] = j;
    }
    for (int i = 1; i <= n; ++i) {
        rows[at(i, 0)] = i;
        for (int j = 1; j <= m; ++j) {
            const auto cost = a[i - 1] == b[j - 1] ? 0 : 1;
            auto best = std::min({rows[at(i - 1, j)] + 1, rows[at(i, j - 1)] + 1, rows[at(i - 1, j - 1)] + cost});
            if (i > 1 && j > 1 && a[i - 1] == b[j - 2] && a[i - 2] == b[j - 1]) {
                best = std::min(best, rows[at(i - 2, j - 2)] + 1);
            }
            rows[at(i, j)] = best;
        }
    }
    return rows[at(n, m)];
}
}

// The best fuzzy match of `query` (lower case) in `shown`, whose lower case
// is `lower`, or none when its letters aren't all there in order.
inline std::optional<Match> fuzzyIn(const juce::String& query, const juce::String& shown, const juce::String& lower) {
    const auto n = query.length(), m = std::min(lower.length(), 160);
    if (n == 0) {
        return Match {};
    }
    if (n > m || n > 48 || !detail::isSubsequence(query, lower)) {
        return std::nullopt;
    }
    // Best score with the query's i-th letter on the text's j-th, where a
    // letter right after the last one's earns a run bonus and a gap costs
    // more to open than to widen.
    constexpr int matchScore = 16, runBonus = 4, gapOpen = 3, gapWiden = 1, none = INT_MIN / 4;
    const auto cell = [m](int i, int j) { return static_cast<size_t>(i * m + j); };
    // The letters as an array (a String's are found by walking it), and what
    // landing on each is worth.
    thread_local std::vector<juce::juce_wchar> letters, typed;
    thread_local std::vector<int> best, from, bonus, carried;
    letters.assign(static_cast<size_t>(m), 0);
    bonus.assign(static_cast<size_t>(m), 0);
    {
        auto lowered = lower.getCharPointer();
        for (int j = 0; j < m; ++j) {
            letters[static_cast<size_t>(j)] = lowered.getAndAdvance();
        }
        auto original = shown.getCharPointer();
        juce::juce_wchar previous = 0;
        for (int j = 0; j < m && !original.isEmpty(); ++j) {
            const auto current = original.getAndAdvance();
            bonus[static_cast<size_t>(j)] = detail::boundaryBonus(previous, current, j);
            previous = current;
        }
    }
    typed.assign(static_cast<size_t>(n), 0);
    {
        auto pointer = query.getCharPointer();
        for (int i = 0; i < n; ++i) {
            typed[static_cast<size_t>(i)] = pointer.getAndAdvance();
        }
    }
    best.assign(static_cast<size_t>(n * m), none);
    from.assign(static_cast<size_t>(n * m), -1);
    // A letter in a run earns the bonus its run began with, as fzf has it,
    // so "loop" in Loop playback stays together rather than jumping to the
    // p of playback.
    carried.assign(static_cast<size_t>(n * m), 0);
    for (int j = 0; j < m; ++j) {
        if (letters[static_cast<size_t>(j)] == typed[0]) {
            // Letters skipped before the first match cost a little each; where
            // the first letter lands counts double, as fzf has it.
            best[cell(0, j)] = matchScore + 2 * bonus[static_cast<size_t>(j)] - std::min(j, 12);
            carried[cell(0, j)] = bonus[static_cast<size_t>(j)];
        }
    }
    for (int i = 1; i < n; ++i) {
        // The best earlier match with at least one letter between, as it
        // stands for the text's j-th letter.
        int gapped = none, gappedFrom = -1;
        for (int j = i; j < m; ++j) {
            if (j >= 2) {
                const auto opened = best[cell(i - 1, j - 2)] == none ? none : best[cell(i - 1, j - 2)] - gapOpen;
                const auto widened = gapped == none ? none : gapped - gapWiden;
                if (opened >= widened) {
                    gapped = opened;
                    gappedFrom = j - 2;
                } else {
                    gapped = widened;
                }
            }
            if (letters[static_cast<size_t>(j)] != typed[static_cast<size_t>(i)]) {
                continue;
            }
            const auto here = bonus[static_cast<size_t>(j)];
            const auto runOn = std::max(here, carried[cell(i - 1, j - 1)]);
            const auto adjacent = best[cell(i - 1, j - 1)] == none ? none : best[cell(i - 1, j - 1)] + runBonus + runOn;
            const auto jumped = gapped == none ? none : gapped + here;
            if (adjacent == none && jumped == none) {
                continue;
            }
            const auto together = adjacent >= jumped;
            best[cell(i, j)] = (together ? adjacent : jumped) + matchScore;
            from[cell(i, j)] = together ? j - 1 : gappedFrom;
            carried[cell(i, j)] = together ? runOn : here;
        }
    }
    int end = -1;
    for (int j = n - 1; j < m; ++j) {
        if (best[cell(n - 1, j)] != none && (end < 0 || best[cell(n - 1, j)] > best[cell(n - 1, end)])) {
            end = j;
        }
    }
    if (end < 0) {
        return std::nullopt;
    }
    Match match;
    match.score = best[cell(n - 1, end)];
    match.positions.resize(static_cast<size_t>(n));
    for (int i = n - 1, j = end; i >= 0; --i) {
        match.positions[static_cast<size_t>(i)] = j;
        j = from[cell(i, j)];
    }
    // A word the text starts with is the strongest hint of all.
    if (lower.startsWith(query)) {
        match.score += 12;
    }
    return match;
}

// The best fuzzy match of `word` in `text`, or none when its letters aren't
// all there in order. Case is ignored.
inline std::optional<Match> fuzzy(const juce::String& word, const juce::String& text) {
    return fuzzyIn(word.toLowerCase(), text, text.toLowerCase());
}

// A near miss of `word` against the words of `text`: one letter wrong,
// missing, extra or swapped (two for words of nine letters or more), against a whole word or
// its start. Used only when `fuzzy` finds nothing, and scored below it.
inline std::optional<Match> approximateIn(const juce::String& query, const juce::String& lower) {
    const auto n = query.length();
    if (n < 4) {
        return std::nullopt;
    }
    const auto limit = n >= 9 ? 2 : 1;
    std::optional<Match> found;
    for (int start = 0; start < lower.length();) {
        while (start < lower.length() && detail::isSeparator(lower[start])) {
            ++start;
        }
        auto end = start;
        while (end < lower.length() && !detail::isSeparator(lower[end])) {
            ++end;
        }
        const auto token = lower.substring(start, end);
        if (token.length() >= 3) {
            // The whole word, or its start as long as the query (so a typo
            // in a word not yet typed out still finds it).
            const auto whole = detail::editDistance(query, token, limit);
            auto distance = whole;
            for (const auto length : {n - 1, n, n + 1}) {
                if (length >= 3 && length < token.length()) {
                    distance = std::min(distance, detail::editDistance(query, token.substring(0, length), limit));
                }
            }
            if (distance <= limit) {
                Match match;
                // A miss of a whole word ("glwo", Glow) beats one of a word's
                // start ("glwo", global).
                match.score = 10 * n - 24 * distance + (whole == distance ? 8 : 0);
                for (int index = start; index < start + std::min(n, token.length()); ++index) {
                    match.positions.push_back(index);
                }
                if (!found.has_value() || match.score > found->score) {
                    found = std::move(match);
                }
            }
        }
        start = end;
    }
    return found;
}

inline std::optional<Match> approximate(const juce::String& word, const juce::String& text) {
    return approximateIn(word.toLowerCase(), text.toLowerCase());
}
}
