#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <optional>

class FileSelectionManager {
public:
    enum class Source : std::uint8_t {
        user,
        internal,
        parameter,
        programChange,
        stateRestore
    };

    enum class ActiveSource : std::uint8_t {
        files,
        textureInput,
        objectServer
    };

    struct Change {
        bool changed = false;
        ActiveSource previousActiveSource = ActiveSource::files;
        ActiveSource activeSource = ActiveSource::files;
        std::optional<std::size_t> fileIndex;
        bool updateParameter = false;
        bool notifyHost = false;
    };

    static constexpr int programChangeOff = -1;
    static constexpr int programChangeOmni = 0;
    static constexpr int maxProgramChangeFiles = 100;

    Change select(std::optional<std::size_t> index, Source source, std::size_t fileCount) noexcept;
    Change removeFile(std::size_t removedIndex, std::size_t remainingFileCount) noexcept;
    Change startTextureInput() noexcept;
    Change stopTextureInput() noexcept;
    Change setObjectServerActive(bool active) noexcept;

    std::optional<std::size_t> getSelectedFile() const noexcept;
    std::optional<std::size_t> getVisibleFile() const noexcept;
    ActiveSource getActiveSource() const noexcept;
    bool isTextureInputActive() const noexcept;
    bool isObjectServerActive() const noexcept;

    void queueParameterSelection(int oneBasedFileNumber) noexcept;
    void queueParameterSelectionIfChanged(int oneBasedFileNumber) noexcept;
    void queueProgramChange(int program, int midiChannel) noexcept;
    std::optional<Change> applyPendingSelection(std::size_t fileCount) noexcept;
    void clearPendingSelection() noexcept;

    int getProgramChangeChannel() const noexcept;
    void setProgramChangeChannel(int channel) noexcept;

private:
    struct SelectionState {
        std::uint32_t index = 0;
        bool hasFile = false;
        ActiveSource activeSource = ActiveSource::files;

        bool operator==(const SelectionState&) const = default;
    };

    enum class PendingKind : std::uint8_t {
        none,
        parameter,
        programChange
    };

    struct PendingSelection {
        std::uint8_t index = 0;
        PendingKind kind = PendingKind::none;
    };

    static_assert(std::atomic<SelectionState>::is_always_lock_free);
    static_assert(std::atomic<PendingSelection>::is_always_lock_free);

    Change update(SelectionState next, Source source, bool updateParameter) noexcept;

    std::atomic<SelectionState> state { SelectionState {} };
    std::atomic<PendingSelection> pending { PendingSelection {} };
    std::atomic<int> programChangeChannel { programChangeOmni };
    std::atomic<int> lastObservedParameter { 1 };
};
