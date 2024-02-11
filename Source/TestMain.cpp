#include <JuceHeader.h>

class MyTest : public juce::UnitTest {
public:
    MyTest() : juce::UnitTest("Foobar testing") {}

    void runTest() override {
        beginTest("Part 1");

        expect(true);

        beginTest("Part 2");

        expect(true);
    }
};

class MyTest2 : public juce::UnitTest {
public:
    MyTest2() : juce::UnitTest("Foobar testing 2") {}

    void runTest() override {
        beginTest("Part A");

        expect(true);

        beginTest("Part B");

        expect(true);
    }
};

static MyTest2 test2;

int main(int argc, char* argv[]) {
    juce::UnitTestRunner runner;
    
    runner.runAllTests();
    
	for (int i = 0; i < runner.getNumResults(); ++i) {
		const juce::UnitTestRunner::TestResult* result = runner.getResult(i);
		if (result->failures > 0) {
			return 1;
		}
	}
    
	return 0;
}
