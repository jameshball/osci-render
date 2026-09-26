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
            if (validRow(row) && onBake) { onBake(assetId(row)); }
        };
        addChildComponent(bakeSettings);
        setError({});
        refresh();
    }

    std::function<void(motion::Id)> onInsert;
    std::function<void()> onCancelImport;
    std::function<void(motion::Id)> onBake;

    void setImportStatus(const juce::String& message) {
        if (importStatus == message) { return; }
        importStatus = message;
        updateStatus();
    }

    void refresh() {
        const auto selectedId = assetId(list.getSelectedRow());
        assets = document.project().assets;
        list.updateContent();
        list.deselectAllRows();
        selectAsset(selectedId);
        repaint();
    }

    void selectAsset(motion::Id id) {
        for (std::size_t row = 0; row < assets.size(); ++row) {
            if (assets[row]->id == id) {
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
        status.setText(importStatus.isNotEmpty() ? importStatus : (hasError ? errorMessage : "Double-click or press Enter to insert. Drag onto the timeline to place a copy."), juce::dontSendNotification);
        resized();
    }

    void resized() override {
        auto area = getLocalBounds();
        if (cancelImport.isVisible()) {
            cancelImport.setBounds(area.removeFromBottom(30).reduced(6, 2));
        }
        status.setBounds(area.removeFromBottom(hasError ? 110 : 68).reduced(6, 4));
        if (bakeSettings.isVisible()) { bakeSettings.setBounds(area.removeFromBottom(30).reduced(6, 2)); }
        list.setBounds(area);
    }

    void paint(juce::Graphics& graphics) override {
        if (assets.empty()) {
            graphics.setColour(osci::Colours::text().withAlpha(0.6f));
            graphics.setFont(13.0f);
            graphics.drawFittedText("Import a source to add it to your asset library.", list.getBounds().reduced(12), juce::Justification::centred, 3);
        }
    }

private:
    void selectedRowsChanged(int row) override {
        const bool raster = validRow(row) && motion::Document::isRasterSource(assets[static_cast<std::size_t>(row)]->extension);
        bakeSettings.setButtonText(raster ? "Image settings..." : "Bake settings...");
        bakeSettings.setVisible(raster || (validRow(row) && assets[static_cast<std::size_t>(row)]->extension.equalsIgnoreCase(".lua")));
        resized();
    }
    int getNumRows() override { return static_cast<int>(assets.size()); }

    juce::String getNameForRow(int row) override {
        return validRow(row) ? assets[static_cast<std::size_t>(row)]->name : juce::String();
    }

    void paintListBoxItem(int row, juce::Graphics& graphics, int width, int height, bool selected) override {
        if (!validRow(row)) {
            return;
        }
        const auto& asset = *assets[static_cast<std::size_t>(row)];
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
        graphics.drawText(asset.name, bounds.removeFromTop(20), juce::Justification::centredLeft);
        graphics.setColour(osci::Colours::text().withAlpha(0.55f));
        graphics.setFont(11.0f);
        auto detail = asset.extension.trimCharactersAtStart(".").toUpperCase();
        if (asset.source != nullptr && asset.source->frameCount() > 1) {
            detail += " | " + juce::String(asset.source->duration(), 2) + "s | " + juce::String(static_cast<int>(asset.source->frameCount())) + " frames";
        }
        graphics.drawText(detail, bounds, juce::Justification::centredLeft);
    }

    void listBoxItemDoubleClicked(int row, const juce::MouseEvent&) override { insert(row); }
    void returnKeyPressed(int row) override { insert(row); }

    juce::var getDragSourceDescription(const juce::SparseSet<int>& rows) override {
        if (rows.size() == 0 || !validRow(rows[0])) {
            return {};
        }
        return "motion-asset:" + juce::String(static_cast<juce::uint64>(assetId(rows[0])));
    }

    bool validRow(int row) const {
        return row >= 0 && static_cast<std::size_t>(row) < assets.size();
    }

    motion::Id assetId(int row) const {
        return validRow(row) ? assets[static_cast<std::size_t>(row)]->id : 0;
    }

    void insert(int row) {
        if (validRow(row) && onInsert) {
            onInsert(assetId(row));
        }
    }

    motion::Document& document;
    std::vector<std::shared_ptr<const motion::Asset>> assets;
    juce::ListBox list;
    juce::Label status;
    juce::TextButton cancelImport, bakeSettings;
    juce::String importStatus, errorMessage;
    bool hasError = false;
};
