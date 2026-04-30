#include "LfoPresetBrowserOverlay.h"

// ============================================================================
// LfoPresetBrowserOverlay
// ============================================================================

LfoPresetBrowserOverlay::LfoPresetBrowserOverlay(LfoPresetManager& manager, Listener* listener)
    : presetManager(manager), browserListener(listener) {
    setOpaque(false);

    addAndMakeVisible(browserPanel);

    viewport.setViewedComponent(&content, false);
    viewport.setScrollBarsShown(true, false);
    viewport.getVerticalScrollBar().setColour(juce::ScrollBar::thumbColourId,
                                                juce::Colours::white.withAlpha(0.3f));
    browserPanel.addAndMakeVisible(viewport);

    saveEditor.setFont(juce::Font(juce::FontOptions(13.0f)));
    saveEditor.setTextToShowWhenEmpty("Preset name...", juce::Colours::white.withAlpha(0.3f));
    saveEditor.setColour(juce::TextEditor::backgroundColourId, Colours::veryDark());
    saveEditor.setColour(juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    saveEditor.setColour(juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);
    saveEditor.setColour(juce::TextEditor::textColourId, juce::Colours::white);
    saveEditor.setJustification(juce::Justification::centredLeft);
    saveEditor.onReturnKey = [this]() { doSave(); };
    browserPanel.addAndMakeVisible(saveEditor);

    saveButton.setButtonText("Save");
    saveButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xFF2a6e2a));
    saveButton.setColour(juce::TextButton::textColourOnId, juce::Colours::white);
    saveButton.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    saveButton.onClick = [this]() { doSave(); };
    browserPanel.addAndMakeVisible(saveButton);
}

void LfoPresetBrowserOverlay::show(LfoPreset currentFactoryPreset, const juce::String& currentUserName,
                                    const juce::String& defaultFactoryName, const juce::String& defaultFileName) {
    activeFactoryPreset = currentFactoryPreset;
    activeUserName = currentUserName;
    defaultFactoryPresetName = defaultFactoryName;
    defaultPresetFilePath = defaultFileName;

    rebuildContent();
    resized();
    // Re-layout after resized() so viewport dimensions are valid on first open
    layoutRows();
    browserPanel.resized();
    grabKeyboardFocus();
}

void LfoPresetBrowserOverlay::resized() {
    browserPanel.setBounds(getLocalBounds());
}

void LfoPresetBrowserOverlay::refresh(LfoPreset currentFactoryPreset, const juce::String& currentUserName,
                                       const juce::String& defaultFactoryName, const juce::String& defaultFileName) {
    activeFactoryPreset = currentFactoryPreset;
    activeUserName = currentUserName;
    defaultFactoryPresetName = defaultFactoryName;
    defaultPresetFilePath = defaultFileName;
    rebuildContent();
}

bool LfoPresetBrowserOverlay::keyPressed(const juce::KeyPress& key) {
    if (key == juce::KeyPress::escapeKey) {
        if (onDismissRequested)
            onDismissRequested();
        return true;
    }
    return false;
}

// ============================================================================
// BrowserPanel
// ============================================================================

void LfoPresetBrowserOverlay::BrowserPanel::paint(juce::Graphics& g) {
    auto bounds = getLocalBounds().toFloat();
    constexpr float radius = 6.0f;

    g.setColour(Colours::veryDark().brighter(0.08f));
    g.fillRoundedRectangle(bounds, radius);
}

void LfoPresetBrowserOverlay::BrowserPanel::resized() {
    auto inner = getLocalBounds().reduced(6);

    auto bottomBar = inner.removeFromBottom(26);
    saveButton.setBounds(bottomBar.removeFromRight(48));
    bottomBar.removeFromRight(4);
    saveEditor.setBounds(bottomBar);

    inner.removeFromBottom(4);

    viewport.setBounds(inner);
    int scrollBarW = (contentHeight > inner.getHeight()) ? 8 : 0;
    contentComp.setSize(inner.getWidth() - scrollBarW,
                         juce::jmax(inner.getHeight(), contentHeight));
}

// ============================================================================
// PresetRow
// ============================================================================

void LfoPresetBrowserOverlay::PresetRow::paint(juce::Graphics& g) {
    auto bounds = getLocalBounds().toFloat();
    bool hovering = isMouseOver();

    if (isActive) {
        g.setColour(juce::Colours::white.withAlpha(0.1f));
        g.fillRoundedRectangle(bounds.reduced(1.0f, 0.0f), 3.0f);
    } else if (hovering) {
        g.setColour(juce::Colours::white.withAlpha(0.06f));
        g.fillRoundedRectangle(bounds.reduced(1.0f, 0.0f), 3.0f);
    }

    auto textBounds = bounds.reduced(8.0f, 0.0f);
    if (isUserPreset)
        textBounds.removeFromRight(kDeleteButtonSize + 4);

    g.setColour(isActive ? juce::Colours::white : juce::Colours::white.withAlpha(0.75f));
    g.setFont(juce::Font(juce::FontOptions(12.5f)));

    juce::String displayName = name;
    if (isDefault)
        displayName += " (default)";
    g.drawText(displayName, textBounds, juce::Justification::centredLeft);
}

