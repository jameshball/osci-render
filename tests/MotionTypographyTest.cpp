#include <JuceHeader.h>

// osci-motion's UI text uses only the type styles in MotionStyle.h. This scans
// the Motion sources so a stray font size cannot creep back in.
class MotionTypographyTest : public juce::UnitTest {
public:
    MotionTypographyTest() : juce::UnitTest("Motion typography", "Motion") {}

    void runTest() override {
        beginTest("Motion UI text uses only the MotionStyle type styles");
        const auto root = juce::File(__FILE__).getParentDirectory().getSiblingFile("Source").getChildFile("motion");
        expect(root.isDirectory(), root.getFullPathName());
        // Fonts are defined by MotionStyle; text sources render the user's own font.
        const juce::StringArray allowed {"MotionStyle.h", "TextSettings.h"};
        const juce::StringArray forbidden {"FontOptions(", "juce::Font(", "Font::bold", "boldened(", "italicised(", "withPointHeight("};
        int scanned = 0;
        for (const auto& entry : juce::RangedDirectoryIterator(root, true, "*.h;*.cpp")) {
            const auto file = entry.getFile();
            if (allowed.contains(file.getFileName())) { continue; }
            ++scanned;
            juce::StringArray lines;
            lines.addLines(file.loadFileAsString());
            for (int index = 0; index < lines.size(); ++index) {
                const auto& line = lines[index];
                bool stray = false;
                for (const auto& token : forbidden) { stray = stray || line.contains(token); }
                // setFont with anything other than a style (or the user's text font).
                const auto call = line.indexOf("setFont(");
                if (call >= 0) {
                    const auto argument = line.substring(call + 8);
                    stray = stray || !(argument.contains("style::") || argument.startsWith("settings.font("));
                }
                // A style resized or restyled is a new style.
                for (const auto& style : {"title()", "body()", "caption()", "mono()"}) {
                    const auto at = line.indexOf(style);
                    stray = stray || (at >= 0 && line.substring(at + juce::String(style).length()).startsWith(".with"));
                }
                expect(!stray, file.getFileName() + ":" + juce::String(index + 1) + " uses a font outside MotionStyle: " + line.trim());
            }
        }
        expect(scanned > 20, "scanned the Motion sources");
    }
};

static MotionTypographyTest motionTypographyTest;
