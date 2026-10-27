#pragma once

#include <JuceHeader.h>

namespace motion {
// Opens exactly `requestedChannels` outputs of the standalone app's audio
// device, and no inputs: osci-motion only produces audio. On the message
// thread; a layout or device that can't take it leaves the running
// configuration as it was (or says that putting it back failed too).
juce::Result configureOutputOnlyChannels(juce::StandalonePluginHolder& holder, int requestedChannels);
}
