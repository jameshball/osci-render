#include <juce_core/juce_core.h>
#include "../modules/osci_render_core/shape/osci_Shape.h"
#include "../modules/osci_render_core/concurrency/osci_BlockingQueue.h"

#include <thread>

class BlockingQueueTests final : public juce::UnitTest {
public:
    BlockingQueueTests() : juce::UnitTest("Blocking queue shutdown", "Concurrency") {}

    void runTest() override {
        beginTest("A killed full queue preserves rejected input ownership");
        osci::BlockingQueue full(1);
        osci::Frame first(1), rejected(2);
        full.push(std::move(first));
        full.kill();
        full.push(std::move(rejected));
        expect(rejected.size() == 2);
        expect(!full.try_push(std::move(rejected)));
        expect(rejected.size() == 2);

        beginTest("A killed queue preserves the consumer's current frame");
        osci::BlockingQueue empty(1);
        osci::Frame current(3);
        empty.kill();
        empty.pop(current);
        expect(current.size() == 3);
        expect(!empty.try_pop(current));
        expect(!full.try_pop(current));
        expect(current.size() == 3);
        expect(!empty.try_push(std::move(current)));
        expect(current.size() == 3);

        beginTest("Shutdown releases waiting producers and consumers");
        osci::BlockingQueue producerQueue(1), consumerQueue(1);
        osci::Frame initial(1), input(2), output(3);
        producerQueue.push(std::move(initial));
        std::thread producer([&] { producerQueue.push(std::move(input)); });
        std::thread consumer([&] { consumerQueue.pop(output); });
        producerQueue.kill();
        consumerQueue.kill();
        producer.join();
        consumer.join();
        expect(input.size() == 2);
        expect(output.size() == 3);

        beginTest("Normal handoff preserves FIFO order and swaps retired frames back");
        osci::BlockingQueue queue(2);
        osci::Frame a(1), b(2), c(3), destination(4);
        expect(queue.try_push(std::move(a)));
        expect(queue.try_push(std::move(b)));
        expect(!queue.try_push(std::move(c)));
        expect(queue.try_pop(destination));
        expect(destination.size() == 1);
        expect(queue.try_push(std::move(c)));
        expect(queue.try_pop(destination));
        expect(destination.size() == 2);
        expect(queue.try_pop(destination));
        expect(destination.size() == 3);
        expect(!queue.try_pop(destination));
        expect(destination.size() == 3);
    }
};

static BlockingQueueTests blockingQueueTests;
