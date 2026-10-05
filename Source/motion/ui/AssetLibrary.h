#pragma once

#include "MotionStyle.h"
#include "DocumentMenu.h"

#include "../model/Document.h"
#include "../model/Drawing.h"
#include <osci_gui/osci_gui.h>

class MotionAssetLibrary : public juce::Component, private juce::ListBoxModel {
public:
    explicit MotionAssetLibrary(motion::Document& document);
    std::function<void(motion::Id)> onSelectUses;
    std::function<void(const juce::String&)> onMessage;

    std::function<void(motion::Id)> onInsert, onOpenComposition, onRemoveComposition, onReplace, onEditDrawing;
    std::function<void()> onCancelImport;
    std::function<void(motion::Id)> onBake;
    std::function<juce::String(motion::Id)> liveStatus;
    void updateLiveStatus();

    void setImportStatus(const juce::String& message);

    void refresh();

    void selectAsset(motion::Id id);

    void setError(const juce::String& error);

    void updateStatus();

    void resized() override;

    void paint(juce::Graphics& graphics) override;

private:
    void selectedRowsChanged(int row) override;
    int getNumRows() override { return static_cast<int>(assets.size() + definitions.size()); }

    juce::String getNameForRow(int row) override;

    void paintListBoxItem(int row, juce::Graphics& graphics, int width, int height, bool selected) override;

    // A source's middle frame as a path inside the unit square, broken at the
    // jumps between strokes (dark travel in baked points, long steps in
    // vector shapes, which carry no colour).
    static juce::Path traceThumbnail(const motion::Asset& asset);
    // Keyed by the prepared data itself, so a replaced source re-traces.
    struct Thumbnail {
        std::shared_ptr<const motion::PreparedSource> source;
        juce::Path path;
    };
    mutable std::vector<Thumbnail> thumbnails;

    void paintThumbnail(juce::Graphics& graphics, int row, juce::Rectangle<int> box) const;

    void listBoxItemClicked(int row, const juce::MouseEvent& event) override;

    void showSourceMenu(int row);
    // Replace the project's tempo map with a MIDI file's, in one undo step.
    void adoptMidiTempo(motion::Id id);
    // Rename by identity: the list may have changed while the menu was open.
    void beginRename(motion::Id id);
    void finishRename(bool accept);
    void listBoxItemDoubleClicked(int row, const juce::MouseEvent&) override { insert(row); }
    void returnKeyPressed(int row) override { insert(row); }

    juce::var getDragSourceDescription(const juce::SparseSet<int>& rows) override;

    bool validRow(int row) const { return row >= 0 && static_cast<std::size_t>(row) < assets.size() + definitions.size(); }
    bool validAssetRow(int row) const { return row >= 0 && static_cast<std::size_t>(row) < assets.size(); }
    bool definitionRow(int row) const { return validRow(row) && !validAssetRow(row); }

    motion::Id assetId(int row) const;

    void insert(int row);

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
