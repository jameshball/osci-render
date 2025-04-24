/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#include "CommonPluginProcessor.h"
#include "CommonPluginEditor.h"
#include "components/AudioPlayerComponent.h"

//==============================================================================
CommonAudioProcessor::CommonAudioProcessor(const BusesProperties& busesProperties)
#ifndef JucePlugin_PreferredChannelConfigurations
     : AudioProcessor(busesProperties)
#endif
{
    if (!applicationFolder.exists()) {
        applicationFolder.createDirectory();
    }

    // Initialize the global settings with the plugin name
    juce::PropertiesFile::Options options;
    options.applicationName = JucePlugin_Name + juce::String("_globals");
    options.filenameSuffix = ".settings";
    options.osxLibrarySubFolder = "Application Support";
    
    #if JUCE_LINUX || JUCE_BSD
    options.folderName = "~/.config";
    #else
    options.folderName = "";
    #endif
    
    globalSettings = std::make_unique<juce::PropertiesFile>(options);
    
    // locking isn't necessary here because we are in the constructor

    for (auto effect : visualiserParameters.effects) {
        permanentEffects.push_back(effect);
        effects.push_back(effect);
    }
    
    for (auto effect : visualiserParameters.audioEffects) {
        effects.push_back(effect);
    }
        
    for (auto parameter : visualiserParameters.booleans) {
        booleanParameters.push_back(parameter);
    }
    
    for (auto parameter : visualiserParameters.integers) {
        intParameters.push_back(parameter);
    }

    muteParameter = new osci::BooleanParameter("Mute", "mute", VERSION_HINT, false, "Mute audio output");
    booleanParameters.push_back(muteParameter);

    permanentEffects.push_back(volumeEffect);
    permanentEffects.push_back(thresholdEffect);
    effects.push_back(volumeEffect);
    effects.push_back(thresholdEffect);

    wavParser.setLooping(false);
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

CommonAudioProcessor::~CommonAudioProcessor() 
{
    setGlobalValue("endTime", juce::Time::getCurrentTime().toISO8601(true));
    saveGlobalSettings();
}

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
std::shared_ptr<osci::Effect> CommonAudioProcessor::getEffect(juce::String id) {
    for (auto& effect : effects) {
        if (effect->getId() == id) {
            return effect;
        }
    }
    return nullptr;
}

// effectsLock should be held when calling this
osci::BooleanParameter* CommonAudioProcessor::getBooleanParameter(juce::String id) {
    for (auto& parameter : booleanParameters) {
        if (parameter->paramID == id) {
            return parameter;
        }
    }
    return nullptr;
}

// effectsLock should be held when calling this
osci::FloatParameter* CommonAudioProcessor::getFloatParameter(juce::String id) {
    for (auto& parameter : floatParameters) {
        if (parameter->paramID == id) {
            return parameter;
        }
    }
    return nullptr;
}

// effectsLock should be held when calling this
osci::IntParameter* CommonAudioProcessor::getIntParameter(juce::String id) {
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

void CommonAudioProcessor::loadAudioFile(const juce::File& file) {
    auto stream = std::make_unique<juce::FileInputStream>(file);
    if (stream->openedOk()) {
        loadAudioFile(std::move(stream));
    }
}

void CommonAudioProcessor::loadAudioFile(std::unique_ptr<juce::InputStream> stream) {
    if (stream != nullptr) {
        juce::SpinLock::ScopedLockType lock(wavParserLock);
        wavParser.parse(std::move(stream));

        juce::SpinLock::ScopedLockType lock2(audioPlayerListenersLock);
        for (auto listener : audioPlayerListeners) {
            listener->parserChanged();
        }
    }
}

void CommonAudioProcessor::stopAudioFile() {
    juce::SpinLock::ScopedLockType lock(wavParserLock);
    wavParser.close();

    juce::SpinLock::ScopedLockType lock2(audioPlayerListenersLock);
    for (auto listener : audioPlayerListeners) {
        listener->parserChanged();
    }
}

void CommonAudioProcessor::addAudioPlayerListener(AudioPlayerListener* listener) {
    juce::SpinLock::ScopedLockType lock(audioPlayerListenersLock);
    audioPlayerListeners.push_back(listener);
}

void CommonAudioProcessor::removeAudioPlayerListener(AudioPlayerListener* listener) {
    juce::SpinLock::ScopedLockType lock(audioPlayerListenersLock);
    audioPlayerListeners.erase(std::remove(audioPlayerListeners.begin(), audioPlayerListeners.end(), listener), audioPlayerListeners.end());
}

std::any CommonAudioProcessor::getProperty(const std::string& key) {
    juce::SpinLock::ScopedLockType lock(propertiesLock);
    return properties[key];
}

std::any CommonAudioProcessor::getProperty(const std::string& key, std::any defaultValue) {
    juce::SpinLock::ScopedLockType lock(propertiesLock);
    auto it = properties.find(key);
    if (it == properties.end()) {
        properties[key] = defaultValue;
        return defaultValue;
    }
    return it->second;
}

void CommonAudioProcessor::setProperty(const std::string& key, std::any value) {
    juce::SpinLock::ScopedLockType lock(propertiesLock);
    properties[key] = value;
}

void CommonAudioProcessor::saveProperties(juce::XmlElement& xml) {
    juce::SpinLock::ScopedLockType lock(propertiesLock);
    
    auto propertiesXml = xml.createNewChildElement("properties");
    
    for (auto& property : properties) {
        auto element = propertiesXml->createNewChildElement("property");
        element->setAttribute("key", property.first);
        if (std::any_cast<int>(&property.second) != nullptr) {
            element->setAttribute("type", "int");
            element->setAttribute("value", std::any_cast<int>(property.second));
        } else if (std::any_cast<float>(&property.second) != nullptr) {
            element->setAttribute("type", "float");
            element->setAttribute("value", std::any_cast<float>(property.second));
        } else if (std::any_cast<double>(&property.second) != nullptr) {
            element->setAttribute("type", "double");
            element->setAttribute("value", std::any_cast<double>(property.second));
        } else if (std::any_cast<bool>(&property.second) != nullptr) {
            element->setAttribute("type", "bool");
            element->setAttribute("value", std::any_cast<bool>(property.second));
        } else if (std::any_cast<juce::String>(&property.second) != nullptr) {
            element->setAttribute("type", "string");
            element->setAttribute("value", std::any_cast<juce::String>(property.second));
        } else {
            jassertfalse;
        }
    }
}

void CommonAudioProcessor::loadProperties(juce::XmlElement& xml) {
    juce::SpinLock::ScopedLockType lock(propertiesLock);
    
    auto propertiesXml = xml.getChildByName("properties");
    
    if (propertiesXml != nullptr) {
        for (auto property : propertiesXml->getChildIterator()) {
            auto key = property->getStringAttribute("key").toStdString();
            auto type = property->getStringAttribute("type");
            
            if (type == "int") {
                properties[key] = property->getIntAttribute("value");
            } else if (type == "float") {
                properties[key] = property->getDoubleAttribute("value");
            } else if (type == "double") {
                properties[key] = property->getDoubleAttribute("value");
            } else if (type == "bool") {
                properties[key] = property->getBoolAttribute("value");
            } else if (type == "string") {
                properties[key] = property->getStringAttribute("value");
            } else {
                jassertfalse;
            }
        }
    }
}

bool CommonAudioProcessor::getGlobalBoolValue(const juce::String& keyName, bool defaultValue) const
{
    return globalSettings != nullptr ? globalSettings->getBoolValue(keyName, defaultValue) : defaultValue;
}

int CommonAudioProcessor::getGlobalIntValue(const juce::String& keyName, int defaultValue) const
{
    return globalSettings != nullptr ? globalSettings->getIntValue(keyName, defaultValue) : defaultValue;
}

double CommonAudioProcessor::getGlobalDoubleValue(const juce::String& keyName, double defaultValue) const
{
    return globalSettings != nullptr ? globalSettings->getDoubleValue(keyName, defaultValue) : defaultValue;
}

juce::String CommonAudioProcessor::getGlobalStringValue(const juce::String& keyName, const juce::String& defaultValue) const
{
    return globalSettings != nullptr ? globalSettings->getValue(keyName, defaultValue) : defaultValue;
}

void CommonAudioProcessor::setGlobalValue(const juce::String& keyName, const juce::var& value)
{
    if (globalSettings != nullptr)
        globalSettings->setValue(keyName, value);
}

void CommonAudioProcessor::removeGlobalValue(const juce::String& keyName)
{
    if (globalSettings != nullptr)
        globalSettings->removeValue(keyName);
}

void CommonAudioProcessor::saveGlobalSettings()
{
    if (globalSettings != nullptr)
        globalSettings->saveIfNeeded();
}

void CommonAudioProcessor::reloadGlobalSettings()
{
    if (globalSettings != nullptr)
        globalSettings->reload();
}

juce::File CommonAudioProcessor::getLastOpenedDirectory()
{
    juce::String savedDir = getGlobalStringValue("lastOpenedDirectory");
    if (savedDir.isEmpty())
        return juce::File::getSpecialLocation(juce::File::userHomeDirectory);
    
    juce::File dir(savedDir);
    if (dir.exists() && dir.isDirectory())
        return dir;
    
    return juce::File::getSpecialLocation(juce::File::userHomeDirectory);
}

void CommonAudioProcessor::setLastOpenedDirectory(const juce::File& directory)
{
    if (directory.exists() && directory.isDirectory())
    {
        setGlobalValue("lastOpenedDirectory", directory.getFullPathName());
        saveGlobalSettings();
    }
}

bool CommonAudioProcessor::programCrashedAndUserWantsToReset() {
    bool userWantsToReset = false;
    
    if (!hasSetSessionStartTime) {
        // check that the previous end time is later than the start time.
        // if not, the program did not close properly.
        juce::String startTime = getGlobalStringValue("startTime");
        juce::String endTime = getGlobalStringValue("endTime");
        
        if ((startTime.isNotEmpty() && endTime.isNotEmpty()) || (startTime.isNotEmpty() && endTime.isEmpty())) {
            juce::Time start = juce::Time::fromISO8601(startTime);
            juce::Time end = juce::Time::fromISO8601(endTime);
            
            if ((start > end || end == juce::Time()) && juce::MessageManager::getInstance()->isThisTheMessageThread()) {
                juce::String message = "It appears that " + juce::String(ProjectInfo::projectName) + " did not close properly during your last session. This may indicate a problem with your project or session.";
                
                // Use a synchronous dialog to ensure user makes a choice before proceeding
                bool userPressedReset = juce::AlertWindow::showOkCancelBox(
                    juce::AlertWindow::WarningIcon,
                    "Possible Crash Detected",
                    message + "\n\nDo you want to reset to a new project, or continue loading your previous session?",
                    "Reset to New Project",
                    "Continue",
                    nullptr,
                    nullptr
                );
                
                if (userPressedReset) {
                    userWantsToReset = true;
                }
            }
        }
        
        setGlobalValue("startTime", juce::Time::getCurrentTime().toISO8601(true));
        saveGlobalSettings();
        hasSetSessionStartTime = true;
    }
    
    return userWantsToReset;
}

#if OSCI_PREMIUM
juce::String CommonAudioProcessor::getFFmpegURL() {
    juce::String ffmpegURL = juce::String("https://github.com/eugeneware/ffmpeg-static/releases/download/b6.0/") +
#if JUCE_WINDOWS
    #if JUCE_64BIT
        "ffmpeg-win32-x64"
    #elif JUCE_32BIT
        "ffmpeg-win32-ia32"
    #endif
#elif JUCE_MAC
    #if JUCE_ARM
        "ffmpeg-darwin-arm64"
    #elif JUCE_INTEL
        "ffmpeg-darwin-x64"
    #endif
#elif JUCE_LINUX
    #if JUCE_ARM
        #if JUCE_64BIT
            "ffmpeg-linux-arm64"
        #elif JUCE_32BIT
            "ffmpeg-linux-arm"
        #endif
    #elif JUCE_INTEL
        #if JUCE_64BIT
            "ffmpeg-linux-x64"
        #elif JUCE_32BIT
            "ffmpeg-linux-ia32"
        #endif
    #endif
#endif
    + ".gz";
    
    return ffmpegURL;
}

bool CommonAudioProcessor::ensureFFmpegExists(std::function<void()> onStart, std::function<void()> onSuccess) {
    juce::File ffmpegFile = getFFmpegFile();
    
    if (ffmpegFile.exists()) {
        // FFmpeg already exists
        if (onSuccess != nullptr) {
            onSuccess();
        }
        return true;
    }

    auto editor = dynamic_cast<CommonPluginEditor*>(getActiveEditor());
    if (editor == nullptr) {
        return false; // Editor not found
    }
    
    juce::String url = getFFmpegURL();
    editor->ffmpegDownloader.setup(url, ffmpegFile);
    
    editor->ffmpegDownloader.onSuccessfulDownload = [this, onSuccess]() {
        if (onSuccess != nullptr) {
            juce::MessageManager::callAsync(onSuccess);
        }
    };

    // Ask the user if they want to download ffmpeg
    juce::MessageBoxOptions options = juce::MessageBoxOptions()
        .withTitle("FFmpeg Required")
        .withMessage("FFmpeg is required to process video files.\n\nWould you like to download it now?")
        .withButton("Yes")
        .withButton("No")
        .withIconType(juce::AlertWindow::QuestionIcon)
        .withAssociatedComponent(editor);

    juce::AlertWindow::showAsync(options, [this, onStart, editor](int result) {
        if (result == 1) {  // Yes
            editor->ffmpegDownloader.setVisible(true);
            editor->ffmpegDownloader.download();
            if (onStart != nullptr) {
                onStart();
            }
            editor->resized();
        }
    });
    
    return false;
}
#endif
