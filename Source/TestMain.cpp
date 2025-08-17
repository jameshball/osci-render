#include <JuceHeader.h>

class ProducerThread : public juce::Thread {
public:
    ProducerThread(osci::BufferConsumer& consumer) : juce::Thread("Producer Thread"), consumer(consumer) {}
    
    void run() override {
        for (int i = 0; i < 1024 * 100; i++) {
            consumer.write(osci::Point(counter++));
        }
    }

private:
    osci::BufferConsumer& consumer;
    int counter = 0;
};

class ConsumerThread : public juce::Thread {
public:
    ConsumerThread(osci::BufferConsumer& consumer) : juce::Thread("Consumer Thread"), consumer(consumer) {}
    
    void run() override {
        for (int i = 0; i < 100; i++) {
            consumer.waitUntilFull();
            auto buffer = consumer.getBuffer();
            for (auto& point : buffer) {
                values.push_back(point.x);
            }
        }
    }
    
    bool containsGap() {
        for (int i = 0; i < values.size() - 1; i++) {
            if (values[i] + 1 != values[i + 1]) {
                return true;
            }
        }
        
        return false;
    }

private:
    osci::BufferConsumer& consumer;
    std::vector<double> values;
};

class BufferConsumerTest : public juce::UnitTest {
public:
    BufferConsumerTest() : juce::UnitTest("Buffer Consumer") {}

    void runTest() override {
        beginTest("All data received");
        
        osci::BufferConsumer consumer(1024);
        ProducerThread producer(consumer);
        ConsumerThread consumerThread(consumer);
        
        consumer.setBlockOnWrite(true);
        
        for (int i = 0; i < 100; i++) {
            producer.startThread();
            consumerThread.startThread();
            
            producer.waitForThreadToExit(-1);
            consumerThread.waitForThreadToExit(-1);
            
            expect(!consumerThread.containsGap(), "There was a gap in the data");
        }
    }
};

static BufferConsumerTest bufferConsumerTest;

int main(int argc, char* argv[]) {
    juce::UnitTestRunner runner;
    runner.setAssertOnFailure(false);
    
    runner.runAllTests();
    
	for (int i = 0; i < runner.getNumResults(); ++i) {
		const juce::UnitTestRunner::TestResult* result = runner.getResult(i);
		if (result->failures > 0) {
			return 1;
		}
	}
    
	return 0;
}
