#pragma once

#include <JuceHeader.h>
#include <osci_render_core/osci_render_core.h>

#include "audio/synth/ShapeSound.h"

#include <atomic>
#include <optional>
#include <vector>

class FileParser;
class OscirenderAudioProcessor;
class VoiceManager;

class FileSelectionController : public juce::ChangeBroadcaster, private juce::AsyncUpdater {
public:
    static constexpr int programChangeOff = -1;
    static constexpr int programChangeOmni = 0;

    FileSelectionController(OscirenderAudioProcessor& processor, VoiceManager& voices);
    ~FileSelectionController() override;

    int addFile(const juce::File& file);
    int addFile(juce::String name, const char* data, int size);
    int addFile(juce::String name, std::shared_ptr<juce::MemoryBlock> data);
    void updateFile(int index, std::shared_ptr<juce::MemoryBlock> data);
    juce::String renameFile(int index, juce::String newName);
    int duplicateFile(int index);
    void removeFile(int index);
    void removeParser(FileParser* parser);

    int size() const noexcept;
    bool contains(int index) const noexcept;
    std::optional<int> getCurrentFileIndex() const noexcept;
    std::optional<int> getAdjacentFileIndex(int offset) const noexcept;
    std::shared_ptr<FileParser> getCurrentParser() const;
    std::shared_ptr<FileParser> getParser(int index) const;
    juce::String getCurrentFileName() const;
    juce::String getFileName(int index) const;
    juce::String getFileId(int index) const;
    std::shared_ptr<juce::MemoryBlock> getFileData(int index) const;

    void selectFile(int index);
    void selectAdjacentFile(int offset);
    void clearPendingSelection() noexcept;

    void startTextureInput(juce::String sourceName, int width, int height);
    void updateTextureInputFrame(const std::vector<std::uint8_t>& rgba, int width, int height, bool verticallyFlipped);
    void stopTextureInput();
    bool isTextureInputActive() const noexcept;
    juce::String getTextureInputName() const;

    bool isObjectServerActive() const noexcept;

    int getProgramChangeChannel() const noexcept;
    void setProgramChangeChannel(int channel);

    void setFileRemovedCallback(std::function<void(int)> callback);

    juce::SpinLock lock;

private:
    friend class OscirenderAudioProcessor;

    static constexpr int maxSelectableFiles = 100;

    enum class Source {
        user,
        internal,
        parameter,
        stateRestore
    };

    enum class ActiveSource {
        files,
        textureInput,
        objectServer
    };

    struct File {
        int id;
        juce::String name;
        std::shared_ptr<juce::MemoryBlock> data;
        std::shared_ptr<FileParser> parser;
        ShapeSound::Ptr sound;
    };

    int addFile(juce::String name, std::shared_ptr<juce::MemoryBlock> data,
        std::shared_ptr<FileParser> parser, ShapeSound::Ptr sound);
    void initialise();
    void clearFiles();
    void removeFile(int index, bool notifyEditor);
    void parseFile(int index);
    void selectFile(int index, Source source);
    void selectNoFile(Source source);
    void select(std::optional<int> index, Source source, bool forceSoundUpdate = false);
    std::optional<int> getSelectedFileForState() const noexcept;
    void updatePendingSelectionFromParameter() noexcept;
    void queueProgramChange(int program, int midiChannel) noexcept;
    void applyPendingSelection();
    void setObjectServerActive(bool active);
    void addObjectServerFrame(std::vector<std::unique_ptr<osci::Shape>>& frame, bool force);
    ShapeSound* getActiveSound() const noexcept;
    void saveState(juce::XmlElement& xml) const;
    void restoreState(const juce::XmlElement& xml, bool legacyFileEncoding);
    void updateActiveSound(bool forceUpdate);
    void notifySelectionChanged();
    void handleAsyncUpdate() override;

    OscirenderAudioProcessor& processor;
    VoiceManager& voices;
    std::vector<File> files;
    int nextFileId = 0;

    ShapeSound::Ptr defaultSound;
    ShapeSound::Ptr objectServerSound;
    std::shared_ptr<FileParser> textureInputParser;
    ShapeSound::Ptr textureInputSound;
    juce::String textureInputName;

    std::atomic<int> selectedFileIndex { 0 };
    std::atomic<bool> hasSelectedFile { false };
    std::atomic<int> pendingFileNumber { 0 };
    std::atomic<int> lastObservedParameter { 1 };
    std::atomic<int> programChangeChannel { programChangeOmni };
    std::atomic<ActiveSource> activeSource { ActiveSource::files };
    std::atomic<ShapeSound*> activeSound { nullptr };

    std::function<void(int)> fileRemovedCallback;
};
