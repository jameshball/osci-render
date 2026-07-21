#pragma once

#include <JuceHeader.h>
#include <osci_render_core/osci_render_core.h>

#include <atomic>
#include <optional>
#include <vector>

class FileParser;

class FileSelectionController : public juce::ChangeBroadcaster, private juce::AsyncUpdater {
public:
    using SoundPtr = juce::ReferenceCountedObjectPtr<juce::SynthesiserSound>;

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
        SoundPtr sound;
    };

    struct FileResources {
        std::shared_ptr<FileParser> parser;
        SoundPtr sound;
    };

    struct Dependencies {
        std::function<FileResources()> createFileResources;
        std::function<void(File&, juce::Font&)> parseFile;
        std::function<void(const std::shared_ptr<FileParser>&, int, int)> prepareTextureInput;
        std::function<void(const std::shared_ptr<FileParser>&, const std::vector<std::uint8_t>&, int, int, bool)> updateTextureInput;
        std::function<void(juce::SynthesiserSound*)> activeSoundChanged;
    };

    static constexpr int programChangeOff = -1;
    static constexpr int programChangeOmni = 0;
    static constexpr int maxSelectableFiles = 100;

    FileSelectionController(osci::IntParameter& fileSelectParameter, juce::Font& font, Dependencies dependencies);
    ~FileSelectionController() override;

    void initialise(SoundPtr defaultSound, SoundPtr objectServerSound);

    int addFile(const juce::File& file);
    int addFile(juce::String name, const char* data, int size);
    int addFile(juce::String name, std::shared_ptr<juce::MemoryBlock> data);
    void updateFile(int index, std::shared_ptr<juce::MemoryBlock> data);
    juce::String renameFile(int index, juce::String newName);
    int duplicateFile(int index);
    void removeFile(int index);
    void removeParser(FileParser* parser);
    void clear();

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

    void selectFile(int index, Source source = Source::user);
    void selectNoFile(Source source = Source::internal);
    void queueParameterSelectionIfChanged(int oneBasedFileNumber) noexcept;
    void queueProgramChange(int program, int midiChannel) noexcept;
    void applyPendingSelection();
    void clearPendingSelection() noexcept;

    void startTextureInput(juce::String sourceName, int width, int height);
    void updateTextureInputFrame(const std::vector<std::uint8_t>& rgba, int width, int height, bool verticallyFlipped);
    void stopTextureInput();
    bool isTextureInputActive() const noexcept;
    juce::String getTextureInputName() const;

    void setObjectServerActive(bool active);
    bool isObjectServerActive() const noexcept;
    juce::SynthesiserSound& getObjectServerSound() const noexcept;

    juce::SynthesiserSound* getActiveSound() const noexcept;
    std::optional<int> getSelectedFileForState() const noexcept;

    int getProgramChangeChannel() const noexcept;
    void setProgramChangeChannel(int channel);

    void setFileRemovedCallback(std::function<void(int)> callback);

    juce::SpinLock lock;

private:
    int addFile(juce::String name, std::shared_ptr<juce::MemoryBlock> data,
        std::shared_ptr<FileParser> parser, SoundPtr sound);
    void parseFile(int index);
    void select(std::optional<int> index, Source source, bool forceSoundUpdate = false);
    void updateActiveSound(bool forceUpdate);
    void notifySelectionChanged();
    void handleAsyncUpdate() override;

    osci::IntParameter& fileSelectParameter;
    juce::Font& font;
    Dependencies dependencies;
    std::vector<File> files;
    int nextFileId = 0;

    SoundPtr defaultSound;
    SoundPtr objectServerSound;
    std::shared_ptr<FileParser> textureInputParser;
    SoundPtr textureInputSound;
    juce::String textureInputName;

    std::atomic<int> selectedFileIndex { 0 };
    std::atomic<bool> hasSelectedFile { false };
    std::atomic<int> pendingFileNumber { 0 };
    std::atomic<int> lastObservedParameter { 1 };
    std::atomic<int> programChangeChannel { programChangeOmni };
    std::atomic<ActiveSource> activeSource { ActiveSource::files };
    std::atomic<juce::SynthesiserSound*> activeSound { nullptr };

    std::function<void(int)> fileRemovedCallback;
};
