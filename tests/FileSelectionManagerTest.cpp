#include <JuceHeader.h>

#include "../Source/FileSelectionManager.h"

class FileSelectionManagerTest : public juce::UnitTest {
public:
    FileSelectionManagerTest() : juce::UnitTest("FileSelectionManager", "Processor") {}

    void runTest() override {
        beginTest("Selection uses optional state and validates file bounds");
        {
            FileSelectionManager manager;
            expect(!manager.getSelectedFile().has_value());
            expect(!manager.select(3, FileSelectionManager::Source::user, 3).changed);
            expect(manager.select(2, FileSelectionManager::Source::user, 3).changed);
            expectEquals((int)*manager.getSelectedFile(), 2);
            expect(manager.select(std::nullopt, FileSelectionManager::Source::internal, 3).changed);
            expect(!manager.getSelectedFile().has_value());
        }

        beginTest("Temporary sources preserve and restore the selected file");
        {
            FileSelectionManager manager;
            manager.select(1, FileSelectionManager::Source::internal, 3);
            const auto textureChange = manager.startTextureInput();
            expect(textureChange.changed);
            expect(!textureChange.updateParameter);
            expect(manager.isTextureInputActive());
            expect(!manager.getVisibleFile().has_value());
            expectEquals((int)*manager.getSelectedFile(), 1);
            expect(manager.stopTextureInput().changed);
            expectEquals((int)*manager.getVisibleFile(), 1);

            expect(manager.setObjectServerActive(true).changed);
            expect(manager.isObjectServerActive());
            expect(!manager.select(2, FileSelectionManager::Source::user, 3).changed);
            expect(!manager.select(2, FileSelectionManager::Source::programChange, 3).changed);
            expect(manager.select(2, FileSelectionManager::Source::internal, 3).changed);
            expectEquals((int)*manager.getSelectedFile(), 2);
            expect(manager.setObjectServerActive(false).changed);
            expectEquals((int)*manager.getVisibleFile(), 2);
        }

        beginTest("Program Change honours channel, range, and file count");
        {
            FileSelectionManager manager;
            manager.setProgramChangeChannel(3);
            manager.queueProgramChange(2, 2);
            expect(!manager.applyPendingSelection(5).has_value());
            manager.queueProgramChange(2, 3);
            auto change = manager.applyPendingSelection(5);
            expect(change.has_value() && change->changed);
            expectEquals((int)*manager.getSelectedFile(), 2);

            manager.queueProgramChange(99, 3);
            change = manager.applyPendingSelection(5);
            expect(change.has_value() && !change->changed);
            expectEquals((int)*manager.getSelectedFile(), 2);
        }

        beginTest("Automatable selection clamps to the available file range");
        {
            FileSelectionManager manager;
            manager.queueParameterSelectionIfChanged(100);
            const auto change = manager.applyPendingSelection(4);
            expect(change.has_value() && change->changed);
            expectEquals((int)*manager.getSelectedFile(), 3);
            expect(!change->updateParameter);
            manager.queueParameterSelectionIfChanged(100);
            expect(!manager.applyPendingSelection(4).has_value());
        }

        beginTest("Removing a file selects the same position or the new last file");
        {
            FileSelectionManager manager;
            manager.select(2, FileSelectionManager::Source::internal, 4);
            expect(manager.removeFile(2, 3).changed);
            expectEquals((int)*manager.getSelectedFile(), 2);
            expect(manager.removeFile(2, 2).changed);
            expectEquals((int)*manager.getSelectedFile(), 1);
            expect(manager.removeFile(0, 0).changed);
            expect(!manager.getSelectedFile().has_value());
        }
    }
};

static FileSelectionManagerTest fileSelectionManagerTest;
