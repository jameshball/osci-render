#include "VoiceBuilder.h"

void VoiceBuilder::run() {
    while (!threadShouldExit()) {
        wait(-1);
        if (threadShouldExit()) { break; }

        // Build or remove voices one at a time, re-checking the target
        // between each operation to handle rapid slider changes.
        while (!threadShouldExit()) {
            const int target = targetCount.load(std::memory_order_acquire);
            const int current = voices.getNumVoices();

            if (current == target) {
                break;
            }

            if (current < target) {
                // Build one voice (the expensive part — runs off the
                // message and audio threads).
                auto* voice = new ShapeVoice(context, externalAudio, current);

                // Re-check: is this voice still needed?
                if (targetCount.load(std::memory_order_acquire) > current) {
                    voices.addVoice(voice); // internally locked
                    readyVoiceCount.store(current + 1, std::memory_order_release);
                    firstVoiceReady.signal();
                } else {
                    delete voice;
                }
            } else {
                // Removal is cheap — just do it directly.
                voices.removeVoice(current - 1); // internally locked
                readyVoiceCount.store(current - 1, std::memory_order_release);
            }
        }
    }
}
