#pragma once
#include <JuceHeader.h>

class PublicSynthesiser : public juce::Synthesiser {
public:
	void publicHandleMidiEvent(const juce::MidiMessage& m) {
        handleMidiEvent(m);
    }
};