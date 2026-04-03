#include "RandomComponent.h"
#include "../effects/EffectComponent.h"
#include "../../PluginProcessor.h"
#include "../../LookAndFeel.h"

// ============================================================================
// Colours
// ============================================================================

juce::Colour RandomComponent::getRandomColour(int randomIndex) {
    static const juce::Colour colours[] = {
        juce::Colour(0xFF50FA7B), // 0 Green
        juce::Colour(0xFFFFB86C), // 1 Orange
        juce::Colour(0xFFBD93F9), // 2 Purple
    };
    return colours[juce::jlimit(0, (int)std::size(colours) - 1, randomIndex)];
}

// ============================================================================
// Config builders
// ============================================================================

static ModulationSourceConfig buildRandomConfig(OscirenderAudioProcessor& proc) {
    ModulationSourceConfig cfg;
    cfg.sourceCount = NUM_RANDOM_SOURCES;
    cfg.dragPrefix = "RNG";
    cfg.getLabel = [](int i) { return "RAND " + juce::String(i + 1); };
    cfg.getSourceColour = &RandomComponent::getRandomColour;
    cfg.getCurrentValue = [&proc](int i) { return proc.randomParameters.getCurrentValue(i); };
    cfg.isSourceActive = [&proc](int i) { return proc.randomParameters.isActive(i); };
    cfg.getAssignments = [&proc]() { return proc.randomParameters.getAssignments(); };
    cfg.addAssignment = [&proc](const ModAssignment& a) { proc.randomParameters.addAssignment(a); };
    cfg.removeAssignment = [&proc](int idx, const juce::String& pid) { proc.randomParameters.removeAssignment(idx, pid); };
    cfg.getParamDisplayName = [&proc](const juce::String& pid) -> juce::String {
        return proc.getParamDisplayName(pid);
    };
    cfg.broadcaster = &proc.broadcaster;
    cfg.getActiveTab = [&proc]() { return proc.randomParameters.activeTab; };
    cfg.setActiveTab = [&proc](int i) { proc.randomParameters.activeTab = i; };
    return cfg;
}

static ModulationRateConfig buildRandomRateConfig(OscirenderAudioProcessor& proc) {
    ModulationRateConfig cfg;
    cfg.getRateParam = [&proc](int i) -> osci::FloatParameter* { return proc.randomParameters.rate[i]; };
    cfg.getRateMode = [&proc](int i) { return proc.randomParameters.getRateMode(i); };
    cfg.setRateMode = [&proc](int i, LfoRateMode m) { proc.randomParameters.setRateMode(i, m); };
    cfg.getTempoDivision = [&proc](int i) { return proc.randomParameters.getTempoDivision(i); };
    cfg.setTempoDivision = [&proc](int i, int d) { proc.randomParameters.setTempoDivision(i, d); };
    cfg.getCurrentBpm = [&proc]() { return proc.currentBpm.load(std::memory_order_relaxed); };
    cfg.maxIndex = NUM_RANDOM_SOURCES;
    return cfg;
}

static ModulationModeConfig buildRandomStyleConfig(OscirenderAudioProcessor& proc) {
    ModulationModeConfig cfg;
    cfg.modes = {
        { static_cast<int>(RandomStyle::Perlin),           "Perlin" },
        { static_cast<int>(RandomStyle::SampleAndHold),    "Sample & Hold" },
        { static_cast<int>(RandomStyle::SineInterpolate),  "Sine Interpolate" },
    };
    cfg.getMode = [&proc](int i) { return static_cast<int>(proc.randomParameters.getStyle(i)); };
    cfg.setMode = [&proc](int i, int m) { proc.randomParameters.setStyle(i, static_cast<RandomStyle>(m)); };
    cfg.labelText = "STYLE";
    cfg.maxIndex = NUM_RANDOM_SOURCES;
    return cfg;
}

// ============================================================================
// RandomComponent
// ============================================================================

