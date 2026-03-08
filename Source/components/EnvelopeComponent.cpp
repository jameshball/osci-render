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
#include "../PluginProcessor.h"

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

constexpr std::array<const char*, kDahdsrHandles> kLegendLabels = {
	"",
	"Delay time (s): ",
	"Attack time (s): ",
	"Hold time (s): ",
	"Decay time (s): ",
	"Release time (s): "
};

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

EnvelopeComponent::EnvelopeComponent(OscirenderAudioProcessor& processorToUse)
	: processor(processorToUse)
{
	addAndMakeVisible(graph);

	// --- Configure NodeGraphComponent for DAHDSR-specific behaviour ---
	graph.setNodeLimits(kDahdsrNumHandles, kDahdsrNumHandles);
	graph.setAllowNodeAddRemove(false);
	graph.setWheelMode(NodeGraphComponent::WheelMode::DomainZoom);
	graph.setDomainZoomLimits(osci_audio::kEnvelopeZoomMinSeconds,
	                          osci_audio::kEnvelopeZoomMaxSeconds);
	graph.setSnapDivisions(0); // not used in logarithmic snap mode
	graph.setGridStyle(NodeGraphComponent::GridStyle::LogarithmicTime);
	graph.setSnapStyle(NodeGraphComponent::SnapStyle::LogarithmicTime);

	// Node 0 is fully locked at origin and invisible
	graph.isHandleVisible = [](int nodeIndex) { return nodeIndex > 0; };

	// Only Attack (2), Decay (4), Release (5) segments have curve handles
	graph.hasCurveHandle = [](int nodeIndex) {
		return nodeIndex == 2 || nodeIndex == 4 || nodeIndex == 5;
	};

	// Lock node 0 to origin. Lock value-constrained nodes.
	graph.constrainNode = [](int nodeIndex, double& time, double& value) {
		if (nodeIndex == 0) { time = 0.0; value = 0.0; return; }
		if (nodeIndex >= 0 && nodeIndex < kDahdsrHandles) {
			double locked = kLockedValues[(size_t)nodeIndex];
			if (std::isfinite(locked))
				value = locked;
		}
	};

	// Hover callback for legend updates
	graph.onNodeHover = [this](int nodeIndex, bool isCurve) {
		updateLegendForHover(nodeIndex, isCurve);
	};

	// DAW gesture tracking
	graph.onDragStarted = [this]() { beginGestureForDrag(); };
	graph.onDragEnded   = [this]() { endGestureForDrag(); };
	graph.onNodesChanged = [this]() { onGraphNodesChanged(); };

	// Parameter references
	timeParams  = { nullptr, processor.delayTime, processor.attackTime,
	                processor.holdTime, processor.decayTime, processor.releaseTime };
	valueParams = { nullptr, nullptr, nullptr, nullptr, processor.sustainLevel, nullptr };
	curveParams = { nullptr, nullptr, processor.attackShape, nullptr,
	                processor.decayShape, processor.releaseShape };

	// Initial state from processor
	DahdsrParams params;
	params.delaySeconds   = processor.delayTime->getValueUnnormalised();
	params.attackSeconds  = processor.attackTime->getValueUnnormalised();
	params.holdSeconds    = processor.holdTime->getValueUnnormalised();
	params.decaySeconds   = processor.decayTime->getValueUnnormalised();
	params.sustainLevel   = processor.sustainLevel->getValueUnnormalised();
	params.releaseSeconds = processor.releaseTime->getValueUnnormalised();
	params.attackCurve    = processor.attackShape->getValueUnnormalised();
	params.decayCurve     = processor.decayShape->getValueUnnormalised();
	params.releaseCurve   = processor.releaseShape->getValueUnnormalised();
	setDahdsrParams(params);
	syncGraphColours();
}

EnvelopeComponent::~EnvelopeComponent() = default;

// --- Domain / Value ---

void EnvelopeComponent::setDomainRange(double min, double max)
{
	graph.setDomainRange(min, max);
}

void EnvelopeComponent::setValueRange(double min, double max)
{
	graph.setValueRange(min, max);
}

// --- Grid ---

void EnvelopeComponent::setGrid(GridMode display, GridMode quantise,
                                 double /*domain*/, double /*value*/)
{
	juce::ignoreUnused(quantise);
	if (display != GridLeaveUnchanged && display != gridDisplayMode) {
		gridDisplayMode = display;
		graph.setGridDisplay((display & GridDomain) != 0, (display & GridValue) != 0);
	}
}

// --- DAHDSR params <-> GraphNodes ---

