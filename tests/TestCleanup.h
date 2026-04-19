#pragma once
#include <JuceHeader.h>
#include "../Source/audio/modulation/LfoParameters.h"

namespace testutil {

// Clean up EffectParameter sub-params (LFO, sidechain) that would leak in test code.
// In production these are registered with AudioProcessor via addParameter() which owns them.
inline void cleanupSubParams(osci::EffectParameter& ep) {
    ep.disableLfo();
    ep.disableSidechain();
}

// Clean up an Effect whose parameters were heap-allocated in test code.
// Frees sub-params, deletes params, and deletes enabled/linked/selected.
inline void cleanupEffectParams(osci::Effect& effect) {
    for (auto* p : effect.parameters) {
        if (auto* ep = dynamic_cast<osci::EffectParameter*>(p))
            cleanupSubParams(*ep);
        delete p;
    }
    effect.parameters.clear();
    delete effect.enabled;  effect.enabled  = nullptr;
    delete effect.linked;   effect.linked   = nullptr;
    delete effect.selected; effect.selected = nullptr;
}

// Clean up an LfoParameters whose raw-pointer parameters were heap-allocated in
// its constructor. In production, AudioProcessor::addParameter() adopts ownership
// via getFloatParameters()/getIntParameters(); in tests (where there is no host
// processor) the params leak unless deleted explicitly.
inline void cleanupLfoParams(LfoParameters& lp) {
    for (int i = 0; i < NUM_LFOS; ++i) {
        delete lp.rate[i];          lp.rate[i] = nullptr;
        delete lp.phaseOffset[i];   lp.phaseOffset[i] = nullptr;
        delete lp.smoothAmount[i];  lp.smoothAmount[i] = nullptr;
        delete lp.delayAmount[i];   lp.delayAmount[i] = nullptr;
        delete lp.mode[i];          lp.mode[i] = nullptr;
        delete lp.preset[i];        lp.preset[i] = nullptr;
        delete lp.rateMode[i];      lp.rateMode[i] = nullptr;
        delete lp.tempoDivision[i]; lp.tempoDivision[i] = nullptr;
    }
}

} // namespace testutil
