namespace osci::licensing
{

juce::String HardwareInfo::getCurrentPlatform()
{
#if JUCE_MAC
   #if JUCE_ARM
    return "mac-arm64";
   #else
    return "mac-x86_64";
   #endif
#elif JUCE_WINDOWS
    return "win-x86_64";
#elif JUCE_LINUX
    return "linux-x86_64";
#else
    return "unknown";
#endif
}

juce::String HardwareInfo::getMachineFingerprint()
{
    const auto material = juce::SystemStats::getComputerName() + "|"
                        + juce::SystemStats::getLogonName() + "|"
                        + juce::SystemStats::getOperatingSystemName() + "|"
                        + juce::String (juce::SystemStats::getDeviceDescription());

    return juce::SHA256 (material.toRawUTF8(), std::strlen (material.toRawUTF8())).toHexString();
}

juce::File HardwareInfo::getDefaultStorageDirectory (juce::StringRef productSlug)
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
        .getChildFile (juce::String (productSlug));
}

} // namespace osci::licensing
