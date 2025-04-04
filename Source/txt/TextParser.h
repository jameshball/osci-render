#pragma once
#include "../shape/OsciPoint.h"
#include <JuceHeader.h>
#include "../shape/Shape.h"

class OscirenderAudioProcessor;
class TextParser {
public:
	TextParser(OscirenderAudioProcessor &p, juce::String text, juce::Font font);
	~TextParser();

	std::vector<std::unique_ptr<Shape>> draw();
    
private:
    void parse(juce::String text, juce::Font font);
    juce::AttributedString parseFormattedText(const juce::String& text, juce::Font font);
    void processFormattedTextBody(const juce::String& text, juce::AttributedString& result, juce::Font font);
    
    OscirenderAudioProcessor &audioProcessor;
	std::vector<std::unique_ptr<Shape>> shapes;
    juce::Font lastFont;
    juce::String text;
    juce::AttributedString attributedString;
};
