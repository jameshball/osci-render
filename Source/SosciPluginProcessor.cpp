/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#include "SosciPluginProcessor.h"
#include "SosciPluginEditor.h"
#include "audio/EffectParameter.h"

//==============================================================================
SosciAudioProcessor::SosciAudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
     : AudioProcessor (BusesProperties().withInput("Input", juce::AudioChannelSet::stereo(), true)
                                        .withInput("Brightness", juce::AudioChannelSet::mono(), true)
                                        .withOutput("Output", juce::AudioChannelSet::stereo(), true))
#endif
    {
    // locking isn't necessary here because we are in the constructor

    allEffects.push_back(parameters.brightnessEffect);
    allEffects.push_back(parameters.intensityEffect);
    allEffects.push_back(parameters.persistenceEffect);
    allEffects.push_back(parameters.hueEffect);

    for (auto effect : allEffects) {
        for (auto effectParameter : effect->parameters) {
            auto parameters = effectParameter->getParameters();
            for (auto parameter : parameters) {
                addParameter(parameter);
            }
        }
    }

    booleanParameters.push_back(parameters.graticuleEnabled);
    booleanParameters.push_back(parameters.smudgesEnabled);
    booleanParameters.push_back(parameters.upsamplingEnabled);
    booleanParameters.push_back(parameters.legacyVisualiserEnabled);
    booleanParameters.push_back(parameters.visualiserFullScreen);

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

SosciAudioProcessor::~SosciAudioProcessor() {}

const juce::String SosciAudioProcessor::getName() const {
    return JucePlugin_Name;
}

bool SosciAudioProcessor::acceptsMidi() const {
   #if JucePlugin_WantsMidiInput
    return true;
   #else
    return false;
   #endif
}

bool SosciAudioProcessor::producesMidi() const {
   #if JucePlugin_ProducesMidiOutput
    return true;
   #else
    return false;
   #endif
}

bool SosciAudioProcessor::isMidiEffect() const {
   #if JucePlugin_IsMidiEffect
    return true;
   #else
    return false;
   #endif
}

double SosciAudioProcessor::getTailLengthSeconds() const {
    return 0.0;
}

int SosciAudioProcessor::getNumPrograms() {
    return 1;   // NB: some hosts don't cope very well if you tell them there are 0 programs,
                // so this should be at least 1, even if you're not really implementing programs.
}

int SosciAudioProcessor::getCurrentProgram() {
    return 0;
}

void SosciAudioProcessor::setCurrentProgram(int index) {
}

const juce::String SosciAudioProcessor::getProgramName(int index) {
    return {};
}

void SosciAudioProcessor::changeProgramName(int index, const juce::String& newName) {}

void SosciAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock) {
	currentSampleRate = sampleRate;
    
    for (auto& effect : allEffects) {
        effect->updateSampleRate(currentSampleRate);
    }
}

void SosciAudioProcessor::releaseResources() {
    // When playback stops, you can use this as an opportunity to free up any
    // spare memory, etc.
}

bool SosciAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    auto numIns  = layouts.getMainInputChannels();
    auto numOuts = layouts.getMainOutputChannels();

    return numIns >= 2 && numOuts >= 2;
}

// effectsLock should be held when calling this
std::shared_ptr<Effect> SosciAudioProcessor::getEffect(juce::String id) {
    for (auto& effect : allEffects) {
        if (effect->getId() == id) {
            return effect;
        }
    }
    return nullptr;
}

// effectsLock should be held when calling this
BooleanParameter* SosciAudioProcessor::getBooleanParameter(juce::String id) {
    for (auto& parameter : booleanParameters) {
        if (parameter->paramID == id) {
            return parameter;
        }
    }
    return nullptr;
}

// effectsLock should be held when calling this
FloatParameter* SosciAudioProcessor::getFloatParameter(juce::String id) {
    for (auto& parameter : floatParameters) {
        if (parameter->paramID == id) {
            return parameter;
        }
    }
    return nullptr;
}

// effectsLock should be held when calling this
IntParameter* SosciAudioProcessor::getIntParameter(juce::String id) {
    for (auto& parameter : intParameters) {
        if (parameter->paramID == id) {
            return parameter;
        }
    }
    return nullptr;
}

void SosciAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) {
    juce::ScopedNoDenormals noDenormals;

    auto input = getBusBuffer(buffer, true, 0);
    auto brightness = getBusBuffer(buffer, true, 1);

    midiMessages.clear();

    auto inputArray = input.getArrayOfWritePointers();
    auto brightnessArray = brightness.getArrayOfWritePointers();

	for (int sample = 0; sample < input.getNumSamples(); ++sample) {
        juce::SpinLock::ScopedLockType scope(consumerLock);

        float x = input.getNumChannels() > 0 ? inputArray[0][sample] : 0.0f;
        float y = input.getNumChannels() > 1 ? inputArray[1][sample] : 0.0f;
        float z = brightness.getNumChannels() > 0 ? brightnessArray[0][sample] : 1.0f;

        Point point = { x, y, z };

        for (auto& effect : allEffects) {
            point = effect->apply(sample, point);
        }

        for (auto consumer : consumers) {
            consumer->write(point);
            consumer->notifyIfFull();
        }
	}
}

//==============================================================================
bool SosciAudioProcessor::hasEditor() const {
    return true; // (change this to false if you choose to not supply an editor)
}

juce::AudioProcessorEditor* SosciAudioProcessor::createEditor() {
    auto editor = new SosciPluginEditor(*this);
    return editor;
}

//==============================================================================
void SosciAudioProcessor::getStateInformation(juce::MemoryBlock& destData) {
    juce::SpinLock::ScopedLockType lock2(effectsLock);

    std::unique_ptr<juce::XmlElement> xml = std::make_unique<juce::XmlElement>("project");
    xml->setAttribute("version", ProjectInfo::versionString);
    auto effectsXml = xml->createNewChildElement("effects");
    for (auto effect : allEffects) {
        effect->save(effectsXml->createNewChildElement("effect"));
    }

    auto booleanParametersXml = xml->createNewChildElement("booleanParameters");
    for (auto parameter : booleanParameters) {
        auto parameterXml = booleanParametersXml->createNewChildElement("parameter");
        parameter->save(parameterXml);
    }

    auto floatParametersXml = xml->createNewChildElement("floatParameters");
    for (auto parameter : floatParameters) {
        auto parameterXml = floatParametersXml->createNewChildElement("parameter");
        parameter->save(parameterXml);
    }

    auto intParametersXml = xml->createNewChildElement("intParameters");
    for (auto parameter : intParameters) {
        auto parameterXml = intParametersXml->createNewChildElement("parameter");
        parameter->save(parameterXml);
    }

    copyXmlToBinary(*xml, destData);
}

void SosciAudioProcessor::setStateInformation(const void* data, int sizeInBytes) {
    std::unique_ptr<juce::XmlElement> xml;

    const uint32_t magicXmlNumber = 0x21324356;
    if (sizeInBytes > 8 && juce::ByteOrder::littleEndianInt(data) == magicXmlNumber) {
        // this is a binary xml format
        xml = getXmlFromBinary(data, sizeInBytes);
    } else {
        // this is a text xml format
        xml = juce::XmlDocument::parse(juce::String((const char*)data, sizeInBytes));
    }

    if (xml.get() != nullptr && xml->hasTagName("project")) {
        juce::SpinLock::ScopedLockType lock2(effectsLock);

        auto effectsXml = xml->getChildByName("effects");
        if (effectsXml != nullptr) {
            for (auto effectXml : effectsXml->getChildIterator()) {
                auto effect = getEffect(effectXml->getStringAttribute("id"));
                if (effect != nullptr) {
                    effect->load(effectXml);
                }
            }
        }

        auto booleanParametersXml = xml->getChildByName("booleanParameters");
        if (booleanParametersXml != nullptr) {
            for (auto parameterXml : booleanParametersXml->getChildIterator()) {
                auto parameter = getBooleanParameter(parameterXml->getStringAttribute("id"));
                if (parameter != nullptr) {
                    parameter->load(parameterXml);
                }
            }
        }

        auto floatParametersXml = xml->getChildByName("floatParameters");
        if (floatParametersXml != nullptr) {
            for (auto parameterXml : floatParametersXml->getChildIterator()) {
                auto parameter = getFloatParameter(parameterXml->getStringAttribute("id"));
                if (parameter != nullptr) {
                    parameter->load(parameterXml);
                }
            }
        }

        auto intParametersXml = xml->getChildByName("intParameters");
        if (intParametersXml != nullptr) {
            for (auto parameterXml : intParametersXml->getChildIterator()) {
                auto parameter = getIntParameter(parameterXml->getStringAttribute("id"));
                if (parameter != nullptr) {
                    parameter->load(parameterXml);
                }
            }
        }
    }
}

double SosciAudioProcessor::getSampleRate() {
    return currentSampleRate;
}


//==============================================================================
// This creates new instances of the plugin..
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SosciAudioProcessor();
}
