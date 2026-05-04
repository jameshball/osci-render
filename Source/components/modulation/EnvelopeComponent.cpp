// $Id$
// $HeadURL$

/*
 ==============================================================================
 
 This file is part of the UGEN++ library
 Copyright 2008-11 The University of the West of England.
 by Martin Robinson
 
 ------------------------------------------------------------------------------
 
 UGEN++ can be redistributed and/or modified under the terms of the
 GNU General Public License, as published by the Free Software Foundation;
 either version 2 of the License, or (at your option) any later version.
 
 UGEN++ is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.
 
 You should have received a copy of the GNU General Public License
 along with UGEN++; if not, visit www.gnu.org/licenses or write to the
 Free Software Foundation, Inc., 59 Temple Place, Suite 330,
 Boston, MA 02111-1307 USA
 
 The idea for this project and code in the UGen implementations is
 derived from SuperCollider which is also released under the 
 GNU General Public License:
 
 SuperCollider real time audio synthesis system
 Copyright (c) 2002 James McCartney. All rights reserved.
 http://www.audiosynth.com
 
 ==============================================================================
 */

#ifndef UGEN_NOEXTGPL

#include "EnvelopeComponent.h"
#include "../effects/EffectComponent.h"
#include "../../PluginProcessor.h"
#include "../../LookAndFeel.h"

#include <algorithm>
#include <cmath>
#include <limits>

static void updateIfApproxEqual(osci::FloatParameter* parameter, float newValue)
{
	if (parameter == nullptr)
		return;
	if (std::abs(parameter->getValueUnnormalised() - newValue) > 0.000001f)
		parameter->setUnnormalisedValueNotifyingHost(newValue);
}

namespace
{
constexpr int kDahdsrHandles = 6;

constexpr std::array<double, kDahdsrHandles> kLockedValues = {
	0.0,
	0.0,
	1.0,
	1.0,
	std::numeric_limits<double>::quiet_NaN(),
	0.0
};

template <typename Params, typename Fn>
void withParamAtIndex(const Params& params, int index, Fn&& fn)
{
	if (index < 0 || index >= (int)params.size())
		return;
	if (auto* param = params[(size_t)index])
		fn(param);
}
} // anonymous namespace

// ============================================================================
// EnvelopeComponent
// ============================================================================

juce::Colour EnvelopeComponent::getEnvColour(int envIndex) {
    static const juce::Colour colours[] = {
        juce::Colour(0xFFFF6E4A), // 0 Warm Red-Orange
        juce::Colour(0xFF4ECDC4), // 1 Teal
        juce::Colour(0xFFFFD93D), // 2 Gold
        juce::Colour(0xFFA78BFA), // 3 Lavender
        juce::Colour(0xFFFF79C6), // 4 Rose Pink
    };
    return colours[juce::jlimit(0, (int)std::size(colours) - 1, envIndex)];
}

static ModulationSourceConfig buildEnvConfig(OscirenderAudioProcessor& proc) {
    ModulationSourceConfig cfg;
    cfg.sourceCount = NUM_ACTIVE_ENVELOPES;
    cfg.getSourceColour = &EnvelopeComponent::getEnvColour;
    cfg.getCurrentValue = [&proc](int i) { return proc.envelopeParameters.getCurrentValue(i); };
    cfg.isSourceActive = [&proc](int i) { return proc.envelopeParameters.isActive(i); };
    cfg.getLabel = [](int i) { return "ENV " + juce::String(i + 1); };

    cfg.dragPrefix = "ENV";
    cfg.getAssignments = [&proc]() { return proc.envelopeParameters.getAssignments(); };
    cfg.addAssignment = [&proc](const ModAssignment& a) { proc.envelopeParameters.addAssignment(a); };
    cfg.removeAssignment = [&proc](int idx, const juce::String& pid) { proc.envelopeParameters.removeAssignment(idx, pid); };
    cfg.getParamDisplayName = [&proc](const juce::String& pid) -> juce::String {
        return proc.getParamDisplayName(pid);
    };
    cfg.broadcaster = &proc.broadcaster;
    cfg.getActiveTab = [&proc]() { return proc.envelopeParameters.activeTab; };
    cfg.setActiveTab = [&proc](int i) { proc.envelopeParameters.activeTab = i; };
#if OSCI_PREMIUM
    cfg.typeId = "env";
    cfg.midiCCManager = &proc.midiCCManager;
    cfg.buildModDepthCustomId = [](int idx, const juce::String& pid) {
        return OscirenderAudioProcessor::modDepthCustomId("env", idx, pid);
    };
    cfg.buildModDepthSetter = [&proc](int idx, const juce::String& pid) {
        return proc.buildModDepthSetter("env", idx, pid);
    };
#endif
    return cfg;
}

