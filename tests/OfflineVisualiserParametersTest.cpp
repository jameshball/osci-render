#include <JuceHeader.h>
#include "../Source/visualiser/OfflineVisualiserParameters.h"

class OfflineVisualiserParametersTest : public juce::UnitTest {
public:
    OfflineVisualiserParametersTest() : juce::UnitTest("Offline beam parameter ownership", "Motion") {}

    void runTest() override {
        beginTest("Offline snapshots copy authored effects and LFOs into independently owned storage");
        std::shared_ptr<OfflineVisualiserParameters> snapshot;
        {
            OfflineVisualiserParameters source;
            auto* parameter = source.params.hueEffect->parameters[0];
            parameter->setUnnormalisedValueNotifyingHost(240);
            parameter->lfo->setValue(0.5f);
            parameter->lfoRate->setUnnormalisedValueNotifyingHost(2.5f);
            parameter->lfoStartPercent->setUnnormalisedValueNotifyingHost(0.2f);
            parameter->lfoEndPercent->setUnnormalisedValueNotifyingHost(0.8f);
            parameter->smoothValueChange.store(0.15f);
            parameter->phase = 0.7f;
            source.params.sweepEnabled->setBoolValue(true);
            snapshot = std::make_shared<OfflineVisualiserParameters>(source.params);
            auto* captured = snapshot->params.hueEffect->parameters[0];
            expect(captured != parameter && captured->lfo != parameter->lfo && captured->lfoRate != parameter->lfoRate);
            expectEquals(captured->getValueUnnormalised(), parameter->getValueUnnormalised());
            expectEquals(captured->lfo->getValue(), parameter->lfo->getValue());
            expectEquals(captured->lfoRate->getValueUnnormalised(), 2.5f);
            expectEquals(captured->smoothValueChange.load(), 0.15f);
            expectEquals(captured->phase, 0.0f);
            expect(snapshot->params.sweepEnabled->getBoolValue());
            juce::XmlElement originalState("effect"), capturedState("effect");
            source.params.hueEffect->save(&originalState);
            snapshot->params.hueEffect->save(&capturedState);
            expectEquals(originalState.toString(), capturedState.toString());
            source.params.hueEffect->prepareToPlay(48000, 32);
            snapshot->params.hueEffect->prepareToPlay(48000, 32);
            source.params.hueEffect->animateValues(32, nullptr);
            snapshot->params.hueEffect->animateValues(32, nullptr);
            expect(source.params.hueEffect->getAnimatedValuesWritePointer(0, 32)
                != snapshot->params.hueEffect->getAnimatedValuesWritePointer(0, 32));
            parameter->setUnnormalisedValueNotifyingHost(30);
            source.params.sweepEnabled->setBoolValue(false);
            expectEquals(captured->getValueUnnormalised(), 240.0f);
            expect(snapshot->params.sweepEnabled->getBoolValue());
#if OSCI_GUI_ENABLE_ADVANCED_VISUALISER_FEATURES
            expect(snapshot->params.scaleEffect->linked != source.params.scaleEffect->linked);
            expect(snapshot->params.stereoEffect != source.params.stereoEffect);
#endif
        }
        beginTest("Snapshot lifetime survives destruction of every source parameter");
        expectEquals(snapshot->params.hueEffect->parameters[0]->getValueUnnormalised(), 240.0f);
        snapshot->params.hueEffect->animateValues(32, nullptr);
        snapshot.reset();
        // Repeated complete lifetimes also exercise duplicate linked-parameter
        // adoption and recursive LFO ownership under leak/ASan builds.
        for (int index = 0; index < 8; ++index) {
            OfflineVisualiserParameters source;
            OfflineVisualiserParameters copy(source.params);
            expect(copy.params.audioEffects[0] != source.params.audioEffects[0]);
        }
        beginTest("Processor-capturing external modulation is rejected instead of shared");
        OfflineVisualiserParameters source;
        source.params.applyExternalModulation = [](int) {};
        bool rejected = false;
        try {
            OfflineVisualiserParameters unsafe(source.params);
        } catch (const std::invalid_argument&) {
            rejected = true;
        }
        expect(rejected);
    }
};
static OfflineVisualiserParametersTest offlineVisualiserParametersTest;