void LfoPresetBrowserOverlay::PresetRow::mouseDown(const juce::MouseEvent& e) {
    if (e.mods.isRightButtonDown()) {
        juce::PopupMenu menu;
        if (isDefault) {
            menu.addItem("Clear Default", [this]() { if (onClearDefault) onClearDefault(); });
        } else {
            menu.addItem("Set as Default", [this]() { if (onSetDefault) onSetDefault(); });
        }
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea(
            { e.getScreenX(), e.getScreenY(), 1, 1 }));
    }
}

void LfoPresetBrowserOverlay::PresetRow::mouseUp(const juce::MouseEvent& e) {
    if (e.mods.isRightButtonDown()) return;

    auto bounds = getLocalBounds();
    if (isUserPreset) {
        auto deleteArea = bounds.removeFromRight(kDeleteButtonSize + 8);
        if (deleteArea.contains(e.getPosition())) {
            if (onDelete) onDelete();
            return;
        }
    }
    if (onSelect) onSelect();
}

void LfoPresetBrowserOverlay::PresetRow::mouseMove(const juce::MouseEvent&) {
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
}

void LfoPresetBrowserOverlay::PresetRow::paintOverChildren(juce::Graphics& g) {
    if (!isUserPreset) return;

    auto bounds = getLocalBounds();
    auto deleteArea = bounds.removeFromRight(kDeleteButtonSize + 4)
                             .withSizeKeepingCentre(kDeleteButtonSize, kDeleteButtonSize);

    bool hovering = isMouseOver() && deleteArea.expanded(4).contains(getMouseXYRelative());
    float alpha = hovering ? 0.9f : 0.35f;

    g.setColour(juce::Colours::red.withAlpha(alpha));
    auto cx = deleteArea.getCentreX();
    auto cy = deleteArea.getCentreY();
    float sz = 5.0f;
    g.drawLine((float)cx - sz, (float)cy - sz, (float)cx + sz, (float)cy + sz, 1.5f);
    g.drawLine((float)cx + sz, (float)cy - sz, (float)cx - sz, (float)cy + sz, 1.5f);
}

// ============================================================================
// SectionHeader
// ============================================================================

void LfoPresetBrowserOverlay::SectionHeader::paint(juce::Graphics& g) {
    g.setColour(juce::Colours::white.withAlpha(0.4f));
    g.setFont(juce::Font(juce::FontOptions(11.0f, juce::Font::bold)));
    g.drawText(title, getLocalBounds().reduced(8, 0).toFloat(), juce::Justification::centredLeft);

    auto bounds = getLocalBounds().toFloat();
    g.setColour(juce::Colours::white.withAlpha(0.08f));
    g.drawLine(8.0f, bounds.getBottom() - 1.0f, bounds.getRight() - 8.0f, bounds.getBottom() - 1.0f);
}

// ============================================================================
// Content building & layout
// ============================================================================

