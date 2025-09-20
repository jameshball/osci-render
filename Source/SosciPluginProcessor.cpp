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

    auto input = getBusBuffer(buffer, true, 0);
    auto output = getBusBuffer(buffer, false, 0);
    float EPSILON = 0.0001f;

    midiMessages.clear();

    auto inputArray = input.getArrayOfWritePointers();
    auto outputArray = output.getArrayOfWritePointers();
    
    juce::SpinLock::ScopedLockType lock2(wavParserLock);
    bool readingFromWav = wavParser.isInitialised();
    
	for (int sample = 0; sample < input.getNumSamples(); ++sample) {
        osci::Point point;
        
        if (readingFromWav) {
            point = wavParser.getSample();
        } else {
            float x = input.getNumChannels() > 0 ? inputArray[0][sample] : 0.0f;
            float y = input.getNumChannels() > 1 ? inputArray[1][sample] : 0.0f;
            float zAsBrightnessOrR = 1.0f;
            if (input.getNumChannels() > 2 && !forceDisableBrightnessInput) {
                float zChan = inputArray[2][sample];
                if (!brightnessEnabled && zChan > EPSILON) brightnessEnabled = true;
                if (brightnessEnabled) zAsBrightnessOrR = zChan;
            }
            // RGB detection: if channels 3 or 4 present and not forced off, treat as RGB mode.
            float gChan = 0.0f, bChan = 0.0f;
            bool haveG = input.getNumChannels() > 3;
            bool haveB = input.getNumChannels() > 4;
            if (!forceDisableRgbInput && (haveG || haveB)) {
                float gIn = haveG ? inputArray[3][sample] : 0.0f;
                float bIn = haveB ? inputArray[4][sample] : 0.0f;
                // Enable RGB only when we actually receive signal
                if (!rgbEnabled && (std::abs(gIn) > EPSILON || std::abs(bIn) > EPSILON)) {
                    rgbEnabled = true;
                }
                if (rgbEnabled) {
                    gChan = gIn;
                    bChan = bIn;
                }
            }

            // Build point:
            // - In RGB mode: use z as R, and channels 3/4 as G/B
            // - Otherwise: use z as brightness and leave RGB at defaults
            if (rgbEnabled && !forceDisableRgbInput) {
                point = osci::Point(x, y, 1.0f, zAsBrightnessOrR, gChan, bChan);
            } else {
                point = osci::Point(x, y, zAsBrightnessOrR, 1.0f, 0.0f, 0.0f);
            }
        }

    // Clamp brightness
    point.z = juce::jlimit(0.0, 1.0, point.z);

        for (auto& effect : permanentEffects) {
            point = effect->apply(sample, point);
        }

        // this is the point that the visualiser will draw
        threadManager.write(point, "VisualiserRenderer");

        if (juce::JUCEApplication::isStandaloneApp()) {
            point.scale(volume, volume, 1.0);

            // clip
            point.x = juce::jmax(-threshold, juce::jmin(threshold.load(), point.x));
            point.y = juce::jmax(-threshold, juce::jmin(threshold.load(), point.y));

            // Apply mute if active
            if (muteParameter->getBoolValue()) {
                point.x = 0.0;
                point.y = 0.0;
            }

            // this is the point that the volume component will draw (i.e. post scale/clipping)
            threadManager.write(point, "VolumeComponent");
        }
        
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
