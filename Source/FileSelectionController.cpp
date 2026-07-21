#include "FileSelectionController.h"

#include "PluginProcessor.h"
#include "audio/synth/ShapeVoice.h"
#include "audio/synth/VoiceManager.h"
#include "parser/FileParser.h"

#include <algorithm>

FileSelectionController::FileSelectionController(OscirenderAudioProcessor& processor, VoiceManager& voices)
    : processor(processor), voices(voices) {}

FileSelectionController::~FileSelectionController() {
    processor.midiManager.setMessageHandler(osci::MidiManager::MessageType::programChange, {});
    cancelPendingUpdate();
}

void FileSelectionController::initialise() {
    auto defaultParser = std::make_shared<FileParser>(processor);
    defaultSound = new ShapeSound(processor, defaultParser);
    objectServerSound = new ShapeSound();
    voices.addSound(defaultSound.get());
    activeSound.store(defaultSound.get(), std::memory_order_release);

    // Standalone MIDI routing is global; plugin instances start on Omni until their host restores instance state.
    const int savedChannel = juce::JUCEApplicationBase::isStandaloneApp()
        ? processor.globalSettings.getInt("programChangeChannel", programChangeOmni)
        : programChangeOmni;
    programChangeChannel.store(savedChannel, std::memory_order_release);
    processor.midiManager.setMessageHandler(osci::MidiManager::MessageType::programChange,
        [this](const juce::MidiMessage& message) {
            queueProgramChange(message.getProgramChangeNumber(), message.getChannel());
        });
}

int FileSelectionController::addFile(const juce::File& file) {
    auto data = std::make_shared<juce::MemoryBlock>();
    auto stream = file.createInputStream();
    if (stream != nullptr) {
        stream->readIntoMemoryBlock(*data);
    }
    return addFile(file.getFileName(), std::move(data));
}

int FileSelectionController::addFile(juce::String name, const char* data, int size) {
    auto block = std::make_shared<juce::MemoryBlock>();
    block->append(data, static_cast<size_t>(size));
    return addFile(std::move(name), std::move(block));
}

int FileSelectionController::addFile(juce::String name, std::shared_ptr<juce::MemoryBlock> data) {
    juce::SpinLock::ScopedLockType fileLock(lock);
    juce::SpinLock::ScopedLockType effectLock(processor.effectsLock);
    auto parser = std::make_shared<FileParser>(processor, processor.errorCallback);
    ShapeSound::Ptr sound = new ShapeSound(processor, parser);
    return addFile(std::move(name), std::move(data), std::move(parser), std::move(sound));
}

int FileSelectionController::addFile(juce::String name, std::shared_ptr<juce::MemoryBlock> data,
    std::shared_ptr<FileParser> parser, ShapeSound::Ptr sound) {
    files.push_back({ nextFileId++, std::move(name), std::move(data), std::move(parser), std::move(sound) });
    const int index = size() - 1;
    parseFile(index);
    selectFile(index, Source::internal);
    return index;
}

void FileSelectionController::updateFile(int index, std::shared_ptr<juce::MemoryBlock> data) {
    juce::SpinLock::ScopedLockType fileLock(lock);
    juce::SpinLock::ScopedLockType effectLock(processor.effectsLock);
    if (!contains(index)) {
        return;
    }
    files[index].data = std::move(data);
    parseFile(index);
    selectFile(index, Source::internal);
}

juce::String FileSelectionController::renameFile(int index, juce::String newName) {
    juce::SpinLock::ScopedLockType fileLock(lock);
    if (!contains(index)) {
        return {};
    }

    const juce::String legalName = juce::File::createLegalFileName(newName.trim());
    if (legalName.isNotEmpty()) {
        files[index].name = legalName;
    }
    return legalName;
}

int FileSelectionController::duplicateFile(int index) {
    juce::SpinLock::ScopedLockType fileLock(lock);
    juce::SpinLock::ScopedLockType effectLock(processor.effectsLock);
    if (!contains(index)) {
        return -1;
    }

    const juce::File source(files[index].name);
    auto data = std::make_shared<juce::MemoryBlock>(*files[index].data);
    auto parser = std::make_shared<FileParser>(processor, processor.errorCallback);
    ShapeSound::Ptr sound = new ShapeSound(processor, parser);
    return addFile(source.getFileNameWithoutExtension() + " copy" + source.getFileExtension(),
        std::move(data), std::move(parser), std::move(sound));
}

void FileSelectionController::removeFile(int index) {
    juce::SpinLock::ScopedLockType fileLock(lock);
    juce::SpinLock::ScopedLockType effectLock(processor.effectsLock);
    removeFile(index, true);
}

