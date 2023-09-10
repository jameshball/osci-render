#include "MainComponent.h"
#include "parser/FileParser.h"
#include "parser/FrameProducer.h"
#include "PluginEditor.h"

MainComponent::MainComponent(OscirenderAudioProcessor& p, OscirenderAudioProcessorEditor& editor) : audioProcessor(p), pluginEditor(editor) {
	setText("Main Settings");

    addAndMakeVisible(fileButton);
    fileButton.setButtonText("Choose File(s)");
    
	fileButton.onClick = [this] {
		chooser = std::make_unique<juce::FileChooser>("Open", juce::File::getSpecialLocation(juce::File::userHomeDirectory), "*.obj;*.svg;*.lua;*.txt");
		auto flags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectMultipleItems |
            juce::FileBrowserComponent::canSelectFiles;

		chooser->launchAsync(flags, [this](const juce::FileChooser& chooser) {
			juce::SpinLock::ScopedLockType lock(audioProcessor.parsersLock);
			bool fileAdded = false;
			for (auto& url : chooser.getURLResults()) {
				if (url.isLocalFile()) {
					auto file = url.getLocalFile();
					audioProcessor.addFile(file);
					fileAdded = true;
				}
			}
			if (fileAdded) {
				pluginEditor.addCodeEditor(audioProcessor.getCurrentFileIndex());
				pluginEditor.fileUpdated(audioProcessor.getCurrentFileName());
			}
		});
	};

	addAndMakeVisible(closeFileButton);
	closeFileButton.setButtonText("Close File");
	
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

	addAndMakeVisible(fileLabel);
	updateFileLabel();

	
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
		pluginEditor.fileUpdated(fileName);
	};

	fileName.onReturnKey = [this] {
		createFile.triggerClick();
	};

	addAndMakeVisible(visualiser);
	addAndMakeVisible(openOscilloscope);

	openOscilloscope.onClick = [this] {
		// TODO: Log if this fails
		juce::URL("https://james.ball.sh/oscilloscope").launchInDefaultBrowser();
    };

	addAndMakeVisible(frequencyLabel);

	callbackIndex = audioProcessor.pitchDetector.addCallback(
		[this](float frequency) {
			// round to nearest integer
			int roundedFrequency = static_cast<int>(frequency + 0.5f);
			frequencyLabel.setText(juce::String(roundedFrequency) + "Hz", juce::dontSendNotification);
		}
	);
}

MainComponent::~MainComponent() {
	audioProcessor.pitchDetector.removeCallback(callbackIndex);
}

void MainComponent::updateFileLabel() {
	if (audioProcessor.objectServerRendering) {
		fileLabel.setText("Rendering from Blender", juce::dontSendNotification);
	} else if (audioProcessor.getCurrentFileIndex() == -1) {
		fileLabel.setText("No file open", juce::dontSendNotification);
	} else {
		fileLabel.setText(audioProcessor.getCurrentFileName(), juce::dontSendNotification);
	}
}

void MainComponent::resized() {
	auto bounds = getLocalBounds().withTrimmedTop(20).reduced(20);
	auto buttonWidth = 120;
	auto buttonHeight = 30;
	auto padding = 10;
	auto rowPadding = 10;
	
	auto row = bounds.removeFromTop(buttonHeight);
    fileButton.setBounds(row.removeFromLeft(buttonWidth));
	row.removeFromLeft(rowPadding);
	fileLabel.setBounds(row);
	bounds.removeFromTop(padding);
	closeFileButton.setBounds(bounds.removeFromTop(buttonHeight).removeFromLeft(buttonWidth));

	bounds.removeFromTop(padding);
	row = bounds.removeFromTop(buttonHeight);
	fileName.setBounds(row.removeFromLeft(buttonWidth));
	row.removeFromLeft(rowPadding);
	fileType.setBounds(row.removeFromLeft(buttonWidth / 2));
	row.removeFromLeft(rowPadding);
	createFile.setBounds(row.removeFromLeft(buttonWidth));

	bounds.removeFromTop(padding);
	frequencyLabel.setBounds(bounds.removeFromTop(20));

	bounds.removeFromTop(padding);
	openOscilloscope.setBounds(bounds.removeFromBottom(buttonHeight).withSizeKeepingCentre(160, buttonHeight));
	bounds.removeFromBottom(padding);
	auto minDim = juce::jmin(bounds.getWidth(), bounds.getHeight());
	visualiser.setBounds(bounds.withSizeKeepingCentre(minDim, minDim));
}

void MainComponent::paint(juce::Graphics& g) {
	juce::GroupComponent::paint(g);

	// add drop shadow to the visualiser
	auto dc = juce::DropShadow(juce::Colours::black, 30, juce::Point<int>(0, 0));
	dc.drawForRectangle(g, visualiser.getBounds());
}
