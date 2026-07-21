#include "FileSelectionManager.h"

#include <algorithm>

FileSelectionManager::Change FileSelectionManager::select(
    std::optional<std::size_t> index, Source source, std::size_t fileCount) noexcept {
    const bool realtimeSelection = source == Source::parameter || source == Source::programChange;
    auto next = state.load(std::memory_order_acquire);
    if ((next.activeSource == ActiveSource::objectServer && (realtimeSelection || source == Source::user))
        || (next.activeSource == ActiveSource::textureInput && realtimeSelection)
        || (index.has_value() && *index >= fileCount)) {
        return {};
    }

    next.hasFile = index.has_value();
    next.index = index.has_value() ? static_cast<std::uint32_t>(*index) : 0;
    if (next.activeSource != ActiveSource::objectServer) {
        next.activeSource = ActiveSource::files;
    }
    return update(next, source, true);
}

FileSelectionManager::Change FileSelectionManager::removeFile(
    std::size_t removedIndex, std::size_t remainingFileCount) noexcept {
    if (remainingFileCount == 0) {
        return select(std::nullopt, Source::internal, 0);
    }
    auto change = select(std::min(removedIndex, remainingFileCount - 1), Source::internal, remainingFileCount);
    // The entry now occupying the same index may have a different sound even when
    // the numeric selection did not change.
    change.changed = true;
    return change;
}

FileSelectionManager::Change FileSelectionManager::startTextureInput() noexcept {
    auto next = state.load(std::memory_order_acquire);
    next.activeSource = ActiveSource::textureInput;
    return update(next, Source::internal, false);
}

FileSelectionManager::Change FileSelectionManager::stopTextureInput() noexcept {
    auto next = state.load(std::memory_order_acquire);
    if (next.activeSource != ActiveSource::textureInput) {
        return {};
    }
    next.activeSource = ActiveSource::files;
    return update(next, Source::internal, false);
}

FileSelectionManager::Change FileSelectionManager::setObjectServerActive(bool active) noexcept {
    auto next = state.load(std::memory_order_acquire);
    if (active) {
        next.activeSource = ActiveSource::objectServer;
    } else if (next.activeSource == ActiveSource::objectServer) {
        next.activeSource = ActiveSource::files;
    } else {
        return {};
    }
    return update(next, Source::internal, false);
}

std::optional<std::size_t> FileSelectionManager::getSelectedFile() const noexcept {
    const auto current = state.load(std::memory_order_acquire);
    return current.hasFile ? std::optional<std::size_t>(current.index) : std::nullopt;
}

std::optional<std::size_t> FileSelectionManager::getVisibleFile() const noexcept {
    const auto current = state.load(std::memory_order_acquire);
    return current.activeSource == ActiveSource::files && current.hasFile
        ? std::optional<std::size_t>(current.index)
        : std::nullopt;
}

FileSelectionManager::ActiveSource FileSelectionManager::getActiveSource() const noexcept {
    return state.load(std::memory_order_acquire).activeSource;
}

bool FileSelectionManager::isTextureInputActive() const noexcept {
    return getActiveSource() == ActiveSource::textureInput;
}

bool FileSelectionManager::isObjectServerActive() const noexcept {
    return getActiveSource() == ActiveSource::objectServer;
}

void FileSelectionManager::queueParameterSelection(int oneBasedFileNumber) noexcept {
    const auto zeroBased = std::clamp(oneBasedFileNumber, 1, maxProgramChangeFiles) - 1;
    pending.store({ static_cast<std::uint8_t>(zeroBased), PendingKind::parameter }, std::memory_order_release);
}

void FileSelectionManager::queueParameterSelectionIfChanged(int oneBasedFileNumber) noexcept {
    if (lastObservedParameter.exchange(oneBasedFileNumber, std::memory_order_acq_rel) != oneBasedFileNumber) {
        queueParameterSelection(oneBasedFileNumber);
    }
}

void FileSelectionManager::queueProgramChange(int program, int midiChannel) noexcept {
    const int configuredChannel = getProgramChangeChannel();
    if (program < 0 || program >= maxProgramChangeFiles || configuredChannel == programChangeOff
        || (configuredChannel != programChangeOmni && configuredChannel != midiChannel)
        || getActiveSource() != ActiveSource::files) {
        return;
    }
    pending.store({ static_cast<std::uint8_t>(program), PendingKind::programChange }, std::memory_order_release);
}

std::optional<FileSelectionManager::Change> FileSelectionManager::applyPendingSelection(std::size_t fileCount) noexcept {
    const auto request = pending.exchange({}, std::memory_order_acq_rel);
    if (request.kind == PendingKind::none) {
        return std::nullopt;
    }

    const auto source = request.kind == PendingKind::programChange ? Source::programChange : Source::parameter;
    if (source == Source::programChange && request.index >= fileCount) {
        return Change {};
    }
    const auto target = fileCount == 0
        ? std::optional<std::size_t> {}
        : std::optional<std::size_t>(std::min<std::size_t>(request.index, fileCount - 1));
    return select(target, source, fileCount);
}

void FileSelectionManager::clearPendingSelection() noexcept {
    pending.store({}, std::memory_order_release);
}

int FileSelectionManager::getProgramChangeChannel() const noexcept {
    return programChangeChannel.load(std::memory_order_acquire);
}

void FileSelectionManager::setProgramChangeChannel(int channel) noexcept {
    programChangeChannel.store(std::clamp(channel, programChangeOff, 16), std::memory_order_release);
}

FileSelectionManager::Change FileSelectionManager::update(
    SelectionState next, Source source, bool shouldUpdateParameter) noexcept {
    const auto previous = state.exchange(next, std::memory_order_acq_rel);
    Change change;
    change.changed = previous != next;
    change.previousActiveSource = previous.activeSource;
    change.activeSource = next.activeSource;
    if (next.hasFile) {
        change.fileIndex = next.index;
    }
    change.updateParameter = shouldUpdateParameter && next.hasFile && source != Source::parameter;
    change.notifyHost = source == Source::user || source == Source::internal;
    if (change.updateParameter) {
        lastObservedParameter.store(static_cast<int>(next.index) + 1, std::memory_order_release);
    }
    return change;
}