RandomComponent::RandomComponent(OscirenderAudioProcessor& processor)
    : ModulationSourceComponent(buildRandomConfig(processor)),
      audioProcessor(processor),
      rateControl(buildRandomRateConfig(processor), 0),
      styleControl(buildRandomStyleConfig(processor), 0) {

    // Setup graph
    graph.setCornerRadius(6.0f);
    addAndMakeVisible(graph);

    // Rate control
    rateControl.setSourceIndex(getActiveSourceIndex());
    addAndMakeVisible(rateControl);

    // Style control
    styleControl.setSourceIndex(getActiveSourceIndex());
    addAndMakeVisible(styleControl);

    // Register as listener on all Random parameters so undo/redo triggers a UI sync
    for (int i = 0; i < NUM_RANDOM_SOURCES; ++i) {
        paramSync.track(audioProcessor.randomParameters.rate[i]);
        paramSync.track(audioProcessor.randomParameters.style[i]);
        paramSync.track(audioProcessor.randomParameters.rateMode[i]);
        paramSync.track(audioProcessor.randomParameters.tempoDivision[i]);
    }

    // Restore state
    syncFromProcessorState();
}

RandomComponent::~RandomComponent() {
}

void RandomComponent::timerCallback() {
    ModulationSourceComponent::timerCallback();

    int idx = getActiveSourceIndex();

    // Drain all subsampled values from the audio thread ring buffer.
    RandomUIRingBuffer::Entry entries[256];
    int count = audioProcessor.randomParameters.drainUIBuffer(idx, entries, 256);

    if (count > 0) {
        for (int i = 0; i < count; ++i)
            graph.pushValueSilent(entries[i].value, entries[i].active);
        graph.repaint();
    } else {
        // No audio data available — push current snapshot to keep scrolling.
        bool isActive = audioProcessor.randomParameters.isActive(idx);
        float value = audioProcessor.randomParameters.getCurrentValue(idx);
        graph.pushValue(value, isActive);
    }

    wasActive = audioProcessor.randomParameters.isActive(idx);
}

void RandomComponent::resized() {
    ModulationSourceComponent::resized();

    auto bounds = getContentBounds();

    // Bottom row: style + rate controls
    auto bottomRow = bounds.removeFromBottom(kRateHeight);
    bounds.removeFromBottom(kRateGap);

    // Layout style and rate side by side
    int availW = bottomRow.getWidth() - kRateGap;
    int styleW = juce::jmin(kMaxStyleWidth, availW / 2);
    int rateW = juce::jmin(kMaxRateWidth, availW / 2);
    int totalW = styleW + rateW + kRateGap;
    int leftPad = (bottomRow.getWidth() - totalW) / 2;
    if (leftPad > 0) bottomRow.removeFromLeft(leftPad);
    styleControl.setBounds(bottomRow.removeFromLeft(styleW));
    bottomRow.removeFromLeft(kRateGap);
    rateControl.setBounds(bottomRow.removeFromLeft(rateW));

    graph.setBounds(bounds);
    setOutlineBounds(graph.getBounds());
}

void RandomComponent::onActiveSourceChanged(int index) {
    syncGraphColours();
    graph.clearHistory();
    rateControl.setSourceIndex(index);
    rateControl.syncFromProcessor();
    styleControl.setSourceIndex(index);
    styleControl.syncFromProcessor();
}

void RandomComponent::syncGraphColours() {
    int idx = getActiveSourceIndex();
    auto colour = getRandomColour(idx);
    graph.setAccentColour(colour);
}

void RandomComponent::syncFromProcessorState() {
    ModulationSourceComponent::syncFromProcessorState();

    syncGraphColours();

    rateControl.setSourceIndex(getActiveSourceIndex());
    rateControl.syncFromProcessor();
    styleControl.setSourceIndex(getActiveSourceIndex());
    styleControl.syncFromProcessor();
}
