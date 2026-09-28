#pragma once

#include "../model/Document.h"
#include <osci_gui/osci_gui.h>

class MotionAssetLibrary : public juce::Component, private juce::ListBoxModel {
public:
    explicit MotionAssetLibrary(motion::Document& document) : document(document), list("Motion assets", this) {
        setName("Asset library");
        list.setComponentID("motion.assets");
        list.setRowHeight(46);
        list.setMultipleSelectionEnabled(false);
        list.setColour(juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
        list.setOutlineThickness(0);
        addAndMakeVisible(list);
        status.setJustificationType(juce::Justification::topLeft);
        status.setFont(juce::Font(12.0f));
        addAndMakeVisible(status);
        cancelImport.setButtonText("Cancel import");
        cancelImport.onClick = [this] { if (onCancelImport) { onCancelImport(); } };
        addChildComponent(cancelImport);
        bakeSettings.setButtonText("Bake settings...");
        bakeSettings.onClick = [this] {
            const auto row = list.getSelectedRow();
            if (definitionRow(row)) { if (onOpenComposition) { onOpenComposition(assetId(row)); } }
            else if (validAssetRow(row) && onBake) { onBake(assetId(row)); }
        };
        addChildComponent(bakeSettings);
        assignMidi.setButtonText("Assign to selected clip");
        assignMidi.onClick = [this] { insert(list.getSelectedRow()); };
        addChildComponent(assignMidi);
        search.setName("Search sources");
        search.setTextToShowWhenEmpty("Search sources", osci::Colours::textMuted());
        search.setFont(juce::Font(juce::FontOptions(12.0f)));
        search.setColour(juce::TextEditor::backgroundColourId, osci::Colours::veryDark());
        search.setColour(juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
        search.setIndents(8, 5);
        search.onTextChange = [this] { refresh(); };
        search.onEscapeKey = [this] { search.clear(); refresh(); };
        addAndMakeVisible(search);
        rename.setName("Rename source");
        rename.setFont(juce::Font(juce::FontOptions(13.0f)));
        rename.setColour(juce::TextEditor::backgroundColourId, osci::Colours::veryDark());
        rename.onReturnKey = [this] { finishRename(true); };
        rename.onEscapeKey = [this] { finishRename(false); };
        rename.onFocusLost = [this] { finishRename(true); };
        addChildComponent(rename);
        setError({});
        refresh();
    }
    std::function<void(motion::Id)> onSelectUses;
    std::function<void(const juce::String&)> onMessage;

    std::function<void(motion::Id)> onInsert, onOpenComposition, onRemoveComposition;
    std::function<void()> onCancelImport;
    std::function<void(motion::Id)> onBake;
    std::function<juce::String(motion::Id)> liveStatus;
    void updateLiveStatus() {
        const auto row = list.getSelectedRow();
        if (validAssetRow(row) && assets[static_cast<std::size_t>(row)]->liveIdentity != nullptr) { updateStatus(); list.repaint(); }
    }

    void setImportStatus(const juce::String& message) {
        if (importStatus == message) { return; }
        importStatus = message;
        updateStatus();
    }

    void refresh() {
        const auto selectedId = assetId(list.getSelectedRow());
        const auto filter = search.getText().trim();
        assets.clear();
        definitions.clear();
        for (const auto& asset : document.project().assets) {
            if (asset != nullptr && (filter.isEmpty() || asset->name.containsIgnoreCase(filter))) { assets.push_back(asset); }
        }
        for (const auto& definition : document.mainProject().definitions) {
            if (definition != nullptr && (filter.isEmpty() || juce::String(definition->name).containsIgnoreCase(filter))) { definitions.push_back(definition); }
        }
        list.updateContent();
        list.deselectAllRows();
        selectAsset(selectedId);
        repaint();
    }

    void selectAsset(motion::Id id) {
        for (std::size_t row = 0; row < assets.size() + definitions.size(); ++row) {
            if (assetId(static_cast<int>(row)) == id) {
                list.selectRow(static_cast<int>(row));
                return;
            }
        }
    }

    void setError(const juce::String& error) {
        errorMessage = error;
        hasError = error.isNotEmpty();
        updateStatus();
    }

    void updateStatus() {
        cancelImport.setVisible(importStatus.isNotEmpty());
        status.setColour(juce::Label::textColourId, hasError && importStatus.isEmpty() ? juce::Colours::orange : osci::Colours::text().withAlpha(0.6f));
        const auto row = list.getSelectedRow();
        const auto midi = validAssetRow(row) ? assets[static_cast<std::size_t>(row)]->midi : nullptr;
        juce::String help = midi != nullptr ? "Select a visual clip, then assign these notes. Or drag this MIDI file onto a clip." : "Double-click or press Enter to insert. Drag onto the timeline to place a copy.";
        if (validAssetRow(row) && assets[static_cast<std::size_t>(row)]->liveIdentity != nullptr && liveStatus) {
            help = liveStatus(assetId(row)) + "\nEnter to insert. Drag to place.";
        }
        if (definitionRow(row)) {
            help = document.canReferenceComposition(assetId(row))
                ? "Shared composition. Enter or double-click to insert; drag to place. Open to edit."
                : "Contains this scope: insertion would create a loop. Open to edit.";
        }
        if (midi != nullptr) {
            const auto& asset = *assets[static_cast<std::size_t>(row)];
            help += "\n" + juce::String(static_cast<int>(midi->notes().size())) + (midi->notes().size() == 1 ? " note" : " notes");
            if (asset.midiSuggestedBpm > 0) { help += " | " + juce::String(asset.midiSuggestedBpm, 1) + " BPM suggested"; }
            if (asset.midiIgnoredEvents > 0) { help += "\n" + juce::String(asset.midiIgnoredEvents) + " unsupported events were not imported."; }
        }
        status.setText(importStatus.isNotEmpty() ? importStatus : (hasError ? errorMessage : help), juce::dontSendNotification);
        resized();
    }

    void resized() override {
        auto area = getLocalBounds();
        search.setBounds(area.removeFromTop(26).reduced(4, 1));
        area.removeFromTop(4);
        if (cancelImport.isVisible()) {
            cancelImport.setBounds(area.removeFromBottom(30).reduced(6, 2));
        }
        const auto row = list.getSelectedRow();
        const bool live = validAssetRow(row) && assets[static_cast<std::size_t>(row)]->liveIdentity != nullptr;
        status.setBounds(area.removeFromBottom(hasError || assignMidi.isVisible() ? 126 : live ? 92 : 68).reduced(6, 4));
        if (assignMidi.isVisible()) { assignMidi.setBounds(area.removeFromBottom(30).reduced(6, 2)); }
        if (bakeSettings.isVisible()) { bakeSettings.setBounds(area.removeFromBottom(30).reduced(6, 2)); }
        list.setBounds(area);
    }

    void paint(juce::Graphics& graphics) override {
        if (assets.empty() && definitions.empty()) {
            graphics.setColour(osci::Colours::text().withAlpha(0.6f));
            graphics.setFont(13.0f);
            graphics.drawFittedText("Import a source to add it to your asset library.", list.getBounds().reduced(12), juce::Justification::centred, 3);
        }
    }

private:
    void selectedRowsChanged(int row) override {
        assignMidi.setVisible(validAssetRow(row) && assets[static_cast<std::size_t>(row)]->midi != nullptr);
        updateStatus();
        const bool raster = validAssetRow(row) && motion::Document::isRasterSource(assets[static_cast<std::size_t>(row)]->extension);
        const bool text = validAssetRow(row) && assets[static_cast<std::size_t>(row)]->extension.equalsIgnoreCase(".txt");
        const bool live = validAssetRow(row) && assets[static_cast<std::size_t>(row)]->liveIdentity != nullptr;
        const bool fractal = validAssetRow(row) && assets[static_cast<std::size_t>(row)]->extension.equalsIgnoreCase(".lsystem");
        bakeSettings.setButtonText(live ? "Blender settings..." : definitionRow(row) ? "Open composition" : text ? "Edit text..." : (validAssetRow(row) && assets[static_cast<std::size_t>(row)]->extension.equalsIgnoreCase(".lua")) ? "Edit Lua..." : (fractal ? "Fractal settings..." : (raster ? (motion::Document::isVideoSource(assets[static_cast<std::size_t>(row)]->extension) ? "Video settings..." : "Image settings...") : "Bake settings...")));
        bakeSettings.setVisible(live || definitionRow(row) || text || fractal || raster || (validAssetRow(row) && assets[static_cast<std::size_t>(row)]->extension.equalsIgnoreCase(".lua")));
        resized();
    }
    int getNumRows() override { return static_cast<int>(assets.size() + definitions.size()); }

    juce::String getNameForRow(int row) override {
        return definitionRow(row) ? definitions[static_cast<std::size_t>(row) - assets.size()]->name
            : validAssetRow(row) ? assets[static_cast<std::size_t>(row)]->name : juce::String();
    }

    void paintListBoxItem(int row, juce::Graphics& graphics, int width, int height, bool selected) override {
        if (!validRow(row)) {
            return;
        }
        auto bounds = juce::Rectangle<int>(0, 0, width, height).reduced(4, 2);
        if (selected) {
            graphics.setColour(osci::Colours::surfaceRaised().interpolatedWith(osci::Colours::accentColor(), 0.08f));
            graphics.fillRoundedRectangle(bounds.toFloat(), 3.0f);
            graphics.setColour(osci::Colours::accentColor().withAlpha(0.65f));
            graphics.fillRect(bounds.withWidth(2).reduced(0, 5));
        }
        bounds.reduce(8, 3);
        graphics.setColour(osci::Colours::text());
        graphics.setFont(13.0f);
        graphics.drawText(getNameForRow(row), bounds.removeFromTop(20), juce::Justification::centredLeft);
        graphics.setColour(osci::Colours::text().withAlpha(0.55f));
        graphics.setFont(11.0f);
        juce::String detail;
        if (definitionRow(row)) {
            const auto& definition = *definitions[static_cast<std::size_t>(row) - assets.size()];
            const auto clip = motion::Document::makeCompositionClip(0, definition, 0);
            detail = "COMPOSITION | " + juce::String(clip.duration, 2) + "s";
        } else {
            const auto& asset = *assets[static_cast<std::size_t>(row)];
            detail = asset.liveIdentity != nullptr ? "LIVE BLENDER" : asset.extension.equalsIgnoreCase(".blender-capture") ? "CAPTURE" : asset.extension.trimCharactersAtStart(".").toUpperCase();
            if (asset.source != nullptr && (asset.source->frameCount() > 1 || asset.extension.equalsIgnoreCase(".blender-capture"))) {
                detail += " | " + juce::String(asset.source->duration(), 2) + "s | " + juce::String(static_cast<int>(asset.source->frameCount())) + (asset.source->frameCount() == 1 ? " frame" : " frames");
            }
        }
        graphics.drawText(detail, bounds, juce::Justification::centredLeft);
    }

    void listBoxItemClicked(int row, const juce::MouseEvent& event) override {
        if (event.mods.isPopupMenu() && validAssetRow(row)) { showSourceMenu(row); return; }
        if (!event.mods.isPopupMenu() || !definitionRow(row)) { return; }
        const auto id = assetId(row);
        const auto generation = document.generation();
        const auto references = document.compositionReferenceCount(id);
        const bool open = id == document.editingComposition();
        juce::PopupMenu menu;
        menu.addItem(1, "Open composition");
        menu.addItem(2, "Insert instance", document.canReferenceComposition(id));
        menu.addSeparator();
        menu.addItem(3, references != 0 ? "Remove composition (in use)" : open ? "Remove composition (open)" : "Remove unused composition", references == 0 && !open);
        const juce::Component::SafePointer<MotionAssetLibrary> owner(this);
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this).withMousePosition(), [owner, id, generation](int result) {
            if (owner == nullptr || owner->document.generation() != generation) { return; }
            if (result == 1 && owner->onOpenComposition) { owner->onOpenComposition(id); }
            if (result == 2 && owner->onInsert) { owner->onInsert(id); }
            if (result == 3 && owner->onRemoveComposition) { owner->onRemoveComposition(id); }
        });
    }

