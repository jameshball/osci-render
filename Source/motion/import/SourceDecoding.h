#pragma once

#include "../model/AssetKinds.h"
#include "../model/Document.h"

namespace motion {
// Prepares a source from its file data: drawn frames or audio. Runs on
// an import thread; `cancel` stops it and `progress` reports 0..1.
juce::Result decodeAsset(Asset& asset, const std::atomic<bool>* cancel = nullptr, std::atomic<double>* progress = nullptr, const juce::File& videoDecoder = {});
// What a video's prepared frames are kept under, for its data and settings:
// frames prepared ahead (in its settings sheet) are kept as its cache, so
// decoding it uses them.
juce::String videoCacheKey(const Asset& asset);
}