EnvelopeComponent::EnvelopeComponent(OscirenderAudioProcessor& processorToUse)
    : ModulationSourceComponent(buildEnvConfig(processorToUse)),
      audioProcessor(processorToUse)
{
    for (int i = 0; i < NUM_ENVELOPES; ++i)
        initEnvelopeData(i);

    addAndMakeVisible(graph);
    setupDahdsrConstraints();

    // Add knobs (premium only — free version uses graph-only editing)
#if OSCI_PREMIUM
    addAndMakeVisible(delayKnob);
    addAndMakeVisible(attackKnob);
    addAndMakeVisible(holdKnob);
    addAndMakeVisible(decayKnob);
    addAndMakeVisible(sustainKnob);
    addAndMakeVisible(releaseKnob);
#endif

    syncFromProcessorState();
}

void EnvelopeComponent::setupDahdsrConstraints() {
    graph.setNodeLimits(kDahdsrNumHandles, kDahdsrNumHandles);
    graph.setAllowNodeAddRemove(false);
    graph.setWheelMode(NodeGraphComponent::WheelMode::DomainZoom);
    graph.setDomainZoomLimits(osci_audio::kEnvelopeZoomMinSeconds,
                              osci_audio::kEnvelopeZoomMaxSeconds);
    graph.setDomainRange(0.0, 1.3);
    graph.setSnapDivisions(0);
    graph.setGridStyle(NodeGraphComponent::GridStyle::LogarithmicTime);
    graph.setSnapStyle(NodeGraphComponent::SnapStyle::LogarithmicTime);
    graph.setCornerRadius(6.0f);

    graph.isHandleVisible = [](int nodeIndex) { return nodeIndex > 0; };

    graph.hasCurveHandle = [](int nodeIndex) {
        return nodeIndex == 2 || nodeIndex == 4 || nodeIndex == 5;
    };

    graph.constrainNode = [](int nodeIndex, double& time, double& value) {
        if (nodeIndex == 0) { time = 0.0; value = 0.0; return; }
        if (nodeIndex >= 0 && nodeIndex < kDahdsrHandles) {
            double locked = kLockedValues[(size_t)nodeIndex];
            if (std::isfinite(locked))
                value = locked;
        }
    };

    graph.onDragStarted = [this]() { beginGestureForDrag(); };
    graph.onDragEnded   = [this]() { endGestureForDrag(); };
    graph.onNodesChanged = [this]() { onGraphNodesChanged(); };

    syncGraphToActiveEnv();
    syncGraphColours();
}

std::vector<GraphNode> EnvelopeComponent::buildDahdsrNodes(const DahdsrParams& p) const {
    const double delay   = juce::jmax(0.0, p.delaySeconds);
    const double attack  = juce::jmax(0.0, p.attackSeconds);
    const double hold    = juce::jmax(0.0, p.holdSeconds);
    const double decay   = juce::jmax(0.0, p.decaySeconds);
    const double release = juce::jmax(0.0, p.releaseSeconds);
    const double sustain = juce::jlimit(0.0, 1.0, p.sustainLevel);

    std::vector<GraphNode> nodes(kDahdsrNumHandles);
    nodes[0] = { 0.0,                                   0.0,     0.0f };
    nodes[1] = { delay,                                  0.0,     0.0f };
    nodes[2] = { delay + attack,                         1.0,     p.attackCurve };
    nodes[3] = { delay + attack + hold,                  1.0,     0.0f };
    nodes[4] = { delay + attack + hold + decay,          sustain, p.decayCurve };
    nodes[5] = { delay + attack + hold + decay + release, 0.0,    p.releaseCurve };
    return nodes;
}

void EnvelopeComponent::initEnvelopeData(int envIndex) {
    envData[envIndex].nodes = buildDahdsrNodes(audioProcessor.getCurrentDahdsrParams(envIndex));
}

void EnvelopeComponent::onActiveSourceChanged(int) {
    syncGraphToActiveEnv();
    syncKnobsToActiveEnv();
    graph.resetFlowTrail();
}