    void showSourceMenu(int row) {
        list.selectRow(row);
        const auto id = assetId(row);
        const auto uses = document.assetUses(id);
        const auto generation = document.generation();
        juce::PopupMenu menu;
        menu.addSectionHeader(assets[static_cast<std::size_t>(row)]->name);
        menu.addItem(1, "Insert at playhead");
        menu.addItem(2, "Rename...");
        menu.addItem(3, uses == 0 ? "Not used by any clip" : "Select " + juce::String(static_cast<int>(uses)) + (uses == 1 ? " clip using it" : " clips using it"), uses != 0);
        menu.addSeparator();
        menu.addItem(4, uses == 0 ? "Remove source" : "Remove source (in use)", uses == 0);
        menu.addItem(5, "Remove all unused sources");
        const juce::Component::SafePointer<MotionAssetLibrary> owner(this);
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this).withMousePosition(), [owner, id, row, generation](int result) {
            if (owner == nullptr || result == 0 || owner->document.generation() != generation) { return; }
            if (result == 1 && owner->onInsert) { owner->onInsert(id); }
            if (result == 2) { owner->beginRename(row); }
            if (result == 3 && owner->onSelectUses) { owner->onSelectUses(id); }
            if (result == 4 || result == 5) {
                int removed = 0;
                const auto outcome = owner->document.removeUnusedAssets(result == 4 ? std::vector<motion::Id>{id} : std::vector<motion::Id>{}, removed);
                if (owner->onMessage) {
                    owner->onMessage(outcome.failed() ? outcome.getErrorMessage() : "Removed " + juce::String(removed) + (removed == 1 ? " unused source." : " unused sources."));
                }
            }
        });
    }
    void beginRename(int row) {
        if (!validAssetRow(row)) { return; }
        renaming = assetId(row);
        rename.setText(assets[static_cast<std::size_t>(row)]->name, juce::dontSendNotification);
        rename.setBounds(list.getRowPosition(row, true).translated(list.getX(), list.getY()).reduced(8, 10).withHeight(24));
        rename.setVisible(true);
        rename.grabKeyboardFocus();
        rename.selectAll();
    }
    void finishRename(bool accept) {
        if (!rename.isVisible()) { return; }
        rename.setVisible(false);
        const auto id = std::exchange(renaming, 0);
        if (!accept) { return; }
        const auto result = document.renameAsset(id, rename.getText());
        if (result.failed() && onMessage) { onMessage(result.getErrorMessage()); }
    }
    void listBoxItemDoubleClicked(int row, const juce::MouseEvent&) override { insert(row); }
    void returnKeyPressed(int row) override { insert(row); }

    juce::var getDragSourceDescription(const juce::SparseSet<int>& rows) override {
        if (rows.size() == 0 || !validRow(rows[0])) {
            return {};
        }
        return "motion-asset:" + juce::String(static_cast<juce::uint64>(assetId(rows[0])));
    }

    bool validRow(int row) const { return row >= 0 && static_cast<std::size_t>(row) < assets.size() + definitions.size(); }
    bool validAssetRow(int row) const { return row >= 0 && static_cast<std::size_t>(row) < assets.size(); }
    bool definitionRow(int row) const { return validRow(row) && !validAssetRow(row); }

    motion::Id assetId(int row) const {
        return definitionRow(row) ? definitions[static_cast<std::size_t>(row) - assets.size()]->id
            : validAssetRow(row) ? assets[static_cast<std::size_t>(row)]->id : 0;
    }

    void insert(int row) {
        if (validRow(row) && onInsert) {
            onInsert(assetId(row));
        }
    }

    motion::Document& document;
    std::vector<std::shared_ptr<const motion::Asset>> assets;
    std::vector<std::shared_ptr<const motion::CompositionDefinition>> definitions;
    juce::ListBox list;
    juce::TextEditor search, rename;
    motion::Id renaming = 0;
    juce::Label status;
    juce::TextButton cancelImport, bakeSettings, assignMidi;
    juce::String importStatus, errorMessage;
    bool hasError = false;
};
