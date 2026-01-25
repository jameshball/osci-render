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


#define HANDLESIZE 11
#define FINETUNE 0.00001

//#define MYDEBUG 1 // get rid of this later

class EnvelopeComponent;
class EnvelopeHandleComponent;
class EnvelopeLegendComponent;

class EnvelopeHandleComponent :	public juce::Component
{
public:	
	EnvelopeHandleComponent();
	EnvelopeComponent* getParentComponent() const;
	
	void updateTimeAndValue();
	
	void updateLegend();
	void paint(juce::Graphics& g);
	void moved();
		
	void mouseMove         (const juce::MouseEvent& e);
    void mouseEnter        (const juce::MouseEvent& e);
    void mouseExit         (const juce::MouseEvent& e);
    void mouseDown         (const juce::MouseEvent& e);
    void mouseDrag         (const juce::MouseEvent& e);
    void mouseUp           (const juce::MouseEvent& e);
		
	EnvelopeHandleComponent* getPreviousHandle() const;
	EnvelopeHandleComponent* getNextHandle() const;
	void removeThisHandle();
	
	void resetOffsets() { offsetX = offsetY = 0; }
	double getTime() const	{ return time;	}
	double getValue() const	{ return value; }
	float getCurve() const	{ return curve; }
	int getHandleIndex() const;
		
	void setTime(double timeToSet);
	void setValue(double valueToSet);
	void setCurve(float curveToSet);
	void setTimeAndValue(double timeToSet, double valueToSet, double quantise = 0.0);
	void setTimeAndValueUnclamped(double timeToSet, double valueToSet);
	void offsetTimeAndValue(double offsetTime, double offsetValue, double quantise = 0.0);
	double constrainDomain(double domainToConstrain) const;
	double constrainValue(double valueToConstrain) const;
    
    void lockTime(double time);
    void lockValue(double value);
    void unlockTime();
    void unlockValue();
	
	friend class EnvelopeComponent;
	
private:
	bool dontUpdateTimeAndValue;
	void recalculatePosition();
	void recalculateShouldDraw();
	
	juce::ComponentDragger dragger;
	int lastX, lastY;
	int offsetX, offsetY;
	double time, value;
    bool shouldLockTime, shouldLockValue, shouldDraw;
	float curve;
	bool ignoreDrag;
};


class OscirenderAudioProcessor;

/** For displaying and editing a breakpoint envelope. 
 @ingoup EnvUGens
 @see Env */
class EnvelopeComponent : public juce::Component, private juce::Timer
{
public:
	explicit EnvelopeComponent(OscirenderAudioProcessor& processor);
	~EnvelopeComponent();

	void setDomainRange(const double min, const double max);
	void setDomainRange(const double max) { setDomainRange(0.0, max); }
	void getDomainRange(double& min, double& max) const;
	void setValueRange(const double min, const double max);
	void setValueRange(const double max) { setValueRange(0.0, max); }
	void getValueRange(double& min, double& max) const;
	
	enum GridMode { 
		GridLeaveUnchanged = -1,
		GridNone = 0,
		GridValue = 1, 
		GridDomain = 2, 
		GridBoth = GridValue | GridDomain 
	};
	void setGrid(const GridMode display, const GridMode quantise, const double domain = 0.0, const double value = 0.0);
	
	void paint(juce::Graphics& g);
	void paintOverChildren(juce::Graphics& g) override;
	void paintBackground(juce::Graphics& g);
	void resized();
	void timerCallback() override;
	void lookAndFeelChanged() override;
		