// --- Domain / Value ---

void EnvelopeComponent::setDomainRange(double min, double max) {
    graph.setDomainRange(min, max);
}

void EnvelopeComponent::setValueRange(double min, double max) {
    graph.setValueRange(min, max);
}

// --- Grid ---

void EnvelopeComponent::setGrid(GridMode display, GridMode quantise,
                                 double /*domain*/, double /*value*/) {
    juce::ignoreUnused(quantise);
    if (display != GridLeaveUnchanged && display != gridDisplayMode) {
        gridDisplayMode = display;
        graph.setGridDisplay((display & GridDomain) != 0, (display & GridValue) != 0);
    }
}

// --- DAHDSR params ---

void EnvelopeComponent::setDahdsrParams(int envIndex, const DahdsrParams& params) {
    envData[envIndex].nodes = buildDahdsrNodes(params);
    if (envIndex == getActiveSourceIndex())
        graph.setNodes(envData[envIndex].nodes);
}

DahdsrParams EnvelopeComponent::getDahdsrParams(int envIndex) const {
    DahdsrParams p;
    const auto& nodes = envData[envIndex].nodes;
    if ((int)nodes.size() != kDahdsrNumHandles) return p;

    p.delaySeconds   = juce::jmax(0.0, nodes[1].time - nodes[0].time);
    p.attackSeconds  = juce::jmax(0.0, nodes[2].time - nodes[1].time);
    p.holdSeconds    = juce::jmax(0.0, nodes[3].time - nodes[2].time);
    p.decaySeconds   = juce::jmax(0.0, nodes[4].time - nodes[3].time);
    p.releaseSeconds = juce::jmax(0.0, nodes[5].time - nodes[4].time);
    p.sustainLevel   = juce::jlimit(0.0, 1.0, nodes[4].value);
    p.attackCurve    = nodes[2].curve;
    p.decayCurve     = nodes[4].curve;
    p.releaseCurve   = nodes[5].curve;
    return p;
}

// --- Nodes changed -> push to host ---

void EnvelopeComponent::onGraphNodesChanged() {
    syncActiveEnvFromGraph();
    pushDahdsrToHost();
    updateKnobValues();
}

void EnvelopeComponent::updateKnobValues() {
    int idx = getActiveSourceIndex();
    const auto& ep = audioProcessor.envelopeParameters.params[idx];
    delayKnob.getKnob().setValue(ep.delayTime->getValueUnnormalised(), juce::dontSendNotification);
    attackKnob.getKnob().setValue(ep.attackTime->getValueUnnormalised(), juce::dontSendNotification);
    holdKnob.getKnob().setValue(ep.holdTime->getValueUnnormalised(), juce::dontSendNotification);
    decayKnob.getKnob().setValue(ep.decayTime->getValueUnnormalised(), juce::dontSendNotification);
    sustainKnob.getKnob().setValue(ep.sustainLevel->getValueUnnormalised(), juce::dontSendNotification);
    releaseKnob.getKnob().setValue(ep.releaseTime->getValueUnnormalised(), juce::dontSendNotification);
}

std::array<osci::FloatParameter*, EnvelopeComponent::kDahdsrNumHandles>
EnvelopeComponent::getTimeParams(int envIndex) const {
    const auto& ep = audioProcessor.envelopeParameters.params[envIndex];
    return { nullptr, ep.delayTime, ep.attackTime, ep.holdTime, ep.decayTime, ep.releaseTime };
}

std::array<osci::FloatParameter*, EnvelopeComponent::kDahdsrNumHandles>
EnvelopeComponent::getValueParams(int envIndex) const {
    const auto& ep = audioProcessor.envelopeParameters.params[envIndex];
    return { nullptr, nullptr, nullptr, nullptr, ep.sustainLevel, nullptr };
}

std::array<osci::FloatParameter*, EnvelopeComponent::kDahdsrNumHandles>
EnvelopeComponent::getCurveParams(int envIndex) const {
    const auto& ep = audioProcessor.envelopeParameters.params[envIndex];
    return { nullptr, nullptr, ep.attackShape, nullptr, ep.decayShape, ep.releaseShape };
}

