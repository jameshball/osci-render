namespace osci::licensing
{

bool InstallerLauncher::launchAndExitHost (const juce::File& installerOrPayload)
{
    if (! installerOrPayload.existsAsFile())
        return false;

    return juce::Process::openDocument (installerOrPayload.getFullPathName(), {});
}

} // namespace osci::licensing
