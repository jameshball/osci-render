#pragma once
#include <JuceHeader.h>
#include "ShapeSound.h"
#include "../UGen/Env.h"

class OscirenderAudioProcessor;
class ShapeVoice : public juce::SynthesiserVoice {
public:
	ShapeVoice(OscirenderAudioProcessor& p);

	bool canPlaySound(juce::SynthesiserSound* sound) override;
	void startNote(int midiNoteNumber, float velocity, juce::SynthesiserSound* sound, int currentPitchWheelPosition) override;
    void updateSound(juce::SynthesiserSound* sound);
	void renderNextBlock(juce::AudioSampleBuffer& outputBuffer, int startSample, int numSamples) override;
	void stopNote(float velocity, bool allowTailOff) override;
	void pitchWheelMoved(int newPitchWheelValue) override;
	void controllerMoved(int controllerNumber, int newControllerValue) override;

	void extInput(juce::AudioSampleBuffer& inputBuffer);


	void incrementShapeDrawing();
	double getFrequency();

private:
	const double MIN_TRACE = 0.005;
	const double MIN_LENGTH_INCREMENT = 0.000001;

	OscirenderAudioProcessor& audioProcessor;
	std::vector<std::unique_ptr<Shape>> frame;
	std::atomic<ShapeSound*> sound = nullptr;
	double actualTraceMin;
	double actualTraceMax;

	double frameLength = 0.0;
	int currentShape = 0;
	double shapeDrawn = 0.0;
	double frameDrawn = 0.0;
	double lengthIncrement = 0.0;

    bool currentlyPlaying = false;
	double frequency = 1.0;
	std::atomic<double> actualFrequency = 1.0;
    double velocity = 1.0;
	double pitchWheelAdjustment = 1.0;

	lua_State* L = nullptr;
	LuaVariables vars;

	Env adsr;
	double time = 0.0;
	double releaseTime = 0.0;
	double endTime = 99999999;
	bool waitingForRelease = false;

	juce::AudioSampleBuffer exIn;

	void noteStopped();
};
