#pragma once

#include "../model/Document.h"

namespace motion::ui {
// Shows a menu about the document as it is now. The choice runs only while
// `owner` exists and the document has not changed since the menu opened, so a
// choice never acts on a project the user can no longer see.
inline void showDocumentMenu(juce::PopupMenu menu, juce::Component& owner, const Document& document, juce::PopupMenu::Options options, std::function<void(int)> chosen) {
    menu.setLookAndFeel(&owner.getLookAndFeel());
    const juce::Component::SafePointer<juce::Component> safe(&owner);
    const auto generation = document.generation();
    const auto revision = document.revision();
    menu.showMenuAsync(options, [safe, &document, generation, revision, chosen = std::move(chosen)](int result) {
        // The document outlives every component that shows its menus.
        if (result == 0 || safe == nullptr || document.generation() != generation || document.revision() != revision) {
            return;
        }
        chosen(result);
    });
}
}
