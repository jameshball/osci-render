#pragma once

namespace osci::licensing
{

class InstallerLauncher final
{
public:
    static bool launchAndExitHost (const juce::File& installerOrPayload);

private:
    InstallerLauncher() = delete;
};

} // namespace osci::licensing