void EnvelopeComponent::pushDahdsrToHost() {
    int curveIdx = graph.getActiveCurveNodeIndex();
    int nodeIdx  = graph.getActiveNodeIndex();
    int idx = getActiveSourceIndex();

    const auto params = getDahdsrParams(idx);
    const std::array<float, kDahdsrNumHandles> timeValues = {
        0.0f,
        (float)params.delaySeconds,
        (float)params.attackSeconds,
        (float)params.holdSeconds,
        (float)params.decaySeconds,
        (float)params.releaseSeconds
    };
    const std::array<float, kDahdsrNumHandles> valueValues = {
        0.0f, 0.0f, 0.0f, 0.0f,
        (float)params.sustainLevel,
        0.0f
    };
    const std::array<float, kDahdsrNumHandles> curveValues = {
        0.0f, 0.0f,
        params.attackCurve,
        0.0f,
        params.decayCurve,
        params.releaseCurve
    };

    auto tp = getTimeParams(idx);
    auto vp = getValueParams(idx);
    auto cp = getCurveParams(idx);

    if (curveIdx >= 1) {
        withParamAtIndex(cp, curveIdx, [&](auto* param) {
            updateIfApproxEqual(param, curveValues[(size_t)curveIdx]);
        });
        return;
    }

    if (nodeIdx >= 0) {
        withParamAtIndex(tp, nodeIdx, [&](auto* param) {
            updateIfApproxEqual(param, timeValues[(size_t)nodeIdx]);
        });
        withParamAtIndex(vp, nodeIdx, [&](auto* param) {
            updateIfApproxEqual(param, valueValues[(size_t)nodeIdx]);
        });
    }
}

// --- DAW gestures ---

void EnvelopeComponent::beginGestureForDrag() {
    int curveIdx = graph.getActiveCurveNodeIndex();
    int idx = getActiveSourceIndex();
    auto cp = getCurveParams(idx);
    if (curveIdx >= 1) {
        withParamAtIndex(cp, curveIdx, [](auto* p) { p->beginChangeGesture(); });
        return;
    }
    int nodeIdx = graph.getActiveNodeIndex();
    auto tp = getTimeParams(idx);
    auto vp = getValueParams(idx);
    if (nodeIdx >= 0) {
        withParamAtIndex(tp, nodeIdx, [](auto* p) { p->beginChangeGesture(); });
        withParamAtIndex(vp, nodeIdx, [](auto* p) { p->beginChangeGesture(); });
    }
}

void EnvelopeComponent::endGestureForDrag() {
    int curveIdx = graph.getActiveCurveNodeIndex();
    int idx = getActiveSourceIndex();
    auto cp = getCurveParams(idx);
    if (curveIdx >= 1) {
        withParamAtIndex(cp, curveIdx, [](auto* p) { p->endChangeGesture(); });
        return;
    }
    int nodeIdx = graph.getActiveNodeIndex();
    auto tp = getTimeParams(idx);
    auto vp = getValueParams(idx);
    if (nodeIdx >= 0) {
        withParamAtIndex(tp, nodeIdx, [](auto* p) { p->endChangeGesture(); });
        withParamAtIndex(vp, nodeIdx, [](auto* p) { p->endChangeGesture(); });
    }
}

// --- Flow markers ---

void EnvelopeComponent::setFlowMarkerTimesForUi(const double* timesSeconds, int numTimes) {
    graph.setFlowMarkerDomainPositions(timesSeconds, numTimes);
}

void EnvelopeComponent::clearFlowMarkerTimesForUi() {
    graph.clearFlowMarkers();
}

void EnvelopeComponent::resetFlowPersistenceForUi() {
    graph.resetFlowTrail();
}

// --- Switching ---

void EnvelopeComponent::syncGraphToActiveEnv() {
    int idx = getActiveSourceIndex();
    graph.setNodes(envData[idx].nodes);

    auto colour = getEnvColour(idx);
    graph.setColour(NodeGraphComponent::lineColourId, colour);
    graph.setColour(NodeGraphComponent::fillColourId, colour.withAlpha(0.15f));
    graph.setColour(NodeGraphComponent::nodeColourId, colour);
}

void EnvelopeComponent::syncActiveEnvFromGraph() {
    envData[getActiveSourceIndex()].nodes = graph.getNodes();
}

void EnvelopeComponent::syncFromProcessorState() {
    for (int i = 0; i < NUM_ENVELOPES; ++i)
        initEnvelopeData(i);

    ModulationSourceComponent::syncFromProcessorState();

    syncKnobsToActiveEnv();
    syncGraphToActiveEnv();
    repaint();
}

