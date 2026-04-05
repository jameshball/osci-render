#pragma once
#include <JuceHeader.h>

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

} // namespace testutil
