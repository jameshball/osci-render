#include <JuceHeader.h>

#include "../Source/FileSelectionController.h"

namespace {

class TestSound : public juce::SynthesiserSound {
public:
    bool appliesToNote(int) override { return true; }
    bool appliesToChannel(int) override { return true; }
};

FileSelectionController::Dependencies createDependencies() {
    FileSelectionController::Dependencies dependencies;
    dependencies.createFileResources = [] {
        return FileSelectionController::FileResources { nullptr, new TestSound() };
    };
    dependencies.parseFile = [](FileSelectionController::File&, juce::Font&) {};
    return dependencies;
}

} // namespace

class FileSelectionControllerTest : public juce::UnitTest {
public:
    FileSelectionControllerTest() : juce::UnitTest("FileSelectionController", "Processor") {}

    void runTest() override {
        beginTest("Selection has explicit empty state and validates file bounds");
        {
            Fixture fixture;
            expect(!fixture.controller.getCurrentFileIndex().has_value());
            fixture.addFiles(3);
            fixture.controller.selectFile(1);
            expectEquals(*fixture.controller.getCurrentFileIndex(), 1);
            fixture.controller.selectFile(3);
            expectEquals(*fixture.controller.getCurrentFileIndex(), 1);
            fixture.controller.selectNoFile();
            expect(!fixture.controller.getCurrentFileIndex().has_value());
        }

        beginTest("Program Change honours its channel and clamps to the available files");
        {
            Fixture fixture;
            fixture.addFiles(4);
            fixture.controller.selectFile(0);
            fixture.controller.setProgramChangeChannel(3);
            fixture.controller.queueProgramChange(2, 2);
            fixture.controller.applyPendingSelection();
            expectEquals(*fixture.controller.getCurrentFileIndex(), 0);
            fixture.controller.queueProgramChange(99, 3);
            fixture.controller.applyPendingSelection();
            expectEquals(*fixture.controller.getCurrentFileIndex(), 3);
            expectEquals(fixture.fileSelect.getValueUnnormalised(), 100);
        }

        beginTest("Automatable selection clamps to the available files");
        {
            Fixture fixture;
            fixture.addFiles(4);
            fixture.controller.queueParameterSelectionIfChanged(100);
            fixture.controller.applyPendingSelection();
            expectEquals(*fixture.controller.getCurrentFileIndex(), 3);
        }

        beginTest("Removing a file selects the same position or the new last file");
        {
            Fixture fixture;
            fixture.addFiles(4);
            fixture.controller.selectFile(2);
            fixture.controller.removeFile(2);
            expectEquals(*fixture.controller.getCurrentFileIndex(), 2);
            fixture.controller.removeFile(2);
            expectEquals(*fixture.controller.getCurrentFileIndex(), 1);
            fixture.controller.clear();
            expect(!fixture.controller.getCurrentFileIndex().has_value());
        }

        beginTest("Object Server temporarily owns the active source without losing file selection");
        {
            Fixture fixture;
            fixture.addFiles(3);
            fixture.controller.selectFile(1);
            fixture.controller.setObjectServerActive(true);
            expect(fixture.controller.isObjectServerActive());
            expect(!fixture.controller.getCurrentFileIndex().has_value());
            fixture.controller.queueProgramChange(2, 1);
            fixture.controller.applyPendingSelection();
            fixture.controller.setObjectServerActive(false);
            expectEquals(*fixture.controller.getCurrentFileIndex(), 1);
        }
    }

private:
    struct Fixture {
        Fixture()
            : controller(fileSelect, font, createDependencies()) {
            controller.initialise(new TestSound(), new TestSound());
        }

        void addFiles(int count) {
            for (int i = 0; i < count; ++i) {
                controller.addFile("file" + juce::String(i) + ".svg", std::make_shared<juce::MemoryBlock>());
            }
        }

        osci::IntParameter fileSelect { "File Select", "fileSelect", 2, 1, 1, 100 };
        juce::Font font;
        FileSelectionController controller;
    };
};

static FileSelectionControllerTest fileSelectionControllerTest;
