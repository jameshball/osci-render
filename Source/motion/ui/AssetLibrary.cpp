#include "AssetLibrary.h"
#include "../../parser/FileFormatRegistry.h"
#include "SourcePreview.h"
#include "GeneratorIcons.h"
#include "SourceKind.h"
#include "../model/SourceParts.h"
#include "MenuIcons.h"

MotionAssetLibrary::MotionAssetLibrary(motion::Document& document) : document(document), list("Motion assets", this) {
    setName("Asset library");
    list.setComponentID("motion.assets");
    list.setRowHeight(46);
    list.setMultipleSelectionEnabled(false);
    list.setColour(juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
    list.setOutlineThickness(0);
    addAndMakeVisible(list);
    list.addMouseListener(this, true);
    status.setJustificationType(juce::Justification::topLeft);
    status.setFont(motion::style::body());
    addAndMakeVisible(status);
    cancelImport.setButtonText("Cancel import");
    cancelImport.onClick = [this] { if (onCancelImport) { onCancelImport(); } };
    addChildComponent(cancelImport);
    bakeSettings.setButtonText("Bake settings...");
    bakeSettings.onClick = [this] {
        const auto row = list.getSelectedRow();
        if (definitionRow(row)) {
            if (onOpenComposition) { onOpenComposition(assetId(row)); }
        } else if (validAssetRow(row) && onBake) {
            onBake(assetId(row));
        }
    };
    addChildComponent(bakeSettings);
    originalSizeLabel.setFont(motion::style::body());
    originalSizeLabel.setColour(juce::Label::textColourId, osci::Colours::text());
    originalSizeLabel.setInterceptsMouseClicks(false, false);
    originalSize.setTooltip("Keep the model at its file's own size and place, rather than fitted to the screen");
    originalSize.onClick = [this] {
        const auto row = list.getSelectedRow();
        if (modelRow(row) && onOriginalSize) { onOriginalSize(assetId(row), originalSize.getToggleState()); }
    };
    addChildComponent(originalSizeLabel);
    addChildComponent(originalSize);
    search.setName("Search sources");
    search.setTextToShowWhenEmpty("Search sources", osci::Colours::textMuted());
    search.setFont(motion::style::body());
    search.setIndents(0, 5);
    motion::style::styleField(search);
    search.onTextChange = [this] { refresh(); };
    search.onEscapeKey = [this] { search.clear(); refresh(); };
    addAndMakeVisible(search);
    rename.setName("Rename source");
    rename.setFont(motion::style::body());
    motion::style::styleField(rename);
    rename.onReturnKey = [this] { finishRename(true); };
    rename.onEscapeKey = [this] { finishRename(false); };
    rename.onFocusLost = [this] { finishRename(true); };
    addChildComponent(rename);
    updateStatus();
    refresh();
}

void MotionAssetLibrary::updateLiveStatus() {
    const auto row = list.getSelectedRow();
    if (validAssetRow(row) && assets[static_cast<std::size_t>(row)]->liveIdentity != nullptr) { updateStatus(); list.repaint(); }
}

void MotionAssetLibrary::setImportStatus(const juce::String& message) {
    if (importStatus == message) { return; }
    importStatus = message;
    updateStatus();
}

void MotionAssetLibrary::refresh() {
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
        return std::none_of(document.project().assets.begin(), document.project().assets.end(), [&](const auto& asset) { return asset != nullptr && asset->source == thumbnail.source; });
    });
    list.updateContent();
    list.deselectAllRows();
    selectAsset(selectedId);
    repaint();
}

void MotionAssetLibrary::selectAsset(motion::Id id) {
    for (std::size_t row = 0; row < assets.size() + definitions.size(); ++row) {
        if (assetId(static_cast<int>(row)) == id) {
            list.selectRow(static_cast<int>(row));
            return;
        }
    }
}

