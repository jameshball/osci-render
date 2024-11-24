#pragma once

#include <JuceHeader.h>
#include "../shape/OsciPoint.h"
#include <mutex>
#include <condition_variable>
#include "readerwritercircularbuffer.h"

// FROM https://gist.github.com/Kuxe/6bdd5b748b5f11b303a5cccbb8c8a731
/** General semaphore with N permissions **/
class Semaphore {
    const size_t num_permissions;
    size_t avail;
    std::mutex m;
    std::condition_variable cv;
public:
    /** Default constructor. Default semaphore is a binary semaphore **/
    explicit Semaphore(const size_t& num_permissions = 1) : num_permissions(num_permissions), avail(num_permissions) { }

    /** Copy constructor. Does not copy state of mutex or condition variable,
        only the number of permissions and number of available permissions **/
    Semaphore(const Semaphore& s) : num_permissions(s.num_permissions), avail(s.avail) { }

    void acquire() {
        std::unique_lock<std::mutex> lk(m);
        cv.wait(lk, [this] { return avail > 0; });
        avail--;
        lk.unlock();
    }

    void release() {
        m.lock();
        avail++;
        m.unlock();
        cv.notify_one();
    }

    size_t available() const {
        return avail;
    }
};


// TODO: I am aware this will cause read/write data races, but I don't think
// this matters too much in practice.
class BufferConsumer {
public:
    BufferConsumer(std::size_t size) {
        returnBuffer.resize(size);
        buffer1.resize(size);
        buffer2.resize(size);
        queue = std::make_unique<moodycamel::BlockingReaderWriterCircularBuffer<OsciPoint>>(2 * size);
    }

    ~BufferConsumer() {}
    
    // CONSUMER
    // loop dequeuing until full
    // get buffer
    
    // PRODUCER
    // enqueue point
    
    void waitUntilFull() {
        if (blockOnWrite) {
            for (int i = 0; i < returnBuffer.size() && blockOnWrite; i++) {
                queue->wait_dequeue(returnBuffer[i]);
            }
        } else {
            sema.acquire();
        }
    }
    
    // to be used when the audio thread is being destroyed to
    // make sure that everything waiting on it stops waiting.
    void forceNotify() {
        sema.release();
        queue->try_enqueue(OsciPoint());
    }

    void write(OsciPoint point) {
        if (blockOnWrite) {
            queue->wait_enqueue(point);
        } else {
            if (offset >= buffer->size()) {
                {
                    juce::SpinLock::ScopedLockType scope(bufferLock);
                    buffer = buffer == &buffer1 ? &buffer2 : &buffer1;
                }
                offset = 0;
                sema.release();
            }
            
            (*buffer)[offset++] = point;
        }
    }

    std::vector<OsciPoint>& getBuffer() {
        if (blockOnWrite) {
            return returnBuffer;
        } else {
            // whatever buffer is not currently being written to
            juce::SpinLock::ScopedLockType scope(bufferLock);
            return buffer == &buffer1 ? buffer2 : buffer1;
        }
    }
    
    void setBlockOnWrite(bool block) {
        blockOnWrite = block;
        if (blockOnWrite) {
            sema.release();
        } else {
            OsciPoint item;
            queue->try_dequeue(item);
        }
    }

private:
    std::unique_ptr<moodycamel::BlockingReaderWriterCircularBuffer<OsciPoint>> queue;
    std::vector<OsciPoint> returnBuffer;
    std::vector<OsciPoint> buffer1;
    std::vector<OsciPoint> buffer2;
    juce::SpinLock bufferLock;
    std::atomic<bool> blockOnWrite = false;
    std::vector<OsciPoint>* buffer = &buffer1;
    Semaphore sema{0};
    int offset = 0;
};
