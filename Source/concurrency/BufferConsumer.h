#pragma once

#include <JuceHeader.h>
#include <mutex>
#include <condition_variable>

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


class BufferConsumer {
public:
    BufferConsumer(std::vector<float>& buffer) : buffer(buffer) {}

    ~BufferConsumer() {}
    
    void waitUntilFull() {
        sema.acquire();
    }

    void notifyIfFull() {
        if (offset >= buffer.size()) {
            sema.release();
        }
    }

    void write(double d) {
        if (offset < buffer.size()) {
            buffer[offset++] = d;
        }
    }

private:
    std::vector<float>& buffer;
    Semaphore sema{0};
    int offset = 0;
};
