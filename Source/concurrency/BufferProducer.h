#pragma once

#include <JuceHeader.h>
#include "BufferConsumer.h"

// This is needed over juce::SpinLock because juce::SpinLock yeilds, which
// leads to some consumers never holding the lock.
// TODO: verify that this is a legitimate solution.
struct crude_spinlock {
    std::atomic<bool> lock_ = {0};

    void lock() noexcept {
        for (;;) {
            // Optimistically assume the lock is free on the first try
            if (!lock_.exchange(true, std::memory_order_acquire)) {
                return;
            }
            // Wait for lock to be released without generating cache misses
            while (lock_.load(std::memory_order_relaxed)) {}
        }
    }

    bool try_lock() noexcept {
        // First do a relaxed load to check if lock is free in order to prevent
        // unnecessary cache misses if someone does while(!try_lock())
        return !lock_.load(std::memory_order_relaxed) &&
            !lock_.exchange(true, std::memory_order_acquire);
    }

    void unlock() noexcept {
        lock_.store(false, std::memory_order_release);
    }
};

class BufferProducer {
public:
    BufferProducer() {}
    ~BufferProducer() {}

    // This should add the buffers and locks to the vectors
    // and then lock the first buffer lock so it can start
    // being written to.
    // This is only called by the thread that owns the consumer thread.
    void registerConsumer(std::shared_ptr<BufferConsumer> consumer) {
        lock.lock();
        consumers.push_back(consumer);
        bufferPositions.push_back(0);
        consumer->getBuffer(true);
        lock.unlock();
    }

    // This is only called by the thread that owns the consumer thread.
    // This can't happen at the same time as write() it locks the producer lock.
    void unregisterConsumer(std::shared_ptr<BufferConsumer> consumer) {
        lock.lock();
        for (int i = 0; i < consumers.size(); i++) {
            if (consumers[i] == consumer) {
                consumer->releaseLock();
                consumers.erase(consumers.begin() + i);
                bufferPositions.erase(bufferPositions.begin() + i);
                break;
            }
        }
        lock.unlock();
    }

    // Writes a sample to the current buffer for all consumers.
    void write(float left, float right) {
        lock.lock();
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
        lock.unlock();
    }

private:
    crude_spinlock lock;
    std::vector<std::shared_ptr<BufferConsumer>> consumers;
    std::vector<int> bufferPositions;
};