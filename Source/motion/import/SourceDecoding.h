#pragma once

#include "../model/AssetKinds.h"
#include "../model/Document.h"

namespace motion {
// Prepares a source from its file data: drawn frames, audio or MIDI. Runs on
// an import thread; `cancel` stops it and `progress` reports 0..1.
juce::Result decodeAsset(Asset& asset, const std::atomic<bool>* cancel = nullptr, std::atomic<double>* progress = nullptr, const juce::File& videoDecoder = {});
}