	void mouseMove         (const juce::MouseEvent& e);
    void mouseEnter        (const juce::MouseEvent& e);
	void mouseExit         (const juce::MouseEvent& e);
	void mouseDown         (const juce::MouseEvent& e);
	void mouseDrag         (const juce::MouseEvent& e);
	void mouseUp           (const juce::MouseEvent& e);
	void mouseWheelMove    (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;
	
	void sendChangeMessage();
	void sendStartDrag();
	void sendEndDrag();
	void setHoverHandleFromChild(EnvelopeHandleComponent* handle);
	void clearHoverHandleFromChild(EnvelopeHandleComponent* handle);
	
    void clear();
    
	EnvelopeLegendComponent* getLegend();
	void setLegendText(juce::String legendText);
	void setLegendTextToDefault();
	int getHandleIndex(EnvelopeHandleComponent* handle) const;
	EnvelopeHandleComponent* getHandle(const int index) const;
    int getNumHandles() const { return handles.size(); }
	void setActiveHandle(EnvelopeHandleComponent* handle);
	void setActiveCurveHandle(EnvelopeHandleComponent* handle);
	EnvelopeHandleComponent* getActiveHandle() const { return activeHandle; }
	EnvelopeHandleComponent* getActiveCurveHandle() const { return activeCurveHandle; }
	
	EnvelopeHandleComponent* getPreviousHandle(const EnvelopeHandleComponent* thisHandle) const;
	EnvelopeHandleComponent* getNextHandle(const EnvelopeHandleComponent* thisHandle) const;
	EnvelopeHandleComponent* addHandle(int newX, int newY, float curve);
	EnvelopeHandleComponent* addHandle(double newDomain, double newValue, float curve);
	void removeHandle(EnvelopeHandleComponent* thisHandle);
	void quantiseHandle(EnvelopeHandleComponent* thisHandle);

	
	double convertPixelsToDomain(int pixelsX, int pixelsXMax = -1) const;
	double convertPixelsToValue(int pixelsY, int pixelsYMax = -1) const;
	double convertDomainToPixels(double domainValue) const;
	double convertValueToPixels(double value) const;
	
	DahdsrParams getDahdsrParams() const;
	void setDahdsrParams(const DahdsrParams& params);
	float lookup(const float time) const;

	// Optional UI-only overlay: vertical markers that “flow” through the envelope fill.
	// Times are in the same domain as the envelope (seconds for ADSR in osci-render).
	void setFlowMarkerTimesForUi(const double* timesSeconds, int numTimes);
	void clearFlowMarkerTimesForUi();
	void resetFlowPersistenceForUi();
	
	double constrainDomain(double domainToConstrain) const;
	double constrainValue(double valueToConstrain) const;
	
//	double quantiseDomain(double value);
//	double quantiseValue(double value);
	
	const static int COLOUR_OFFSET = 0x6082100;

	enum EnvColours {
		Node = COLOUR_OFFSET,
		ReleaseNode = COLOUR_OFFSET + 1,
		LoopNode = COLOUR_OFFSET + 2,
		NodeOutline = COLOUR_OFFSET + 3,
		Line = COLOUR_OFFSET + 4,
		LoopLine = COLOUR_OFFSET + 5,
		Background = COLOUR_OFFSET + 6,
		GridLine = COLOUR_OFFSET + 7,
		LegendText = COLOUR_OFFSET + 8,
		LegendBackground = COLOUR_OFFSET + 9,
		LineBackground = COLOUR_OFFSET + 10,
		NumEnvColours = 11,
	};
	
	enum MoveMode { MoveClip, MoveSlide, NumMoveModes };
	
private:
	static constexpr int kDahdsrNumHandles = 6;
	class EnvelopeGridRenderer;
	class EnvelopeFlowTrailRenderer;
	class EnvelopeInteraction;

	struct ScopedStructuralEdit
	{
		explicit ScopedStructuralEdit(EnvelopeComponent& owner) : env(owner) { ++env.structuralEditDepth; }
		~ScopedStructuralEdit() { --env.structuralEditDepth; }
		EnvelopeComponent& env;
	};

	bool canEditStructure() const { return structuralEditDepth > 0; }

	void recalculateHandles();
	EnvelopeHandleComponent* findHandle(double time);
	void applyDahdsrHandleLocks();
	void ensureFlowRepaintTimerRunning();
	
	void beginGestureForActiveHandle();
	void endGestureForActiveHandle();
	void pushParamsForActiveHandle();

	OscirenderAudioProcessor& processor;
	std::array<osci::FloatParameter*, kDahdsrNumHandles> timeParams{};
	std::array<osci::FloatParameter*, kDahdsrNumHandles> valueParams{};
	std::array<osci::FloatParameter*, kDahdsrNumHandles> curveParams{};

	juce::Array<EnvelopeHandleComponent*> handles;
	int minNumHandles, maxNumHandles;
	double domainMin, domainMax;
	double valueMin, valueMax;
	double valueGrid, domainGrid;
	GridMode gridDisplayMode, gridQuantiseMode;
	EnvelopeHandleComponent* activeHandle;
	EnvelopeHandleComponent* activeCurveHandle;
	int curvePoints;

	int structuralEditDepth = 0;

	std::unique_ptr<EnvelopeGridRenderer> gridRenderer;
	std::unique_ptr<EnvelopeFlowTrailRenderer> flowTrailRenderer;
	std::unique_ptr<EnvelopeInteraction> interaction;

	static constexpr double kFlowTrailMaxBridgeTimeSeconds = 0.05; // don't connect jumps larger than this
	static constexpr float kCurveStrokeThickness = 1.75f;
	static constexpr float kCurveHandleRadius = 3.0f;
	static constexpr float kHandleOutlineThickness = kCurveStrokeThickness;
	static constexpr float kHoverRingRadius = 10.0f;
	static constexpr float kHoverRingThickness = 1.5f;
	static constexpr float kHoverOuterRingThickness = 4.5f;
	static constexpr float kHoverRingAlpha = 0.85f;
	static constexpr float kHoverOuterRingAlpha = 0.35f;
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

/** For displaying and editing a breakpoint envelope with a legend. 
 This provides additional information about the points. 
 @ingoup EnvUGens
 @see Env */
class EnvelopeContainerComponent : public juce::Component
{
public:
	explicit EnvelopeContainerComponent(OscirenderAudioProcessor& processor, juce::String defaultText = juce::String());
	~EnvelopeContainerComponent();
	void resized();
	
	EnvelopeComponent* getEnvelopeComponent() const		{ return envelope; }
	EnvelopeLegendComponent* getLegendComponent() const	{ return legend;   }


	
	void setDahdsrParams(const DahdsrParams& params) { return getEnvelopeComponent()->setDahdsrParams(params); }

	
	void setGrid(const EnvelopeComponent::GridMode display, 
				 const EnvelopeComponent::GridMode quantise, 
				 const double domain = 0.0, 
				 const double value = 0.0)
	{
		envelope->setGrid(display, quantise, domain, value);
	}


private:
	EnvelopeComponent*			envelope;
	EnvelopeLegendComponent*	legend;
};

#endif // gpl


#endif // ENVELOPE_COMPONENT_H
