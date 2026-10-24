#include <JuceHeader.h>
#include "../Source/motion/search/SearchIndex.h"

class MotionSearchTest : public juce::UnitTest {
public:
    MotionSearchTest() : juce::UnitTest("Motion search", "Motion") {}

    static motion::search::Entry entry(juce::String title, motion::search::Kind kind = motion::search::Kind::action, juce::String path = {}, juce::StringArray keywords = {}) {
        motion::search::Entry made;
        made.id = title;
        made.title = std::move(title);
        made.kind = kind;
        made.path = std::move(path);
        made.keywords = std::move(keywords);
        return made;
    }

    static motion::search::Index sample() {
        using motion::search::Kind;
        motion::search::Index index;
        index.entries = {
            entry("Split at playhead", Kind::action, "Clip menu"),
            entry("Show key lanes", Kind::action, "Clip menu"),
            entry("Select all", Kind::action, "Edit menu"),
            entry("Persistence", Kind::setting, "Scope settings › Picture"),
            entry("Hue", Kind::setting, "Scope settings › Picture"),
            entry("Colour", Kind::property, "Properties › Appearance"),
            entry("Spiral", Kind::property, "Properties › Surface", {"surface style"}),
            entry("Export video...", Kind::action, "File menu"),
            entry("Keyboard shortcuts", Kind::help, "Help menu"),
            entry("Polygon", Kind::add, "Add source › Shapes"),
            entry("Sample rate", Kind::setting, "Composition › Format", {"samples"}),
            entry("Loop playback", Kind::action, "Transport menu"),
        };
        return index;
    }

    static juce::String first(const motion::search::Index& index, const juce::String& query) {
        const auto results = index.search(query);
        return results.empty() ? juce::String() : results.front().entry->title;
    }

    static bool finds(const motion::search::Index& index, const juce::String& query, const juce::String& title) {
        const auto results = index.search(query);
        return std::any_of(results.begin(), results.end(), [&](const motion::search::Result& result) { return result.entry->title == title; });
    }

    void runTest() override {
        beginTest("Letters in order match, starts of words count most");
        {
            const auto match = motion::search::fuzzy("sap", "Split at playhead");
            expect(match.has_value());
            expect(match->positions == std::vector<int> {0, 6, 9}, "s, a and p are the starts of the three words");
            expect(!motion::search::fuzzy("xyz", "Split at playhead").has_value());
            expect(motion::search::fuzzy("split", "Split at playhead")->score > motion::search::fuzzy("split", "Show key lanes, plus it").value_or(motion::search::Match {}).score);
        }

        beginTest("Initials find a command");
        {
            const auto index = sample();
            expectEquals(first(index, "sap"), juce::String("Split at playhead"));
            expectEquals(first(index, "skl"), juce::String("Show key lanes"));
        }

        beginTest("A title starting with the query comes first, shorter ones before longer");
        {
            const auto index = sample();
            expectEquals(first(index, "s"), juce::String("Spiral"));
            expectEquals(first(index, "sel"), juce::String("Select all"));
            expectEquals(first(index, "sample"), juce::String("Sample rate"));
        }

        beginTest("Other words for the same thing find it");
        {
            const auto index = sample();
            expect(finds(index, "color", "Colour"), "American spelling");
            expect(finds(index, "trail", "Persistence"), "A plain word for an app term");
            expect(finds(index, "render", "Export video..."), "Another app's name");
            const auto results = index.search("color");
            const auto colour = std::find_if(results.begin(), results.end(), [](const motion::search::Result& result) { return result.entry->title == "Colour"; });
            expect(colour != results.end() && !colour->highlights.empty(), "A synonym in the title is highlighted");
        }

        beginTest("Keywords match and say so");
        {
            const auto index = sample();
            const auto results = index.search("surface style");
            expect(!results.empty() && results.front().entry->title == "Spiral");
            expectEquals(results.front().via, juce::String("surface style"));
        }

        beginTest("A typing mistake still finds it");
        {
            const auto index = sample();
            expectEquals(first(index, "spilt"), juce::String("Split at playhead"));
            expectEquals(first(index, "persitence"), juce::String("Persistence"));
            expect(!finds(index, "zzzz", "Persistence"));
        }

        beginTest("Every word must match, in any order");
        {
            const auto index = sample();
            expectEquals(first(index, "playhead split"), juce::String("Split at playhead"));
            expect(!finds(index, "split loop", "Split at playhead"));
        }

        beginTest("Where it lives narrows the search");
        {
            const auto index = sample();
            expectEquals(first(index, "scope hue"), juce::String("Hue"));
            expectEquals(first(index, "surface spiral"), juce::String("Spiral"));
        }

        beginTest("Scattered letters don't count as a match");
        {
            const auto index = sample();
            expect(!finds(index, "spi", "Sample rate"));
            expect(!finds(index, "ae", "Select all"), "Two letters must start a word");
        }

        beginTest("Recent picks rise and lead an empty search");
        {
            auto index = sample();
            index.used("Loop playback");
            index.used("Hue");
            const auto empty = index.search({});
            expect(empty.size() == index.entries.size(), "An empty search lists everything");
            expectEquals(empty[0].entry->title, juce::String("Hue"));
            expectEquals(empty[1].entry->title, juce::String("Loop playback"));
            expectEquals(first(index, "l"), juce::String("Loop playback"));
            for (int count = 0; count < 30; ++count) {
                index.used("id " + juce::String(count));
            }
            expectEquals(index.recent.size(), motion::search::Index::mostRecent);
        }

        beginTest("What can't run now is still found, with the reason, and sinks");
        {
            auto index = sample();
            index.entries[0].unavailable = [] { return juce::String("Select a clip first"); };
            index.entries.push_back(entry("Split view"));
            const auto results = index.search("split");
            expectEquals(results.front().entry->title, juce::String("Split view"));
            expectEquals(results[1].unavailable, juce::String("Select a clip first"));
        }

        beginTest("Synonyms expand from a typed start");
        {
            expect(motion::search::synonymsOf("colo").contains("hue"));
            expect(motion::search::synonymsOf("del").contains("remove"));
            expect(motion::search::synonymsOf("x").isEmpty());
        }
    }
};

static MotionSearchTest motionSearchTest;