void FileSelectionController::removeFile(int index, bool notifyEditor) {
    if (!contains(index)) {
        return;
    }

    files.erase(files.begin() + index);
    if (files.empty()) {
        selectNoFile(Source::internal);
    } else {
        select(std::min(index, size() - 1), Source::internal, true);
    }

    if (notifyEditor && fileRemovedCallback) {
        fileRemovedCallback(index);
    }
}

void FileSelectionController::removeParser(FileParser* parser) {
    juce::SpinLock::ScopedLockType fileLock(lock);
    juce::SpinLock::ScopedLockType effectLock(processor.effectsLock);
    const auto match = std::find_if(files.begin(), files.end(), [parser](const File& file) {
        return file.parser.get() == parser;
    });
    if (match != files.end()) {
        removeFile(static_cast<int>(std::distance(files.begin(), match)), true);
    }
}

void FileSelectionController::clearFiles() {
    while (!files.empty()) {
        removeFile(0, true);
    }
}

int FileSelectionController::size() const noexcept {
    return static_cast<int>(files.size());
}

bool FileSelectionController::contains(int index) const noexcept {
    return index >= 0 && index < size();
}

std::optional<int> FileSelectionController::getCurrentFileIndex() const noexcept {
    if (activeSource.load(std::memory_order_acquire) != ActiveSource::files
        || !hasSelectedFile.load(std::memory_order_acquire)) {
        return std::nullopt;
    }
    return selectedFileIndex.load(std::memory_order_acquire);
}

std::optional<int> FileSelectionController::getAdjacentFileIndex(int offset) const noexcept {
    const auto current = getCurrentFileIndex();
    if (!current.has_value()) {
        return std::nullopt;
    }
    const int adjacent = *current + offset;
    return contains(adjacent) ? std::optional<int>(adjacent) : std::nullopt;
}

std::shared_ptr<FileParser> FileSelectionController::getCurrentParser() const {
    if (isTextureInputActive()) {
        return textureInputParser;
    }
    const auto index = getCurrentFileIndex();
    return index.has_value() ? getParser(*index) : nullptr;
}

std::shared_ptr<FileParser> FileSelectionController::getParser(int index) const {
    return contains(index) ? files[index].parser : nullptr;
}

juce::String FileSelectionController::getCurrentFileName() const {
    if (isTextureInputActive()) {
        return textureInputName;
    }
    const auto index = getCurrentFileIndex();
    return index.has_value() ? getFileName(*index) : juce::String();
}

juce::String FileSelectionController::getFileName(int index) const {
    return contains(index) ? files[index].name : juce::String();
}

juce::String FileSelectionController::getFileId(int index) const {
    return contains(index) ? juce::String(files[index].id) : juce::String();
}

std::shared_ptr<juce::MemoryBlock> FileSelectionController::getFileData(int index) const {
    return contains(index) ? files[index].data : nullptr;
}

void FileSelectionController::selectFile(int index) {
    juce::SpinLock::ScopedLockType fileLock(lock);
    juce::SpinLock::ScopedLockType effectLock(processor.effectsLock);
    selectFile(index, Source::user);
}

void FileSelectionController::selectAdjacentFile(int offset) {
    juce::SpinLock::ScopedLockType fileLock(lock);
    juce::SpinLock::ScopedLockType effectLock(processor.effectsLock);
    const auto adjacentFile = getAdjacentFileIndex(offset);
    if (adjacentFile.has_value()) {
        selectFile(*adjacentFile, Source::user);
    }
}

void FileSelectionController::selectFile(int index, Source source) {
    if (contains(index)) {
        select(index, source);
    }
}

void FileSelectionController::selectNoFile(Source source) {
    select(std::nullopt, source);
}

void FileSelectionController::select(std::optional<int> index, Source source, bool forceSoundUpdate) {
    const auto currentSource = activeSource.load(std::memory_order_acquire);
    const bool realtimeSelection = source == Source::parameter;
    if ((currentSource == ActiveSource::objectServer && (realtimeSelection || source == Source::user))
        || (currentSource == ActiveSource::textureInput && realtimeSelection)) {
        return;
    }

    const auto previousFile = getSelectedFileForState();
    if (index.has_value()) {
        selectedFileIndex.store(*index, std::memory_order_release);
        hasSelectedFile.store(true, std::memory_order_release);
    } else {
        hasSelectedFile.store(false, std::memory_order_release);
    }
    auto nextSource = currentSource;
    if (currentSource != ActiveSource::objectServer) {
        nextSource = ActiveSource::files;
        activeSource.store(nextSource, std::memory_order_release);
        if (currentSource == ActiveSource::textureInput) {
            textureInputName.clear();
        }
    }

    if (index.has_value() && source != Source::parameter) {
        const int fileNumber = *index + 1;
        if (source == Source::stateRestore) {
            processor.fileSelect->setValueUnnormalised(static_cast<float>(fileNumber));
        } else {
            processor.fileSelect->setUnnormalisedValueNotifyingHost(static_cast<float>(fileNumber));
        }
        lastObservedParameter.store(fileNumber, std::memory_order_release);
    }

    const bool changed = previousFile != index || currentSource != nextSource;
    if (changed || forceSoundUpdate) {
        updateActiveSound(forceSoundUpdate);
        notifySelectionChanged();
    }
}

