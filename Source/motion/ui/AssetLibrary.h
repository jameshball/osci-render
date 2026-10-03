#pragma once

#include "MotionStyle.h"

#include "../model/Document.h"
#include "../model/Drawing.h"
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
        status.setFont(motion::style::body());
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
        search.setFont(motion::style::body());
        search.setColour(juce::TextEditor::backgroundColourId, osci::Colours::veryDark());
        search.setColour(juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
        search.setIndents(8, 5);
        search.onTextChange = [this] { refresh(); };
        search.onEscapeKey = [this] { search.clear(); refresh(); };
        addAndMakeVisible(search);
        rename.setName("Rename source");
        rename.setFont(motion::style::body());
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

    std::function<void(motion::Id)> onInsert, onOpenComposition, onRemoveComposition, onReplace, onEditDrawing;
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
        std::erase_if(thumbnails, [&](const Thumbnail& thumbnail) {
            return std::none_of(document.project().assets.begin(), document.project().assets.end(), [&](const auto& asset) { return asset != nullptr && asset->source == thumbnail.source && asset->drawing == thumbnail.drawing; });
        });
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
        // Only say something the row does not already show; the generic
        // how-to lives in the list's tooltip.
        juce::String help = midi != nullptr ? "Select a visual clip, then assign these notes. Or drag this MIDI file onto a clip." : juce::String();
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
        // The status line takes room only when it has something to say.
        const auto statusHeight = status.getText().isEmpty() && importStatus.isEmpty() ? 0 : hasError || assignMidi.isVisible() ? 126 : live ? 92 : 68;
        status.setBounds(area.removeFromBottom(statusHeight).reduced(6, statusHeight > 0 ? 4 : 0));
        if (assignMidi.isVisible()) { assignMidi.setBounds(area.removeFromBottom(30).reduced(6, 2)); }
        if (bakeSettings.isVisible()) { bakeSettings.setBounds(area.removeFromBottom(30).reduced(6, 2)); }
        list.setBounds(area);
    }

    void paint(juce::Graphics& graphics) override {
        if (assets.empty() && definitions.empty()) {
            graphics.setColour(osci::Colours::text().withAlpha(0.6f));
            graphics.setFont(motion::style::body());
            graphics.drawFittedText("Drop files here", list.getBounds().reduced(12), juce::Justification::centred, 3);
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
        // A small picture of the source, like a project panel thumbnail.
        paintThumbnail(graphics, row, bounds.removeFromLeft(36).withSizeKeepingCentre(34, 34));
        bounds.removeFromLeft(8);
        graphics.setColour(osci::Colours::text());
        graphics.setFont(motion::style::body());
        graphics.drawText(getNameForRow(row), bounds.removeFromTop(20), juce::Justification::centredLeft);
        graphics.setColour(osci::Colours::text().withAlpha(0.55f));
        graphics.setFont(motion::style::caption());
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

    // A source's middle frame as a path inside the unit square, broken at the
    // jumps between strokes (dark travel in baked points, long steps in
    // vector shapes, which carry no colour).
    static juce::Path traceThumbnail(const motion::Asset& asset) {
        juce::Path path;
        const auto source = asset.source;
        const auto drawing = asset.drawing;
        const auto frames = source != nullptr ? source->frameCount() : 0;
        if (frames == 0 && (drawing == nullptr || drawing->empty())) { return path; }
        const auto frame = frames / 2;
        const auto pointFrames = frames > 0 && source->drawingAt(frame) == nullptr;
        constexpr int steps = 400;
        std::vector<juce::Point<float>> points;
        std::vector<bool> lit;
        float left = 1e9f, right = -1e9f, top = 1e9f, bottom = -1e9f;
        for (int index = 0; index <= steps; ++index) {
            const auto phase = static_cast<double>(index) / steps;
            const auto point = frames > 0 ? source->sampleFrame(frame, phase, 0) : drawing->sample(phase, 0);
            if (!std::isfinite(point.x) || !std::isfinite(point.y)) { continue; }
            points.emplace_back(point.x, point.y);
            lit.push_back(point.r > 0 || point.g > 0 || point.b > 0);
            left = std::min(left, point.x); right = std::max(right, point.x);
            top = std::min(top, point.y); bottom = std::max(bottom, point.y);
        }
        if (points.empty()) { return path; }
        const auto size = std::max({right - left, bottom - top, 1e-6f});
        const auto jump = size * .2f;
        const auto map = [&](juce::Point<float> point) {
            return juce::Point<float>(.5f + (point.x - (left + right) * .5f) / size, .5f - (point.y - (top + bottom) * .5f) / size);
        };
        bool open = false;
        for (std::size_t index = 0; index < points.size(); ++index) {
            const auto dark = pointFrames && !lit[index];
            const auto jumped = index > 0 && points[index].getDistanceFrom(points[index - 1]) > jump;
            if (dark) { open = false; continue; }
            if (open && !jumped) { path.lineTo(map(points[index])); } else { path.startNewSubPath(map(points[index])); open = true; }
        }
        return path;
    }
    // Keyed by the prepared data itself, so a replaced source re-traces.
    struct Thumbnail {
        std::shared_ptr<const motion::PreparedSource> source;
        std::shared_ptr<const osci::PreparedDrawing> drawing;
        juce::Path path;
    };
    mutable std::vector<Thumbnail> thumbnails;

    void paintThumbnail(juce::Graphics& graphics, int row, juce::Rectangle<int> box) const {
        graphics.setColour(juce::Colours::black.withAlpha(.35f));
        graphics.fillRoundedRectangle(box.toFloat(), 3.0f);
        const auto area = box.toFloat().reduced(4);
        graphics.setColour(motion::style::key().withAlpha(.85f));
        if (definitionRow(row)) {
            // A composition: stacked layers.
            for (int layer = 0; layer < 3; ++layer) {
                graphics.drawRoundedRectangle(area.withHeight(area.getHeight() * .3f).translated(0, area.getHeight() * .35f * static_cast<float>(layer)), 1.5f, 1.0f);
            }
            return;
        }
        const auto& asset = *assets[static_cast<std::size_t>(row)];
        if (asset.audio != nullptr) {
            graphics.setColour(juce::Colour(0xff97c7df).withAlpha(.6f));
            const auto columns = static_cast<int>(area.getWidth());
            for (int x = 0; x < columns; ++x) {
                const auto from = asset.audio->duration() * x / columns, to = asset.audio->duration() * (x + 1) / columns;
                const auto peak = asset.audio->querySeconds(0, from, to);
                const auto high = std::clamp(peak.maximum, -1.0f, 1.0f), low = std::clamp(peak.minimum, -1.0f, 1.0f);
                graphics.drawVerticalLine(juce::roundToInt(area.getX()) + x, area.getCentreY() - high * area.getHeight() * .4f, area.getCentreY() - low * area.getHeight() * .4f + 1);
            }
            return;
        }
        if (asset.midi != nullptr) {
            // Note bars over the file's pitch range and length.
            const auto& notes = asset.midi->notes();
            if (notes.empty()) { return; }
            int low = 127, high = 0;
            double end = 0;
            for (const auto& note : notes) { low = std::min(low, note.pitch); high = std::max(high, note.pitch); end = std::max(end, note.start + note.duration); }
            if (!(end > 0)) { return; }
            const auto rows = static_cast<float>(std::max(1, high - low + 1));
            for (const auto& note : notes) {
                const auto x = area.getX() + static_cast<float>(note.start / end) * area.getWidth();
                const auto y = area.getBottom() - (static_cast<float>(note.pitch - low) + 1) / rows * area.getHeight();
                graphics.fillRect(x, y, std::max(1.0f, static_cast<float>(note.duration / end) * area.getWidth()), std::max(1.0f, area.getHeight() / rows));
            }
            return;
        }
        // Traced once per source (a unit-square path), then scaled to the row.
        auto found = std::find_if(thumbnails.begin(), thumbnails.end(), [&](const Thumbnail& thumbnail) { return thumbnail.source == asset.source && thumbnail.drawing == asset.drawing; });
        if (found == thumbnails.end()) {
            thumbnails.push_back({asset.source, asset.drawing, traceThumbnail(asset)});
            found = std::prev(thumbnails.end());
        }
        auto path = found->path;
        path.applyTransform(juce::AffineTransform::scale(area.getWidth(), area.getHeight()).translated(area.getX(), area.getY()));
        graphics.strokePath(path, juce::PathStrokeType(1.0f));
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
        menu.setLookAndFeel(&getLookAndFeel());
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
        menu.addItem(1, "Insert at playhead");
        menu.addItem(2, "Rename...");
        const auto& drawn = *assets[static_cast<std::size_t>(row)];
        if (drawn.extension.equalsIgnoreCase(".svg") && motion::drawing::isDrawing(juce::String::fromUTF8(static_cast<const char*>(drawn.data.getData()), static_cast<int>(drawn.data.getSize())))) {
            menu.addItem(8, "Edit drawing...");
        }
        menu.addItem(6, "Replace with file...");
        menu.addItem(3, uses == 0 ? "Not used by any clip" : "Select " + juce::String(static_cast<int>(uses)) + (uses == 1 ? " clip using it" : " clips using it"), uses != 0);
        const auto& source = *assets[static_cast<std::size_t>(row)];
        if (source.midi != nullptr) {
            const auto changes = source.midiTempoChanges != nullptr ? static_cast<int>(source.midiTempoChanges->size()) : 0;
            menu.addItem(7, "Use this file's tempo (" + juce::String(source.midiSuggestedBpm, source.midiSuggestedBpm == std::round(source.midiSuggestedBpm) ? 0 : 2) + " BPM"
                + (changes > 0 ? ", " + juce::String(changes) + (changes == 1 ? " change)" : " changes)") : ")"));
        }
        menu.addSeparator();
        menu.addItem(4, uses == 0 ? "Remove source" : "Remove source (in use)", uses == 0);
        menu.addItem(5, "Remove all unused sources");
        const juce::Component::SafePointer<MotionAssetLibrary> owner(this);
        menu.setLookAndFeel(&getLookAndFeel());
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this).withMousePosition(), [owner, id, generation](int result) {
            if (owner == nullptr || result == 0 || owner->document.generation() != generation) { return; }
            if (result == 1 && owner->onInsert) { owner->onInsert(id); }
            if (result == 2) { owner->beginRename(id); }
            if (result == 6 && owner->onReplace) { owner->onReplace(id); }
            if (result == 8 && owner->onEditDrawing) { owner->onEditDrawing(id); }
            if (result == 3 && owner->onSelectUses) { owner->onSelectUses(id); }
            if (result == 7) { owner->adoptMidiTempo(id); }
            if (result == 4 || result == 5) {
                int removed = 0;
                const auto outcome = owner->document.removeUnusedAssets(result == 4 ? std::vector<motion::Id>{id} : std::vector<motion::Id>{}, removed);
                if (owner->onMessage) {
                    owner->onMessage(outcome.failed() ? outcome.getErrorMessage() : "Removed " + juce::String(removed) + (removed == 1 ? " unused source." : " unused sources."));
                }
            }
        });
    }
    // Replace the project's tempo map with a MIDI file's, in one undo step.
    void adoptMidiTempo(motion::Id id) {
        const auto found = std::find_if(assets.begin(), assets.end(), [id](const auto& item) { return item != nullptr && item->id == id; });
        if (found == assets.end() || (*found)->midi == nullptr) { return; }
        const auto bars = document.project().timeDisplay == motion::TimeDisplay::beats;
        const auto result = document.setTempoMap((*found)->midiSuggestedBpm, (*found)->midiTempoChanges, "Use MIDI tempo", true);
        if (onMessage) { onMessage(result.failed() ? result.getErrorMessage() : "Tempo now follows " + (*found)->name + (bars ? "." : "; the ruler shows bars and beats.")); }
    }
    // Rename by identity: the list may have changed while the menu was open.
    void beginRename(motion::Id id) {
        int row = -1;
        for (int candidate = 0; candidate < static_cast<int>(assets.size()); ++candidate) {
            if (assetId(candidate) == id) { row = candidate; }
        }
        if (!validAssetRow(row)) { return; }
        renaming = id;
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