void MotionAssetLibrary::updateStatus() {
    cancelImport.setVisible(importStatus.isNotEmpty());
    status.setColour(juce::Label::textColourId, osci::Colours::text().withAlpha(0.6f));
    const auto row = list.getSelectedRow();
    // Only say something the row does not already show; the generic
    // how-to lives in the list's tooltip.
    juce::String help;
    if (validAssetRow(row) && assets[static_cast<std::size_t>(row)]->liveIdentity != nullptr && liveStatus) { help = liveStatus(assetId(row)); }
    if (definitionRow(row) && !document.canReferenceComposition(assetId(row))) { help = "It contains the composition you're editing, so it can't go inside it."; }
    status.setText(importStatus.isNotEmpty() ? importStatus : help, juce::dontSendNotification);
    resized();
}

juce::String MotionAssetLibrary::getTooltipForRow(int row) {
    if (definitionRow(row) && !document.canReferenceComposition(assetId(row))) { return "Open it to edit it; it can't go inside the composition you're editing"; }
    return "Drag into the Scene or timeline, or click + (or double-click) to add at the playhead";
}

void MotionAssetLibrary::resized() {
    auto area = getLocalBounds();
    search.setBounds(area.removeFromTop(26).reduced(4, 1));
    area.removeFromTop(4);
    if (cancelImport.isVisible()) {
        cancelImport.setBounds(area.removeFromBottom(30).reduced(6, 2));
    }
    const auto row = list.getSelectedRow();
    const bool live = validAssetRow(row) && assets[static_cast<std::size_t>(row)]->liveIdentity != nullptr;
    // The status line takes room only when it has something to say.
    const auto statusHeight = status.getText().isEmpty() && importStatus.isEmpty() ? 0 : live ? 48 : 52;
    status.setBounds(area.removeFromBottom(statusHeight).reduced(6, statusHeight > 0 ? 4 : 0));
    if (bakeSettings.isVisible()) { bakeSettings.setBounds(area.removeFromBottom(30).reduced(6, 2)); }
    if (originalSize.isVisible()) {
        auto line = area.removeFromBottom(30).reduced(10, 2);
        originalSize.setBounds(line.removeFromRight(motion::ui::Switch::width + 4));
        originalSizeLabel.setBounds(line.withTrimmedLeft(-4));
    }
    list.setBounds(area);
}

void MotionAssetLibrary::paint(juce::Graphics& graphics) {
    if (assets.empty() && definitions.empty()) {
        graphics.setColour(osci::Colours::text().withAlpha(0.6f));
        graphics.setFont(motion::style::body());
        // Empty, or nothing found: which says what to do.
        const auto filter = search.getText().trim();
        const auto message = filter.isEmpty() ? juce::String("Drop files here") : "No sources match " + juce::String::fromUTF8("\xe2\x80\x9c") + filter + juce::String::fromUTF8("\xe2\x80\x9d");
        graphics.drawFittedText(message, list.getBounds().reduced(12), juce::Justification::centred, 3);
    }
}

void MotionAssetLibrary::selectedRowsChanged(int row) {
    updateStatus();
    const bool raster = validAssetRow(row) && osci::files::isImage(assets[static_cast<std::size_t>(row)]->extension);
    const bool text = validAssetRow(row) && assets[static_cast<std::size_t>(row)]->extension.equalsIgnoreCase(".txt");
    const bool live = validAssetRow(row) && assets[static_cast<std::size_t>(row)]->liveIdentity != nullptr;
    const bool fractal = validAssetRow(row) && assets[static_cast<std::size_t>(row)]->extension.equalsIgnoreCase(".lsystem");
    const bool texture = live && motion::texture::isSource(assets[static_cast<std::size_t>(row)]->extension);
    bakeSettings.setButtonText(texture ? motion::texture::technology() + " settings..." : live ? "Blender settings..." : definitionRow(row) ? "Open composition" : text ? "Edit text..." : (validAssetRow(row) && assets[static_cast<std::size_t>(row)]->extension.equalsIgnoreCase(".lua")) ? "Edit Lua..." : (fractal ? "Fractal settings..." : (raster ? (osci::files::isVideo(assets[static_cast<std::size_t>(row)]->extension) ? "Video settings..." : "Image settings...") : "Bake settings...")));
    bakeSettings.setVisible(live || definitionRow(row) || text || fractal || raster || (validAssetRow(row) && assets[static_cast<std::size_t>(row)]->extension.equalsIgnoreCase(".lua")));
    const auto model = modelRow(row);
    originalSize.setVisible(model);
    originalSizeLabel.setVisible(model);
    if (model) { originalSize.setToggleState(assets[static_cast<std::size_t>(row)]->originalSize, juce::dontSendNotification); }
    resized();
}

