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
    fades.setBackground(osci::Colours::surface());
    addAndMakeVisible(list);
    list.addMouseListener(this, true);
    status.setJustificationType(juce::Justification::topLeft);
    status.setFont(motion::style::body());
    addAndMakeVisible(status);
    cancelImport.setButtonText("Cancel import");
    cancelImport.onClick = [this] { if (onCancelImport) { onCancelImport(); } };
    addChildComponent(cancelImport);
    edit.iconSize = 18.0f;
    edit.onClick = [this] {
        const auto row = hoveredRow;
        list.selectRow(row);
        editRow(row);
    };
    // Leaving the button off its row hides it with the +.
    edit.addMouseListener(this, false);
    addChildComponent(edit);
    list.getVerticalScrollBar().addListener(this);
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

void MotionAssetLibrary::revealAsset(motion::Id id) {
    for (std::size_t row = 0; row < assets.size() + definitions.size(); ++row) {
        if (assetId(static_cast<int>(row)) == id) {
            list.selectRow(static_cast<int>(row), true);
            osci::revealRow(list, static_cast<int>(row));
            return;
        }
    }
}

void MotionAssetLibrary::updateStatus() {
    cancelImport.setVisible(importStatus.isNotEmpty());
    status.setColour(juce::Label::textColourId, osci::Colours::text().withAlpha(0.6f));
    const auto row = list.getSelectedRow();
    // Only say something the row does not already show.
    juce::String help;
    if (validAssetRow(row) && assets[static_cast<std::size_t>(row)]->liveIdentity != nullptr && liveStatus) { help = liveStatus(assetId(row)); }
    if (definitionRow(row) && !document.canReferenceComposition(assetId(row))) { help = "It contains the composition you're editing, so it can't go inside it."; }
    status.setText(importStatus.isNotEmpty() ? importStatus : help, juce::dontSendNotification);
    // Laid out once the event that changed it is over: resizing the list
    // rebuilds its rows, and a click on one is still being handled.
    triggerAsyncUpdate();
}

void MotionAssetLibrary::handleAsyncUpdate() {
    resized();
}

juce::String MotionAssetLibrary::getTooltipForRow(int row) {
    if (definitionRow(row) && !document.canReferenceComposition(assetId(row))) { return "Open it to edit it; it can't go inside the composition you're editing"; }
    // Over a linked source's badge: where its file is.
    if (validAssetRow(row) && assets[static_cast<std::size_t>(row)]->isLinked()) {
        const auto position = list.getRowPosition(row, true);
        const auto at = list.getMouseXYRelative() - position.getPosition();
        if (linkBadge(position.getWidth(), position.getHeight()).expanded(2).contains(at.toFloat())) {
            return "Linked from disk, not kept in the project: " + assets[static_cast<std::size_t>(row)]->linked.getFullPathName();
        }
    }
    return {};
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
    list.setBounds(area);
    placeEdit();
}

juce::String MotionAssetLibrary::editLabel(int row) const {
    if (definitionRow(row)) { return "Open composition"; }
    if (!validAssetRow(row)) { return {}; }
    const auto& asset = *assets[static_cast<std::size_t>(row)];
    const auto extension = asset.extension.toLowerCase();
    if (asset.missing) { return "Find its file..."; }
    if (motion::texture::isSource(extension)) { return motion::texture::technology() + " settings..."; }
    if (asset.liveIdentity != nullptr) { return "Blender settings..."; }
    if (extension == ".txt") { return "Edit text..."; }
    if (extension == ".lua") { return "Edit Lua..."; }
    if (extension == ".lsystem") { return "Fractal settings..."; }
    if (osci::files::isVideo(extension)) { return "Video settings..."; }
    if (osci::files::isImage(extension)) { return "Image settings..."; }
    if (extension == ".svg" && motion::drawing::isDrawing(asset.data.toString())) { return "Edit drawing..."; }
    if (modelRow(row)) { return "3D object settings..."; }
    return {};
}

void MotionAssetLibrary::editRow(int row) {
    if (definitionRow(row)) {
        if (onOpenComposition) { onOpenComposition(assetId(row)); }
        return;
    }
    if (!validAssetRow(row)) { return; }
    const auto& asset = *assets[static_cast<std::size_t>(row)];
    if (asset.missing) {
        if (onReplace) { onReplace(assetId(row)); }
    } else if (modelRow(row)) {
        if (onModelSettings) { onModelSettings(assetId(row), edit); }
    } else if (asset.extension.equalsIgnoreCase(".svg") && motion::drawing::isDrawing(asset.data.toString())) {
        if (onEditDrawing) { onEditDrawing(assetId(row)); }
    } else if (onBake) {
        onBake(assetId(row));
    }
}

