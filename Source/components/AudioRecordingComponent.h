#pragma once
#include "DoubleTextBox.h"
#include "../audio/AudioRecorder.h"

class AudioRecordingComponent final : public juce::Component {
public:
	AudioRecordingComponent(OscirenderAudioProcessor& p) : audioProcessor(p) {
        addAndMakeVisible(recordButton);
		addAndMakeVisible(timedRecord);
		addAndMakeVisible(recordLength);

		recordButton.setTooltip("Start recording audio to a WAV file. Press again to stop and save the recording.");
		timedRecord.setTooltip("Record for a set amount of time in seconds. When enabled, the recording will automatically stop once the time is reached.");
        
        recordLength.setValue(1);

        recordButton.setPulseAnimation(true);
        recordButton.onClick = [this] {
            if (recordButton.getToggleState()) {
				startRecording();
			} else {
				stopRecording();
            }
        };

		timedRecord.onClick = [this] {
			if (timedRecord.getToggleState()) {
				addAndMakeVisible(recordLength);
			} else {
				removeChildComponent(&recordLength);
			}
            resized();
		};

		recorder.stopCallback = [this] {
			juce::MessageManager::callAsync([this] {
				recordButton.setToggleState(false, juce::sendNotification);
			});
        };

        audioProcessor.setAudioThreadCallback([this](const juce::AudioBuffer<float>& buffer) { recorder.audioThreadCallback(buffer); });
    }

    ~AudioRecordingComponent() {
        audioProcessor.setAudioThreadCallback(nullptr);
    }

    void resized() override {
		double iconSize = 25;
        
        auto area = getLocalBounds();
        recordButton.setBounds(area.removeFromLeft(iconSize).withSizeKeepingCentre(iconSize, iconSize));
        area.removeFromLeft(5);
		timedRecord.setBounds(area.removeFromLeft(iconSize).withSizeKeepingCentre(iconSize, iconSize));
        if (timedRecord.getToggleState()) {
            recordLength.setBounds(area.removeFromLeft(80).withSizeKeepingCentre(60, 25));
        }
		area.removeFromLeft(5);
    }

private:
	OscirenderAudioProcessor& audioProcessor;

    AudioRecorder recorder;

    SvgButton recordButton{ "record", BinaryData::record_svg, juce::Colours::red, juce::Colours::red.withAlpha(0.01f) };
    juce::File lastRecording;
    juce::FileChooser chooser { "Output file...", juce::File::getCurrentWorkingDirectory().getChildFile("recording.wav"), "*.wav" };
	SvgButton timedRecord{ "timedRecord", BinaryData::timer_svg, juce::Colours::white, juce::Colours::red };
	DoubleTextBox recordLength{ 0, 60 * 60 * 24 };

    void startRecording() {
        auto parentDir = juce::File::getSpecialLocation(juce::File::tempDirectory);

        lastRecording = parentDir.getNonexistentChildFile("osci-render-recording", ".wav");
        if (timedRecord.getToggleState()) {
            recorder.setRecordLength(recordLength.getValue());
		} else {
			recorder.setRecordLength(99999999999.0);
		}
        recorder.startRecording(lastRecording);

        recordButton.setColour(juce::TextButton::buttonColourId, juce::Colours::red);
        recordButton.setColour(juce::TextButton::textColourOnId, juce::Colours::black);
    }

    void stopRecording() {
        recorder.stop();

        recordButton.setColour(juce::TextButton::buttonColourId, findColour(juce::TextButton::buttonColourId));
        recordButton.setColour(juce::TextButton::textColourOnId, findColour(juce::TextButton::textColourOnId));

        chooser.launchAsync(juce::FileBrowserComponent::saveMode
            | juce::FileBrowserComponent::canSelectFiles
            | juce::FileBrowserComponent::warnAboutOverwriting,
            [this](const juce::FileChooser& c) {
                if (juce::FileInputStream inputStream(lastRecording); inputStream.openedOk()) {
                    juce::URL url = c.getURLResult();
                    if (url.isLocalFile()) {
                        if (const auto outputStream = url.getLocalFile().createOutputStream()) {
                            outputStream->setPosition(0);
                            outputStream->truncate();
                            outputStream->writeFromInputStream(inputStream, -1);
                        }
                    }
                }

                lastRecording.deleteFile();
            });
    }

    inline juce::Colour getUIColourIfAvailable(juce::LookAndFeel_V4::ColourScheme::UIColour uiColour, juce::Colour fallback = juce::Colour(0xff4d4d4d)) noexcept {
        if (auto* v4 = dynamic_cast<juce::LookAndFeel_V4*> (&juce::LookAndFeel::getDefaultLookAndFeel()))
            return v4->getCurrentColourScheme().getUIColour(uiColour);

        return fallback;
    }

    inline std::unique_ptr<juce::OutputStream> makeOutputStream(const juce::URL& url) {
        if (const auto doc = juce::AndroidDocument::fromDocument(url))
            return doc.createOutputStream();

#if ! JUCE_IOS
        if (url.isLocalFile())
            return url.getLocalFile().createOutputStream();
#endif

        return url.createOutputStream();
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioRecordingComponent)
};
