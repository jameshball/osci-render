#pragma once

#include <JuceHeader.h>
#include <osci_gui/osci_gui.h>

// Shows a temporary inline TextEditor over a Slider to let the user type a value.
// The editor is added as a child of |parent| at the given |bounds|.
// On Enter/FocusLost the value is applied; on Escape it is dismissed.
// Clicking anywhere outside the editor also applies the value (via modal state).
//
// Uses shared_ptr so that the TextEditor stays alive for the duration of the
// callback even if the parent or storage is reset. A shared dismissed flag
// prevents re-entrant or double invocations.
namespace InlineValueEditor {

// TextEditor subclass that detects click-away via JUCE's modal component system.
class Editor : public osci::TextEditor {
public:
    std::function<void()> onClickAway;

    void inputAttemptWhenModal() override {
        // Move to local so the function stays alive if the callback clears onClickAway
        if (auto fn = std::move(onClickAway))
            fn();
    }
};

inline void show(juce::Component& parent,
                 juce::Slider& slider,
                 std::shared_ptr<juce::TextEditor>& storage,
                 juce::Rectangle<int> bounds) {
    // Dismiss any existing editor to avoid orphaned children
    if (storage != nullptr) {
        storage->exitModalState(0);
        storage->onFocusLost = nullptr;
        storage->onReturnKey = nullptr;
        storage->onEscapeKey = nullptr;
        if (auto* modal = dynamic_cast<Editor*>(storage.get()))
            modal->onClickAway = nullptr;
        parent.removeChildComponent(storage.get());
        storage.reset();
    }

    auto editor = std::make_shared<Editor>();
    editor->setMultiLine(false);
    editor->setJustification(juce::Justification::centred);
    editor->setText(juce::String(slider.getValue()), false);
    editor->selectAll();

    int editorW = juce::jmax(bounds.getWidth(), 60);
    int editorH = 22;
    int x = bounds.getX() + (bounds.getWidth() - editorW) / 2;
    int y = bounds.getY() + (bounds.getHeight() - editorH) / 2;
    editor->setBounds(x, y, editorW, editorH);

    storage = editor;
    parent.addAndMakeVisible(editor.get());
    editor->enterModalState(false);
    editor->grabKeyboardFocus();

    auto safeParent = juce::Component::SafePointer<juce::Component>(&parent);
    auto safeSlider = juce::Component::SafePointer<juce::Slider>(&slider);
    std::weak_ptr<juce::TextEditor> weakEditor = editor;
    auto dismissed = std::make_shared<bool>(false);

    auto apply = [safeParent, safeSlider, weakEditor, dismissed]() {
        if (*dismissed) return;
        *dismissed = true;
        auto ed = weakEditor.lock();
        if (!ed) return;
        // Copy captures to locals — clearing callbacks below may destroy this lambda
        auto parent = safeParent;
        auto slider = safeSlider;
        ed->exitModalState(0);
        ed->onFocusLost = nullptr;
        ed->onReturnKey = nullptr;
        ed->onEscapeKey = nullptr;
        if (auto* modal = dynamic_cast<Editor*>(ed.get()))
            modal->onClickAway = nullptr;

        if (parent != nullptr && slider != nullptr) {
            double newValue = ed->getText().getDoubleValue();
            newValue = juce::jlimit(slider->getMinimum(), slider->getMaximum(), newValue);
            slider->setValue(newValue, juce::sendNotificationSync);
        }
        if (parent != nullptr)
            parent->removeChildComponent(ed.get());
    };

    auto dismiss = [safeParent, weakEditor, dismissed]() {
        if (*dismissed) return;
        *dismissed = true;
        auto ed = weakEditor.lock();
        if (!ed) return;
        auto parent = safeParent;
        ed->exitModalState(0);
        ed->onFocusLost = nullptr;
        ed->onReturnKey = nullptr;
        ed->onEscapeKey = nullptr;
        if (auto* modal = dynamic_cast<Editor*>(ed.get()))
            modal->onClickAway = nullptr;
        if (parent != nullptr)
            parent->removeChildComponent(ed.get());
    };

    editor->onReturnKey = apply;
    editor->onEscapeKey = dismiss;
    editor->onFocusLost = apply;
    editor->onClickAway = apply;
}

} // namespace InlineValueEditor