// A 3D object as imported, not parts split from one (which keep their place).
bool MotionAssetLibrary::modelRow(int row) const {
    if (!validAssetRow(row)) { return false; }
    const auto& asset = *assets[static_cast<std::size_t>(row)];
    return asset.extension.equalsIgnoreCase(".obj") && !motion::parts::isLineSet(asset.data.toString());
}

juce::String MotionAssetLibrary::getNameForRow(int row) {
    return definitionRow(row) ? definitions[static_cast<std::size_t>(row) - assets.size()]->name
        : validAssetRow(row) ? assets[static_cast<std::size_t>(row)]->name : juce::String();
}

void MotionAssetLibrary::paintListBoxItem(int row, juce::Graphics& graphics, int width, int height, bool selected) {
    if (!validRow(row)) {
        return;
    }
    auto bounds = juce::Rectangle<int>(0, 0, width, height).reduced(4, 2);
    if (selected) {
        // The one selected-row look: a neutral lift and an accent bar.
        graphics.setColour(osci::Colours::surfaceRaised());
        graphics.fillRoundedRectangle(bounds.toFloat(), motion::style::radius);
        graphics.setColour(motion::style::accent());
        graphics.fillRect(bounds.withWidth(2).reduced(0, 5));
    }
    bounds.reduce(8, 3);
    // A small picture of the source, like a project panel thumbnail.
    paintThumbnail(graphics, row, bounds.removeFromLeft(36).withSizeKeepingCentre(34, 34));
    bounds.removeFromLeft(8);
    const auto hovered = row == hoveredRow && addable(row);
    if (hovered) {
        const auto add = addArea(width, height).toFloat();
        bounds.setRight(juce::roundToInt(add.getX()) - 4);
        graphics.setColour(osci::Colours::surfaceRaised().brighter(selected ? .25f : .15f));
        graphics.fillEllipse(add);
        graphics.setColour(osci::Colours::text());
        const auto centre = add.getCentre();
        graphics.drawLine(centre.x - 5.0f, centre.y, centre.x + 5.0f, centre.y, 1.5f);
        graphics.drawLine(centre.x, centre.y - 5.0f, centre.x, centre.y + 5.0f, 1.5f);
    }
    graphics.setColour(osci::Colours::text());
    graphics.setFont(motion::style::body());
    // The name as the timeline and renaming show it, file extension and all.
    graphics.drawText(getNameForRow(row), bounds.removeFromTop(20), juce::Justification::centredLeft);
    graphics.setColour(osci::Colours::text().withAlpha(0.55f));
    graphics.setFont(motion::style::caption());
    juce::String detail;
    if (definitionRow(row)) {
        const auto& definition = *definitions[static_cast<std::size_t>(row) - assets.size()];
        const auto clip = motion::Document::makeCompositionClip(0, definition, 0);
        detail = "Composition" + motion::style::dot() + juce::String(clip.duration, 1) + " s";
    } else {
        const auto& asset = *assets[static_cast<std::size_t>(row)];
        detail = motion::ui::sourceKind(asset);
        // A model says how it was sized for the screen.
        if (modelRow(row) && asset.originalSize) {
            detail += motion::style::dot() + "original size";
        } else if (modelRow(row) && std::abs(asset.fittedScale - 1) > 1.0e-3) {
            const auto scale = asset.fittedScale;
            const auto places = scale >= 10 ? 0 : scale >= 1 ? 1 : std::clamp(1 - static_cast<int>(std::floor(std::log10(scale))), 2, 6);
            detail += motion::style::dot() + "scaled " + juce::String::fromUTF8("\xc3\x97") + juce::String(scale, places);
        }
        // Animated sources say how long they run.
        if (asset.source != nullptr && (asset.source->frameCount() > 1 || asset.extension.equalsIgnoreCase(".blender-capture"))) {
            detail += motion::style::dot() + juce::String(asset.source->duration(), 1) + " s";
        }
    }
    graphics.drawText(detail, bounds, juce::Justification::centredLeft);
}

