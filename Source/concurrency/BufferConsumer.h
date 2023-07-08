#pragma once

#include <JuceHeader.h>

// This is a helper class for the producer and consumer threads.
//
// ORDER OF OPERATIONS:
// 1. Consumer is created.
// 2. The thread that owns the consumers calls registerConsumer() on the producer, which acquires the lock on the first buffer to be written to by calling getBuffer().
// LOOP:
// 3. The consumer calls startProcessing() to signal that they want to start processing the current buffer.
// 4. The producer calls finishedWriting() to signal that they have finished writing to the current buffer, which gives the lock to the consumer.
// 5. The consumer calls finishedProcessing() to signal that they have finished processing the current buffer.
// 6. The producer calls getBuffer() to acquire the lock on the next buffer to be written to.
// GOTO LOOP
// 7. The thread that owns the consumer calls unregisterConsumer() on the producer at some point during the loop, which releases the lock on the current buffer.
//
class BufferConsumer {
public:
    // bufferSize MUST be a multiple of 2.
    BufferConsumer(int bufferSize) {
        firstBuffer->resize(bufferSize, 0.0);
        secondBuffer->resize(bufferSize, 0.0);
    }

    ~BufferConsumer() {}

    // Returns the buffer that is ready to be written to.
    // This is only called by the producer thread.
    // force forces the lock to be acquired.
    // Returns nullptr if the lock can't be acquired when force is false.
    // It is only called when the global producer lock is held.
    std::shared_ptr<std::vector<float>> getBuffer(bool force) {
        auto buffer = firstBufferWriting ? firstBuffer : secondBuffer;
        if (lockHeldForWriting) {
            return buffer;
        }
        auto bufferLock = firstBufferWriting ? firstBufferLock : secondBufferLock;

        if (force) {
            bufferLock->enter();
            lockHeldForWriting = true;
            return buffer;
        } else if (bufferLock->tryEnter()) {
            lockHeldForWriting = true;
            return buffer;
        } else {
            return nullptr;
        }
    }

    // This is only called by the producer thread. It is only called when the global
    // producer lock is held.
    void finishedWriting() {
        auto bufferLock = firstBufferWriting ? firstBufferLock : secondBufferLock;
        lockHeldForWriting = false;
        firstBufferWriting = !firstBufferWriting;
        // Try locking before we unlock the current buffer so that
        // the consumer doesn't start processing before we
        // unlock the buffer. Ignore if we can't get the lock
        // because the consumer is still processing.
        getBuffer(false);
        bufferLock->exit();
    }

    void releaseLock() {
        if (lockHeldForWriting) {
            auto bufferLock = firstBufferWriting ? firstBufferLock : secondBufferLock;
            bufferLock->exit();
        }
    }

    // Returns the buffer that has been written to fully and is ready to be processed.
    // This will lock the buffer so that the producer can't write to it while we're processing.
    std::shared_ptr<std::vector<float>> startProcessing() {
        auto buffer = firstBufferProcessing ? firstBuffer : secondBuffer;
        auto bufferLock = firstBufferProcessing ? firstBufferLock : secondBufferLock;

        bufferLock->enter();
        return buffer;
    }

    // This should be called after processing has finished.
    // It releases the lock on the buffer so that the producer can write to it again.
    void finishedProcessing() {
        auto bufferLock = firstBufferProcessing ? firstBufferLock : secondBufferLock;
        firstBufferProcessing = !firstBufferProcessing;
        bufferLock->exit();
    }

    std::shared_ptr<std::vector<float>> firstBuffer = std::make_shared<std::vector<float>>();
    std::shared_ptr<std::vector<float>> secondBuffer = std::make_shared<std::vector<float>>();
    std::shared_ptr<juce::SpinLock> firstBufferLock = std::make_shared<juce::SpinLock>();
    std::shared_ptr<juce::SpinLock> secondBufferLock = std::make_shared<juce::SpinLock>();
private:
    // Indirectly used by the producer to signal whether it holds the lock on the buffer.
    // This is accurate if the global producer lock is held as the buffer lock is acquired
    // and this is set to true before the global producer lock is released.
    bool lockHeldForWriting = false;
    bool firstBufferWriting = true;
    bool firstBufferProcessing = true;
};

class DummyConsumer : public juce::Thread {
public:
    DummyConsumer(std::shared_ptr<BufferConsumer> consumer) : juce::Thread("DummyConsumer"), consumer(consumer) {}
    ~DummyConsumer() {}

    void run() override {
        while (!threadShouldExit()) {
            auto buffer = consumer->startProcessing();

            float total = 0.0;
            for (int i = 0; i < buffer->size(); i++) {
                total += (*buffer)[i];
            }
            DBG(total);

            consumer->finishedProcessing();
        }
    }
private:
    std::shared_ptr<BufferConsumer> consumer;
};