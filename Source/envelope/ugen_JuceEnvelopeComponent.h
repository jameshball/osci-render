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

#ifndef UGEN_JUCEENVELOPECOMPONENT_H
#define UGEN_JUCEENVELOPECOMPONENT_H

#ifndef UGEN_NOEXTGPL

#include <JuceHeader.h>
#include "../audio/DahdsrEnvelope.h"


#define HANDLESIZE 11
#define FINETUNE 0.001

//#define MYDEBUG 1 // get rid of this later

class EnvelopeComponent;
class EnvelopeHandleComponent;
class EnvelopeLegendComponent;

class EnvelopeHandleComponentConstrainer :	public juce::ComponentBoundsConstrainer
{
public:
	EnvelopeHandleComponentConstrainer(EnvelopeHandleComponent* handle);
	
	void checkBounds (juce::Rectangle<int>& bounds,
					  const juce::Rectangle<int>& old, const juce::Rectangle<int>& limits,
					  bool isStretchingTop, bool isStretchingLeft,
					  bool isStretchingBottom, bool isStretchingRight);
	
	void setAdjacentHandleLimits(int setLeftLimit, int setRightLimit);
	
private:
	int leftLimit, rightLimit;
	EnvelopeHandleComponent* handle;
};

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
	
	void setMousePositionToThisHandle();
	
	void resetOffsets() { offsetX = offsetY = 0; }
	double getTime() const	{ return time;	}
	double getValue() const	{ return value; }
	float getCurve() const	{ return curve; }
	int getHandleIndex() const;
		
	void setTime(double timeToSet);
	void setValue(double valueToSet);
	void setCurve(float curveToSet);
	void setTimeAndValue(double timeToSet, double valueToSet, double quantise = 0.0);
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
	EnvelopeHandleComponentConstrainer resizeLimits;
	
	double time, value;
    bool shouldLockTime, shouldLockValue, shouldDraw;
	float curve;
	bool ignoreDrag;
};


class EnvelopeComponentListener
{
public:
	EnvelopeComponentListener() throw() {}
	virtual ~EnvelopeComponentListener() {}
	virtual void envelopeChanged(EnvelopeComponent* changedEnvelope) = 0;
    virtual void envelopeStartDrag(EnvelopeComponent*) { }
    virtual void envelopeEndDrag(EnvelopeComponent*) { }
};

/** For displaying and editing a breakpoint envelope. 
 @ingoup EnvUGens
 @see Env */
class EnvelopeComponent : public juce::Component, private juce::Timer
{
public:
	EnvelopeComponent();
	~EnvelopeComponent();

	// DAHDSR editor mode (fixed 6 points: start, delay, attack, hold, sustain, release).
	void setDahdsrMode(bool enabled);
	bool getDahdsrMode() const;
	
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
	void paintBackground(juce::Graphics& g);
	void resized();
	void timerCallback() override;
		
	void mouseMove         (const juce::MouseEvent& e);
    void mouseEnter        (const juce::MouseEvent& e);
	void mouseDown         (const juce::MouseEvent& e);
	void mouseDrag         (const juce::MouseEvent& e);
	void mouseUp           (const juce::MouseEvent& e);
	
	void addListener (EnvelopeComponentListener* const listener);
    void removeListener (EnvelopeComponentListener* const listener);
	void sendChangeMessage();
    void sendStartDrag();
    void sendEndDrag();
	
    void clear();
    
	EnvelopeLegendComponent* getLegend();
	void setLegendText(juce::String legendText);
	void setLegendTextToDefault();
	int getHandleIndex(EnvelopeHandleComponent* handle) const;
	EnvelopeHandleComponent* getHandle(const int index) const;
    int getNumHandles() const { return handles.size(); }
	
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
	void setMinMaxNumHandles(int min, int max);

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

	struct ScopedStructuralEdit
	{
		explicit ScopedStructuralEdit(EnvelopeComponent& owner) : env(owner) { ++env.structuralEditDepth; }
		~ScopedStructuralEdit() { --env.structuralEditDepth; }
		EnvelopeComponent& env;
	};

	bool canEditStructure() const { return structuralEditDepth > 0 || !dahdsrMode; }

	void recalculateHandles();
	EnvelopeHandleComponent* findHandle(double time);
	void applyDahdsrHandleLocks();
	void ensureFlowRepaintTimerRunning();
	
	juce::SortedSet <void*> listeners;
	juce::Array<EnvelopeHandleComponent*> handles;
	int minNumHandles, maxNumHandles;
	double domainMin, domainMax;
	double valueMin, valueMax;
	double valueGrid, domainGrid;
	GridMode gridDisplayMode, gridQuantiseMode;
	EnvelopeHandleComponent* draggingHandle;
	EnvelopeHandleComponent* adjustingHandle;
	bool adjustable = false;
	double prevCurveValue = 0.0;
	int curvePoints;

	bool dahdsrMode = false;
	int structuralEditDepth = 0;

	int numFlowMarkers = 0;
	std::vector<double> flowMarkerTimesSeconds;
	std::vector<double> flowTrailLastSeenMs; // per-x timestamp; 0 means "never"
	std::vector<float> prevFlowMarkerX;
	std::vector<char> prevFlowMarkerValid;
	std::vector<double> prevFlowMarkerTimeSeconds;
	double flowTrailNewestMs = 0.0;

	// Tunables (time-based, independent of envelope domain range)
	float flowTrailTauMs = 140.0f; // decay time constant
	float flowTrailMaxAlpha = 0.38f;
	float flowTrailMaxBridgeTimeSeconds = 0.05f; // don't connect jumps larger than this
	int flowTrailPaintStepPx = 2; // reduce overdraw in paint()
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
	EnvelopeContainerComponent(juce::String defaultText = juce::String());
	~EnvelopeContainerComponent();
	void resized();
	
	EnvelopeComponent* getEnvelopeComponent() const		{ return envelope; }
	EnvelopeLegendComponent* getLegendComponent() const	{ return legend;   }
	void setLegendComponent(EnvelopeLegendComponent* newLegend);
	
	
	void addListener (EnvelopeComponentListener* const listener) { envelope->addListener(listener); }
    void removeListener (EnvelopeComponentListener* const listener) { envelope->removeListener(listener); }
	
	DahdsrParams getDahdsrParams() const { return getEnvelopeComponent()->getDahdsrParams(); }
	void setDahdsrParams(const DahdsrParams& params) { return getEnvelopeComponent()->setDahdsrParams(params); }
	float lookup(const float time) const { return getEnvelopeComponent()->lookup(time); }

	
	void setDomainRange(const double min, const double max)		{ envelope->setDomainRange(min, max);	}
	void setDomainRange(const double max)						{ setDomainRange(0.0, max);				}
	void getDomainRange(double& min, double& max) const			{ envelope->getDomainRange(min, max);	}
	void setValueRange(const double min, const double max)		{ envelope->setValueRange(min, max);	} 
	void setValueRange(const double max)						{ setValueRange(0.0, max);				}
	void getValueRange(double& min, double& max) const			{ envelope->getValueRange(min, max);	}
	
	void setGrid(const EnvelopeComponent::GridMode display, 
				 const EnvelopeComponent::GridMode quantise, 
				 const double domain = 0.0, 
				 const double value = 0.0)
	{
		envelope->setGrid(display, quantise, domain, value);
	}

	void setDahdsrMode(const bool enabled)		{ envelope->setDahdsrMode(enabled);			}

	
private:
	EnvelopeComponent*			envelope;
	EnvelopeLegendComponent*	legend;
};

#endif // gpl


#endif //UGEN_JUCEENVELOPECOMPONENT_H
