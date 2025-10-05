#include "SosciPluginProcessor.h"
#include "SosciPluginEditor.h"

SosciAudioProcessor::SosciAudioProcessor() : CommonAudioProcessor(BusesProperties().withInput("Input", juce::AudioChannelSet::namedChannelSet(5), true).withOutput("Output", juce::AudioChannelSet::stereo(), true)) {
    // demo audio file on standalone only
    if (juce::JUCEApplicationBase::isStandaloneApp()) {
        std::unique_ptr<juce::InputStream> stream = std::make_unique<juce::MemoryInputStream>(BinaryData::sosci_flac, BinaryData::sosci_flacSize, false);
        loadAudioFile(std::move(stream));
    }

    addAllParameters();
}

SosciAudioProcessor::~SosciAudioProcessor() {}

void SosciAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) {
    juce::ScopedNoDenormals noDenormals;

    juce::AudioBuffer<float> input = getBusBuffer(buffer, true, 0);
    juce::AudioBuffer<float> output = getBusBuffer(buffer, false, 0);
    const float EPSILON = 0.0001f;
    const int numSamples = input.getNumSamples();

    midiMessages.clear();

    // Get source buffer (either from WAV parser or input)
    juce::AudioBuffer<float> sourceBuffer;
    
    juce::SpinLock::ScopedLockType lock2(wavParserLock);
    bool readingFromWav = wavParser.isInitialised();

    if (readingFromWav) {
        wavBuffer.setSize(6, numSamples, false, true, false);
        wavBuffer.clear();
        wavParser.processBlock(wavBuffer);
        sourceBuffer = juce::AudioBuffer<float>(wavBuffer.getArrayOfWritePointers(), wavBuffer.getNumChannels(), numSamples);
    } else {
        sourceBuffer = juce::AudioBuffer<float>(input.getArrayOfWritePointers(), input.getNumChannels(), numSamples);
    }

    // Resize working buffer with 6 channels: x, y, z/brightness, r, g, b
    workBuffer.setSize(6, numSamples, false, true, false);
    auto sourceArray = sourceBuffer.getArrayOfReadPointers();
    auto workArray = workBuffer.getArrayOfWritePointers();
    
    // Copy X and Y channels
    for (int ch = 0; ch < 2; ++ch) {
        if (sourceBuffer.getNumChannels() > ch) {
            juce::FloatVectorOperations::copy(workArray[ch], sourceArray[ch], numSamples);
        } else {
            juce::FloatVectorOperations::clear(workArray[ch], numSamples);
        }
    }
    
    // Detect brightness mode: check if channel 2 has any signal > EPSILON
    if (!brightnessEnabled && sourceBuffer.getNumChannels() > 2 && !forceDisableBrightnessInput) {
        auto range = juce::FloatVectorOperations::findMinAndMax(sourceArray[2], numSamples);
        if (range.getEnd() > EPSILON) {
            brightnessEnabled = true;
        }
    }
    
    // Detect RGB mode: check if channels 3 or 4 have any signal > EPSILON
    bool haveG = sourceBuffer.getNumChannels() > 3;
    bool haveB = sourceBuffer.getNumChannels() > 4;
    if (!rgbEnabled && !forceDisableRgbInput && (haveG || haveB)) {
        bool hasGSignal = false;
        bool hasBSignal = false;
        
        if (haveG) {
            auto gRange = juce::FloatVectorOperations::findMinAndMax(sourceArray[3], numSamples);
            hasGSignal = std::abs(gRange.getStart()) > EPSILON || std::abs(gRange.getEnd()) > EPSILON;
        }
        if (haveB) {
            auto bRange = juce::FloatVectorOperations::findMinAndMax(sourceArray[4], numSamples);
            hasBSignal = std::abs(bRange.getStart()) > EPSILON || std::abs(bRange.getEnd()) > EPSILON;
        }
        
        if (hasGSignal || hasBSignal) {
            rgbEnabled = true;
        }
    }
    
    // Populate remaining channels based on detected mode
    if (rgbEnabled && !forceDisableRgbInput) {
        // RGB mode: z=1.0, r=ch2, g=ch3, b=ch4
        juce::FloatVectorOperations::fill(workArray[2], 1.0f, numSamples);
        
        if (sourceBuffer.getNumChannels() > 2) {
            juce::FloatVectorOperations::copy(workArray[3], sourceArray[2], numSamples);
        } else {
            juce::FloatVectorOperations::fill(workArray[3], 1.0f, numSamples);
        }
        
        if (haveG) {
            juce::FloatVectorOperations::copy(workArray[4], sourceArray[3], numSamples);
        } else {
            juce::FloatVectorOperations::clear(workArray[4], numSamples);
        }
        
        if (haveB) {
            juce::FloatVectorOperations::copy(workArray[5], sourceArray[4], numSamples);
        } else {
            juce::FloatVectorOperations::clear(workArray[5], numSamples);
        }
    } else {
        // Brightness mode: z=ch2 or 1.0, r=1.0, g=0, b=0
        if (brightnessEnabled && sourceBuffer.getNumChannels() > 2) {
            juce::FloatVectorOperations::copy(workArray[2], sourceArray[2], numSamples);
        } else {
            juce::FloatVectorOperations::fill(workArray[2], 1.0f, numSamples);
        }
        
        juce::FloatVectorOperations::fill(workArray[3], 1.0f, numSamples);
        juce::FloatVectorOperations::clear(workArray[4], numSamples);
        juce::FloatVectorOperations::clear(workArray[5], numSamples);
    }
    
    // Clamp brightness channel
    juce::FloatVectorOperations::clip(workBuffer.getWritePointer(2), workBuffer.getReadPointer(2), 0.0f, 1.0f, numSamples);

    // Apply effects
    juce::AudioBuffer<float> effectBuffer(workBuffer.getArrayOfWritePointers(), 3, numSamples);
    for (auto& effect : permanentEffects) {
        effect->processBlock(effectBuffer, midiMessages);
    }

    // Process output sample-by-sample for visualiser, volume, clipping
    auto outputArray = output.getArrayOfWritePointers();
    
    for (int sample = 0; sample < numSamples; ++sample) {
        osci::Point point(workArray[0][sample], workArray[1][sample], workArray[2][sample], 
                         workArray[3][sample], workArray[4][sample], workArray[5][sample]);
        threadManager.write(point, "VisualiserRenderer");
    }

    if (juce::JUCEApplication::isStandaloneApp()) {
        // Scale output by volume
        juce::FloatVectorOperations::multiply(workArray[0], workArray[0], volume.load(), numSamples);
        juce::FloatVectorOperations::multiply(workArray[1], workArray[1], volume.load(), numSamples);

        // Hard clip to threshold
        float thresholdVal = threshold.load();
        juce::FloatVectorOperations::clip(workArray[0], workArray[0], -thresholdVal, thresholdVal, numSamples);
        juce::FloatVectorOperations::clip(workArray[1], workArray[1], -thresholdVal, thresholdVal, numSamples);

        // apply mute if active
        if (muteParameter->getBoolValue()) {
            juce::FloatVectorOperations::clear(workArray[0], numSamples);
            juce::FloatVectorOperations::clear(workArray[1], numSamples);
        }

        // Copy to output for all channels available from work buffer
        for (int ch = 0; ch < output.getNumChannels(); ++ch) {
            if (workBuffer.getNumChannels() > ch) {
                juce::FloatVectorOperations::copy(outputArray[ch], workArray[ch], numSamples);
            } else {
                juce::FloatVectorOperations::clear(outputArray[ch], numSamples);
            }
        }

        for (int sample = 0; sample < numSamples; ++sample) {
            osci::Point point(workArray[0][sample], workArray[1][sample], workArray[2][sample], 
                            workArray[3][sample], workArray[4][sample], workArray[5][sample]);
            threadManager.write(point, "VolumeComponent");
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
    
    saveProperties(*xml);

    copyXmlToBinary(*xml, destData);
}

void SosciAudioProcessor::setStateInformation(const void* data, int sizeInBytes) {
    if (juce::JUCEApplicationBase::isStandaloneApp() && programCrashedAndUserWantsToReset()) {
        return;
    }

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
        
        loadProperties(*xml);
    }
}

juce::AudioProcessorEditor* SosciAudioProcessor::createEditor() {
    auto editor = new SosciPluginEditor(*this);
    return editor;
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new SosciAudioProcessor();
}
