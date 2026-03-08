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

#ifndef ENVELOPE_COMPONENT_H
#define ENVELOPE_COMPONENT_H

#ifndef UGEN_NOEXTGPL

#include <JuceHeader.h>
#include <array>
#include <memory>
#include "../audio/DahdsrEnvelope.h"
#include "lfo/NodeGraphComponent.h"

class EnvelopeComponent;
class EnvelopeLegendComponent;
class OscirenderAudioProcessor;

/** DAHDSR envelope editor -- thin wrapper around NodeGraphComponent.
    All interaction, rendering, and curve editing is handled by the graph;
    this class wires up DAHDSR-specific constraints, grid painting,
    legend updates, and DAW parameter synchronisation. */
class EnvelopeComponent : public juce::Component
{
public:
	explicit EnvelopeComponent(OscirenderAudioProcessor& processor);
	~EnvelopeComponent();

	void setDomainRange(double min, double max);
	void setDomainRange(double max) { setDomainRange(0.0, max); }
	void setValueRange(double min, double max);
	void setValueRange(double max) { setValueRange(0.0, max); }

	enum GridMode {
		GridLeaveUnchanged = -1,
		GridNone = 0,
		GridValue = 1,
		GridDomain = 2,
		GridBoth = GridValue | GridDomain
	};
	void setGrid(GridMode display, GridMode quantise,
	             double domain = 0.0, double value = 0.0);

	DahdsrParams getDahdsrParams() const;
	void setDahdsrParams(const DahdsrParams& params);
	float lookup(float time) const;

	// Flow marker API (forwarded to underlying graph)
	void setFlowMarkerTimesForUi(const double* timesSeconds, int numTimes);
	void clearFlowMarkerTimesForUi();
	void resetFlowPersistenceForUi();

	void resized() override;
	void lookAndFeelChanged() override;

	// Legend helpers
	EnvelopeLegendComponent* getLegend();
	void setLegendText(juce::String legendText);
	void setLegendTextToDefault();

	const static int COLOUR_OFFSET = 0x6082100;

	enum EnvColours {
		Node              = COLOUR_OFFSET,
		ReleaseNode       = COLOUR_OFFSET + 1,
		LoopNode          = COLOUR_OFFSET + 2,
		NodeOutline       = COLOUR_OFFSET + 3,
		Line              = COLOUR_OFFSET + 4,
		LoopLine          = COLOUR_OFFSET + 5,
		Background        = COLOUR_OFFSET + 6,
		GridLine          = COLOUR_OFFSET + 7,
		LegendText        = COLOUR_OFFSET + 8,
		LegendBackground  = COLOUR_OFFSET + 9,
		LineBackground    = COLOUR_OFFSET + 10,
		NumEnvColours     = 11,
	};

private:
	static constexpr int kDahdsrNumHandles = 6;

	void syncGraphColours();
	void onGraphNodesChanged();
	void beginGestureForDrag();
	void endGestureForDrag();
	void pushDahdsrToHost();
	void updateLegendForHover(int nodeIndex, bool isCurveHandle);

	OscirenderAudioProcessor& processor;
	NodeGraphComponent graph;

	std::array<osci::FloatParameter*, kDahdsrNumHandles> timeParams{};
	std::array<osci::FloatParameter*, kDahdsrNumHandles> valueParams{};
	std::array<osci::FloatParameter*, kDahdsrNumHandles> curveParams{};

	GridMode gridDisplayMode = GridNone;
};

class EnvelopeLegendComponent : public juce::Component
{
public:
	EnvelopeLegendComponent(juce::String defaultText = juce::String());
	virtual ~EnvelopeLegendComponent() noexcept;

	EnvelopeComponent* getEnvelopeComponent() const;

	void paint(juce::Graphics& g);
	void resized();
	void setText(juce::String legendText);
	void setText();

	virtual double mapValue(double value);
	virtual double mapTime(double time);
	virtual juce::String getValueUnits() { return juce::String(); }
	virtual juce::String getTimeUnits() { return juce::String(); }

private:
	juce::Label* text;
	juce::String defaultText;
};

/** For displaying and editing a breakpoint envelope with a legend. */
class EnvelopeContainerComponent : public juce::Component
{
public:
	explicit EnvelopeContainerComponent(OscirenderAudioProcessor& processor,
	                                    juce::String defaultText = juce::String());
	~EnvelopeContainerComponent();
	void resized();
	void paint(juce::Graphics& g) override;
	void paintOverChildren(juce::Graphics& g) override;

	EnvelopeComponent* getEnvelopeComponent() const   { return envelope.get(); }
	EnvelopeLegendComponent* getLegendComponent() const { return legend.get(); }

	void setDahdsrParams(const DahdsrParams& params) { getEnvelopeComponent()->setDahdsrParams(params); }

	void setGrid(const EnvelopeComponent::GridMode display,
	             const EnvelopeComponent::GridMode quantise,
	             const double domain = 0.0,
	             const double value = 0.0)
	{
		envelope->setGrid(display, quantise, domain, value);
	}

private:
	std::unique_ptr<EnvelopeComponent>       envelope;
	std::unique_ptr<EnvelopeLegendComponent> legend;
};

#endif // gpl

#endif // ENVELOPE_COMPONENT_H
