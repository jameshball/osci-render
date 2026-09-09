#pragma once

#include <JuceHeader.h>
#include <osci_render_core/osci_render_core.h>

#include "audio/synth/ShapeSound.h"
#include "scene/Scene.h"

#include <atomic>
#include <optional>
#include <vector>

class FileParser;
class OscirenderAudioProcessor;
class VoiceManager;

class FileController : public juce::ChangeBroadcaster, private juce::AsyncUpdater {
public:
    static constexpr int programChangeOff = -1;
    static constexpr int programChangeOmni = 0;

    FileController(OscirenderAudioProcessor& processor, VoiceManager& voices);
    ~FileController() override;

    int addFile(const juce::File& file);
    int addFile(juce::String name, const char* data, int size);
    int addFile(juce::String name, std::shared_ptr<juce::MemoryBlock> data);
    void updateFile(int index, std::shared_ptr<juce::MemoryBlock> data);
    void updateFileById(const juce::String& id, std::shared_ptr<juce::MemoryBlock> data);
    juce::String renameFile(int index, juce::String newName);
    int duplicateFile(int index);
    int restoreFile(int index, juce::String name, std::shared_ptr<juce::MemoryBlock> data, const juce::XmlElement& scene);
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

    scene::Automation sceneAutomation;
    std::shared_ptr<scene::Scene> getScene(int index) const;
    std::shared_ptr<scene::Scene> ensureScene(int index);
    std::shared_ptr<scene::Object> addSceneObject(int index, juce::String name, std::shared_ptr<juce::MemoryBlock> data);
    std::shared_ptr<scene::Object> addLiveSceneObject(int index, bool blender);
    void updateSceneObject(const std::shared_ptr<scene::Object>& object, juce::String text);
    void sceneChanged();
    void saveScene(const scene::Scene& scene, juce::XmlElement& xml) const;
    void restoreScene(int index, const juce::XmlElement& xml, bool copy = false);

    juce::SpinLock lock;

private:
    friend class OscirenderAudioProcessor;

    static constexpr int maxSelectableFiles = 100;

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

    int appendFile(juce::String name, std::shared_ptr<juce::MemoryBlock> data,
        std::shared_ptr<FileParser> parser, ShapeSound::Ptr sound);
    std::shared_ptr<scene::Scene> ensureSceneUnlocked(int index);
    void initialise();
    void clearFiles();
    void updateFileUnlocked(int index, std::shared_ptr<juce::MemoryBlock> data);
    std::optional<int> findFileIndexByIdUnlocked(const juce::String& id) const;
    void removeFileUnlocked(int index);
    void parseFile(int index);
    void selectFileUnlocked(int index, bool forceSoundUpdate = false);
    void applySelection(int index, bool forceSoundUpdate = false);
    void clearSelection();
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
    std::weak_ptr<scene::Object> blenderSceneObject;
    std::weak_ptr<scene::Object> textureSceneObject;

    std::atomic<int> selectedFileIndex { 0 };
    std::atomic<bool> hasSelectedFile { false };
    std::atomic<int> pendingFileNumber { 0 };
    std::atomic<int> lastObservedParameter { 1 };
    std::atomic<int> programChangeChannel { programChangeOmni };
    std::atomic<ActiveSource> activeSource { ActiveSource::files };
    std::atomic<ShapeSound*> activeSound { nullptr };

    std::function<void(int)> fileRemovedCallback;
};
