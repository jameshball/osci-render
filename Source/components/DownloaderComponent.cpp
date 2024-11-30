#include "DownloaderComponent.h"

DownloaderComponent::DownloaderComponent(juce::URL url, juce::File file) : juce::Thread("DownloaderComponent"), url(url), file(file) {
    addChildComponent(progressBar);
    addChildComponent(successLabel);
    
    successLabel.setText(file.getFileName() + " downloaded!", juce::dontSendNotification);
    
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
    }
    
    if (error) {
        file.deleteFile();
        progressValue = -2;
    } else {
        progressValue = 1;
        if (onSuccessfulDownload != nullptr) {
            onSuccessfulDownload();
        }
        juce::MessageManager::callAsync([this]() {
            progressBar.setVisible(false);
            successLabel.setVisible(true);
        });
        
        juce::Timer::callAfterDelay(3000, [this]() {
            successLabel.setVisible(false);
        });
    }
}

void DownloaderComponent::resized() {
    progressBar.setBounds(getLocalBounds());
    successLabel.setBounds(getLocalBounds());
}

void DownloaderComponent::finished(juce::URL::DownloadTask* task, bool success) {
    notify();
}

void DownloaderComponent::progress(juce::URL::DownloadTask* task, juce::int64 bytesDownloaded, juce::int64 totalLength) {
    if (uncompressOnFinish) {
        progressValue = ((double) bytesDownloaded / (double) totalLength) * 0.9;
    } else {
        progressValue = (double) bytesDownloaded / (double) totalLength;
    }
}

void DownloaderComponent::download() {
    progressValue = -1;
    successLabel.setVisible(false);
    progressBar.setVisible(true);
    startThread();
}
