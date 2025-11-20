/*
  ==============================================================================

    PostHogAnalyticsDestination.h
    Created: 20 Nov 2025
    Author:  James Ball
    
    PostHog analytics destination for tracking anonymous app usage.
    
    Usage example:
    
    // Log a simple event
    audioProcessor.logAnalyticsEvent("feature_used");
    
    // Log an event with parameters
    juce::StringPairArray params;
    params.set("file_type", "wav");
    params.set("duration_seconds", juce::String(duration));
    audioProcessor.logAnalyticsEvent("file_loaded", params);
    
    All events automatically include:
    - Anonymized session ID (changes per app launch)
    - Project name (e.g., "osci-render", "sosci")
    - App version
    - OS version (e.g., "macOS", "Windows")
    - Plugin type ("standalone" or "plugin")
    - Rounded timestamp (to nearest hour for anonymity)

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>

namespace osci {

/**
 * Analytics destination that sends events to PostHog.
 * This follows the JUCE analytics pattern but sends data to PostHog instead of Google Analytics.
 */
class PostHogAnalyticsDestination : public juce::ThreadedAnalyticsDestination
{
public:
    PostHogAnalyticsDestination();
    ~PostHogAnalyticsDestination() override;

    // ThreadedAnalyticsDestination overrides
    bool logBatchedEvents(const juce::Array<AnalyticsEvent>& events) override;
    void stopLoggingEvents() override;
    int getMaximumBatchSize() override { return 20; }
    
    // No persistence - events are not saved/restored
    void saveUnloggedEvents(const std::deque<AnalyticsEvent>&) override {}
    void restoreUnloggedEvents(std::deque<AnalyticsEvent>&) override {}

private:
    // PostHog API configuration
    const juce::String apiKey = "phc_hvPejmk7pP0zsG8GzQfwSfsE2ud4oGz9Q4zeWAvPUou";
    
    // Session ID generated at startup (anonymous, not persistent)
    const juce::String sessionId;
    
    // App metadata
    const juce::String projectName;
    const juce::String appVersion;
    const juce::String osVersion;
    const juce::String pluginType;
    
    // Batch period management
    static constexpr int initialPeriodMs = 2000; // 2 seconds
    int periodMs = initialPeriodMs;
    
    // Thread safety
    juce::CriticalSection webStreamCreation;
    std::unique_ptr<juce::WebInputStream> webStream;
    std::atomic<bool> shouldExit { false };
    
    // Helper methods
    static juce::String generateSessionId();
    static juce::String getOSVersion();
    static juce::String getPluginType();
    static juce::int64 roundTimestamp(juce::int64 timestamp);
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PostHogAnalyticsDestination)
};

} // namespace osci
