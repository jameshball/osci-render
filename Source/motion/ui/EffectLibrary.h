#pragma once

#include "../model/Effects.h"

class MotionEffectLibrary : public juce::Component, private juce::ListBoxModel {
public:
    MotionEffectLibrary() : list("Effect library", this) {
        setName("Effect library");
        list.setRowHeight(40);
        list.setColour(juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
        list.setOutlineThickness(0);
        addAndMakeVisible(list);
    }
    std::function<void(const std::string&)> onInsert;
    void resized() override { list.setBounds(getLocalBounds().withTrimmedBottom(60)); }
    void paint(juce::Graphics& g) override {
        g.setColour(osci::Colours::text().withAlpha(0.6f));
        g.setFont(12);
        g.drawFittedText("Drag onto a clip or track. Double-click to add to the selected effects scope.", getLocalBounds().removeFromBottom(60).reduced(8), juce::Justification::centredLeft, 3);
    }
private:
    int getNumRows() override { return static_cast<int>(motion::effectCatalog().size()); }
    juce::String getNameForRow(int row) override {
        return row >= 0 && row < getNumRows() ? juce::String(motion::effectCatalog()[static_cast<std::size_t>(row)].name) : juce::String();
    }
    void paintListBoxItem(int row, juce::Graphics& g, int width, int height, bool selected) override {
        if (row < 0 || row >= getNumRows()) { return; }
        auto bounds = juce::Rectangle<int>(0, 0, width, height).reduced(4, 2);
        g.setColour(selected ? osci::Colours::surfaceRaised().interpolatedWith(osci::Colours::accentColor(), 0.08f) : osci::Colours::veryDark());
        g.fillRoundedRectangle(bounds.toFloat(), 3);
        if (selected) {
            g.setColour(osci::Colours::accentColor().withAlpha(0.65f));
            g.fillRect(bounds.withWidth(2).reduced(0, 5));
        }
        g.setColour(osci::Colours::text());
        g.setFont(14);
        g.drawText(getNameForRow(row), bounds.reduced(10, 0), juce::Justification::centredLeft);
    }
    void listBoxItemDoubleClicked(int row, const juce::MouseEvent&) override { insert(row); }
    void returnKeyPressed(int row) override { insert(row); }
    void insert(int row) {
        if (row >= 0 && row < getNumRows() && onInsert) { onInsert(motion::effectCatalog()[static_cast<std::size_t>(row)].id); }
    }
    juce::var getDragSourceDescription(const juce::SparseSet<int>& rows) override {
        if (rows.size() == 0 || rows[0] < 0 || rows[0] >= getNumRows()) { return {}; }
        return "motion-effect:" + juce::String(motion::effectCatalog()[static_cast<std::size_t>(rows[0])].id);
    }
    juce::ListBox list;
};
