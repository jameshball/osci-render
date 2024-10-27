#pragma once

#include <JuceHeader.h>
#include "../shape/OsciPoint.h"
#include "BufferConsumer.h"


class ConsumerManager {
public:
    ConsumerManager() {}
    ~ConsumerManager() {}

    std::shared_ptr<BufferConsumer> consumerRegister(std::size_t size) {
        std::shared_ptr<BufferConsumer> consumer = std::make_shared<BufferConsumer>(size);
        juce::SpinLock::ScopedLockType scope(consumerLock);
        consumers.push_back(consumer);

        return consumer;
    }
    
    void consumerRead(std::shared_ptr<BufferConsumer> consumer) {
        consumer->waitUntilFull();
    }

    void consumerStop(std::shared_ptr<BufferConsumer> consumer) {
        if (consumer != nullptr) {
            juce::SpinLock::ScopedLockType scope(consumerLock);
            consumer->forceNotify();
        }
    }

protected:
    juce::SpinLock consumerLock;
    std::vector<std::shared_ptr<BufferConsumer>> consumers;
};