// Over the hovered row's end, beside the +, while that row is in view.
void MotionAssetLibrary::placeEdit() {
    const auto row = hoveredRow;
    const auto label = editLabel(row);
    auto bounds = label.isEmpty() ? juce::Rectangle<int>() : list.getRowPosition(row, true).translated(list.getX(), list.getY());
    const auto inView = !bounds.isEmpty() && list.getBounds().contains(bounds.getCentre());
    edit.setVisible(inView);
    if (!inView) { return; }
    edit.setName(label);
    edit.setTitle(label);
    edit.setTooltip(label.trimCharactersAtEnd("."));
    edit.setBounds(bounds.getRight() - 4 - 64, bounds.getCentreY() - 14, 28, 28);
    edit.toFront(false);
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
    juce::ignoreUnused(row);
    list.repaint();
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
    } else if (row == hoveredRow) {
        graphics.setColour(motion::style::hoverFill());
        graphics.fillRoundedRectangle(bounds.toFloat(), motion::style::radius);
    }
    bounds.reduce(8, 3);
    // A small picture of the source, like a project panel thumbnail.
    paintThumbnail(graphics, row, thumbnailArea(width, height));
    bounds.removeFromLeft(44);
    // A file linked rather than held: a link in the picture's corner.
    if (validAssetRow(row) && assets[static_cast<std::size_t>(row)]->isLinked()) {
        const auto badge = linkBadge(width, height);
        graphics.setColour(osci::Colours::surface());
        graphics.fillEllipse(badge.expanded(1));
        motion::icons::draw(graphics, motion::icons::Icon::link, badge, osci::Colours::text().withAlpha(.8f), 12.0f);
    }
    // The edit button covers the selected row's end.
    const auto hovered = row == hoveredRow && addable(row);
    if (row == hoveredRow && editLabel(row).isNotEmpty()) { bounds.setRight(width - 72); }
    if (hovered) {
        const auto add = addArea(width, height).toFloat();
        bounds.setRight(std::min(bounds.getRight(), juce::roundToInt(add.getX()) - 4));
        // Drawn as the edit button beside it is drawn when hovered.
        graphics.setColour(juce::Colours::white.withAlpha(.08f));
        graphics.fillRoundedRectangle(add.reduced(1.0f), motion::style::radius + 1);
        motion::icons::draw(graphics, motion::icons::Icon::add, add, osci::Colours::text().withAlpha(.72f), 18.0f);
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
        // A model kept at its file's size says so (its settings say how
        // much fitting it would scale it).
        if (modelRow(row) && asset.originalSize) { detail += motion::style::dot() + "unscaled"; }
        // Animated sources say how long they run.
        if (asset.source != nullptr && (asset.source->frameCount() > 1 || asset.extension.equalsIgnoreCase(".blender-capture"))) {
            detail += motion::style::dot() + juce::String(asset.source->duration(), 1) + " s";
        }
        // Its linked file wasn't found: its edit button finds it again.
        if (asset.missing) {
            detail = "File missing";
            graphics.setColour(motion::style::error());
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

// The + under the pointer is a button: it points.
void MotionAssetLibrary::mouseMove(const juce::MouseEvent& event) {
    hover(event.getEventRelativeTo(&list).getPosition());
    auto* under = event.eventComponent;
    if (under == nullptr || !list.isParentOf(under)) { return; }
    const auto onAdd = addable(hoveredRow) && addArea(under->getWidth(), under->getHeight()).contains(event.getPosition());
    under->setMouseCursor(onAdd ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
}

void MotionAssetLibrary::hover(juce::Point<int> inList) {
    const auto row = list.getLocalBounds().contains(inList) ? list.getRowContainingPosition(inList.x, inList.y) : -1;
    if (row == hoveredRow) { return; }
    list.repaintRow(hoveredRow);
    hoveredRow = row;
    list.repaintRow(hoveredRow);
    placeEdit();
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
    // What its edit button does, as the first thing to change.
    const auto label = editLabel(row);
    if (label.isNotEmpty()) { menu.addItem(8, label); }
    menu.addItem(2, "Rename...");
    menu.addItem(6, "Replace with file...");
    menu.addItem(3, uses == 0 ? "Not used by any clip" : "Select " + juce::String(static_cast<int>(uses)) + (uses == 1 ? " clip using it" : " clips using it"), uses != 0);
    menu.addSeparator();
    menu.addItem(4, uses == 0 ? "Remove source" : "Remove source (in use)", uses == 0);
    menu.addItem(5, "Remove all unused sources");
    motion::ui::showDocumentMenu(menu, *this, document, juce::PopupMenu::Options().withTargetComponent(this).withMousePosition(), [this, id](int result) {
        if (result == 1 && onInsert) { onInsert(id); }
        if (result == 2) { beginRename(id); }
        if (result == 6 && onReplace) { onReplace(id); }
        if (result == 8) {
            for (int candidate = 0; candidate < static_cast<int>(assets.size()); ++candidate) {
                if (assetId(candidate) == id) { editRow(candidate); }
            }
        }
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
