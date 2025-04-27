#pragma once
#include <JuceHeader.h>

class OscirenderAudioProcessor;
class TextParser {
public:
	TextParser(OscirenderAudioProcessor &p, juce::String text, juce::Font font);
	~TextParser();

	std::vector<std::unique_ptr<osci::Shape>> draw();
    
private:
    void parse(juce::String text, juce::Font font);
    juce::AttributedString parseFormattedText(const juce::String& text, juce::Font font);
    void processFormattedTextBody(const juce::String& text, juce::AttributedString& result, juce::Font font);
    
    OscirenderAudioProcessor &audioProcessor;
	std::vector<std::unique_ptr<osci::Shape>> shapes;
    juce::Font lastFont;
    juce::String text;
    juce::AttributedString attributedString;
};
