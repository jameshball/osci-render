#pragma once
#include "../shape/Point.h"
#include <JuceHeader.h>
#include "../shape/Shape.h"

class VideoParser {
public:
	VideoParser(juce::String fileName, juce::MemoryBlock data);
	~VideoParser();
private:
	foleys::VideoEngine videoEngine;
};