juce::Path MotionAssetLibrary::traceThumbnail(const motion::Asset& asset) {
    // A generator shows its picture, a solid turned to show its sides.
    if (const auto* generator = motion::generatorOf(asset)) { return motion::ui::generatorPicture(*generator, 1, 0); }
    // Enough samples for text's many small outlines; traced once per source.
    if (asset.source == nullptr || asset.source->frameCount() == 0) { return {}; }
    return motion::ui::traceSource(*asset.source, asset.source->frameCount() / 2, 2000);
}

void MotionAssetLibrary::paintThumbnail(juce::Graphics& graphics, int row, juce::Rectangle<int> box) const {
    motion::style::fillWell(graphics, box.toFloat());
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
        graphics.setColour(motion::style::waveform().withAlpha(.6f));
        const auto columns = static_cast<int>(area.getWidth());
        for (int x = 0; x < columns; ++x) {
            const auto from = asset.audio->duration() * x / columns, to = asset.audio->duration() * (x + 1) / columns;
            const auto peak = asset.audio->querySeconds(0, from, to);
            const auto high = std::clamp(peak.maximum, -1.0f, 1.0f), low = std::clamp(peak.minimum, -1.0f, 1.0f);
            graphics.drawVerticalLine(juce::roundToInt(area.getX()) + x, area.getCentreY() - high * area.getHeight() * .4f, area.getCentreY() - low * area.getHeight() * .4f + 1);
        }
        return;
    }
    // A live source has no frames of its own to picture: what kind it is.
    if (asset.liveIdentity != nullptr) {
        namespace icons = motion::ui::menu_icons;
        const auto glyph = motion::texture::isSource(asset.extension) ? icons::glyph::screen() : icons::glyph::live();
        graphics.setColour(motion::style::key().withAlpha(.85f));
        graphics.strokePath(glyph, juce::PathStrokeType(1.4f), glyph.getTransformToScaleToFit(area.reduced(5), true));
        return;
    }
    // Traced once per source (a unit-square path), then scaled to the row.
    auto found = std::find_if(thumbnails.begin(), thumbnails.end(), [&](const Thumbnail& thumbnail) { return thumbnail.source == asset.source; });
    if (found == thumbnails.end()) {
        thumbnails.push_back({asset.source, traceThumbnail(asset)});
        found = std::prev(thumbnails.end());
    }
    auto path = found->path;
    path.applyTransform(juce::AffineTransform::scale(area.getWidth(), area.getHeight()).translated(area.getX(), area.getY()));
    graphics.strokePath(path, juce::PathStrokeType(1.0f));
}

void MotionAssetLibrary::hover(const juce::MouseEvent& event) {
    const auto local = event.getEventRelativeTo(&list).getPosition();
    const auto inside = event.eventComponent != this || list.getBounds().contains(event.getPosition());
    const auto row = inside && list.getLocalBounds().contains(local) ? list.getRowContainingPosition(local.x, local.y) : -1;
    if (row == hoveredRow) { return; }
    list.repaintRow(hoveredRow);
    hoveredRow = row;
    list.repaintRow(hoveredRow);
}

