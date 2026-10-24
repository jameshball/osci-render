#include <JuceHeader.h>
#include "../Source/motion/search/SearchIndex.h"
#include "../Source/motion/search/Vocabulary.h"

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
            entry("Persistence", Kind::setting, juce::String::fromUTF8("Scope settings › Picture")),
            entry("Hue", Kind::setting, juce::String::fromUTF8("Scope settings › Picture")),
            entry("Colour", Kind::property, juce::String::fromUTF8("Properties › Appearance")),
            entry("Spiral", Kind::property, juce::String::fromUTF8("Properties › Surface"), {"surface style"}),
            entry("Export video...", Kind::action, "File menu"),
            entry("Keyboard shortcuts", Kind::help, "Help menu"),
            entry("Polygon", Kind::add, juce::String::fromUTF8("Add source › Shapes")),
            entry("Sample rate", Kind::setting, juce::String::fromUTF8("Composition › Format"), {"samples"}),
            entry("Loop playback", Kind::action, "Transport menu"),
        };
        return index;
    }

    static juce::String first(const motion::search::Index& index, const juce::String& query) {
        const auto results = index.search(query).results;
        return results.empty() ? juce::String() : results.front().entry->title;
    }

    static bool finds(const motion::search::Index& index, const juce::String& query, const juce::String& title) {
        const auto results = index.search(query).results;
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
            expectEquals(first(index, "s"), juce::String("Split at playhead"), "Ties keep the listed order");
            expectEquals(first(index, "sel"), juce::String("Select all"));
            expectEquals(first(index, "sample"), juce::String("Sample rate"));
        }

        beginTest("Other words for the same thing find it");
        {
            const auto index = sample();
            expect(finds(index, "color", "Colour"), "American spelling");
            expect(finds(index, "trail", "Persistence"), "A plain word for an app term");
            expect(finds(index, "render", "Export video..."), "Another app's name");
            const auto results = index.search("color").results;
            const auto colour = std::find_if(results.begin(), results.end(), [](const motion::search::Result& result) { return result.entry->title == "Colour"; });
            expect(colour != results.end() && colour->highlights.empty() && colour->via == "color", "Another word for it marks none of the title, and says what found it");
        }

        beginTest("Keywords match and say so");
        {
            const auto index = sample();
            const auto results = index.search("surface style").results;
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
            const auto both = index.search("split loop");
            expect(both.note.isNotEmpty(), "No entry has both words: those with one, said so");
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
            auto more = sample();
            more.entries.push_back(entry("View from front", motion::search::Kind::action, "Scene"));
            more.entries.push_back(entry("Fit timeline to composition", motion::search::Kind::action, "View"));
            more.entries.push_back(entry("Scope settings", motion::search::Kind::place, "Scope"));
            expect(!finds(more, "rot", "View from front"), "Letters starting mid-word must run together");
            expect(!finds(more, "ron", "View from front"), "Three letters inside a word are too few");
            expect(finds(more, "ront", "View from front"), "...four run together are enough");
            expect(!finds(more, "colour", "Fit timeline to composition"), "A synonym (tint) isn't matched letter by letter");
            expect(!finds(more, "rotate", "Scope settings"), "Nor is spin");
        }

        beginTest("Recent picks rise and lead an empty search");
        {
            auto index = sample();
            index.used("Loop playback");
            index.used("Hue");
            const auto empty = index.search({}).results;
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
            const auto results = index.search("split").results;
            expectEquals(results.front().entry->title, juce::String("Split view"));
            expectEquals(results[1].unavailable, juce::String("Select a clip first"));
        }

        beginTest("Real queries find what they mean, and only that");
        {
            using motion::search::Kind;
            motion::search::Index index;
            index.entries = {
                entry("Free look", Kind::action, "Scene"),
                entry("Look through camera", Kind::action, "Scene"),
                entry("Loop playback", Kind::action, "Transport"),
                entry("Beam frequency", Kind::setting, "Composition", {"hz", "refresh"}),
                entry("Play slower", Kind::action, "Transport"),
                entry("Play faster", Kind::action, "Transport"),
                entry("Glow", Kind::setting, juce::String::fromUTF8("Scope › Picture")),
                entry("Composition settings", Kind::place, "Composition"),
                entry("Make composition", Kind::action, "Clip"),
                entry("Position", Kind::property, juce::String::fromUTF8("Road › Transform")),
                entry("Frame selection", Kind::action, "Scene", {"zoom to fit", "fit view"}),
                entry("Stretch tool", Kind::action, "Timeline", {"time stretch", "speed"}),
            };
            expect(finds(index, "loop", "Loop playback"));
            expect(!finds(index, "loop", "Free look") && !finds(index, "loop", "Look through camera"), "No near misses beside a real match");
            expect(!finds(index, "loop", "Beam frequency"));
            expectEquals(first(index, "glow"), juce::String("Glow"));
            expect(!finds(index, "glow", "Play slower"));
            expect(!finds(index, "rate", "Play slower"), "Rate is no word for speed");
            expect(!finds(index, "blur", "Frame selection"), "Nor is focus for blur");
            expectEquals(first(index, "position"), juce::String("Position"), "A word start beats one inside composition");
            expectEquals(first(index, "how do i make it glow"), juce::String("Glow"), "The filler of a sentence is ignored");
            expectEquals(first(index, "glwo"), juce::String("Glow"), "A near miss, once nothing matches as typed");
            const auto some = index.search("glow zzzz");
            expect(some.note.isNotEmpty() && !some.results.empty() && some.results.front().entry->title == "Glow", "Some of the words, said so");
            expect(index.search("glow").note.isEmpty());
        }

        beginTest("A name is found by its own words only");
        {
            motion::search::Index index;
            index.entries = {entry("osci-render", motion::search::Kind::add, juce::String::fromUTF8("Add source › Examples › Text"))};
            index.entries.front().literal = true;
            expect(finds(index, "osci", "osci-render"));
            expect(!finds(index, "export", "osci-render"), "Render is another word for export, but not in a name");
        }

        beginTest("A list's own search matches as search does");
        {
            expect(motion::search::Index::matches("color", "Colour"));
            expect(motion::search::Index::matches("rot", "Vortex", {"Strength", "Amount", "Rotation"}));
            expect(!motion::search::Index::matches("blur", "Bulge"));
        }

        beginTest("Other forms of a word find it");
        {
            const auto index = sample();
            expect(finds(index, "trails", "Persistence"), "A plural, through another word for it");
            expectEquals(first(index, "loops"), juce::String("Loop playback"));
            const auto stems = motion::search::Index::stemsOf("spinning");
            expect(stems.contains("spin"), "A doubled letter undone");
            expect(motion::search::Index::stemsOf("brighter").contains("bright"));
            expect(motion::search::Index::stemsOf("fading").contains("fade"));
            expect(!motion::search::Index::stemsOf("small").contains("smal"), "A word's own double letter stays");
            const auto results = index.search("loops").results;
            expect(!results.empty() && !results.front().highlights.empty(), "The form found in the title is marked");
        }

        beginTest("Alike entries show a few, and say how many more");
        {
            using motion::search::Kind;
            motion::search::Index index;
            for (const auto* meter : {"2/4", "3/4", "4/4", "5/4", "6/8"}) {
                index.entries.push_back(entry(juce::String("Meter: ") + meter, Kind::setting, "Composition"));
                index.entries.back().family = "Meter";
            }
            index.entries.push_back(entry("Meter", Kind::setting, "Composition"));
            const auto found = index.search("meter");
            expectEquals(static_cast<int>(found.results.size()), 1 + motion::search::Index::perFamily);
            expectEquals(found.folded, 2);
            expect(found.foldedExample != nullptr);
            expectEquals(first(index, "meter 6/8"), juce::String("Meter: 6/8"), "A word for one of them still finds it");
        }

        beginTest("When only some words match, the telling ones count");
        {
            using motion::search::Kind;
            motion::search::Index index;
            for (const auto* meter : {"2/4", "3/4", "4/4"}) {
                index.entries.push_back(entry(juce::String("Meter: ") + meter, Kind::setting, "Composition", {"bars"}));
            }
            index.entries.push_back(entry("Loop playback", Kind::action, "Transport"));
            for (int filler = 0; filler < 20; ++filler) {
                index.entries.push_back(entry("Thing " + juce::String(filler), Kind::action, "Elsewhere", {"bars"}));
            }
            expectEquals(first(index, "loop 4 bars"), juce::String("Loop playback"), "A number and a common word don't outweigh the rare one");
        }

        beginTest("A word the app knows isn't taken for a typing mistake");
        {
            using motion::search::Kind;
            motion::search::Index index;
            index.entries = {entry("Meter", Kind::setting, "Composition"), entry("Persistence", Kind::setting, "Scope")};
            expect(!finds(index, "mute", "Meter"), "Mute is a word of its own");
            expect(finds(index, "persitence", "Persistence"), "An unknown word may be a mistake");
        }

        beginTest("Longer words don't scatter across a title");
        {
            using motion::search::Kind;
            motion::search::Index index;
            index.entries = {entry("Move start to playhead", Kind::action, "Clip"), entry("Export video...", Kind::action, "File"), entry("Split at playhead", Kind::action, "Clip")};
            expect(!finds(index, "stop", "Move start to playhead"), "s-to-p: single letters past the third");
            expect(finds(index, "exvid", "Export video..."), "Runs of letters at word starts");
            expect(finds(index, "sap", "Split at playhead"), "Three initials");
        }

        beginTest("Golden queries, with the app's own words");
        {
            using motion::search::Kind;
            namespace vocabulary = motion::search::vocabulary;
            motion::search::Index index;
            const auto add = [&index](juce::String title, Kind kind, juce::StringArray keywords, bool unavailable = false) {
                index.entries.push_back(entry(title, kind, {}, keywords));
                if (unavailable) {
                    index.entries.back().unavailable = [] { return juce::String("Select a clip first"); };
                }
            };
            add("Import source...", Kind::action, vocabulary::menuKeywords("Import source..."));
            add("Add Audio loudness modulator", Kind::add, vocabulary::modulatorKeywords("Audio loudness"));
            add("Add Saw modulator", Kind::add, vocabulary::modulatorKeywords("Saw"));
            add("Rotation", Kind::property, vocabulary::propertyKeywords("Rotation"));
            add("Mute Road", Kind::action, {"silence", "hide", "disable"});
            add("Field of view", Kind::property, vocabulary::propertyKeywords("Field of view"));
            add("Intensity", Kind::setting, vocabulary::scopeKeywords("Intensity"));
            add("Glow", Kind::setting, vocabulary::scopeKeywords("Glow"));
            add("Persistence", Kind::setting, vocabulary::scopeKeywords("Persistence"));
            add("Export beam XYRGB (5 ch)...", Kind::action, vocabulary::menuKeywords("Export beam XYRGB (5 ch)..."));
            add("Make it spin", Kind::action, {"spin", "rotate forever"}, true);
            add("Keyframe rotation", Kind::action, vocabulary::menuKeywords("Keyframe rotation"));
            expectEquals(first(index, "add music"), juce::String("Import source..."), "A phrase that is a keyword wins");
            expectEquals(first(index, "spin"), juce::String("Rotation"), "What can run comes before what can't");
            expect(!finds(index, "spin", "Mute Road"), "Turn (for spin) isn't the start of turn off");
            expect(!finds(index, "spin", "Field of view"), "Nor is angle the end of wide angle");
            expect(finds(index, "brighter", "Intensity") && finds(index, "brighter", "Glow"));
            expectEquals(first(index, "longer trails"), juce::String("Persistence"));
            expectEquals(first(index, "export for my laser"), juce::String("Export beam XYRGB (5 ch)..."));
            expectEquals(first(index, "add keyframe"), juce::String("Keyframe rotation"));
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
