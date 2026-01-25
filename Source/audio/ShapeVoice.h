#pragma once
#include <JuceHeader.h>
#include "ShapeSound.h"
#include "DahdsrEnvelope.h"

class OscirenderAudioProcessor;
class ShapeVoice : public juce::SynthesiserVoice {
public:
	ShapeVoice(OscirenderAudioProcessor& p, juce::AudioSampleBuffer& externalAudio, int voiceIndex);

	void prepareToPlay(double sampleRate, int samplesPerBlock);
	bool canPlaySound(juce::SynthesiserSound* sound) override;
	void startNote(int midiNoteNumber, float velocity, juce::SynthesiserSound* sound, int currentPitchWheelPosition) override;
    void updateSound(juce::SynthesiserSound* sound);
	void renderNextBlock(juce::AudioSampleBuffer& outputBuffer, int startSample, int numSamples) override;
	void stopNote(float velocity, bool allowTailOff) override;
	void pitchWheelMoved(int newPitchWheelValue) override;
	void controllerMoved(int controllerNumber, int newControllerValue) override;

	void incrementShapeDrawing();
	double getFrequency();

	// Per-voice effect management
	void initializeEffectsFromGlobal();
	void setPreviewEffect(std::shared_ptr<osci::SimpleEffect> effect);
	void clearPreviewEffect();

	bool renderingSample = false;
private:
	const double MIN_LENGTH_INCREMENT = 0.000001;

	OscirenderAudioProcessor& audioProcessor;
	const int voiceIndex = 0;
	std::vector<std::unique_ptr<osci::Shape>> frame;
	std::atomic<ShapeSound*> sound = nullptr;

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

	DahdsrParams dahdsr;
	DahdsrState envState;

	juce::AudioSampleBuffer& externalAudio;

	// Per-voice effect instances (cloned from global toggleableEffects)
	// Mapped by effect ID so we can use global ordering from toggleableEffects
	std::unordered_map<juce::String, std::shared_ptr<osci::SimpleEffect>> voiceEffectsMap;
	std::shared_ptr<osci::SimpleEffect> voicePreviewEffect;
	
	// Working buffers for per-voice effect processing
	juce::AudioBuffer<float> voiceBuffer;
	juce::AudioBuffer<float> frequencyBuffer;
	juce::AudioBuffer<float> volumeBuffer;
	juce::AudioBuffer<float> frameSyncBuffer;
	bool pendingFrameStart = true;

	void noteStopped();
};