void EnvelopeComponent::setDahdsrParams(const DahdsrParams& params)
{
	const double delay   = juce::jmax(0.0, params.delaySeconds);
	const double attack  = juce::jmax(0.0, params.attackSeconds);
	const double hold    = juce::jmax(0.0, params.holdSeconds);
	const double decay   = juce::jmax(0.0, params.decaySeconds);
	const double release = juce::jmax(0.0, params.releaseSeconds);
	const double sustain = juce::jlimit(0.0, 1.0, params.sustainLevel);

	std::vector<GraphNode> nodes(kDahdsrNumHandles);
	nodes[0] = { 0.0,                                   0.0,     0.0f };
	nodes[1] = { delay,                                  0.0,     0.0f };
	nodes[2] = { delay + attack,                         1.0,     params.attackCurve };
	nodes[3] = { delay + attack + hold,                  1.0,     0.0f };
	nodes[4] = { delay + attack + hold + decay,          sustain, params.decayCurve };
	nodes[5] = { delay + attack + hold + decay + release, 0.0,    params.releaseCurve };

	graph.setNodes(nodes);
}

DahdsrParams EnvelopeComponent::getDahdsrParams() const
{
	DahdsrParams p;
	auto nodes = graph.getNodes();
	if ((int)nodes.size() != kDahdsrNumHandles)
		return p;

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

float EnvelopeComponent::lookup(float time) const
{
	return graph.lookup(time);
}

// --- Nodes changed -> push to host ---

void EnvelopeComponent::onGraphNodesChanged()
{
	pushDahdsrToHost();
}

void EnvelopeComponent::pushDahdsrToHost()
{
	int curveIdx = graph.getActiveCurveNodeIndex();
	int nodeIdx  = graph.getActiveNodeIndex();

	const auto params = getDahdsrParams();
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

	if (curveIdx >= 1) {
		withParamAtIndex(curveParams, curveIdx, [&](auto* param) {
			updateIfApproxEqual(param, curveValues[(size_t)curveIdx]);
		});
		return;
	}

	if (nodeIdx >= 0) {
		withParamAtIndex(timeParams, nodeIdx, [&](auto* param) {
			updateIfApproxEqual(param, timeValues[(size_t)nodeIdx]);
		});
		withParamAtIndex(valueParams, nodeIdx, [&](auto* param) {
			updateIfApproxEqual(param, valueValues[(size_t)nodeIdx]);
		});
	}
}

// --- DAW gestures ---

void EnvelopeComponent::beginGestureForDrag()
{
	int curveIdx = graph.getActiveCurveNodeIndex();
	if (curveIdx >= 1) {
		withParamAtIndex(curveParams, curveIdx, [](auto* p) { p->beginChangeGesture(); });
		return;
	}
	int nodeIdx = graph.getActiveNodeIndex();
	if (nodeIdx >= 0) {
		withParamAtIndex(timeParams, nodeIdx, [](auto* p) { p->beginChangeGesture(); });
		withParamAtIndex(valueParams, nodeIdx, [](auto* p) { p->beginChangeGesture(); });
	}
}

void EnvelopeComponent::endGestureForDrag()
{
	int curveIdx = graph.getActiveCurveNodeIndex();
	if (curveIdx >= 1) {
		withParamAtIndex(curveParams, curveIdx, [](auto* p) { p->endChangeGesture(); });
		return;
	}
	int nodeIdx = graph.getActiveNodeIndex();
	if (nodeIdx >= 0) {
		withParamAtIndex(timeParams, nodeIdx, [](auto* p) { p->endChangeGesture(); });
		withParamAtIndex(valueParams, nodeIdx, [](auto* p) { p->endChangeGesture(); });
	}
}

// --- Flow markers ---

void EnvelopeComponent::setFlowMarkerTimesForUi(const double* timesSeconds, int numTimes)
{
	graph.setFlowMarkerDomainPositions(timesSeconds, numTimes);
}

void EnvelopeComponent::clearFlowMarkerTimesForUi()
{
	graph.clearFlowMarkers();
}

void EnvelopeComponent::resetFlowPersistenceForUi()
{
	graph.resetFlowTrail();
}

// --- Layout ---

void EnvelopeComponent::resized()
{
	graph.setBounds(getLocalBounds());
}

// --- Look & Feel colour sync ---

void EnvelopeComponent::lookAndFeelChanged()
{
	syncGraphColours();
	graph.invalidateGrid();
	graph.repaint();
}

void EnvelopeComponent::syncGraphColours()
{
	graph.setColour(NodeGraphComponent::backgroundColourId,  findColour(Background));
	graph.setColour(NodeGraphComponent::gridLineColourId,    findColour(GridLine));
	graph.setColour(NodeGraphComponent::lineColourId,        findColour(Line));
	graph.setColour(NodeGraphComponent::fillColourId,        findColour(LineBackground).withAlpha(0.26f));
	graph.setColour(NodeGraphComponent::nodeColourId,        findColour(Node));
	graph.setColour(NodeGraphComponent::nodeOutlineColourId, findColour(NodeOutline));
}

// --- Legend ---

EnvelopeLegendComponent* EnvelopeComponent::getLegend()
{
	auto* container = dynamic_cast<EnvelopeContainerComponent*>(getParentComponent());
	if (container == nullptr) return nullptr;
	return container->getLegendComponent();
}

void EnvelopeComponent::setLegendText(juce::String legendText)
{
	if (auto* legend = getLegend())
		legend->setText(legendText);
}

void EnvelopeComponent::setLegendTextToDefault()
{
	if (auto* legend = getLegend())
		legend->setText();
}

void EnvelopeComponent::updateLegendForHover(int nodeIndex, bool /*isCurveHandle*/)
{
	if (nodeIndex < 0) {
		setLegendTextToDefault();
		return;
	}

	auto* legend = getLegend();
	if (legend == nullptr)
		return;

	const auto dahdsr = getDahdsrParams();
	const std::array<double, kDahdsrHandles> segmentTimes = {
		0.0,
		dahdsr.delaySeconds,
		dahdsr.attackSeconds,
		dahdsr.holdSeconds,
		dahdsr.decaySeconds,
		dahdsr.releaseSeconds
	};

	int places = (getWidth() >= 115) ? 3 : (getWidth() >= 100) ? 2 : 1;

	juce::String text;
	if (nodeIndex > 0 && nodeIndex < (int)kLegendLabels.size()) {
		text = kLegendLabels[(size_t)nodeIndex]
		     + juce::String(legend->mapTime(segmentTimes[(size_t)nodeIndex]), places);
		if (nodeIndex == 4)
			text << ", Sustain level: " << juce::String(legend->mapValue(dahdsr.sustainLevel), places);
	}

	setLegendText(text);
}

// ============================================================================
// EnvelopeLegendComponent
// ============================================================================

EnvelopeLegendComponent::EnvelopeLegendComponent(juce::String _defaultText)
	: defaultText(_defaultText)
{
	addAndMakeVisible(text = new juce::Label("legend", defaultText));
	setBounds(0, 0, 100, 16);
}

EnvelopeLegendComponent::~EnvelopeLegendComponent()
{
	deleteAllChildren();
}

EnvelopeComponent* EnvelopeLegendComponent::getEnvelopeComponent() const
{
	auto* parent = dynamic_cast<EnvelopeContainerComponent*>(getParentComponent());
	if (parent == nullptr) return nullptr;
	return parent->getEnvelopeComponent();
}

void EnvelopeLegendComponent::resized()
{
	text->setBounds(0, 0, getWidth(), getHeight());
}

void EnvelopeLegendComponent::paint(juce::Graphics& g)
{
	g.setColour(findColour(EnvelopeComponent::LegendBackground));
	g.fillRect(0, 0, getWidth(), getHeight());

	juce::Colour textColour = findColour(EnvelopeComponent::LegendText);
	text->setColour(juce::Label::textColourId, textColour);
}

void EnvelopeLegendComponent::setText(juce::String legendText)
{
	text->setText(legendText, juce::dontSendNotification);
	repaint();
}

void EnvelopeLegendComponent::setText()
{
	text->setText(defaultText, juce::dontSendNotification);
	repaint();
}

double EnvelopeLegendComponent::mapValue(double value) { return value; }
double EnvelopeLegendComponent::mapTime(double time) { return time; }

// ============================================================================
// EnvelopeContainerComponent
// ============================================================================

EnvelopeContainerComponent::EnvelopeContainerComponent(OscirenderAudioProcessor& processor,
                                                       juce::String defaultText)
{
	setOpaque(false);
	legend = std::make_unique<EnvelopeLegendComponent>(defaultText);
	addAndMakeVisible(legend.get());
	envelope = std::make_unique<EnvelopeComponent>(processor);
	addAndMakeVisible(envelope.get());
}

EnvelopeContainerComponent::~EnvelopeContainerComponent() = default;

void EnvelopeContainerComponent::paint(juce::Graphics& g)
{
	auto bounds = getLocalBounds().toFloat();
	g.setColour(envelope->findColour(EnvelopeComponent::Background));
	g.fillRoundedRectangle(bounds, 6.0f);
}

void EnvelopeContainerComponent::paintOverChildren(juce::Graphics& g)
{
	auto bounds = getLocalBounds().toFloat();
	float r = 6.0f;

	juce::Path cornerMask;
	cornerMask.setUsingNonZeroWinding(false);
	cornerMask.addRectangle(bounds);
	cornerMask.addRoundedRectangle(bounds, r);
	g.setColour(findColour(juce::ResizableWindow::backgroundColourId));
	g.fillPath(cornerMask);

	g.setColour(juce::Colours::white.withAlpha(0.06f));
	g.drawRoundedRectangle(bounds.reduced(0.5f), r, 1.0f);
}

void EnvelopeContainerComponent::resized()
{
	int legendHeight = legend->getHeight();
	auto area = getLocalBounds();
	envelope->setBounds(area.removeFromTop(area.getHeight() - legendHeight));
	legend->setBounds(area);
}

#endif // UGEN_NOEXTGPL