void LfoPresetBrowserOverlay::rebuildContent() {
    rows.clear();
    content.removeAllChildren();

    int y = 0;

    // Factory section header
    auto* factoryHeader = new SectionHeader();
    factoryHeader->title = "FACTORY";
    rows.add(factoryHeader);
    content.addAndMakeVisible(factoryHeader);
    y += kSectionHeaderHeight;

    // Factory presets
    auto& registry = getLfoPresetRegistry();
    for (int i = 0; i < (int)registry.size(); ++i) {
        auto* row = new PresetRow();
        row->name = registry[i].name;
        row->isActive = (registry[i].preset == activeFactoryPreset) && activeUserName.isEmpty();
        row->isDefault = (defaultPresetFilePath.isEmpty() && defaultFactoryPresetName == registry[i].name);
        row->isUserPreset = false;
        auto preset = registry[i].preset;
        row->onSelect = [this, preset]() {
            if (browserListener) browserListener->presetBrowserFactorySelected(preset);
        };
        row->onSetDefault = [this, preset]() {
            if (browserListener) browserListener->presetBrowserSetDefaultFactory(preset);
        };
        row->onClearDefault = [this]() {
            if (browserListener) browserListener->presetBrowserClearDefault();
        };
        rows.add(row);
        content.addAndMakeVisible(row);
        y += kRowHeight;
    }

    // User section
    auto userPresets = presetManager.getUserPresets();
    numUserPresets = (int)userPresets.size();

    if (!userPresets.empty()) {
        y += 4;
        auto* userHeader = new SectionHeader();
        userHeader->title = "USER";
        rows.add(userHeader);
        content.addAndMakeVisible(userHeader);
        y += kSectionHeaderHeight;

        for (int i = 0; i < (int)userPresets.size(); ++i) {
            auto* row = new PresetRow();
            row->name = userPresets[i].name;
            row->isActive = (activeUserName == userPresets[i].name);
            row->isDefault = (userPresets[i].file.getFullPathName() == defaultPresetFilePath);
            row->isUserPreset = true;
            row->file = userPresets[i].file;
            auto file = userPresets[i].file;
            row->onSelect = [this, file]() {
                if (browserListener) browserListener->presetBrowserUserSelected(file);
            };
            row->onDelete = [this, file]() {
                if (browserListener) browserListener->presetBrowserUserDeleted(file);
            };
            row->onSetDefault = [this, file]() {
                if (browserListener) browserListener->presetBrowserSetDefaultFile(file);
            };
            row->onClearDefault = [this]() {
                if (browserListener) browserListener->presetBrowserClearDefault();
            };
            rows.add(row);
            content.addAndMakeVisible(row);
            y += kRowHeight;
        }
    }

    // Vital LFOs sections — grouped by category folder
    auto vitalPresets = presetManager.getVitalUserPresets();
    numVitalPresets = (int)vitalPresets.size();
    numVitalHeaders = 0;

    if (!vitalPresets.empty()) {
        juce::String currentCategory;

        for (int i = 0; i < (int)vitalPresets.size(); ++i) {
            if (vitalPresets[i].category != currentCategory) {
                currentCategory = vitalPresets[i].category;
                y += 4;
                auto* catHeader = new SectionHeader();
                catHeader->title = currentCategory.toUpperCase() + " (VITAL)";
                rows.add(catHeader);
                content.addAndMakeVisible(catHeader);
                y += kSectionHeaderHeight;
                numVitalHeaders++;
            }

            auto* row = new PresetRow();
            row->name = vitalPresets[i].name;
            row->isActive = (activeUserName == vitalPresets[i].name);
            row->isDefault = (vitalPresets[i].file.getFullPathName() == defaultPresetFilePath);
            row->isUserPreset = false;
            auto file = vitalPresets[i].file;
            row->onSelect = [this, file]() {
                if (browserListener) browserListener->presetBrowserUserSelected(file);
            };
            row->onSetDefault = [this, file]() {
                if (browserListener) browserListener->presetBrowserSetDefaultFile(file);
            };
            row->onClearDefault = [this]() {
                if (browserListener) browserListener->presetBrowserClearDefault();
            };
            rows.add(row);
            content.addAndMakeVisible(row);
            y += kRowHeight;
        }
    }

    contentHeight = y;
    browserPanel.contentHeight = y;
    layoutRows();
    browserPanel.resized();
}

void LfoPresetBrowserOverlay::layoutRows() {
    int w = viewport.getWidth();
    if (w <= 0) w = getWidth();
    int scrollBarW = (contentHeight > viewport.getHeight()) ? 8 : 0;
    int rowW = w - scrollBarW;

    int y = 0;
    int rowIdx = 0;
    auto& registry = getLfoPresetRegistry();

    // Factory header
    if (rowIdx < rows.size()) {
        rows[rowIdx]->setBounds(0, y, rowW, kSectionHeaderHeight);
        y += kSectionHeaderHeight;
        rowIdx++;
    }

    // Factory presets
    for (int i = 0; i < (int)registry.size() && rowIdx < rows.size(); ++i, ++rowIdx) {
        rows[rowIdx]->setBounds(0, y, rowW, kRowHeight);
        y += kRowHeight;
    }

    // User presets (use cached count instead of re-scanning filesystem)
    if (numUserPresets > 0 && rowIdx < rows.size()) {
        y += 4;
        rows[rowIdx]->setBounds(0, y, rowW, kSectionHeaderHeight);
        y += kSectionHeaderHeight;
        rowIdx++;

        for (int i = 0; i < numUserPresets && rowIdx < rows.size(); ++i, ++rowIdx) {
            rows[rowIdx]->setBounds(0, y, rowW, kRowHeight);
            y += kRowHeight;
        }
    }

    // Vital presets — multiple category headers interleaved with preset rows
    int vitalItemsRemaining = numVitalPresets + numVitalHeaders;
    while (vitalItemsRemaining > 0 && rowIdx < rows.size()) {
        // Determine if this is a header or a preset row
        auto* asHeader = dynamic_cast<SectionHeader*>(rows[rowIdx]);
        if (asHeader != nullptr) {
            y += 4;
            rows[rowIdx]->setBounds(0, y, rowW, kSectionHeaderHeight);
            y += kSectionHeaderHeight;
        } else {
            rows[rowIdx]->setBounds(0, y, rowW, kRowHeight);
            y += kRowHeight;
        }
        rowIdx++;
        vitalItemsRemaining--;
    }

    content.setSize(rowW, juce::jmax(viewport.getHeight(), contentHeight));
}

void LfoPresetBrowserOverlay::doSave() {
    auto name = saveEditor.getText().trim();
    if (name.isEmpty()) return;
    if (browserListener) browserListener->presetBrowserSaveRequested(name);
    saveEditor.clear();
}
