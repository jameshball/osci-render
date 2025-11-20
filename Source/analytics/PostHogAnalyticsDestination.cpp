/*
  ==============================================================================

    PostHogAnalyticsDestination.cpp
    Created: 20 Nov 2025
    Author:  James Ball

  ==============================================================================
*/

#include "PostHogAnalyticsDestination.h"

namespace osci {

PostHogAnalyticsDestination::PostHogAnalyticsDestination()
    : ThreadedAnalyticsDestination("PostHogAnalyticsThread"),
      sessionId(generateSessionId()),
      projectName(JucePlugin_Name),
      appVersion(JucePlugin_VersionString),
      osVersion(getOSVersion()),
      pluginType(getPluginType())
{
    // Start the analytics thread with initial batch period
    startAnalyticsThread(initialPeriodMs);
}

PostHogAnalyticsDestination::~PostHogAnalyticsDestination()
{
    // Allow one last batch to be sent before shutting down
    // Sleep briefly to give the background thread a chance to send the last batch
    juce::Thread::sleep(initialPeriodMs);
    stopAnalyticsThread(1000);
}

bool PostHogAnalyticsDestination::logBatchedEvents(const juce::Array<AnalyticsEvent>& events)
{
    if (events.isEmpty())
        return true;
    
    // Build batch array with all events
    juce::Array<juce::var> batchArray;
    
    for (const auto& event : events)
    {
        // Build properties object with event-specific data and metadata
        juce::DynamicObject::Ptr properties = new juce::DynamicObject();
        properties->setProperty("distinct_id", sessionId);
        properties->setProperty("project_name", projectName);
        properties->setProperty("app_version", appVersion);
        properties->setProperty("os_version", osVersion);
        properties->setProperty("plugin_type", pluginType);
        properties->setProperty("$process_person_profile", false);
        
        // Add all custom event parameters
        for (const auto& key : event.parameters.getAllKeys())
        {
            properties->setProperty(key, event.parameters[key]);
        }
        
        // Build individual event object
        juce::DynamicObject::Ptr eventObj = new juce::DynamicObject();
        eventObj->setProperty("event", event.name);
        eventObj->setProperty("properties", properties.get());
        
        // Add rounded timestamp (for anonymity)
        // event.timestamp is relative (from Time::getMillisecondCounter()), so use current time
        auto currentTimeMs = juce::Time::getCurrentTime().toMilliseconds();
        auto roundedTimestamp = roundTimestamp(currentTimeMs);
        juce::Time time(roundedTimestamp);
        eventObj->setProperty("timestamp", time.toISO8601(true));
        
        batchArray.add(juce::var(eventObj.get()));
    }
    
    // Build the main batch payload
    juce::DynamicObject::Ptr payload = new juce::DynamicObject();
    payload->setProperty("api_key", apiKey);
    payload->setProperty("batch", batchArray);
    
    // Convert to JSON
    juce::var payloadVar(payload.get());
    juce::String jsonPayload = juce::JSON::toString(payloadVar);
    
    // Create the web request
    {
        const juce::ScopedLock lock(webStreamCreation);
        
        if (shouldExit)
            return false;
        
        // Use batch endpoint
        juce::URL url("https://eu.i.posthog.com/batch/");
        url = url.withPOSTData(jsonPayload);
        
        // Create web input stream with JSON headers
        webStream.reset(new juce::WebInputStream(url, true));
        webStream->withExtraHeaders("Content-Type: application/json");
    }
    
    // Connect and send the request
    auto success = webStream->connect(nullptr);
    
    if (!success)
    {
        // Exponential backoff on failure
        periodMs = juce::jmin(periodMs * 2, 60000); // Cap at 60 seconds
        setBatchPeriod(periodMs);
        return false;
    }
    
    // Reset batch period on success
    if (periodMs != initialPeriodMs)
    {
        periodMs = initialPeriodMs;
        setBatchPeriod(periodMs);
    }
    
    return true;
}

void PostHogAnalyticsDestination::stopLoggingEvents()
{
    const juce::ScopedLock lock(webStreamCreation);
    shouldExit = true;
    
    if (webStream != nullptr)
        webStream->cancel();
}

juce::String PostHogAnalyticsDestination::generateSessionId()
{
    // Generate a random UUID for this session
    // This is anonymous and not persistent
    return juce::Uuid().toString();
}

juce::String PostHogAnalyticsDestination::getOSVersion()
{
    // Return OS name with major version - still anonymous as it doesn't identify individuals
    auto osType = juce::SystemStats::getOperatingSystemType();
    
    #if JUCE_WINDOWS
        if (osType >= juce::SystemStats::Windows11)
            return "Windows 11";
        else if (osType >= juce::SystemStats::Windows10)
            return "Windows 10";
        else if (osType >= juce::SystemStats::Windows8_1)
            return "Windows 8.1";
        else if (osType >= juce::SystemStats::Windows8_0)
            return "Windows 8";
        else if (osType >= juce::SystemStats::Windows7)
            return "Windows 7";
        return "Windows";
        
    #elif JUCE_MAC
        if (osType >= juce::SystemStats::MacOS_15)
            return "macOS 15";
        else if (osType >= juce::SystemStats::MacOS_14)
            return "macOS 14";
        else if (osType >= juce::SystemStats::MacOS_13)
            return "macOS 13";
        else if (osType >= juce::SystemStats::MacOS_12)
            return "macOS 12";
        else if (osType >= juce::SystemStats::MacOS_11)
            return "macOS 11";
        else if (osType >= juce::SystemStats::MacOSX_10_15)
            return "macOS 10.15";
        else if (osType >= juce::SystemStats::MacOSX_10_14)
            return "macOS 10.14";
        else if (osType >= juce::SystemStats::MacOSX_10_13)
            return "macOS 10.13";
        else if (osType >= juce::SystemStats::MacOSX_10_12)
            return "macOS 10.12";
        return "macOS";
        
    #elif JUCE_LINUX
        return "Linux";
        
    #elif JUCE_IOS
        // iOS doesn't have detailed version enums in JUCE, so return generic
        return "iOS";
        
    #elif JUCE_ANDROID
        return "Android";
        
    #else
        return "Unknown";
    #endif
}

juce::String PostHogAnalyticsDestination::getPluginType()
{
    // Determine if running as standalone or plugin
    #if JucePlugin_Build_Standalone
        return "standalone";
    #else
        return "plugin";
    #endif
}

juce::int64 PostHogAnalyticsDestination::roundTimestamp(juce::int64 timestamp)
{
    // Round timestamp to nearest 10 minutes for anonymity
    // This prevents precise timing analysis while maintaining useful time-of-day data
    const juce::int64 tenMinutesInMs = 600000;
    return (timestamp / tenMinutesInMs) * tenMinutesInMs;
}

} // namespace osci
