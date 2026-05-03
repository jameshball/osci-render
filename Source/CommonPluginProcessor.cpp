/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#include "CommonPluginProcessor.h"
#include "CommonPluginEditor.h"

namespace
{
    osci::LicenseManager::Config makeLicenseManagerConfig() {
        osci::LicenseManager::Config config;
        const juce::String pluginName (JucePlugin_Name);
        config.productSlug = pluginName.equalsIgnoreCase ("sosci") ? "sosci" : "osci-render";
        return config;
    }
}

//==============================================================================
CommonAudioProcessor::CommonAudioProcessor(const BusesProperties& busesProperties)
#ifndef JucePlugin_PreferredChannelConfigurations
     : AudioProcessor(busesProperties),
       licenseManager (makeLicenseManagerConfig())
#else
     : licenseManager (makeLicenseManagerConfig())
#endif
{

    if (!applicationFolder.exists()) {
        applicationFolder.createDirectory();
    }

    // Set up a global file logger so that juce::Logger::writeToLog() writes to disk
    fileLogger = std::make_unique<juce::FileLogger>(
        applicationFolder.getChildFile(juce::String(JucePlugin_Name) + ".log"),
        "Log started: " + juce::Time::getCurrentTime().toString(true, true, true, true),
        1024 * 1024  // max 1 MB before rotating
    );
    juce::Logger::setCurrentLogger(fileLogger.get());

    // Log startup details so support tickets always have build/runtime context.
    {
        const bool premium =
        #if OSCI_PREMIUM
            true;
        #else
            false;
        #endif
        juce::Logger::writeToLog("==== " + juce::String(JucePlugin_Name) + " starting ====");
        juce::Logger::writeToLog("Version: " + juce::String(JucePlugin_VersionString)
                                 + (premium ? " (Premium)" : " (Free)"));
        juce::Logger::writeToLog("Wrapper: " + juce::String(getWrapperTypeDescription(wrapperType)));
        juce::Logger::writeToLog("JUCE: " + juce::SystemStats::getJUCEVersion());
        juce::Logger::writeToLog("OS: " + juce::SystemStats::getOperatingSystemName()
                                 + (juce::SystemStats::isOperatingSystem64Bit() ? " (64-bit)" : " (32-bit)"));
        juce::Logger::writeToLog("CPU: " + juce::SystemStats::getCpuModel()
                                 + " (" + juce::String(juce::SystemStats::getNumCpus()) + " cores, "
                                 + juce::String(juce::SystemStats::getCpuSpeedInMegahertz()) + " MHz)");
        juce::Logger::writeToLog("RAM: " + juce::String(juce::SystemStats::getMemorySizeInMegabytes()) + " MB");
        juce::Logger::writeToLog("Device: " + juce::SystemStats::getDeviceDescription()
                                 + " / " + juce::SystemStats::getDeviceManufacturer());
        juce::Logger::writeToLog("User: " + juce::SystemStats::getLogonName()
                                 + " @ " + juce::SystemStats::getComputerName()
                                 + " (" + juce::SystemStats::getUserLanguage() + "_"
                                 + juce::SystemStats::getUserRegion() + ")");
    }

    globalSettings = osci::SettingsStore::forProductGlobals (JucePlugin_Name);

    const auto licenseCacheResult = licenseManager.loadCachedToken();
    if (licenseCacheResult.failed()) {
        juce::Logger::writeToLog ("License cache load failed: " + licenseCacheResult.getErrorMessage());
    }

    // Restore recently-opened project files (shared across instances).
    recentProjectFiles.setMaxNumberOfItems(10);
    {
        const auto savedRecent = globalSettings.getString("recentProjectFiles");
        if (savedRecent.isNotEmpty())
            recentProjectFiles.restoreFromString(savedRecent);
    }
    
    // locking isn't necessary here because we are in the constructor

    for (auto effect : visualiserParameters.effects) {
        // Only add to effects (for parameter management / save / load).
        // NOT added to permanentEffects — these are animated and modulated
        // exclusively on the renderer thread to avoid data races with the
        // audio thread. They are shader-only effects (no audio processing).
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
    startHeartbeat();
}

juce::String CommonAudioProcessor::getProductSlug() const
{
    const juce::String pluginName (JucePlugin_Name);
    return pluginName.equalsIgnoreCase ("sosci") ? "sosci" : "osci-render";
}

int CommonAudioProcessor::getNumRecentProjectFiles() const
{
    return recentProjectFiles.getNumFiles();
}

juce::File CommonAudioProcessor::getRecentProjectFile(int index) const
{
    return recentProjectFiles.getFile(index);
}

void CommonAudioProcessor::addRecentProjectFile(const juce::File& file)
{
    if (file == juce::File())
        return;

    recentProjectFiles.addFile(file);

    // Persist to global settings.
    globalSettings.set("recentProjectFiles", recentProjectFiles.toString());
    globalSettings.save();

    // Best-effort: register with OS for native "recent documents" integration (jump lists / dock).
    // This is optional and should be safe across platforms.
    recentProjectFiles.registerRecentFileNatively(file);
}

int CommonAudioProcessor::createRecentProjectsPopupMenuItems(juce::PopupMenu& menuToAddItemsTo,
                                                            int baseItemId,
                                                            bool showFullPaths,
                                                            bool dontAddNonExistentFiles)
{
    if (dontAddNonExistentFiles) {
        const auto before = recentProjectFiles.toString();
        recentProjectFiles.removeNonExistentFiles();
        const auto after = recentProjectFiles.toString();
        if (after != before) {
            globalSettings.set("recentProjectFiles", after);
            globalSettings.save();
        }
    }

    return recentProjectFiles.createPopupMenuItems(menuToAddItemsTo,
                                                   baseItemId,
                                                   showFullPaths,
                                                   dontAddNonExistentFiles,
                                                   nullptr);
}

void CommonAudioProcessor::saveStandaloneProjectFilePathToXml(juce::XmlElement& xml) const
{
    if (!juce::JUCEApplicationBase::isStandaloneApp())
        return;

    xml.setAttribute("projectFilePath", currentProjectFile);
}

void CommonAudioProcessor::restoreStandaloneProjectFilePathFromXml(const juce::XmlElement& xml)
{
    if (!juce::JUCEApplicationBase::isStandaloneApp())
        return;

    const auto projectPath = xml.getStringAttribute("projectFilePath");
    if (projectPath.isNotEmpty()) {
        const juce::File f(projectPath);
        currentProjectFile = f.existsAsFile() ? f.getFullPathName() : juce::String();
    } else {
        currentProjectFile = juce::String();
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

    // Bind all parameters to the ValueTree for undo/redo support
    stateTree.addListener(this);

    // Parameters that should NOT participate in undo/redo
    auto isUndoExcluded = [](const juce::String& paramID) {
        return paramID == "visualiserFullScreen";
    };

    for (auto* param : getParameters()) {
        if (auto* bp = dynamic_cast<osci::BooleanParameter*>(param)) {
            auto* um = isUndoExcluded(bp->paramID) ? nullptr : &undoManager;
            bp->bindToValueTree(stateTree, um, &lastUndoParamId, &undoSuppressed, &undoGrouping);
            paramIdMap[bp->paramID] = bp;
        } else if (auto* ip = dynamic_cast<osci::IntParameter*>(param)) {
            auto* um = isUndoExcluded(ip->paramID) ? nullptr : &undoManager;
            ip->bindToValueTree(stateTree, um, &lastUndoParamId, &undoSuppressed, &undoGrouping);
            paramIdMap[ip->paramID] = ip;
        } else if (auto* fp = dynamic_cast<osci::FloatParameter*>(param)) {
            auto* um = isUndoExcluded(fp->paramID) ? nullptr : &undoManager;
            fp->bindToValueTree(stateTree, um, &lastUndoParamId, &undoSuppressed, &undoGrouping);
            paramIdMap[fp->paramID] = fp;
        }
    }
    undoManager.clearUndoHistory();
    midiCCManager.setUndoManager(&undoManager, &undoSuppressed, &stateTree);

    // Set MidiCCManager on all parameters so UI components can auto-discover
    // CC support from the parameter they're bound to — no manual wiring needed.
    for (auto* param : getParameters()) {
        if (auto* fp = dynamic_cast<osci::FloatParameter*>(param))
            fp->midiCCManager = &midiCCManager;
        else if (auto* bp = dynamic_cast<osci::BooleanParameter*>(param))
            bp->midiCCManager = &midiCCManager;
        else if (auto* ip = dynamic_cast<osci::IntParameter*>(param))
            ip->midiCCManager = &midiCCManager;
    }
}

void CommonAudioProcessor::loadMidiCCState(const juce::XmlElement* xml) {
    midiCCManager.load(xml, [this](const juce::String& paramId) -> osci::MidiCCManager::ParamBinding {
        if (auto* fp = getFloatParameter(paramId)) {
            auto* ep = dynamic_cast<osci::EffectParameter*>(fp);
            return osci::MidiCCManager::makeBinding(fp, ep);
        }
        if (auto* ip = getIntParameter(paramId))
            return osci::MidiCCManager::makeBinding(ip);
        if (auto* bp = getBooleanParameter(paramId))
            return osci::MidiCCManager::makeBinding(bp);
        return {};
    });
}

void CommonAudioProcessor::valueTreePropertyChanged(juce::ValueTree& treeWhosePropertyHasChanged, const juce::Identifier& property) {
    if (treeWhosePropertyHasChanged != stateTree) return;
    auto it = paramIdMap.find(property.toString());
    if (it == paramIdMap.end()) return;
    auto* param = it->second;
    if (auto* bp = dynamic_cast<osci::BooleanParameter*>(param))
        bp->applyValueFromTree();
    else if (auto* ip = dynamic_cast<osci::IntParameter*>(param))
        ip->applyValueFromTree();
    else if (auto* fp = dynamic_cast<osci::FloatParameter*>(param))
        fp->applyValueFromTree();
}


void CommonAudioProcessor::startHeartbeat() {
    if (!heartbeatActive) {
        startTimer(2000); // 2 seconds
        heartbeatActive = true;
    }
}

void CommonAudioProcessor::stopHeartbeat() {
    if (heartbeatActive) {
        stopTimer();
        heartbeatActive = false;
    }
}

void CommonAudioProcessor::timerCallback() {
    globalSettings.set("lastHeartbeatTime", juce::Time::getCurrentTime().toISO8601(true));
    globalSettings.save();
}

CommonAudioProcessor::~CommonAudioProcessor()
{
    globalSettings.set("endTime", juce::Time::getCurrentTime().toISO8601(true));
    globalSettings.save();
    stopHeartbeat();
    juce::Logger::setCurrentLogger(nullptr);
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
    juce::Logger::writeToLog("prepareToPlay: sampleRate=" + juce::String(sampleRate)
        + " samplesPerBlock=" + juce::String(samplesPerBlock)
        + " effects=" + juce::String(effects.size()));

	currentSampleRate = sampleRate;
    
    for (auto& effect : effects) {
        effect->prepareToPlay(currentSampleRate, samplesPerBlock);
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
#if !OSCI_PREMIUM
            // If this is a premium effect and we are not in premium mode, return nullptr
            if (effect->isPremiumOnly()) {
                return nullptr;
            }
#endif
            return effect;
        }
    }
    return nullptr;
}

// Looks up parameters by paramID across all registered parameters, including
// osci::EffectParameter instances owned by individual effects (they are all
// added to paramIdMap in addAllParameters()).
osci::BooleanParameter* CommonAudioProcessor::getBooleanParameter(juce::String id) {
    auto it = paramIdMap.find(id);
    if (it == paramIdMap.end()) return nullptr;
    return dynamic_cast<osci::BooleanParameter*>(it->second);
}

osci::FloatParameter* CommonAudioProcessor::getFloatParameter(juce::String id) {
    auto it = paramIdMap.find(id);
    if (it == paramIdMap.end()) return nullptr;
    return dynamic_cast<osci::FloatParameter*>(it->second);
}

osci::IntParameter* CommonAudioProcessor::getIntParameter(juce::String id) {
    auto it = paramIdMap.find(id);
    if (it == paramIdMap.end()) return nullptr;
    return dynamic_cast<osci::IntParameter*>(it->second);
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

void CommonAudioProcessor::loadEffectsFromXml(const juce::XmlElement* effectsXml) {
    if (effectsXml == nullptr) {
        juce::Logger::writeToLog("setStateInformation: no effects section found");
        return;
    }
    int loadedEffects = 0, skippedEffects = 0;
    for (auto effectXml : effectsXml->getChildIterator()) {
        auto id = effectXml->getStringAttribute("id");
        auto effect = getEffect(id);
        if (effect != nullptr) {
            effect->load(effectXml);
            loadedEffects++;
        } else {
            juce::Logger::writeToLog("setStateInformation: unknown effect id '" + id + "', skipping");
            skippedEffects++;
        }
    }
    juce::Logger::writeToLog("setStateInformation: loaded " + juce::String(loadedEffects) + " effects"
        + (skippedEffects > 0 ? ", skipped " + juce::String(skippedEffects) : ""));
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

juce::File CommonAudioProcessor::getAppSettingsFile()
{
    return osci::SettingsStore::optionsForStandaloneApp (JucePlugin_Name).getDefaultFile();
}

juce::File CommonAudioProcessor::getLastOpenedDirectory()
{
    juce::String savedDir = globalSettings.getString("lastOpenedDirectory");
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
        globalSettings.set("lastOpenedDirectory", directory.getFullPathName());
        globalSettings.save();
    }
}

bool CommonAudioProcessor::programCrashedAndUserWantsToReset() {
    bool userWantsToReset = false;
    if (!hasSetSessionStartTime) {
        juce::String startTime = globalSettings.getString("startTime");
        juce::String endTime = globalSettings.getString("endTime");
        juce::String lastHeartbeat = globalSettings.getString("lastHeartbeatTime");
        juce::Time start = juce::Time::fromISO8601(startTime);
        juce::Time end = juce::Time::fromISO8601(endTime);
        juce::Time heartbeat = juce::Time::fromISO8601(lastHeartbeat);
        juce::Time now = juce::Time::getCurrentTime();
        bool heartbeatStale = (now.toMilliseconds() - heartbeat.toMilliseconds()) > 3000;
        if ((startTime.isNotEmpty() && endTime.isNotEmpty()) || (startTime.isNotEmpty() && endTime.isEmpty())) {
            if (((start > end || end == juce::Time()) && heartbeatStale) && juce::MessageManager::getInstance()->isThisTheMessageThread()) {
                // Ensure the custom look and feel is set before showing the dialog,
                // since the editor (which normally creates it) hasn't been opened yet.
                OscirenderLookAndFeel::getSharedInstance();

                juce::String message = "It appears that " + juce::String(ProjectInfo::projectName) + " did not close properly during your last session. This may indicate a problem with your project or session.";
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
        globalSettings.set("startTime", juce::Time::getCurrentTime().toISO8601(true));
        globalSettings.save();
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

bool CommonAudioProcessor::validateFFmpegBinary() {
    if (ffmpegValidated)
        return true;

    juce::File ffmpegFile = getFFmpegFile();
    if (!ffmpegFile.existsAsFile()) {
        juce::Logger::writeToLog("FFmpeg validation: binary not found at " + ffmpegFile.getFullPathName());
        return false;
    }

    juce::Logger::writeToLog("FFmpeg validation: testing binary at " + ffmpegFile.getFullPathName());

    juce::ChildProcess process;
    juce::StringArray args;
    args.add(ffmpegFile.getFullPathName());
    args.add("-version");

    if (!process.start(args)) {
        juce::Logger::writeToLog("FFmpeg validation: failed to start process");
        return false;
    }

    // Read output before waiting to prevent pipe-buffer deadlock
    auto output = process.readAllProcessOutput();
    process.waitForProcessToFinish(5000);
    int exitCode = process.getExitCode();

    if (exitCode != 0) {
        juce::Logger::writeToLog("FFmpeg validation failed with exit code " + juce::String(exitCode));
        if (output.isNotEmpty())
            juce::Logger::writeToLog("FFmpeg output: " + output.substring(0, 500));
    } else {
        // Log just the first line (version string)
        juce::Logger::writeToLog("FFmpeg validated: " + output.upToFirstOccurrenceOf("\n", false, false));
        ffmpegValidated = true;
    }

    return exitCode == 0;
}

bool CommonAudioProcessor::ensureFFmpegExists(std::function<void()> onStart, std::function<void()> onSuccess) {
    juce::File ffmpegFile = getFFmpegFile();
    
    if (ffmpegFile.existsAsFile()) {
        // Verify the binary actually works on this system
        if (!validateFFmpegBinary()) {
            // Binary exists but is incompatible — delete it and show an error
            juce::Logger::writeToLog("FFmpeg binary incompatible — deleting " + ffmpegFile.getFullPathName());
            ffmpegFile.deleteFile();

            auto editor = dynamic_cast<CommonPluginEditor*>(getActiveEditor());
            juce::String message =
                "The FFmpeg binary is not compatible with your version of macOS.\n\n"
#if JUCE_MAC && JUCE_ARM
                "On Apple Silicon Macs, macOS 12 (Monterey) or later is required "
                "for video recording and video/GIF file import.\n\n"
                "Please update your macOS to use these features."
#else
                "The existing FFmpeg binary could not be launched. "
                "It will be re-downloaded on the next attempt.\n\n"
                "If this problem persists, please contact support."
#endif
                ;

            juce::MessageBoxOptions options = juce::MessageBoxOptions()
                .withTitle("FFmpeg Incompatible")
                .withMessage(message)
                .withButton("OK")
                .withIconType(juce::AlertWindow::WarningIcon)
                .withAssociatedComponent(editor);
            juce::AlertWindow::showAsync(options, nullptr);
            return false;
        }

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

void CommonAudioProcessor::applyVolumeAndThreshold(float* const* channels, int numSamples) {
    const float* volBuf = volumeEffect->getAnimatedValuesReadPointer(0, numSamples);
    const float* thrBuf = thresholdEffect->getAnimatedValuesReadPointer(0, numSamples);
    const float volFallback = volumeEffect->getValue();
    const float thrFallback = thresholdEffect->getValue();
    for (int i = 0; i < numSamples; ++i) {
        float vol = volBuf ? volBuf[i] : volFallback;
        float thr = thrBuf ? thrBuf[i] : thrFallback;
        channels[0][i] = juce::jlimit(-thr, thr, channels[0][i] * vol);
        channels[1][i] = juce::jlimit(-thr, thr, channels[1][i] * vol);
    }
}

