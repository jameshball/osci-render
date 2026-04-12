#pragma once

#include <JuceHeader.h>
#include <osci_render_core/effect/osci_EffectParameter.h>

// Shared context-menu builder for any parameter (used by EffectComponent,
// KnobContainerComponent/RotaryKnobComponent, etc.).
//
// Callers provide lightweight callbacks for the two actions that differ by
// component type ("show inline value editor" and "show range/settings popup").
namespace ParameterContextMenu {

    // Menu item offsets relative to firstId
    enum ItemOffset {
        kResetToDefault = 0,
        kSetValue       = 1,
        kEditRange      = 2,
        kLearnCC        = 3,
        kRemoveCC       = 4,
        kCount          = 5
    };

    struct Context {
        juce::AudioProcessorParameterWithID* param = nullptr;
        osci::EffectParameter* effectParam = nullptr; // non-null ⇒ show "Edit Range…"
        osci::MidiCCManager* midiCCManager = nullptr;       // non-null ⇒ show MIDI CC items
        bool canResetToDefault = true;
        bool canSetValue = true;                        // false for boolean params (toggle only)
        osci::EffectParameter* ccEffectParam = nullptr; // for CC sub-range (may differ from effectParam)
    };

    // Append all items to |menu| starting at |firstId|.
    // Returns the next available item id.
    inline int buildMenu(juce::PopupMenu& menu, int firstId, const Context& ctx) {
        menu.addItem(firstId + kResetToDefault, "Reset to Default Value", ctx.canResetToDefault);

        if (ctx.canSetValue)
            menu.addItem(firstId + kSetValue, "Set Value...");

        if (ctx.effectParam != nullptr) {
            menu.addSeparator();
            menu.addItem(firstId + kEditRange, "Edit Range...");
        }

        if (ctx.midiCCManager != nullptr && ctx.param != nullptr) {
            menu.addSeparator();
            int assignedCC = ctx.midiCCManager->getAssignedCC(ctx.param);
            bool learning = ctx.midiCCManager->isLearning(ctx.param);

            if (learning) {
                menu.addItem(firstId + kLearnCC, "Cancel MIDI CC Learn", true, true);
            } else {
                juce::String learnText = "Learn MIDI CC Assignment";
                if (assignedCC >= 0)
                    learnText += " (CC " + juce::String(assignedCC) + ")";
                menu.addItem(firstId + kLearnCC, learnText);
            }

            if (assignedCC >= 0)
                menu.addItem(firstId + kRemoveCC, "Remove MIDI CC Assignment (CC " + juce::String(assignedCC) + ")");
        }

        return firstId + kCount;
    }

    // Handle a menu result.
    // |resetToDefault| — called for "Reset to Default Value"
    // |showValueEditor| — called for "Set Value…"
    // |showRangeEditor| — called for "Edit Range…"
    // Returns true if the result was handled.
    inline bool handleResult(int result, int firstId, const Context& ctx,
                             std::function<void()> resetToDefault,
                             std::function<void()> showValueEditor,
                             std::function<void()> showRangeEditor) {
        if (result == firstId + kResetToDefault) {
            if (resetToDefault) resetToDefault();
            return true;
        }
        if (result == firstId + kSetValue) {
            if (showValueEditor) showValueEditor();
            return true;
        }
        if (result == firstId + kEditRange && ctx.effectParam != nullptr) {
            if (showRangeEditor) showRangeEditor();
            return true;
        }
        if (result == firstId + kLearnCC && ctx.midiCCManager != nullptr && ctx.param != nullptr) {
#if OSCI_PREMIUM
            if (ctx.midiCCManager->isLearning(ctx.param))
                ctx.midiCCManager->stopLearning();
            else
                ctx.midiCCManager->startLearning(ctx.param, ctx.ccEffectParam);
#else
            juce::AlertWindow::showMessageBoxAsync(
                juce::AlertWindow::InfoIcon,
                "Premium Feature",
                "MIDI CC mapping is available in the premium version of osci-render.");
#endif
            return true;
        }
        if (result == firstId + kRemoveCC && ctx.midiCCManager != nullptr && ctx.param != nullptr) {
#if OSCI_PREMIUM
            ctx.midiCCManager->removeAssignment(ctx.param);
#else
            juce::AlertWindow::showMessageBoxAsync(
                juce::AlertWindow::InfoIcon,
                "Premium Feature",
                "MIDI CC mapping is available in the premium version of osci-render.");
#endif
            return true;
        }
        return false;
    }

    // Wire a ChangeListener to a MidiCCManager, removing any previous listener.
    // Shared by EffectComponent, KnobContainerComponent, and BooleanParamCCHelper.
    inline void wireMidiCCListener(osci::MidiCCManager*& current,
                                   osci::MidiCCManager& manager,
                                   juce::ChangeListener* listener) {
        if (current)
            current->removeChangeListener(listener);
        current = &manager;
        current->addChangeListener(listener);
    }

    // Build the menu, show it asynchronously, and handle the result.
    // |repaintTarget| is repainted when a menu action is handled.
    // The three callbacks should already capture any SafePointers they need.
    inline void showAsync(const Context& ctx,
                          juce::Point<int> screenPos,
                          juce::Component* repaintTarget,
                          std::function<void()> onReset,
                          std::function<void()> onSetValue,
                          std::function<void()> onEditRange) {
        juce::PopupMenu menu;
        buildMenu(menu, 1, ctx);

        auto options = juce::PopupMenu::Options().withTargetScreenArea(
            juce::Rectangle<int>(screenPos.x, screenPos.y, 1, 1));

        auto safeTarget = juce::Component::SafePointer<juce::Component>(repaintTarget);
        menu.showMenuAsync(options,
            [safeTarget, ctx,
             onReset = std::move(onReset),
             onSetValue = std::move(onSetValue),
             onEditRange = std::move(onEditRange)](int result) {
                if (safeTarget == nullptr) return;
                if (handleResult(result, 1, ctx, onReset, onSetValue, onEditRange))
                    safeTarget->repaint();
            });
    }

} // namespace ParameterContextMenu
