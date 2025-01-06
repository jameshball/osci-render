#include "SosciPluginProcessor.h"
#include "SosciPluginEditor.h"
#include "audio/EffectParameter.h"

SosciAudioProcessor::SosciAudioProcessor() {
    addAllParameters();
}

SosciAudioProcessor::~SosciAudioProcessor() {}

void SosciAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) {
    juce::ScopedNoDenormals noDenormals;

    auto input = getBusBuffer(buffer, true, 0);
    auto output = getBusBuffer(buffer, false, 0);
    float EPSILON = 0.0001f;

    midiMessages.clear();

    auto inputArray = input.getArrayOfWritePointers();
    auto outputArray = output.getArrayOfWritePointers();
    
    juce::CriticalSection::ScopedLockType lock2(wavParserLock);
    bool readingFromWav = wavParser != nullptr;
    
	for (int sample = 0; sample < input.getNumSamples(); ++sample) {
        OsciPoint point;
        
        if (readingFromWav) {
            point = wavParser->getSample();
            point.z = 1.0f;
        } else {
            float x = input.getNumChannels() > 0 ? inputArray[0][sample] : 0.0f;
            float y = input.getNumChannels() > 1 ? inputArray[1][sample] : 0.0f;
            float brightness = 1.0f;
            if (input.getNumChannels() > 2 && !forceDisableBrightnessInput) {
                float brightnessChannel = inputArray[2][sample];
                // Only enable brightness if we actually receive a signal on the brightness channel
                if (!brightnessEnabled && brightnessChannel > EPSILON) {
                    brightnessEnabled = true;
                }
                if (brightnessEnabled) {
                    brightness = brightnessChannel;
                }
            }
            
            point = { x, y, brightness };
        }

        for (auto& effect : permanentEffects) {
            point = effect->apply(sample, point);
        }

        // no negative brightness
        point.z = juce::jmax(0.0, juce::jmin(1.0, point.z));

        // this is the point that the visualiser will draw
        threadManager.write(point);

        point.scale(volume, volume, volume);

        // clip
        point.x = juce::jmax(-threshold, juce::jmin(threshold.load(), point.x));
        point.y = juce::jmax(-threshold, juce::jmin(threshold.load(), point.y));
        
        if (output.getNumChannels() > 0) {
            outputArray[0][sample] = point.x;
        }
        if (output.getNumChannels() > 1) {
            outputArray[1][sample] = point.y;
        }
        if (output.getNumChannels() > 2) {
            outputArray[2][sample] = point.z;
        }
	}
}

void SosciAudioProcessor::getStateInformation(juce::MemoryBlock& destData) {
    // we need to stop recording the visualiser when saving the state, otherwise
    // there are issues. This is the only place we can do this because there is
    // no callback when closing the standalone app except for this.
    
    if (haltRecording != nullptr && juce::JUCEApplicationBase::isStandaloneApp()) {
        haltRecording();
    }
    
    juce::SpinLock::ScopedLockType lock2(effectsLock);

    std::unique_ptr<juce::XmlElement> xml = std::make_unique<juce::XmlElement>("project");
    xml->setAttribute("version", ProjectInfo::versionString);
    auto effectsXml = xml->createNewChildElement("effects");
    for (auto effect : effects) {
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

    recordingParameters.save(xml.get());

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

        recordingParameters.load(xml.get());
    }
}

void SosciAudioProcessor::loadAudioFile(const juce::File& file) {
    auto stream = std::make_unique<juce::FileInputStream>(file);
    if (stream->openedOk()) {
        juce::CriticalSection::ScopedLockType lock(wavParserLock);
        wavParser = std::make_unique<WavParser>(*this, std::move(stream));
    }
}

void SosciAudioProcessor::stopAudioFile() {
    juce::CriticalSection::ScopedLockType lock(wavParserLock);
    wavParser = nullptr;
}

juce::AudioProcessorEditor* SosciAudioProcessor::createEditor() {
    auto editor = new SosciPluginEditor(*this);
    return editor;
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new SosciAudioProcessor();
}
