#include <JuceHeader.h>
#include <juce_mts_esp/juce_mts_esp.h>
#include <cmath>

class MTSESPClientTest : public juce::UnitTest {
public:
    MTSESPClientTest() : juce::UnitTest("MTS-ESP Client", "MTS-ESP") {}

    void runTest() override {
        beginTest("Client construction and destruction");
        {
            // Should not crash even if MTS-ESP library is not installed.
            mts_esp::Client client;
            // Without a master plugin, hasMaster should be false.
            expect(!client.hasMaster(), "No master should be connected in test environment");
        }

        beginTest("Fallback 12-TET tuning");
        {
            mts_esp::Client client;
            // Without a master, noteToFrequency should return standard 12-TET.
            // A4 = MIDI note 69 = 440 Hz
            expectWithinAbsoluteError(client.noteToFrequency(69), 440.0, 0.01);
            // Middle C = MIDI note 60
            expectWithinAbsoluteError(client.noteToFrequency(60), 261.63, 0.01);
            // C5 = MIDI note 72
            expectWithinAbsoluteError(client.noteToFrequency(72), 523.25, 0.01);
            // MIDI note 0
            expectWithinAbsoluteError(client.noteToFrequency(0), 8.18, 0.01);
            // MIDI note 127
            expectWithinAbsoluteError(client.noteToFrequency(127), 12543.85, 0.1);
        }

        beginTest("Channel parameter does not affect fallback tuning");
        {
            mts_esp::Client client;
            double freqNoCh = client.noteToFrequency(69, -1);
            double freqCh0 = client.noteToFrequency(69, 0);
            double freqCh15 = client.noteToFrequency(69, 15);
            expectWithinAbsoluteError(freqNoCh, 440.0, 0.01);
            expectWithinAbsoluteError(freqCh0, 440.0, 0.01);
            expectWithinAbsoluteError(freqCh15, 440.0, 0.01);
        }

        beginTest("Disabled client returns fallback tuning");
        {
            mts_esp::Client client;
            client.setEnabled(false);
            expect(!client.isEnabled());
            expectWithinAbsoluteError(client.noteToFrequency(69), 440.0, 0.01);
            expectWithinAbsoluteError(client.noteToFrequency(60), 261.63, 0.01);
        }

        beginTest("Custom fallback reference frequency");
        {
            mts_esp::Client client;
            client.setEnabled(false);
            client.setFallbackReferenceFrequency(432.0);
            expectWithinAbsoluteError(client.getFallbackReferenceFrequency(), 432.0, 0.001);
            // A4 with 432 Hz reference
            expectWithinAbsoluteError(client.noteToFrequency(69), 432.0, 0.01);
            // Middle C with 432 Hz reference
            double expectedC4 = 432.0 * std::pow(2.0, (60 - 69) / 12.0);
            expectWithinAbsoluteError(client.noteToFrequency(60), expectedC4, 0.01);
        }

        beginTest("shouldFilterNote returns false without master");
        {
            mts_esp::Client client;
            // No master connected, no notes should be filtered.
            for (int note = 0; note < 128; ++note)
                expect(!client.shouldFilterNote(note), "Note " + juce::String(note) + " should not be filtered");
        }

        beginTest("frequencyToNote roundtrip");
        {
            mts_esp::Client client;
            // Without master, should round-trip through 12-TET.
            for (int note = 12; note < 116; ++note) {
                double freq = client.noteToFrequency(note);
                int recovered = client.frequencyToNote(freq);
                expectEquals(recovered, note, "Round-trip failed for note " + juce::String(note));
            }
        }

        beginTest("retuningInSemitones is zero without master");
        {
            mts_esp::Client client;
            expectWithinAbsoluteError(client.retuningInSemitones(69), 0.0, 0.001);
            expectWithinAbsoluteError(client.retuningInSemitones(60), 0.0, 0.001);
        }

        beginTest("retuningAsRatio is 1.0 without master");
        {
            mts_esp::Client client;
            expectWithinAbsoluteError(client.retuningAsRatio(69), 1.0, 0.001);
            expectWithinAbsoluteError(client.retuningAsRatio(60), 1.0, 0.001);
        }

        beginTest("Scale info defaults without master");
        {
            mts_esp::Client client;
            expectWithinAbsoluteError(client.getPeriodRatio(), 2.0, 0.001);
            expectWithinAbsoluteError(client.getPeriodSemitones(), 12.0, 0.001);
            expectEquals(client.getMapSize(), -1);
            expectEquals(client.getMapStartKey(), -1);
            expectEquals(client.getRefKey(), -1);
        }

        beginTest("parseMidiBuffer does not crash on empty buffer");
        {
            mts_esp::Client client;
            juce::MidiBuffer emptyBuffer;
            client.parseMidiBuffer(emptyBuffer);
        }

        beginTest("parseMidiBuffer does not crash on normal MIDI");
        {
            mts_esp::Client client;
            juce::MidiBuffer buffer;
            buffer.addEvent(juce::MidiMessage::noteOn(1, 60, 0.8f), 0);
            buffer.addEvent(juce::MidiMessage::noteOff(1, 60, 0.0f), 100);
            client.parseMidiBuffer(buffer);
        }

        beginTest("Enable/disable toggle");
        {
            mts_esp::Client client;
            expect(client.isEnabled());
            client.setEnabled(false);
            expect(!client.isEnabled());
            client.setEnabled(true);
            expect(client.isEnabled());
        }
    }
};

static MTSESPClientTest mtsEspClientTest;
