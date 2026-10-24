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
inline int boundaryBonus(const juce::String& text, int index) {
    if (index == 0) {
        return 10;
    }
    const auto previous = text[index - 1], current = text[index];
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
// `limit` when it is over it.
inline int editDistance(const juce::String& a, const juce::String& b, int limit) {
    const auto n = a.length(), m = b.length();
    if (std::abs(n - m) > limit) {
        return limit + 1;
    }
    std::vector<std::vector<int>> d(static_cast<size_t>(n + 1), std::vector<int>(static_cast<size_t>(m + 1)));
    for (int i = 0; i <= n; ++i) {
        d[static_cast<size_t>(i)][0] = i;
    }
    for (int j = 0; j <= m; ++j) {
        d[0][static_cast<size_t>(j)] = j;
    }
    for (int i = 1; i <= n; ++i) {
        for (int j = 1; j <= m; ++j) {
            const auto cost = a[i - 1] == b[j - 1] ? 0 : 1;
            auto best = std::min({d[static_cast<size_t>(i - 1)][static_cast<size_t>(j)] + 1, d[static_cast<size_t>(i)][static_cast<size_t>(j - 1)] + 1, d[static_cast<size_t>(i - 1)][static_cast<size_t>(j - 1)] + cost});
            if (i > 1 && j > 1 && a[i - 1] == b[j - 2] && a[i - 2] == b[j - 1]) {
                best = std::min(best, d[static_cast<size_t>(i - 2)][static_cast<size_t>(j - 2)] + 1);
            }
            d[static_cast<size_t>(i)][static_cast<size_t>(j)] = best;
        }
    }
    return d[static_cast<size_t>(n)][static_cast<size_t>(m)];
}
}

// The best fuzzy match of `word` in `text`, or none when its letters aren't
// all there in order. Case is ignored.
inline std::optional<Match> fuzzy(const juce::String& word, const juce::String& text) {
    const auto query = word.toLowerCase();
    const auto shown = text.substring(0, 160);
    const auto lower = shown.toLowerCase();
    const auto n = query.length(), m = lower.length();
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
    std::vector<int> best(static_cast<size_t>(n * m), none), from(static_cast<size_t>(n * m), -1);
    for (int j = 0; j < m; ++j) {
        if (lower[j] == query[0]) {
            // Letters skipped before the first match cost a little each.
            best[cell(0, j)] = matchScore + detail::boundaryBonus(shown, j) - std::min(j, 12);
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
            if (lower[j] != query[i]) {
                continue;
            }
            const auto adjacent = best[cell(i - 1, j - 1)] == none ? none : best[cell(i - 1, j - 1)] + runBonus;
            const auto previous = std::max(adjacent, gapped);
            if (previous == none) {
                continue;
            }
            best[cell(i, j)] = previous + matchScore + detail::boundaryBonus(shown, j);
            from[cell(i, j)] = adjacent >= gapped ? j - 1 : gappedFrom;
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

// A near miss of `word` against the words of `text`: one letter wrong,
// missing, extra or swapped (two for longer words), against a whole word or
// its start. Used only when `fuzzy` finds nothing, and scored below it.
inline std::optional<Match> approximate(const juce::String& word, const juce::String& text) {
    const auto query = word.toLowerCase();
    const auto n = query.length();
    if (n < 4) {
        return std::nullopt;
    }
    const auto limit = n >= 7 ? 2 : 1;
    const auto lower = text.toLowerCase();
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
            auto distance = detail::editDistance(query, token, limit);
            for (const auto length : {n - 1, n, n + 1}) {
                if (length >= 3 && length < token.length()) {
                    distance = std::min(distance, detail::editDistance(query, token.substring(0, length), limit));
                }
            }
            if (distance <= limit) {
                Match match;
                match.score = 10 * n - 24 * distance;
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
}