void EnvelopeComponent::bindKnobToParam(KnobContainerComponent& container, osci::FloatParameter* param,
                                          double skewCentre, const juce::String& suffix) {
    container.bindToParam(param, skewCentre, 3);
    auto& knob = container.getKnob();
    if (suffix.isNotEmpty())
        knob.setTextValueSuffix(suffix);

    auto baseChange = knob.onValueChange;
    knob.onValueChange = [this, baseChange]() {
        if (baseChange) baseChange();
        int idx = getActiveSourceIndex();
        envData[idx].nodes = buildDahdsrNodes(audioProcessor.getCurrentDahdsrParams(idx));
        graph.setNodes(envData[idx].nodes);
    };

    auto colour = getEnvColour(getActiveSourceIndex());
    knob.setAccentColour(colour);
    container.setAccentColour(colour);
}

void EnvelopeComponent::syncKnobsToActiveEnv() {
    int idx = getActiveSourceIndex();
    const auto& ep = audioProcessor.envelopeParameters.params[idx];

    bindKnobToParam(delayKnob,    ep.delayTime,     0.5,  " s");
    bindKnobToParam(attackKnob,   ep.attackTime,     0.5,  " s");
    bindKnobToParam(holdKnob,     ep.holdTime,       0.5,  " s");
    bindKnobToParam(decayKnob,    ep.decayTime,      0.5,  " s");
    bindKnobToParam(sustainKnob,  ep.sustainLevel,   0.5,  {});
    bindKnobToParam(releaseKnob,  ep.releaseTime,    0.5,  " s");
}

// --- Layout ---

void EnvelopeComponent::resized() {
    ModulationSourceComponent::resized();
    if (isCollapsed()) return;

    auto bounds = getContentBounds();

#if OSCI_PREMIUM
    // Bottom row: DAHDSR knobs
    auto bottomRow = bounds.removeFromBottom(kKnobRowHeight);
    bounds.removeFromBottom(kKnobGap);

    {
        constexpr int numKnobs = 6;
        int gapTotal = kKnobGap * (numKnobs - 1);
        int avail = bottomRow.getWidth() - gapTotal;
        float knobW = juce::jmin((float)kMaxKnobWidth, (float)avail / (float)numKnobs);
        float totalW = knobW * numKnobs + (float)gapTotal;
        int leftPad = juce::roundToInt(((float)bottomRow.getWidth() - totalW) * 0.5f);
        if (leftPad > 0) bottomRow.removeFromLeft(leftPad);

        int startX = bottomRow.getX();
        int y = bottomRow.getY();
        int h = bottomRow.getHeight();
        float cum = 0.0f;
        int gapsSoFar = 0;

        juce::Component* knobs[] = { &delayKnob, &attackKnob, &holdKnob, &decayKnob, &sustainKnob, &releaseKnob };
        for (int i = 0; i < numKnobs; ++i) {
            int left = startX + juce::roundToInt(cum) + gapsSoFar;
            cum += knobW;
            int right = startX + juce::roundToInt(cum) + gapsSoFar;
            knobs[i]->setBounds(left, y, right - left, h);
            if (i < numKnobs - 1) gapsSoFar += kKnobGap;
        }
    }
#endif

    graph.setBounds(bounds);
    setOutlineBounds(graph.getBounds());
}

// --- Look & Feel colour sync ---

void EnvelopeComponent::syncGraphColours() {
    // Override NodeGraphComponent's constructor defaults with LookAndFeel colours
    graph.setColour(NodeGraphComponent::backgroundColourId,  getLookAndFeel().findColour(NodeGraphComponent::backgroundColourId));
    graph.setColour(NodeGraphComponent::gridLineColourId,    getLookAndFeel().findColour(NodeGraphComponent::gridLineColourId));
    graph.setColour(NodeGraphComponent::nodeOutlineColourId, getLookAndFeel().findColour(NodeGraphComponent::nodeOutlineColourId));

    auto colour = getEnvColour(getActiveSourceIndex());
    graph.setColour(NodeGraphComponent::lineColourId,        colour);
    graph.setColour(NodeGraphComponent::fillColourId,        colour.withAlpha(0.15f));
    graph.setColour(NodeGraphComponent::nodeColourId,        colour);
}

#endif // UGEN_NOEXTGPL
