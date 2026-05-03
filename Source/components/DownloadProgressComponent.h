#pragma once

#include <JuceHeader.h>

// Generic, reusable download progress widget. Shows a progress bar plus an
// optional status label. Driven entirely from outside via setProgress / show*
// methods so it can be reused by anything that performs a download (the
// FFmpeg auto-downloader, the license auto-updater, future downloads, etc.).
//
// Thread safety: setProgress / setStatus / showSuccess / showError marshal
// onto the message thread internally, so callers can invoke them from worker
// threads without further synchronisation.
class DownloadProgressComponent : public juce::Component {
public:
    DownloadProgressComponent() {
        progressBar.setPercentageDisplay(true);
        addChildComponent(progressBar);
        statusLabel.setJustificationType(juce::Justification::centred);
        statusLabel.setColour(juce::Label::textColourId, juce::Colours::white);
        addChildComponent(statusLabel);
    }

    // Update to a fraction in [0, 1], or -1 for indeterminate.
    void setProgress(double fraction) {
        callOnMessageThread([this, fraction] {
            progressValue = fraction;
            progressBar.setVisible(true);
            statusLabel.setVisible(currentStatus.isNotEmpty());
        });
    }

    // Optional status message displayed above the progress bar.
    void setStatus(const juce::String& status) {
        callOnMessageThread([this, status] {
            currentStatus = status;
            statusLabel.setText(status, juce::dontSendNotification);
            statusLabel.setVisible(status.isNotEmpty());
        });
    }

    // Replace the progress bar with a transient success message that fades
    // automatically after `clearAfterMs` milliseconds (0 = leave permanently).
    void showSuccess(const juce::String& message, int clearAfterMs = 3000) {
        callOnMessageThread([this, message, clearAfterMs] {
            currentStatus = message;
            statusLabel.setText(message, juce::dontSendNotification);
            progressBar.setVisible(false);
            statusLabel.setVisible(true);
            if (clearAfterMs > 0) {
                juce::Component::SafePointer<DownloadProgressComponent> safeThis(this);
                juce::Timer::callAfterDelay(clearAfterMs, [safeThis] {
                    if (safeThis != nullptr)
                        safeThis->reset();
                });
            }
        });
    }

    void showError(const juce::String& message) {
        callOnMessageThread([this, message] {
            currentStatus = message;
            statusLabel.setText(message, juce::dontSendNotification);
            statusLabel.setVisible(true);
            progressValue = -2.0;
            progressBar.setVisible(true);
        });
    }

    void reset() {
        callOnMessageThread([this] {
            currentStatus = {};
            statusLabel.setText({}, juce::dontSendNotification);
            statusLabel.setVisible(false);
            progressValue = -1.0;
            progressBar.setVisible(false);
        });
    }

    bool isShowing() const { return progressBar.isVisible() || statusLabel.isVisible(); }

    void resized() override {
        auto bounds = getLocalBounds();
        if (statusLabel.isVisible() && currentStatus.isNotEmpty()) {
            statusLabel.setBounds(bounds.removeFromTop(juce::jmin(bounds.getHeight() / 2, 22)));
            bounds.removeFromTop(4);
        } else {
            statusLabel.setBounds(bounds);
        }
        progressBar.setBounds(bounds);
    }

private:
    void callOnMessageThread(std::function<void()> fn) {
        if (juce::MessageManager::getInstance()->isThisTheMessageThread()) {
            fn();
            resized();
            repaint();
        } else {
            juce::Component::SafePointer<DownloadProgressComponent> safeThis(this);
            juce::MessageManager::callAsync([safeThis, fn = std::move(fn)] {
                if (safeThis == nullptr) return;
                fn();
                safeThis->resized();
                safeThis->repaint();
            });
        }
    }

    double progressValue = -1.0;
    juce::ProgressBar progressBar { progressValue };
    juce::Label statusLabel;
    juce::String currentStatus;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DownloadProgressComponent)
};
