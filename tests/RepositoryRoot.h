#pragma once

#include <JuceHeader.h>

// The repository's root, for tests that read its sources and resources:
// __FILE__ when the build made it absolute (Xcode), else the nearest folder
// above the test program or the working directory that holds Source/motion
// (makefile and Visual Studio builds make __FILE__ relative to themselves).
inline juce::File repositoryRoot() {
    const auto holdsSources = [](const juce::File& folder) { return folder.getChildFile("Source/motion").isDirectory(); };
    const juce::String file(__FILE__);
    if (juce::File::isAbsolutePath(file)) {
        const auto root = juce::File(file).getParentDirectory().getParentDirectory();
        if (holdsSources(root)) { return root; }
    }
    for (auto folder : {juce::File::getSpecialLocation(juce::File::currentExecutableFile), juce::File::getCurrentWorkingDirectory()}) {
        for (int level = 0; level < 10 && folder != folder.getParentDirectory(); ++level, folder = folder.getParentDirectory()) {
            if (holdsSources(folder)) { return folder; }
        }
    }
    return {};
}
