#pragma once

#include "MotionStyle.h"
#include "DocumentMenu.h"
#include "FormControls.h"
#include "MotionIcons.h"

#include "../model/Document.h"
#include "../model/Drawing.h"
#include <osci_gui/osci_gui.h>

class MotionAssetLibrary : public juce::Component, private juce::ListBoxModel, private juce::ScrollBar::Listener, private juce::AsyncUpdater {
public:
    // While a source is written in the Scene, its edit button rests.
    void setEditingSource(bool editing) { edit.setEnabled(!editing); }
    explicit MotionAssetLibrary(motion::Document& document);
    std::function<void(motion::Id)> onSelectUses;
    // A notice (something done) or a problem, for the status bar.
    std::function<void(const juce::String&, bool problem)> onMessage;

    std::function<void(motion::Id)> onInsert, onOpenComposition, onRemoveComposition, onReplace, onEditDrawing;
    std::function<void()> onCancelImport;
    std::function<void(motion::Id)> onBake;
    // The source (or composition) selected, or 0.
    motion::Id chosen() const { return validRow(list.getSelectedRow()) ? assetId(list.getSelectedRow()) : 0; }
    std::function<juce::String(motion::Id)> liveStatus;
    void updateLiveStatus();

    void setImportStatus(const juce::String& message);

    void refresh();
    // As refresh, when what the list shows has changed since it was built:
    // most edits (and a drag's every step) leave it alone.
    void refreshIfChanged() {
        if (builtFrom != shownInputs()) { refresh(); }
    }

    void selectAsset(motion::Id id);
    // Selected, scrolled to and marked: "Show in Sources".
    void revealAsset(motion::Id id);


    void updateStatus();
    void handleAsyncUpdate() override;

    void resized() override;

    void paint(juce::Graphics& graphics) override;

private:
    void selectedRowsChanged(int row) override;
    int getNumRows() override { return static_cast<int>(assets.size() + definitions.size()); }

    juce::String getNameForRow(int row) override;
    juce::String getTooltipForRow(int row) override;

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
    // A row's face (what hover and selection fill), lined up with Add source;
    // where its picture is, and the badge in its corner saying its file is
    // linked from disk.
    static juce::Rectangle<int> rowFace(int width, int height) { return juce::Rectangle<int>(width, height).reduced(4, motion::style::gap / 2); }
    static juce::Rectangle<int> thumbnailArea(int width, int height) { return motion::style::libraryRow::pictureArea(rowFace(width, height)); }
    static juce::Rectangle<float> linkBadge(int width, int height) {
        const auto picture = thumbnailArea(width, height).toFloat();
        return juce::Rectangle<float>(12, 12).withCentre(picture.getBottomRight() - juce::Point<float>(3, 3));
    }

    void listBoxItemClicked(int row, const juce::MouseEvent& event) override;

    void showSourceMenu(int row);
    // Rename by identity: the list may have changed while the menu was open.
    void beginRename(motion::Id id);
    void finishRename(bool accept);
    void listBoxItemDoubleClicked(int row, const juce::MouseEvent&) override { insert(row); }
    // The row under the pointer shows a + that adds it at the playhead, as
    // double-clicking it does.
    int hoveredRow = -1;
    void mouseMove(const juce::MouseEvent& event) override;
    // Leaving a row (for its own edit button, or away) goes by where the
    // pointer now is.
    void mouseExit(const juce::MouseEvent&) override { hover(list.getMouseXYRelative()); }
    void hover(juce::Point<int> inList);
    bool addable(int row) const { return validRow(row) && (!definitionRow(row) || document.canReferenceComposition(assetId(row))); }
    // On the hovered row: the + at its end, its edit button to the left.
    static juce::Rectangle<int> addArea(int width, int height) { return {width - 36, (height - 28) / 2, 28, 28}; }
    // The hovered source's edit button, over its row: what it edits or
    // sets up, by kind ("Edit Lua...", "Image settings..."); empty for a
    // source with nothing to change after the fact.
    juce::String editLabel(int row) const;
    void editRow(int row);
    void placeEdit();
    void scrollBarMoved(juce::ScrollBar*, double) override { placeEdit(); }
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
    // What the list is built from: the scope's sources, the project's
    // compositions, and the scope they are offered to. Held, so a new source
    // can never take a removed one's address.
    struct Inputs {
        std::vector<std::shared_ptr<const motion::Asset>> assets;
        std::vector<std::shared_ptr<const motion::CompositionDefinition>> definitions;
        motion::Id scope = 0;
        bool operator==(const Inputs&) const = default;
    };
    Inputs shownInputs() const { return {document.project().assets, document.mainProject().definitions, document.editingComposition()}; }
    std::optional<Inputs> builtFrom;
    juce::ListBox list;
    osci::ListBoxScrollFades fades {list};
    juce::TextEditor search, rename;
    motion::Id renaming = 0;
    juce::Label status;
    juce::TextButton cancelImport;
    motion::icons::Button edit {"Edit source", motion::icons::Icon::pen};
    bool modelRow(int row) const;
    juce::String importStatus;
};