void FileSelectionController::updatePendingSelectionFromParameter() noexcept {
    const int fileNumber = std::clamp(processor.fileSelect->getValueUnnormalised(), 1, maxSelectableFiles);
    if (lastObservedParameter.exchange(fileNumber, std::memory_order_acq_rel) != fileNumber) {
        pendingFileNumber.store(fileNumber, std::memory_order_release);
    }
}

void FileSelectionController::queueProgramChange(int program, int midiChannel) noexcept {
    const int configuredChannel = getProgramChangeChannel();
    if (configuredChannel == programChangeOff || (configuredChannel != programChangeOmni && configuredChannel != midiChannel)
        || activeSource.load(std::memory_order_acquire) != ActiveSource::files) {
        return;
    }

    const int fileNumber = std::clamp(program + 1, 1, maxSelectableFiles);
    processor.fileSelect->setValueUnnormalised(static_cast<float>(fileNumber));
    lastObservedParameter.store(fileNumber, std::memory_order_release);
    pendingFileNumber.store(fileNumber, std::memory_order_release);
}

void FileSelectionController::applyPendingSelection() {
    const int fileNumber = pendingFileNumber.exchange(0, std::memory_order_acq_rel);
    if (fileNumber == 0 || files.empty()) {
        return;
    }
    selectFile(std::min(fileNumber - 1, size() - 1), Source::parameter);
}

void FileSelectionController::clearPendingSelection() noexcept {
    pendingFileNumber.store(0, std::memory_order_release);
}

void FileSelectionController::startTextureInput(juce::String sourceName, int width, int height) {
    if (processor.inputEnabled->getBoolValue()) {
        processor.inputEnabled->setBoolValueNotifyingHost(false);
    }

    juce::SpinLock::ScopedLockType fileLock(lock);
    juce::SpinLock::ScopedLockType effectLock(processor.effectsLock);
    if (textureInputParser == nullptr) {
        textureInputParser = std::make_shared<FileParser>(processor, processor.errorCallback);
        textureInputSound = new ShapeSound(processor, textureInputParser);
    }
    textureInputParser->prepareLiveImageInput(width, height);

    textureInputName = sourceName.trim().isNotEmpty() ? sourceName : "Texture Input";
    const auto previousSource = activeSource.exchange(ActiveSource::textureInput, std::memory_order_acq_rel);
    if (previousSource != ActiveSource::textureInput) {
        updateActiveSound(false);
    }
    notifySelectionChanged();
}

void FileSelectionController::updateTextureInputFrame(
    const std::vector<std::uint8_t>& rgba, int width, int height, bool verticallyFlipped) {
    std::shared_ptr<FileParser> parser;
    {
        juce::SpinLock::ScopedLockType fileLock(lock);
        if (!isTextureInputActive() || textureInputParser == nullptr) {
            return;
        }
        parser = textureInputParser;
    }
    parser->updateLiveImageFrame(rgba, width, height, verticallyFlipped);
}

void FileSelectionController::stopTextureInput() {
    juce::SpinLock::ScopedLockType fileLock(lock);
    juce::SpinLock::ScopedLockType effectLock(processor.effectsLock);
    if (activeSource.exchange(ActiveSource::files, std::memory_order_acq_rel) == ActiveSource::textureInput) {
        textureInputName.clear();
        updateActiveSound(false);
        notifySelectionChanged();
    }
}

bool FileSelectionController::isTextureInputActive() const noexcept {
    return activeSource.load(std::memory_order_acquire) == ActiveSource::textureInput;
}

juce::String FileSelectionController::getTextureInputName() const {
    return textureInputName;
}

void FileSelectionController::setObjectServerActive(bool active) {
    juce::SpinLock::ScopedLockType fileLock(lock);
    juce::SpinLock::ScopedLockType effectLock(processor.effectsLock);
    const auto previousSource = activeSource.load(std::memory_order_acquire);
    if (active) {
        activeSource.store(ActiveSource::objectServer, std::memory_order_release);
        textureInputName.clear();
    } else if (previousSource == ActiveSource::objectServer) {
        activeSource.store(ActiveSource::files, std::memory_order_release);
    } else {
        return;
    }
    updateActiveSound(false);
    notifySelectionChanged();
}

bool FileSelectionController::isObjectServerActive() const noexcept {
    return activeSource.load(std::memory_order_acquire) == ActiveSource::objectServer;
}

void FileSelectionController::addObjectServerFrame(std::vector<std::unique_ptr<osci::Shape>>& frame, bool force) {
    objectServerSound->addFrame(frame, force);
}

