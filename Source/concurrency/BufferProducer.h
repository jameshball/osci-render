#pragma once

#include <JuceHeader.h>
#include "BufferConsumer.h"

class BufferProducer {
public:
    BufferProducer() {}
    ~BufferProducer() {}

    // This should add the buffers and locks to the vectors
    // and then lock the first buffer lock so it can start
    // being written to.
    // This is only called by the thread that owns the consumer thread.
    void registerConsumer(std::shared_ptr<BufferConsumer> consumer) {
        juce::SpinLock::ScopedLockType scope(lock);
        consumers.push_back(consumer);
        bufferPositions.push_back(0);
        consumer->getBuffer(true);
    }

    // This is only called by the thread that owns the consumer thread.
    // This can't happen at the same time as write() it locks the producer lock.
    void unregisterConsumer(std::shared_ptr<BufferConsumer> consumer) {
        juce::SpinLock::ScopedLockType scope(lock);
        for (int i = 0; i < consumers.size(); i++) {
            if (consumers[i] == consumer) {
                consumer->releaseLock();
                consumers.erase(consumers.begin() + i);
                bufferPositions.erase(bufferPositions.begin() + i);
                break;
            }
        }
    }

    // Writes a sample to the current buffer for all consumers.
    void write(float left, float right) {
        juce::SpinLock::ScopedLockType scope(lock);
        for (int i = 0; i < consumers.size(); i++) {
            std::shared_ptr<std::vector<float>> buffer = consumers[i]->getBuffer(false);
            if (buffer == nullptr) {
                continue;
            }

            (*buffer)[bufferPositions[i]] = left;
            (*buffer)[bufferPositions[i] + 1] = right;
            bufferPositions[i] += 2;

            // If we've reached the end of the buffer, switch
            // to the other buffer and unlock it. This signals
            // to the consumer that it can start processing!
            if (bufferPositions[i] >= buffer->size()) {
                bufferPositions[i] = 0;
                consumers[i]->finishedWriting();
            }
        }
    }

private:
    juce::SpinLock lock;
    std::vector<std::shared_ptr<BufferConsumer>> consumers;
    std::vector<int> bufferPositions;
};