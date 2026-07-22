#include "FileController.h"

#include "PluginProcessor.h"
#include "audio/synth/ShapeVoice.h"
#include "audio/synth/VoiceManager.h"
#include "parser/FileParser.h"

#include <algorithm>

FileController::FileController(OscirenderAudioProcessor& processor, VoiceManager& voices)
    : processor(processor), voices(voices) {}

FileController::~FileController() {
    processor.midiManager.setMessageHandler(osci::MidiManager::MessageType::programChange, {});
    cancelPendingUpdate();
}

void FileController::initialise() {
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

int FileController::addFile(const juce::File& file) {
    auto data = std::make_shared<juce::MemoryBlock>();
    auto stream = file.createInputStream();
    if (stream != nullptr) {
        stream->readIntoMemoryBlock(*data);
    }
    return addFile(file.getFileName(), std::move(data));
}

int FileController::addFile(juce::String name, const char* data, int size) {
    auto block = std::make_shared<juce::MemoryBlock>();
    block->append(data, static_cast<size_t>(size));
    return addFile(std::move(name), std::move(block));
}

int FileController::addFile(juce::String name, std::shared_ptr<juce::MemoryBlock> data) {
    juce::SpinLock::ScopedLockType fileLock(lock);
    juce::SpinLock::ScopedLockType effectLock(processor.effectsLock);
    auto parser = std::make_shared<FileParser>(processor, processor.errorCallback);
    ShapeSound::Ptr sound = new ShapeSound(processor, parser);
    const int index = appendFile(std::move(name), std::move(data), std::move(parser), std::move(sound));
    selectFileUnlocked(index);
    return index;
}

int FileController::appendFile(juce::String name, std::shared_ptr<juce::MemoryBlock> data,
    std::shared_ptr<FileParser> parser, ShapeSound::Ptr sound) {
    files.push_back({ nextFileId++, std::move(name), std::move(data), std::move(parser), std::move(sound) });
    const int index = size() - 1;
    parseFile(index);
    return index;
}

void FileController::updateFile(int index, std::shared_ptr<juce::MemoryBlock> data) {
    juce::SpinLock::ScopedLockType fileLock(lock);
    juce::SpinLock::ScopedLockType effectLock(processor.effectsLock);
    if (!contains(index)) {
        return;
    }
    files[index].data = std::move(data);
    parseFile(index);
    selectFileUnlocked(index);
}

juce::String FileController::renameFile(int index, juce::String newName) {
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

int FileController::duplicateFile(int index) {
    juce::SpinLock::ScopedLockType fileLock(lock);
    juce::SpinLock::ScopedLockType effectLock(processor.effectsLock);
    if (!contains(index)) {
        return -1;
    }

    const juce::File source(files[index].name);
    auto data = std::make_shared<juce::MemoryBlock>(*files[index].data);
    auto parser = std::make_shared<FileParser>(processor, processor.errorCallback);
    ShapeSound::Ptr sound = new ShapeSound(processor, parser);
    const int duplicateIndex = appendFile(source.getFileNameWithoutExtension() + " copy" + source.getFileExtension(),
        std::move(data), std::move(parser), std::move(sound));
    selectFileUnlocked(duplicateIndex);
    return duplicateIndex;
}

void FileController::removeFile(int index) {
    juce::SpinLock::ScopedLockType fileLock(lock);
    juce::SpinLock::ScopedLockType effectLock(processor.effectsLock);
    removeFileUnlocked(index);
}

void FileController::removeFileUnlocked(int index) {
    if (!contains(index)) {
        return;
    }

    files.erase(files.begin() + index);
    if (files.empty()) {
        clearSelection();
    } else {
        selectFileUnlocked(std::min(index, size() - 1), true);
    }

    if (fileRemovedCallback) {
        fileRemovedCallback(index);
    }
}

void FileController::removeParser(FileParser* parser) {
    juce::SpinLock::ScopedLockType fileLock(lock);
    juce::SpinLock::ScopedLockType effectLock(processor.effectsLock);
    const auto match = std::find_if(files.begin(), files.end(), [parser](const File& file) {
        return file.parser.get() == parser;
    });
    if (match != files.end()) {
        removeFileUnlocked(static_cast<int>(std::distance(files.begin(), match)));
    }
}

void FileController::clearFiles() {
    while (!files.empty()) {
        removeFileUnlocked(0);
    }
}

int FileController::size() const noexcept {
    return static_cast<int>(files.size());
}

bool FileController::contains(int index) const noexcept {
    return index >= 0 && index < size();
}

std::optional<int> FileController::getCurrentFileIndex() const noexcept {
    if (activeSource.load(std::memory_order_acquire) != ActiveSource::files
        || !hasSelectedFile.load(std::memory_order_acquire)) {
        return std::nullopt;
    }
    return selectedFileIndex.load(std::memory_order_acquire);
}

std::optional<int> FileController::getAdjacentFileIndex(int offset) const noexcept {
    const auto current = getSelectedFileForState();
    if (!current.has_value()) {
        return std::nullopt;
    }
    const int adjacent = *current + offset;
    return contains(adjacent) ? std::optional<int>(adjacent) : std::nullopt;
}

std::shared_ptr<FileParser> FileController::getCurrentParser() const {
    if (isTextureInputActive()) {
        return textureInputParser;
    }
    const auto index = getCurrentFileIndex();
    return index.has_value() ? getParser(*index) : nullptr;
}

std::shared_ptr<FileParser> FileController::getParser(int index) const {
    return contains(index) ? files[index].parser : nullptr;
}

juce::String FileController::getCurrentFileName() const {
    if (isTextureInputActive()) {
        return textureInputName;
    }
    const auto index = getCurrentFileIndex();
    return index.has_value() ? getFileName(*index) : juce::String();
}

juce::String FileController::getFileName(int index) const {
    return contains(index) ? files[index].name : juce::String();
}

juce::String FileController::getFileId(int index) const {
    return contains(index) ? juce::String(files[index].id) : juce::String();
}

std::shared_ptr<juce::MemoryBlock> FileController::getFileData(int index) const {
    return contains(index) ? files[index].data : nullptr;
}

void FileController::selectFile(int index) {
    juce::SpinLock::ScopedLockType fileLock(lock);
    juce::SpinLock::ScopedLockType effectLock(processor.effectsLock);
    selectFileUnlocked(index);
}

void FileController::selectAdjacentFile(int offset) {
    juce::SpinLock::ScopedLockType fileLock(lock);
    juce::SpinLock::ScopedLockType effectLock(processor.effectsLock);
    const auto adjacentFile = getAdjacentFileIndex(offset);
    if (adjacentFile.has_value()) {
        selectFileUnlocked(*adjacentFile);
    }
}

void FileController::selectFileUnlocked(int index, bool forceSoundUpdate) {
    if (!contains(index)) {
        return;
    }

    const int fileNumber = index + 1;
    processor.fileSelect->setUnnormalisedValueNotifyingHost(static_cast<float>(fileNumber));
    lastObservedParameter.store(fileNumber, std::memory_order_release);
    applySelection(index, forceSoundUpdate);
}

void FileController::applySelection(int index, bool forceSoundUpdate) {
    jassert(contains(index));
    const auto previousFile = getSelectedFileForState();
    selectedFileIndex.store(index, std::memory_order_release);
    hasSelectedFile.store(true, std::memory_order_release);
    const bool changed = previousFile != index;
    if (changed || forceSoundUpdate) {
        updateActiveSound(forceSoundUpdate);
        notifySelectionChanged();
    }
}

void FileController::clearSelection() {
    jassert(files.empty());
    if (!hasSelectedFile.exchange(false, std::memory_order_acq_rel)) {
        return;
    }
    updateActiveSound(false);
    notifySelectionChanged();
}

void FileController::updatePendingSelectionFromParameter() noexcept {
    const int fileNumber = std::clamp(processor.fileSelect->getValueUnnormalised(), 1, maxSelectableFiles);
    if (lastObservedParameter.exchange(fileNumber, std::memory_order_acq_rel) != fileNumber) {
        pendingFileNumber.store(fileNumber, std::memory_order_release);
    }
}

void FileController::queueProgramChange(int program, int midiChannel) noexcept {
    const int configuredChannel = getProgramChangeChannel();
    if (configuredChannel == programChangeOff || (configuredChannel != programChangeOmni && configuredChannel != midiChannel)) {
        return;
    }

    const int fileNumber = std::clamp(program + 1, 1, maxSelectableFiles);
    processor.fileSelect->setValueUnnormalised(static_cast<float>(fileNumber));
    lastObservedParameter.store(fileNumber, std::memory_order_release);
    pendingFileNumber.store(fileNumber, std::memory_order_release);
}

void FileController::applyPendingSelection() {
    const int fileNumber = pendingFileNumber.exchange(0, std::memory_order_acq_rel);
    if (fileNumber == 0 || files.empty()) {
        return;
    }
    applySelection(std::min(fileNumber - 1, size() - 1));
}

void FileController::clearPendingSelection() noexcept {
    pendingFileNumber.store(0, std::memory_order_release);
}

void FileController::startTextureInput(juce::String sourceName, int width, int height) {
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

void FileController::updateTextureInputFrame(
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

void FileController::stopTextureInput() {
    juce::SpinLock::ScopedLockType fileLock(lock);
    juce::SpinLock::ScopedLockType effectLock(processor.effectsLock);
    if (activeSource.exchange(ActiveSource::files, std::memory_order_acq_rel) == ActiveSource::textureInput) {
        textureInputName.clear();
        updateActiveSound(false);
        notifySelectionChanged();
    }
}

bool FileController::isTextureInputActive() const noexcept {
    return activeSource.load(std::memory_order_acquire) == ActiveSource::textureInput;
}

juce::String FileController::getTextureInputName() const {
    return textureInputName;
}

void FileController::setObjectServerActive(bool active) {
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

bool FileController::isObjectServerActive() const noexcept {
    return activeSource.load(std::memory_order_acquire) == ActiveSource::objectServer;
}

void FileController::addObjectServerFrame(std::vector<std::unique_ptr<osci::Shape>>& frame, bool force) {
    objectServerSound->addFrame(frame, force);
}

ShapeSound* FileController::getActiveSound() const noexcept {
    return activeSound.load(std::memory_order_acquire);
}

std::optional<int> FileController::getSelectedFileForState() const noexcept {
    if (!hasSelectedFile.load(std::memory_order_acquire)) {
        return std::nullopt;
    }
    return selectedFileIndex.load(std::memory_order_acquire);
}

int FileController::getProgramChangeChannel() const noexcept {
    return programChangeChannel.load(std::memory_order_acquire);
}

void FileController::setProgramChangeChannel(int channel) {
    const int validChannel = std::clamp(channel, programChangeOff, 16);
    programChangeChannel.store(validChannel, std::memory_order_release);
    if (juce::JUCEApplicationBase::isStandaloneApp()) {
        processor.globalSettings.set("programChangeChannel", validChannel);
        processor.globalSettings.save();
    }
}

void FileController::saveState(juce::XmlElement& xml) const {
    xml.setAttribute("programChangeChannel", getProgramChangeChannel());
    auto* filesXml = xml.createNewChildElement("files");
    for (const auto& file : files) {
        auto* fileXml = filesXml->createNewChildElement("file");
        fileXml->setAttribute("name", file.name);
        fileXml->addTextElement(file.data->toBase64Encoding());
    }
    const auto selectedFile = getSelectedFileForState();
    jassert(files.empty() || selectedFile.has_value());
    xml.setAttribute("currentFile", files.empty() ? -1 : selectedFile.value_or(0));
}

void FileController::restoreState(const juce::XmlElement& xml, bool legacyFileEncoding) {
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
            appendFile(fileXml->getStringAttribute("name"), std::move(data), std::move(parser), std::move(sound));
        }
        juce::Logger::writeToLog("setStateInformation: restored " + juce::String(size()) + " files");
    } else {
        juce::Logger::writeToLog("setStateInformation: no files section found");
    }

    clearPendingSelection();
    if (files.empty()) {
        clearSelection();
    } else {
        const int restoredFile = xml.getIntAttribute("currentFile", 0);
        const int selectedFile = contains(restoredFile) ? restoredFile : 0;
        const int fileNumber = selectedFile + 1;
        processor.fileSelect->setValueUnnormalised(static_cast<float>(fileNumber));
        lastObservedParameter.store(fileNumber, std::memory_order_release);
        applySelection(selectedFile);
    }
}

void FileController::setFileRemovedCallback(std::function<void(int)> callback) {
    fileRemovedCallback = std::move(callback);
}

void FileController::parseFile(int index) {
    auto& file = files[index];
    if (file.data == nullptr) {
        return;
    }
    const auto extension = file.name.fromLastOccurrenceOf(".", true, false).toLowerCase();
    file.parser->parse(juce::String(file.id), file.name, extension,
        std::make_unique<juce::MemoryInputStream>(*file.data, false), processor.font);
}

void FileController::updateActiveSound(bool forceUpdate) {
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

void FileController::notifySelectionChanged() {
    if (juce::MessageManager::getInstanceWithoutCreating() != nullptr) {
        triggerAsyncUpdate();
    }
}

void FileController::handleAsyncUpdate() {
    sendChangeMessage();
}
