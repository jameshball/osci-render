#pragma once

#include <JuceHeader.h>
#include <melatonin_blur/melatonin_blur.h>
#include "../../audio/ModAssignment.h"
#include "../VerticalTabListComponent.h"
#include "../ScrollFadeViewport.h"
#include "../HoverAnimationMixin.h"

class EffectComponent;

// Configuration struct for a modulation source panel.
// Provides all callbacks needed to wire a generic modulation source
// (LFO, Envelope, Random, Step Sequencer, etc.) to the processor.
struct ModulationSourceConfig {
    int sourceCount = 1;
    juce::String dragPrefix;  // "LFO", "ENV", "RNG" — used as "MOD:LFO:0" drag description

    std::function<juce::String(int)> getLabel;        // "LFO 1", "ENV 1"
    std::function<juce::Colour(int)> getSourceColour;
    std::function<float(int)> getCurrentValue;        // 0..1 output for value bar
    std::function<bool(int)> isSourceActive;          // whether the pill should be visible

    // Assignment CRUD
    std::function<std::vector<ModAssignment>()> getAssignments;
    std::function<void(const ModAssignment&)> addAssignment;
    std::function<void(int sourceIndex, const juce::String& paramId)> removeAssignment;

    // For resolving param display names in popups
    std::function<juce::String(const juce::String& paramId)> getParamDisplayName;

    // Broadcaster for state reload notifications
    juce::ChangeBroadcaster* broadcaster = nullptr;

    // Active tab persistence
    std::function<int()> getActiveTab;
    std::function<void(int)> setActiveTab;
};

// Generic modulation source panel: vertical tab/drag-handles with per-connection
// depth indicators and a value bar per source. Subclasses add their own content
// components (graph, controls, etc.) in the content area.
//
// Architecture:
//   +--------+--------------------------+
//   |  TAB   |                          |
//   |  LIST  |   CONTENT AREA           |
//   |        |   (subclass fills this)  |
//   +--------+--------------------------+
//
class ModulationSourceComponent : public juce::Component,
                                   public juce::Timer,
                                   public juce::ChangeListener {
public:
    explicit ModulationSourceComponent(const ModulationSourceConfig& config);
    ~ModulationSourceComponent() override;

    void resized() override;
    void paint(juce::Graphics& g) override;
    void paintOverChildren(juce::Graphics& g) override;
    void timerCallback() override;
    void changeListenerCallback(juce::ChangeBroadcaster* source) override;

    int getActiveSourceIndex() const { return activeSourceIndex; }

    // Callback invoked when a drag starts/ends from a tab.
    std::function<void(bool isDragging)> onDragActiveChanged;

    // Force-refresh all depth indicators from current assignments.
    void refreshAllDepthIndicators();

    // Sync UI state from processor after a state load.
    // Subclasses should override and call base.
    virtual void syncFromProcessorState();

    // Switch to a different source tab.
    void switchToSource(int index);

protected:
    // Returns the content area bounds (right of tabs, inset).
    // Valid after resized() has been called.
    juce::Rectangle<int> getContentBounds() const { return contentBounds; }

    // Sets the bounds (in local coords) used for the coloured source outline.
    // Call this from the subclass resized() with the graph component's bounds
    // so the outline wraps only the graph, not surrounding controls.
    void setOutlineBounds(juce::Rectangle<int> bounds) { outlineBounds = bounds; }

    // Access to the config for subclass use.
    const ModulationSourceConfig& getConfig() const { return config; }

    // The tab list and viewport (accessible for subclass layout if needed).
    VerticalTabListComponent tabList;
    ScrollFadeViewport tabViewport;

    // Called when active source changes. Override for subclass-specific logic
    // (e.g. updating graph, preset selector). Base implementation is empty.
    virtual void onActiveSourceChanged(int /*newIndex*/) {}

    int activeSourceIndex = 0;

private:
    ModulationSourceConfig config;

    // --- Layout constants ---
    static constexpr int kTabWidth        = 55;
    static constexpr int kTabGap          = 1;
    static constexpr int kContentInset    = 4;
    static constexpr int kSeamShadowWidth = 10;
    static constexpr int kMinTabHeight    = 40;

    juce::Rectangle<int> contentBounds;
    juce::Rectangle<int> outlineBounds;
    melatonin::DropShadow panelEdgeShadow { juce::Colours::black.withAlpha(0.5f), 5, {-2, 0}, 0 };

    // DepthIndicator – small arc knob for a single source→param connection.
    class DepthIndicator : public HoverAnimationMixin {
    public:
        DepthIndicator(ModulationSourceComponent& owner, int sourceIndex,
                       juce::String paramId, float depth, bool bipolar);
        void paint(juce::Graphics& g) override;
        void mouseDown(const juce::MouseEvent& e) override;
        void mouseDrag(const juce::MouseEvent& e) override;
        void mouseUp(const juce::MouseEvent& e) override;
        void mouseEnter(const juce::MouseEvent& e) override;
        void mouseExit(const juce::MouseEvent& e) override;
        void mouseDoubleClick(const juce::MouseEvent& e) override;

        void showRightClickMenu();
        void showValuePopup();
        void hideValuePopup();
        void startTextEdit();
        void removeAndRefresh();

        ModulationSourceComponent& owner;
        int sourceIndex;
        juce::String paramId;
        float depth;
        bool bipolar = false;
        bool hovering = false;
        bool dragging = false;
        float dragStartDepth = 0.0f;

        std::unique_ptr<juce::Label> valuePopup;
        std::unique_ptr<juce::TextEditor> inlineEditor;
    };

    // ModTabHandle – tab label + value bar + drag source + depth indicators.
    class ModTabHandle : public VerticalTabListComponent::Tab {
    public:
        ModTabHandle(const juce::String& label, int sourceIndex,
                     ModulationSourceComponent& owner);
        void paint(juce::Graphics& g) override;
        void mouseDown(const juce::MouseEvent& e) override;
        void mouseDrag(const juce::MouseEvent& e) override;
        void mouseUp(const juce::MouseEvent& e) override;
        void mouseEnter(const juce::MouseEvent& e) override;
        void mouseExit(const juce::MouseEvent& e) override;
        void resized() override;

        void refreshDepthIndicators(const std::vector<ModAssignment>& assignments);

        void setSourceValue(float value01) {
            float delta = value01 - sourceValue;
            sourceDelta = delta;
            sourceValue = value01;
            repaint();
        }

        void setSourceActive(bool active) {
            if (active != sourceActive) {
                sourceActive = active;
                repaint();
            }
        }

        juce::String label;
        int sourceIndex;
        ModulationSourceComponent& owner;
        bool isDragging = false;
        bool isHovering = false;
        float sourceValue = 0.0f;
        float sourceDelta = 0.0f;
        bool sourceActive = false;

        juce::OwnedArray<DepthIndicator> depthIndicators;

        float hoverProgress = 0.0f;
        juce::VBlankAnimatorUpdater hoverAnimUpdater { this };
        std::optional<juce::Animator> hoverAnim;
    };

    // Non-owning typed pointers into tab handles (tab list owns them).
    std::vector<ModTabHandle*> tabHandles;
};
