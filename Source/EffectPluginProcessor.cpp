#include "EffectPluginProcessor.h"
#include "EffectPluginEditor.h"

//==============================================================================
EffectAudioProcessor::EffectAudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
     : AudioProcessor(
        BusesProperties()
        .withInput("Input", juce::AudioChannelSet::stereo(), true)
        .withOutput("Output", juce::AudioChannelSet::stereo(), true)
    )
#endif
{
    effects.push_back(bitCrush);
    
    for (auto effect : visualiserParameters.effects) {
        effects.push_back(effect);
    }

    for (auto effect : effects) {
        for (auto effectParameter : effect->parameters) {
            auto parameters = effectParameter->getParameters();
            for (auto parameter : parameters) {
                addParameter(parameter);
            }
        }
    }
    
    // Initialize title shapes
    juce::Font titleFont = juce::Font(1.0f, juce::Font::bold);
    TextParser titleParser{"bit crush", titleFont};
    auto titleShapes = titleParser.draw();
    titleRenderer.setShapes(std::move(titleShapes));
}

const juce::String EffectAudioProcessor::getName() const {
    return JucePlugin_Name;
}

bool EffectAudioProcessor::acceptsMidi() const {
   #if JucePlugin_WantsMidiInput
    return true;
   #else
    return false;
   #endif
}

bool EffectAudioProcessor::producesMidi() const {
   #if JucePlugin_ProducesMidiOutput
    return true;
   #else
    return false;
   #endif
}

bool EffectAudioProcessor::isMidiEffect() const {
   #if JucePlugin_IsMidiEffect
    return true;
   #else
    return false;
   #endif
}

double EffectAudioProcessor::getTailLengthSeconds() const {
    return 0.0;
}

int EffectAudioProcessor::getNumPrograms() {
    return 1;   // NB: some hosts don't cope very well if you tell them there are 0 programs,
                // so this should be at least 1, even if you're not really implementing programs.
}

int EffectAudioProcessor::getCurrentProgram() {
    return 0;
}

void EffectAudioProcessor::setCurrentProgram(int index) {
}

const juce::String EffectAudioProcessor::getProgramName(int index) {
    return {};
}

void EffectAudioProcessor::changeProgramName(int index, const juce::String& newName) {}

void EffectAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock) {
    for (auto& effect : effects) {
        effect->updateSampleRate(sampleRate);
    }
    
    currentSampleRate = sampleRate;
    titleRenderer.setSampleRate(sampleRate);
    
    threadManager.prepare(sampleRate, samplesPerBlock);
}

void EffectAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) {
    juce::ScopedNoDenormals noDenormals;

    auto input = getBusBuffer(buffer, true, 0);
    auto output = getBusBuffer(buffer, false, 0);
    float EPSILON = 0.0001f;

    midiMessages.clear();

    auto inputArray = input.getArrayOfWritePointers();
    auto outputArray = output.getArrayOfWritePointers();
    
	for (int sample = 0; sample < input.getNumSamples(); ++sample) {
        float x = input.getNumChannels() > 0 ? inputArray[0][sample] : 0.0f;
        float y = input.getNumChannels() > 1 ? inputArray[1][sample] : 0.0f;
        
        osci::Point point = { x, y, 1.0f };

        for (auto& effect : effects) {
            point = effect->apply(sample, point);
        }
        
        osci::Point audioGainPoint = autoGain->apply(sample, point);
        double limit = 0.95;
        audioGainPoint.scale(limit, limit, 1.0); // scale the point to fit the screen
        audioGainPoint.x = juce::jlimit(-limit, limit, audioGainPoint.x);
        audioGainPoint.y = juce::jlimit(-limit, limit, audioGainPoint.y);

        threadManager.write(audioGainPoint, "VisualiserRendererMain");
        
        if (output.getNumChannels() > 0) {
            outputArray[0][sample] = point.x;
        }
        if (output.getNumChannels() > 1) {
            outputArray[1][sample] = point.y;
        }
          // handle the title drawing
        osci::Point titlePoint = titleRenderer.nextVector();
        
        // apply bit crush without animating values, as this has already been done
        titlePoint = bitCrush->apply(sample, titlePoint, 0.0, false);

        threadManager.write(titlePoint, "VisualiserRendererTitle");

        osci::Point sliderPoint;
        {
            juce::SpinLock::ScopedLockType lock(sliderLock);
            sliderPoint = sliderRenderer.nextVector();
        }

        threadManager.write(sliderPoint, "VisualiserRendererSlider");
	}
}



void EffectAudioProcessor::getStateInformation(juce::MemoryBlock& destData) {
    std::unique_ptr<juce::XmlElement> xml = std::make_unique<juce::XmlElement>("project");
    xml->setAttribute("version", ProjectInfo::versionString);

    auto effectsXml = xml->createNewChildElement("effects");
    for (auto effect : effects) {
        effect->save(effectsXml->createNewChildElement("effect"));
    }

    copyXmlToBinary(*xml, destData);
}

void EffectAudioProcessor::setStateInformation(const void* data, int sizeInBytes) {
    std::unique_ptr<juce::XmlElement> xml;

    const uint32_t magicXmlNumber = 0x21324356;
    if (sizeInBytes > 8 && juce::ByteOrder::littleEndianInt(data) == magicXmlNumber) {
        // this is a binary xml format
        xml = getXmlFromBinary(data, sizeInBytes);
    } else {
        // this is a text xml format
        xml = juce::XmlDocument::parse(juce::String((const char*)data, sizeInBytes));
    }    if (xml.get() != nullptr && xml->hasTagName("project")) {
        auto effectsXml = xml->getChildByName("effects");
        if (effectsXml != nullptr) {
            for (auto effectXml : effectsXml->getChildIterator()) {
                if (effectXml->getStringAttribute("id") == bitCrush->getId()) {
                    bitCrush->load(effectXml);
                }
            }
        }
    }
}

void EffectAudioProcessor::releaseResources() {
    // When playback stops, you can use this as an opportunity to free up any
    // spare memory, etc.
}

bool EffectAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    return true;
}

bool EffectAudioProcessor::hasEditor() const {
    return true;
}

juce::AudioProcessorEditor* EffectAudioProcessor::createEditor() {
    return new EffectPluginEditor(*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new EffectAudioProcessor();
}
