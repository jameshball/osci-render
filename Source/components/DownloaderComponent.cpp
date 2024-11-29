#include "DownloaderComponent.h"

DownloaderComponent::DownloaderComponent(juce::URL url, juce::File file, juce::String title, juce::Component* parent) : juce::ThreadWithProgressWindow(title, true, true, 1000, juce::String(), parent), url(url), file(file) {
    if (url.toString(false).endsWithIgnoreCase(".gz")) {
        uncompressOnFinish = true;
        this->file = file.getSiblingFile(file.getFileName() + ".gz");
    }
}

void DownloaderComponent::run() {
    downloader = std::make_unique<DownloaderThread>(url, file, this, taskLock, task);
    downloader->startThread();
    while (!threadShouldExit()) {
        {
            juce::CriticalSection::ScopedTryLockType lock(taskLock);
            if (lock.isLocked() && task != nullptr) {
                if (task->isFinished()) {
                    return;
                }
            }
        }
        wait(100);
    }
}

void DownloaderComponent::threadComplete(bool userPressedCancel) {
    if (!userPressedCancel && uncompressOnFinish && task->isFinished() && !task->hadError()) {
        juce::FileInputStream input(file);
        juce::GZIPDecompressorInputStream decompressedInput(&input, false, juce::GZIPDecompressorInputStream::gzipFormat);
        juce::File uncompressedFile = file.getSiblingFile(file.getFileNameWithoutExtension());
        juce::FileOutputStream output(uncompressedFile);
        if (output.writeFromInputStream(decompressedInput, -1) < 1) {
            uncompressedFile.deleteFile();
        } else {
            uncompressedFile.setExecutePermission(true);
        }
    }
}

void DownloaderComponent::finished(juce::URL::DownloadTask* task, bool success) {
    notify();
}

void DownloaderComponent::progress(juce::URL::DownloadTask* task, juce::int64 bytesDownloaded, juce::int64 totalLength) {
    setProgress((float)bytesDownloaded / (float)totalLength);
}

void DownloaderComponent::download() {
    launchThread();
}
