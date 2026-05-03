#include "DownloaderComponent.h"

DownloaderComponent::DownloaderComponent() : juce::Thread("DownloaderComponent") {
    addAndMakeVisible(progress_);
    progress_.reset();
}

void DownloaderComponent::setup(juce::URL url, juce::File file) {
    this->url = url;
    this->file = file;

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
                    break;
                }
            }
        }
        wait(100);
    }
    threadComplete();
}

void DownloaderComponent::threadComplete() {
    bool error = task->hadError();
    if (uncompressOnFinish && task->isFinished() && !error) {
        juce::FileInputStream input(file);
        juce::GZIPDecompressorInputStream decompressedInput(&input, false, juce::GZIPDecompressorInputStream::gzipFormat);
        juce::File uncompressedFile = file.getSiblingFile(file.getFileNameWithoutExtension());
        juce::FileOutputStream output(uncompressedFile);
        if (output.writeFromInputStream(decompressedInput, -1) < 1) {
            uncompressedFile.deleteFile();
            error = true;
        } else {
            uncompressedFile.setExecutePermission(true);
        }
        file.deleteFile();
    }

    if (error) {
        file.deleteFile();
        progress_.showError("Download failed");
    } else {
        if (onSuccessfulDownload != nullptr) {
            onSuccessfulDownload();
        }
        progress_.showSuccess(file.getFileName() + " downloaded!");
    }
}

void DownloaderComponent::resized() {
    progress_.setBounds(getLocalBounds());
}

void DownloaderComponent::finished(juce::URL::DownloadTask* task, bool success) {
    notify();
}

void DownloaderComponent::progress(juce::URL::DownloadTask* task, juce::int64 bytesDownloaded, juce::int64 totalLength) {
    if (totalLength <= 0) {
        progress_.setProgress(-1.0);
        return;
    }
    const double fraction = (double) bytesDownloaded / (double) totalLength;
    // Reserve the last 10% for post-download decompression so the bar doesn't
    // sit at 100% while we're still working.
    progress_.setProgress(uncompressOnFinish ? fraction * 0.9 : fraction);
}

void DownloaderComponent::download() {
    progress_.reset();
    progress_.setStatus("Downloading...");
    progress_.setProgress(-1.0);
    startThread();
}