void MotionAssetLibrary::listBoxItemClicked(int row, const juce::MouseEvent& event) {
    if (!event.mods.isPopupMenu() && addable(row) && event.eventComponent != nullptr && addArea(event.eventComponent->getWidth(), event.eventComponent->getHeight()).contains(event.getPosition())) {
        insert(row);
        return;
    }
    if (event.mods.isPopupMenu() && validAssetRow(row)) { showSourceMenu(row); return; }
    if (!event.mods.isPopupMenu() || !definitionRow(row)) { return; }
    const auto id = assetId(row);
    const auto references = document.compositionReferenceCount(id);
    const bool open = id == document.editingComposition();
    juce::PopupMenu menu;
    menu.addItem(1, "Open composition");
    menu.addItem(2, "Insert instance", document.canReferenceComposition(id));
    menu.addSeparator();
    menu.addItem(3, references != 0 ? "Remove composition (in use)" : open ? "Remove composition (open)" : "Remove unused composition", references == 0 && !open);
    motion::ui::showDocumentMenu(menu, *this, document, juce::PopupMenu::Options().withTargetComponent(this).withMousePosition(), [this, id](int result) {
        if (result == 1 && onOpenComposition) { onOpenComposition(id); }
        if (result == 2 && onInsert) { onInsert(id); }
        if (result == 3 && onRemoveComposition) { onRemoveComposition(id); }
    });
}

void MotionAssetLibrary::showSourceMenu(int row) {
    list.selectRow(row);
    const auto id = assetId(row);
    const auto uses = document.assetUses(id);
    juce::PopupMenu menu;
    menu.addItem(1, "Insert at playhead");
    menu.addItem(2, "Rename...");
    const auto& drawn = *assets[static_cast<std::size_t>(row)];
    if (drawn.extension.equalsIgnoreCase(".svg") && motion::drawing::isDrawing(drawn.data.toString())) {
        menu.addItem(8, "Edit drawing...");
    }
    menu.addItem(6, "Replace with file...");
    menu.addItem(3, uses == 0 ? "Not used by any clip" : "Select " + juce::String(static_cast<int>(uses)) + (uses == 1 ? " clip using it" : " clips using it"), uses != 0);
    menu.addSeparator();
    menu.addItem(4, uses == 0 ? "Remove source" : "Remove source (in use)", uses == 0);
    menu.addItem(5, "Remove all unused sources");
    motion::ui::showDocumentMenu(menu, *this, document, juce::PopupMenu::Options().withTargetComponent(this).withMousePosition(), [this, id](int result) {
        if (result == 1 && onInsert) { onInsert(id); }
        if (result == 2) { beginRename(id); }
        if (result == 6 && onReplace) { onReplace(id); }
        if (result == 8 && onEditDrawing) { onEditDrawing(id); }
        if (result == 3 && onSelectUses) { onSelectUses(id); }
        if (result == 4 || result == 5) {
            int removed = 0;
            const auto outcome = document.removeUnusedAssets(result == 4 ? std::vector<motion::Id>{id} : std::vector<motion::Id>{}, removed);
            if (onMessage) {
                onMessage(outcome.failed() ? outcome.getErrorMessage() : "Removed " + juce::String(removed) + (removed == 1 ? " unused source." : " unused sources."), outcome.failed());
            }
        }
    });
}

void MotionAssetLibrary::beginRename(motion::Id id) {
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

void MotionAssetLibrary::finishRename(bool accept) {
    if (!rename.isVisible()) { return; }
    rename.setVisible(false);
    const auto id = std::exchange(renaming, 0);
    if (!accept) { return; }
    const auto result = document.renameAsset(id, rename.getText());
    if (result.failed() && onMessage) { onMessage(result.getErrorMessage(), true); }
}

juce::var MotionAssetLibrary::getDragSourceDescription(const juce::SparseSet<int>& rows) {
    if (rows.size() == 0 || !validRow(rows[0])) {
        return {};
    }
    return "motion-asset:" + juce::String(static_cast<juce::uint64>(assetId(rows[0])));
}

motion::Id MotionAssetLibrary::assetId(int row) const {
    return definitionRow(row) ? definitions[static_cast<std::size_t>(row) - assets.size()]->id
        : validAssetRow(row) ? assets[static_cast<std::size_t>(row)]->id : 0;
}

void MotionAssetLibrary::insert(int row) {
    if (validRow(row) && onInsert) {
        onInsert(assetId(row));
    }
}
