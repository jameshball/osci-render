#include "MainComponent.h"
#include "parser/FileParser.h"
#include "parser/FrameProducer.h"
#include "PluginEditor.h"

MainComponent::MainComponent(OscirenderAudioProcessor& p, OscirenderAudioProcessorEditor& editor) : audioProcessor(p), pluginEditor(editor) {
	setText("Main Settings");

    addAndMakeVisible(fileButton);
    fileButton.setButtonText("Choose File(s)");
    
	fileButton.onClick = [this] {
		chooser = std::make_unique<juce::FileChooser>("Open", audioProcessor.lastOpenedDirectory, "*.obj;*.svg;*.lua;*.txt;*.gpla;*.gif;*.png;*.jpg;*.jpeg;*.wav;*.aiff;*.ogg;*.flac;*.mp3");
		auto flags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectMultipleItems |
            juce::FileBrowserComponent::canSelectFiles;

		chooser->launchAsync(flags, [this](const juce::FileChooser& chooser) {
			juce::SpinLock::ScopedLockType lock(audioProcessor.parsersLock);
			bool fileAdded = false;
			for (auto& file : chooser.getResults()) {
				if (file != juce::File()) {
                    audioProcessor.lastOpenedDirectory = file.getParentDirectory();
					audioProcessor.addFile(file);
					pluginEditor.addCodeEditor(audioProcessor.getCurrentFileIndex());
					fileAdded = true;
				}
			}
            
            
			if (fileAdded) {
				pluginEditor.fileUpdated(audioProcessor.getCurrentFileName());
			}
		});
	};

	addAndMakeVisible(closeFileButton);
	
	closeFileButton.onClick = [this] {
		juce::SpinLock::ScopedLockType lock(audioProcessor.parsersLock);
		int index = audioProcessor.getCurrentFileIndex();
		if (index == -1) {
			return;
		}
		pluginEditor.removeCodeEditor(audioProcessor.getCurrentFileIndex());
		audioProcessor.removeFile(audioProcessor.getCurrentFileIndex());
		pluginEditor.fileUpdated(audioProcessor.getCurrentFileName());
	};

	closeFileButton.setTooltip("Close the currently open file.");

	addAndMakeVisible(inputEnabled);
	inputEnabled.onClick = [this] {
		audioProcessor.inputEnabled->setBoolValueNotifyingHost(!audioProcessor.inputEnabled->getBoolValue());
	};
	
	addAndMakeVisible(fileLabel);
	fileLabel.setJustificationType(juce::Justification::centred);
	updateFileLabel();

	addAndMakeVisible(leftArrow);
	leftArrow.onClick = [this] {
		juce::SpinLock::ScopedLockType parserLock(audioProcessor.parsersLock);
		juce::SpinLock::ScopedLockType effectsLock(audioProcessor.effectsLock);

		int index = audioProcessor.getCurrentFileIndex();

		if (index > 0) {
			audioProcessor.changeCurrentFile(index - 1);
			pluginEditor.fileUpdated(audioProcessor.getCurrentFileName());
		}
	};
	leftArrow.setTooltip("Change to previous file (k).");
	
	addAndMakeVisible(rightArrow);
	rightArrow.onClick = [this] {
		juce::SpinLock::ScopedLockType parserLock(audioProcessor.parsersLock);
		juce::SpinLock::ScopedLockType effectsLock(audioProcessor.effectsLock);

		int index = audioProcessor.getCurrentFileIndex();

		if (index < audioProcessor.numFiles() - 1) {
			audioProcessor.changeCurrentFile(index + 1);
			pluginEditor.fileUpdated(audioProcessor.getCurrentFileName());
		}
	};
	rightArrow.setTooltip("Change to next file (j).");

	
	addAndMakeVisible(fileName);
	fileType.addItem(".lua", 1);
	fileType.addItem(".svg", 2);
	fileType.addItem(".obj", 3);
    fileType.addItem(".txt", 4);
	fileType.setSelectedId(1);
	addAndMakeVisible(fileType);
	addAndMakeVisible(createFile);

	createFile.onClick = [this] {
		juce::SpinLock::ScopedLockType lock(audioProcessor.parsersLock);
		auto fileNameText = fileName.getText();
		auto fileTypeText = fileType.getText();
		auto fileName = fileNameText + fileTypeText;
		if (fileTypeText == ".lua") {
			audioProcessor.addFile(fileNameText + fileTypeText, BinaryData::demo_lua, BinaryData::demo_luaSize);
		} else if (fileTypeText == ".svg") {
			audioProcessor.addFile(fileNameText + fileTypeText, BinaryData::demo_svg, BinaryData::demo_svgSize);
		} else if (fileTypeText == ".obj") {
			audioProcessor.addFile(fileNameText + fileTypeText, BinaryData::cube_obj, BinaryData::cube_objSize);
		} else if (fileTypeText == ".txt") {
			audioProcessor.addFile(fileNameText + fileTypeText, BinaryData::helloworld_txt, BinaryData::helloworld_txtSize);
		} else {
			return;
		}

		pluginEditor.addCodeEditor(audioProcessor.getCurrentFileIndex());
        pluginEditor.fileUpdated(fileName, fileTypeText == ".lua" || fileTypeText == ".txt");
	};

	fileName.setFont(juce::Font(16.0f, juce::Font::plain));
	fileName.setText("filename");

	fileName.onReturnKey = [this] {
		createFile.triggerClick();
	};

	BooleanParameter* visualiserFullScreen = audioProcessor.visualiserParameters.visualiserFullScreen;
    pluginEditor.visualiser.setFullScreen(visualiserFullScreen->getBoolValue());

    addAndMakeVisible(pluginEditor.visualiser);
	pluginEditor.visualiser.setFullScreenCallback([this, visualiserFullScreen](FullScreenMode mode) {
		if (mode == FullScreenMode::TOGGLE) {
			visualiserFullScreen->setBoolValueNotifyingHost(!visualiserFullScreen->getBoolValue());
		} else if (mode == FullScreenMode::FULL_SCREEN) {
			visualiserFullScreen->setBoolValueNotifyingHost(true);
		} else if (mode == FullScreenMode::MAIN_COMPONENT) {
			visualiserFullScreen->setBoolValueNotifyingHost(false);
        }
        
        pluginEditor.visualiser.setFullScreen(visualiserFullScreen->getBoolValue());
		
		pluginEditor.resized();
		pluginEditor.repaint();
		resized();
		repaint();
    });

	visualiserFullScreen->addListener(this);
}

