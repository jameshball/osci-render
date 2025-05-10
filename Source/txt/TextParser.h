#pragma once
#include <JuceHeader.h>

class TextParser {
public:
	TextParser(juce::String text, juce::Font& font);
	~TextParser();

	std::vector<std::unique_ptr<osci::Shape>> draw();
    
private:
    void parse(juce::String text);
    juce::AttributedString parseFormattedText(const juce::String& text, juce::Font font);
    void processFormattedTextBody(const juce::String& text, juce::AttributedString& result, juce::Font font);
    
    juce::Font& font;
	std::vector<std::unique_ptr<osci::Shape>> shapes;
    juce::Font currentFont;
    juce::String text;
    juce::AttributedString attributedString;
};
