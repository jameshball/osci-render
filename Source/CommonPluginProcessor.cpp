/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#include "CommonPluginProcessor.h"
#include "CommonPluginEditor.h"
#include "audio/EffectParameter.h"

//==============================================================================
CommonAudioProcessor::CommonAudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
     : AudioProcessor (BusesProperties().withInput("Input", juce::AudioChannelSet::namedChannelSet(3), true)
                                        .withOutput("Output", juce::AudioChannelSet::stereo(), true))
#endif
{
    // locking isn't necessary here because we are in the constructor

    for (auto effect : visualiserParameters.effects) {
        permanentEffects.push_back(effect);
        effects.push_back(effect);
    }
    
    effects.push_back(visualiserParameters.smoothEffect);
    permanentEffects.push_back(visualiserParameters.smoothEffect);
        
    for (auto parameter : visualiserParameters.booleans) {
        booleanParameters.push_back(parameter);
    }
}

void CommonAudioProcessor::addAllParameters() {
    for (auto effect : effects) {
        for (auto effectParameter : effect->parameters) {
            auto parameters = effectParameter->getParameters();
            for (auto parameter : parameters) {
                addParameter(parameter);
            }
        }
    }
        
    for (auto parameter : booleanParameters) {
        addParameter(parameter);
    }

    for (auto parameter : floatParameters) {
        addParameter(parameter);
    }

    for (auto parameter : intParameters) {
        addParameter(parameter);
    }
}

CommonAudioProcessor::~CommonAudioProcessor() {}

const juce::String CommonAudioProcessor::getName() const {
    return JucePlugin_Name;
}

bool CommonAudioProcessor::acceptsMidi() const {
   #if JucePlugin_WantsMidiInput
    return true;
   #else
    return false;
   #endif
}

bool CommonAudioProcessor::producesMidi() const {
   #if JucePlugin_ProducesMidiOutput
    return true;
   #else
    return false;
   #endif
}

bool CommonAudioProcessor::isMidiEffect() const {
   #if JucePlugin_IsMidiEffect
    return true;
   #else
    return false;
   #endif
}

double CommonAudioProcessor::getTailLengthSeconds() const {
    return 0.0;
}

int CommonAudioProcessor::getNumPrograms() {
    return 1;   // NB: some hosts don't cope very well if you tell them there are 0 programs,
                // so this should be at least 1, even if you're not really implementing programs.
}

int CommonAudioProcessor::getCurrentProgram() {
    return 0;
}

void CommonAudioProcessor::setCurrentProgram(int index) {
}

const juce::String CommonAudioProcessor::getProgramName(int index) {
    return {};
}

void CommonAudioProcessor::changeProgramName(int index, const juce::String& newName) {}

void CommonAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock) {
	currentSampleRate = sampleRate;
    
    for (auto& effect : effects) {
        effect->updateSampleRate(currentSampleRate);
    }
    
    threadManager.prepare(sampleRate, samplesPerBlock);
}

void CommonAudioProcessor::releaseResources() {
    // When playback stops, you can use this as an opportunity to free up any
    // spare memory, etc.
}

bool CommonAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    return true;
}

// effectsLock should be held when calling this
std::shared_ptr<Effect> CommonAudioProcessor::getEffect(juce::String id) {
    for (auto& effect : effects) {
        if (effect->getId() == id) {
            return effect;
        }
    }
    return nullptr;
}

// effectsLock should be held when calling this
BooleanParameter* CommonAudioProcessor::getBooleanParameter(juce::String id) {
    for (auto& parameter : booleanParameters) {
        if (parameter->paramID == id) {
            return parameter;
        }
    }
    return nullptr;
}

// effectsLock should be held when calling this
FloatParameter* CommonAudioProcessor::getFloatParameter(juce::String id) {
    for (auto& parameter : floatParameters) {
        if (parameter->paramID == id) {
            return parameter;
        }
    }
    return nullptr;
}

// effectsLock should be held when calling this
IntParameter* CommonAudioProcessor::getIntParameter(juce::String id) {
    for (auto& parameter : intParameters) {
        if (parameter->paramID == id) {
            return parameter;
        }
    }
    return nullptr;
}

//==============================================================================
bool CommonAudioProcessor::hasEditor() const {
    return true; // (change this to false if you choose to not supply an editor)
}

double CommonAudioProcessor::getSampleRate() {
    return currentSampleRate;
}
