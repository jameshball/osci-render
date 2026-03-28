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
#include "../../audio/DahdsrEnvelope.h"
#include "../../audio/EnvState.h"
#include "../../audio/EnvelopeParameters.h"
#include "NodeGraphComponent.h"
#include "ModulationSourceComponent.h"
#include "../KnobContainerComponent.h"

class OscirenderAudioProcessor;

/** Full envelope panel using the generic ModulationSourceComponent base for
    tab/drag/depth-indicator infrastructure.  Adds DAHDSR-specific graph
    editing, DAW gesture handling, and flow-marker support. */
class EnvelopeComponent : public ModulationSourceComponent
{
public:
	explicit EnvelopeComponent(OscirenderAudioProcessor& processor);

	void resized() override;

	int getActiveEnvIndex() const { return getActiveSourceIndex(); }

	DahdsrParams getDahdsrParams(int envIndex) const;
	void setDahdsrParams(int envIndex, const DahdsrParams& params);
	void setDahdsrParams(const DahdsrParams& params) { setDahdsrParams(getActiveSourceIndex(), params); }

	// Flow marker API (forwarded to underlying graph)
	void setFlowMarkerTimesForUi(const double* timesSeconds, int numTimes);
	void clearFlowMarkerTimesForUi();
	void resetFlowPersistenceForUi();

	static juce::Colour getEnvColour(int envIndex);

	void syncFromProcessorState() override;

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

	const static int COLOUR_OFFSET = 0x6082100;

	enum EnvColours {
		Node              = COLOUR_OFFSET,
		ReleaseNode       = COLOUR_OFFSET + 1,
		LoopNode          = COLOUR_OFFSET + 2,
		Line              = COLOUR_OFFSET + 3,
		LoopLine          = COLOUR_OFFSET + 4,
		LegendText        = COLOUR_OFFSET + 5,
		LegendBackground  = COLOUR_OFFSET + 6,
		LineBackground    = COLOUR_OFFSET + 7,
		NumEnvColours     = 8,
	};

protected:
	void onActiveSourceChanged(int newIndex) override;

private:
	static constexpr int kDahdsrNumHandles = 6;

	OscirenderAudioProcessor& audioProcessor;

	struct EnvelopeData {
		std::vector<GraphNode> nodes;
	};
	std::array<EnvelopeData, NUM_ENVELOPES> envData;

	// --- Graph helpers ---
	void syncGraphColours();
	void onGraphNodesChanged();
	void beginGestureForDrag();
	void endGestureForDrag();
	void pushDahdsrToHost();

	std::array<osci::FloatParameter*, kDahdsrNumHandles> getTimeParams(int envIndex) const;
	std::array<osci::FloatParameter*, kDahdsrNumHandles> getValueParams(int envIndex) const;
	std::array<osci::FloatParameter*, kDahdsrNumHandles> getCurveParams(int envIndex) const;

	GridMode gridDisplayMode = GridNone;

	NodeGraphComponent graph;

	// --- Knobs for DAHDSR parameters ---
	KnobContainerComponent delayKnob   { "DELAY" };
	KnobContainerComponent attackKnob  { "ATTACK" };
	KnobContainerComponent holdKnob    { "HOLD" };
	KnobContainerComponent decayKnob   { "DECAY" };
	KnobContainerComponent sustainKnob { "SUSTAIN" };
	KnobContainerComponent releaseKnob { "RELEASE" };

	static constexpr int kKnobRowHeight = 48;
	static constexpr int kKnobGap = 4;
	static constexpr int kMaxKnobWidth = 90;

	// Bind a KnobContainerComponent to an osci::FloatParameter*.
	// Sets range, default, suffix, accent colour, and bidirectional sync.
	void bindKnobToParam(KnobContainerComponent& container, osci::FloatParameter* param,
	                     double skewCentre, const juce::String& suffix = {});
	void syncKnobsToActiveEnv();
	void updateKnobValues();

	void syncGraphToActiveEnv();
	void syncActiveEnvFromGraph();
	void initEnvelopeData(int envIndex);
	void setupDahdsrConstraints();
	std::vector<GraphNode> buildDahdsrNodes(const DahdsrParams& p) const;
};

#endif // gpl

#endif // ENVELOPE_COMPONENT_H
