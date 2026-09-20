#pragma once

#include <JuceHeader.h>

namespace osci {

class WorkflowLogger {
public:
    explicit WorkflowLogger(juce::String workflowName)
        : prefix("Workflow [" + std::move(workflowName) + "]: ") {}

    void started(const juce::String& details = {}) const {
        event("started", details);
    }

    void event(const juce::String& name, const juce::String& details = {}) const {
        juce::Logger::writeToLog(prefix + name + (details.isNotEmpty() ? ": " + details : juce::String{}));
    }

    void completed(const juce::String& details = {}) const {
        event("completed", details);
    }

    void cancelled(const juce::String& stage) const {
        event("cancelled", "stage=" + stage);
    }

    void failed(const juce::String& stage, const juce::String& reason) const {
        event("failed", "stage=" + stage + (reason.isNotEmpty() ? ", reason=" + reason : juce::String{}));
    }

private:
    juce::String prefix;
};

namespace WorkflowLoggers {
inline const WorkflowLogger offlineAudioToVideo{"OfflineAudioToVideo"};
}

}
