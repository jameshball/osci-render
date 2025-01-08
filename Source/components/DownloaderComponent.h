#pragma once

#include <JuceHeader.h>

class DownloaderThread : public juce::Thread {
public:
    DownloaderThread(juce::URL url, juce::File file, juce::URL::DownloadTaskListener* listener, juce::CriticalSection& taskLock, std::unique_ptr<juce::URL::DownloadTask>& task) : juce::Thread("Downloader Thread"), url(url), file(file), listener(listener), taskLock(taskLock), task(task) {};
    
    void run() override {
        juce::CriticalSection::ScopedLockType lock(taskLock);
        task = url.downloadToFile(file, juce::URL::DownloadTaskOptions().withListener(listener));
    };

private:
    juce::URL url;
    juce::File file;
    juce::CriticalSection& taskLock;
    juce::URL::DownloadTaskListener* listener;
    std::unique_ptr<juce::URL::DownloadTask>& task;
};

class DownloaderComponent : public juce::Component, public juce::Thread, public juce::URL::DownloadTaskListener {
public:
    DownloaderComponent(juce::URL url, juce::File file);

    void download();
    void run() override;
    void threadComplete();
    void resized() override;
    void finished(juce::URL::DownloadTask* task, bool success) override;
    void progress(juce::URL::DownloadTask* task, juce::int64 bytesDownloaded, juce::int64 totalLength) override;
    
    std::function<void()> onSuccessfulDownload;

private:
    
    juce::URL url;
    juce::File file;
    double progressValue = -1;
    juce::ProgressBar progressBar = juce::ProgressBar(progressValue);
    juce::Label successLabel;
    juce::CriticalSection taskLock;
    std::unique_ptr<juce::URL::DownloadTask> task;
    std::unique_ptr<DownloaderThread> downloader;
    
    bool uncompressOnFinish = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DownloaderComponent)
};
