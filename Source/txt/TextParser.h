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
    
    OscirenderAudioProcessor &audioProcessor;
	std::vector<std::unique_ptr<Shape>> shapes;
    juce::Font lastFont;
    juce::String text;
};
