#include "MainComponent.h"
#include "parser/FileParser.h"
#include "parser/FrameProducer.h"

MainComponent::MainComponent(OscirenderAudioProcessor& p) : audioProcessor(p) {
	setText("Main Settings");

    addAndMakeVisible(fileButton);

    fileButton.setButtonText("Choose File");
    
	fileButton.onClick = [this] {
		chooser = std::make_unique<juce::FileChooser>("Open", juce::File::getSpecialLocation(juce::File::userHomeDirectory), "*.obj;*.svg;*.lua;*.txt");
		auto flags = juce::FileBrowserComponent::openMode;

		chooser->launchAsync(flags, [this](const juce::FileChooser& chooser) {
			if (chooser.getURLResult().isLocalFile()) {
				auto file = chooser.getResult();
				if (file.getFileExtension() == ".obj") {
					audioProcessor.parser.parse();
				} else if (file.getFileExtension() == ".svg") {
					
				} else if (file.getFileExtension() == ".lua") {
					
				} else if (file.getFileExtension() == ".txt") {
					
				}
			}
		});
	};
}

MainComponent::~MainComponent() {
}

void MainComponent::resized() {
	auto xPadding = 10;
	auto yPadding = 20;
	auto buttonWidth = 100;
	auto buttonHeight = 40;
    fileButton.setBounds(xPadding, yPadding, buttonWidth, buttonHeight);
}
