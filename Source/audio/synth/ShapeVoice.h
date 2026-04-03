#pragma once
#include <JuceHeader.h>
#include "VoiceManager.h"
#include "ShapeSound.h"
#include "../modulation/DahdsrEnvelope.h"
#include "../modulation/EnvState.h"
#include "../../lua/LuaParser.h"

class OscirenderAudioProcessor;
class ShapeVoice : public juce::SynthesiserVoice {
public:
	ShapeVoice(OscirenderAudioProcessor& p, juce::AudioSampleBuffer& externalAudio, int voiceIndex);

	void prepareToPlay(double sampleRate, int samplesPerBlock);
	bool canPlaySound(juce::SynthesiserSound* sound) override;

	// JUCE SynthesiserVoice overrides — startNote/stopNote are no longer called
	// by VoiceManager but must be implemented for the interface. The actual
	// lifecycle is driven by voiceActivated/voiceDeactivated/voiceKilled.
	void startNote(int midiNoteNumber, float velocity, juce::SynthesiserSound* sound, int currentPitchWheelPosition) override;
    void updateSound(juce::SynthesiserSound* sound);
	void renderNextBlock(juce::AudioSampleBuffer& outputBuffer, int startSample, int numSamples) override;
	void stopNote(float velocity, bool allowTailOff) override;
	void pitchWheelMoved(int newPitchWheelValue) override;
	void controllerMoved(int controllerNumber, int newControllerValue) override;

	void incrementShapeDrawing();
	double getFrequency();

	// VoiceManager lifecycle callbacks (called by OscirenderAudioProcessor)
	void voiceActivated(const VoiceState& vs, bool isLegato);
	void voiceDeactivated();
	void voiceKilled();

	// Drawing state transfer for voice stealing
	void captureDrawingState();
	void restoreDrawingState(const ShapeVoice* source);

	// Voice silence detection (for VoiceManager's voice-killer)
	bool isSilent() const { return !currentlyPlaying; }

	// Kill-fade duration — voices fade out over this time instead of stopping instantly.
	static constexpr float kKillFadeTimeSec = 0.005f; // 5ms

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
	int currentMidiNote = -1;

	bool killFading = false;
	float killFadeGain = 1.0f;
	double frequency = 1.0;
	std::atomic<double> actualFrequency = 1.0;
    double velocity = 1.0;
	double pitchWheelAdjustment = 1.0;
	int rawPitchWheelValue = 8192;

	// Glide (portamento) state
	double glideSourceFreq = 0.0;
	double glideTargetFreq = 0.0;
	double glideElapsed = 0.0;
	double glideDuration = 0.0;
	float glideSlopePower = 0.0f;
	bool glideActive = false;

	// Snapshot of drawing state, captured before voice kill so a replacement
	// voice can resume from the same position.
	struct DrawingStateSnapshot {
		int currentShape = 0;
		double shapeDrawn = 0.0;
		double frameDrawn = 0.0;
	} savedDrawingState;

	lua_State* L = nullptr;
	LuaVariables vars;

	DahdsrParams dahdsr;
	DahdsrState envState;
	// Modulation envelopes 1..NUM_ENVELOPES-1; envelope 0 is envState above
	DahdsrParams envDahdsr[NUM_ENVELOPES];
	DahdsrState envStates[NUM_ENVELOPES];

	juce::AudioSampleBuffer& externalAudio;

	// Per-voice effect instances (cloned from global toggleableEffects)
	// Mapped by effect ID so we can use global ordering from toggleableEffects
	std::unordered_map<juce::String, std::shared_ptr<osci::SimpleEffect>> voiceEffectsMap;
	std::shared_ptr<osci::SimpleEffect> voicePreviewEffect;
	
	// Working buffers for per-voice effect processing
	juce::AudioBuffer<float> voiceBuffer;
	juce::AudioBuffer<float> frequencyBuffer;
	juce::AudioBuffer<float> envelopeBuffer;
	juce::AudioBuffer<float> frameSyncBuffer;
	bool pendingFrameStart = true;
	bool pendingNoteOn = false;

	void noteStopped();
};