ShapeSound* FileSelectionController::getActiveSound() const noexcept {
    return activeSound.load(std::memory_order_acquire);
}

std::optional<int> FileSelectionController::getSelectedFileForState() const noexcept {
    if (!hasSelectedFile.load(std::memory_order_acquire)) {
        return std::nullopt;
    }
    return selectedFileIndex.load(std::memory_order_acquire);
}

int FileSelectionController::getProgramChangeChannel() const noexcept {
    return programChangeChannel.load(std::memory_order_acquire);
}

void FileSelectionController::setProgramChangeChannel(int channel) {
    const int validChannel = std::clamp(channel, programChangeOff, 16);
    programChangeChannel.store(validChannel, std::memory_order_release);
    if (juce::JUCEApplicationBase::isStandaloneApp()) {
        processor.globalSettings.set("programChangeChannel", validChannel);
        processor.globalSettings.save();
    }
}

void FileSelectionController::saveState(juce::XmlElement& xml) const {
    xml.setAttribute("programChangeChannel", getProgramChangeChannel());
    auto* filesXml = xml.createNewChildElement("files");
    for (const auto& file : files) {
        auto* fileXml = filesXml->createNewChildElement("file");
        fileXml->setAttribute("name", file.name);
        fileXml->addTextElement(file.data->toBase64Encoding());
    }
    xml.setAttribute("currentFile", getSelectedFileForState().value_or(-1));
}

void FileSelectionController::restoreState(const juce::XmlElement& xml, bool legacyFileEncoding) {
    if (!juce::JUCEApplicationBase::isStandaloneApp() && xml.hasAttribute("programChangeChannel")) {
        setProgramChangeChannel(xml.getIntAttribute("programChangeChannel", programChangeOmni));
    }

    clearFiles();
    auto* filesXml = xml.getChildByName("files");
    if (filesXml != nullptr) {
        for (auto* fileXml : filesXml->getChildIterator()) {
            const auto encodedData = fileXml->getAllSubText();
            auto data = std::make_shared<juce::MemoryBlock>();
            if (legacyFileEncoding) {
                juce::MemoryOutputStream stream;
                juce::Base64::convertFromBase64(stream, encodedData);
                data = std::make_shared<juce::MemoryBlock>(stream.getData(), stream.getDataSize());
            } else {
                data->fromBase64Encoding(encodedData);
            }
            auto parser = std::make_shared<FileParser>(processor, processor.errorCallback);
            ShapeSound::Ptr sound = new ShapeSound(processor, parser);
            addFile(fileXml->getStringAttribute("name"), std::move(data), std::move(parser), std::move(sound));
        }
        juce::Logger::writeToLog("setStateInformation: restored " + juce::String(size()) + " files");
    } else {
        juce::Logger::writeToLog("setStateInformation: no files section found");
    }

    clearPendingSelection();
    const int restoredFile = xml.getIntAttribute("currentFile", -1);
    if (restoredFile >= 0) {
        selectFile(restoredFile, Source::stateRestore);
    } else {
        selectNoFile(Source::stateRestore);
    }
}

void FileSelectionController::setFileRemovedCallback(std::function<void(int)> callback) {
    fileRemovedCallback = std::move(callback);
}

void FileSelectionController::parseFile(int index) {
    auto& file = files[index];
    if (file.data == nullptr) {
        return;
    }
    const auto extension = file.name.fromLastOccurrenceOf(".", true, false).toLowerCase();
    file.parser->parse(juce::String(file.id), file.name, extension,
        std::make_unique<juce::MemoryInputStream>(*file.data, false), processor.font);
}

void FileSelectionController::updateActiveSound(bool forceUpdate) {
    auto* sound = defaultSound.get();
    const auto source = activeSource.load(std::memory_order_acquire);
    if (source == ActiveSource::objectServer) {
        sound = objectServerSound.get();
    } else if (source == ActiveSource::textureInput) {
        sound = textureInputSound.get();
    } else {
        const auto index = getCurrentFileIndex();
        if (index.has_value() && contains(*index)) {
            sound = files[*index].sound.get();
        }
    }

    auto* previousSound = activeSound.exchange(sound, std::memory_order_acq_rel);
    if (sound != previousSound || forceUpdate) {
        for (int i = 0; i < voices.getNumVoices(); ++i) {
            auto* voice = dynamic_cast<ShapeVoice*>(voices.getVoice(i));
            if (voice != nullptr) {
                voice->updateSound(sound);
            }
        }
    }
}

void FileSelectionController::notifySelectionChanged() {
    if (juce::MessageManager::getInstanceWithoutCreating() != nullptr) {
        triggerAsyncUpdate();
    }
}

void FileSelectionController::handleAsyncUpdate() {
    sendChangeMessage();
}
