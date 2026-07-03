#pragma once

#include <JuceHeader.h>
#include <osci_gui/osci_gui.h>

namespace osci {

inline juce::String makeDialogOverlayCloseButtonSvg() {
    return juce::String::createStringFromData(BinaryData::close_svg, BinaryData::close_svgSize);
}

inline void showOverlayMessage(juce::Component& parent,
                               juce::StringRef title,
                               juce::StringRef message,
                               ErrorOverlay::Icon icon = ErrorOverlay::Icon::Warning,
                               juce::Point<int> preferredPanelSize = { 420, 420 },
                               juce::Justification messageJustification = juce::Justification::centred) {
    ErrorOverlay::Options options;
    options.closeButtonSvg = makeDialogOverlayCloseButtonSvg();
    options.title = title;
    options.message = message;
    options.icon = icon;
    options.preferredPanelSize = preferredPanelSize;
    options.messageJustification = messageJustification;
    options.buttons.push_back({ "OK", {}, true });
    ErrorOverlay::show(parent, std::move(options));
}

inline void showOverlayMessageOrAlert(juce::Component* parent,
                                      juce::StringRef title,
                                      juce::StringRef message,
                                      ErrorOverlay::Icon icon = ErrorOverlay::Icon::Warning,
                                      juce::MessageBoxIconType fallbackIcon = juce::MessageBoxIconType::WarningIcon,
                                      juce::Point<int> preferredPanelSize = { 420, 420 },
                                      juce::Justification messageJustification = juce::Justification::centred) {
    if (parent != nullptr) {
        showOverlayMessage(*parent, title, message, icon, preferredPanelSize, messageJustification);
        return;
    }

    juce::AlertWindow::showMessageBoxAsync(fallbackIcon,
                                           title,
                                           juce::String(message),
                                           "OK");
}

inline void showOverlayConfirmationOrAlert(juce::Component* parent,
                                           juce::StringRef title,
                                           juce::StringRef message,
                                           juce::StringRef confirmText,
                                           juce::StringRef cancelText,
                                           std::function<void()> onConfirmed,
                                           std::function<void()> onCancelled = {},
                                           ErrorOverlay::Icon icon = ErrorOverlay::Icon::Warning,
                                           juce::Point<int> preferredPanelSize = { 460, 300 },
                                           juce::Justification messageJustification = juce::Justification::centredTop) {
    auto confirmedCallback = std::make_shared<std::function<void()>>(std::move(onConfirmed));
    auto cancelledCallback = std::make_shared<std::function<void()>>(std::move(onCancelled));

    if (parent != nullptr) {
        ErrorOverlay::Options options;
        options.closeButtonSvg = makeDialogOverlayCloseButtonSvg();
        options.title = title;
        options.message = message;
        options.icon = icon;
        options.preferredPanelSize = preferredPanelSize;
        options.messageJustification = messageJustification;
        options.buttons.push_back({
            juce::String(confirmText),
            [confirmedCallback] {
                if (*confirmedCallback != nullptr) {
                    (*confirmedCallback)();
                }
            },
            true,
        });
        options.buttons.push_back({
            juce::String(cancelText),
            [cancelledCallback] {
                if (*cancelledCallback != nullptr) {
                    (*cancelledCallback)();
                }
            },
            false,
        });
        options.onDismissed = [cancelledCallback] {
            if (*cancelledCallback != nullptr) {
                (*cancelledCallback)();
            }
        };

        ErrorOverlay::show(*parent, std::move(options));
        return;
    }

    juce::AlertWindow::showOkCancelBox(
        juce::MessageBoxIconType::WarningIcon,
        title,
        message,
        confirmText,
        cancelText,
        nullptr,
        juce::ModalCallbackFunction::create([confirmedCallback, cancelledCallback] (int result) mutable {
            if (result == 1) {
                if (*confirmedCallback != nullptr) {
                    (*confirmedCallback)();
                }
            } else if (*cancelledCallback != nullptr) {
                (*cancelledCallback)();
            }
        }));
}

} // namespace osci
