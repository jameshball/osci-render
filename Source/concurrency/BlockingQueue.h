#pragma once

#include <mutex>
#include <condition_variable>
#include <queue>

typedef std::vector<std::unique_ptr<Shape>> Frame;
class BlockingQueue {
    std::vector<Frame> content;
    std::atomic<int> size = 0;
    int head = 0;
    std::atomic<bool> killed = false;

    std::mutex mutex;
    std::condition_variable not_empty;
    std::condition_variable not_full;

    BlockingQueue(const BlockingQueue &) = delete;
    BlockingQueue(BlockingQueue &&) = delete;
    BlockingQueue &operator = (const BlockingQueue &) = delete;
    BlockingQueue &operator = (BlockingQueue &&) = delete;

public:
    BlockingQueue(size_t capacity) {
        content = std::vector<Frame>(capacity);
    }

    void kill() {
        killed = true;
        not_empty.notify_all();
        not_full.notify_all();
    }

    void push(Frame &&item) {
        {
            std::unique_lock<std::mutex> lk(mutex);
            not_full.wait(lk, [this]() { return size < content.size() || killed; });
            content[(head + size) % content.size()] = std::move(item);
            size++;
        }
        not_empty.notify_one();
    }

    bool try_push(Frame &&item) {
        {
            std::unique_lock<std::mutex> lk(mutex);
            if (size == content.size()) {
                return false;
            }
            content[(head + size) % content.size()] = std::move(item);
            size++;
        }
        not_empty.notify_one();
        return true;
    }

    void pop(Frame &item) {
        {
            std::unique_lock<std::mutex> lk(mutex);
            not_empty.wait(lk, [this]() { return size > 0 || killed; });
            content[head].swap(item);
            head = (head + 1) % content.size();
            size--;
        }
        not_full.notify_one();
    }

    bool try_pop(Frame &item) {
        {
            std::unique_lock<std::mutex> lk(mutex);
            if (size == 0) {
                return false;
            }
            content[head].swap(item);
            head = (head + 1) % content.size();
            size--;
        }
        not_full.notify_one();
        return true;
    }

    bool check_stale(int howOld) {
        return size >= howOld;
    }
};