MainComponent::~MainComponent() {
	audioProcessor.visualiserParameters.visualiserFullScreen->removeListener(this);
}

void MainComponent::updateFileLabel() {
	showLeftArrow = audioProcessor.getCurrentFileIndex() > 0;
	showRightArrow = audioProcessor.getCurrentFileIndex() < audioProcessor.numFiles() - 1;
	
	if (audioProcessor.objectServerRendering) {
		fileLabel.setText("Rendering from Blender", juce::dontSendNotification);
	} else if (audioProcessor.getCurrentFileIndex() == -1) {
		fileLabel.setText("No file open", juce::dontSendNotification);
	} else {
		fileLabel.setText(audioProcessor.getCurrentFileName(), juce::dontSendNotification);
	}

	resized();
}

void MainComponent::parameterValueChanged(int parameterIndex, float newValue) {
	juce::MessageManager::callAsync([this] {
		pluginEditor.resized();
		pluginEditor.repaint();
		resized();
		repaint();
    });
}

void MainComponent::parameterGestureChanged(int parameterIndex, bool gestureIsStarting) {}

void MainComponent::resized() {
    juce::Rectangle<int> bounds = getLocalBounds().withTrimmedTop(20).reduced(20);
	auto buttonWidth = 120;
	auto buttonHeight = 30;
	auto padding = 10;
	auto rowPadding = 10;
	
	auto row = bounds.removeFromTop(buttonHeight);
    fileButton.setBounds(row.removeFromLeft(buttonWidth));
	row.removeFromLeft(rowPadding);
	inputEnabled.setBounds(row.removeFromLeft(20));
	row.removeFromLeft(rowPadding);
	if (audioProcessor.getCurrentFileIndex() != -1) {
		closeFileButton.setBounds(row.removeFromRight(20));
		row.removeFromRight(rowPadding);
	} else {
		closeFileButton.setBounds(juce::Rectangle<int>());
	}
	
	auto arrowLeftBounds = row.removeFromLeft(15);
	if (showLeftArrow) {
		leftArrow.setBounds(arrowLeftBounds);
	} else {
		leftArrow.setBounds(0, 0, 0, 0);
	}
	row.removeFromLeft(rowPadding);
	
	auto arrowRightBounds = row.removeFromRight(15);
	if (showRightArrow) {
		rightArrow.setBounds(arrowRightBounds);
	} else {
		rightArrow.setBounds(0, 0, 0, 0);
	}
	row.removeFromRight(rowPadding);
	
	fileLabel.setBounds(row);

	bounds.removeFromTop(padding);
	row = bounds.removeFromTop(buttonHeight);
	fileName.setBounds(row.removeFromLeft(buttonWidth));
	row.removeFromLeft(rowPadding);
	fileType.setBounds(row.removeFromLeft(buttonWidth / 2));
	row.removeFromLeft(rowPadding);
	createFile.setBounds(row.removeFromLeft(buttonWidth));

	bounds.removeFromTop(padding);
	bounds.expand(10, 0);
	if (!audioProcessor.visualiserParameters.visualiserFullScreen->getBoolValue()) {
		auto minDim = juce::jmin(bounds.getWidth(), bounds.getHeight());
        juce::Point<int> localTopLeft = {bounds.getX(), bounds.getY()};
        juce::Point<int> topLeft = pluginEditor.getLocalPoint(this, localTopLeft);
        auto shiftedBounds = bounds;
        shiftedBounds.setX(topLeft.getX());
        shiftedBounds.setY(topLeft.getY());
        //if (minDim < 35) {
        //    minDim = 35;
        //}
		pluginEditor.visualiser.setBounds(shiftedBounds.withSizeKeepingCentre(minDim - 25, minDim));
	}
}
