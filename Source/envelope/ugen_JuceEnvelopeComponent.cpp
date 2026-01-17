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

#include "ugen_JuceEnvelopeComponent.h"

#include <algorithm>
#include <cmath>


EnvelopeHandleComponentConstrainer::EnvelopeHandleComponentConstrainer(EnvelopeHandleComponent* handleOwner)
	:	leftLimit(0),
		rightLimit(0xffffff),
		handle(handleOwner)
{
}

void EnvelopeHandleComponentConstrainer::checkBounds (juce::Rectangle<int>& bounds,
					  const juce::Rectangle<int>& old, const juce::Rectangle<int>& limits,
					  bool isStretchingTop, bool isStretchingLeft,
					  bool isStretchingBottom, bool isStretchingRight)
{
	
	ComponentBoundsConstrainer::checkBounds(bounds,
											old,limits,
											isStretchingTop,isStretchingLeft,
											isStretchingBottom,isStretchingRight);
	
	// prevent this handle moving before the previous point
	// or after the next point	
	bounds.setPosition(juce::jlimit(leftLimit, rightLimit, bounds.getX()), bounds.getY());
	
	
	// then use the handle to access the envelope to then quantise x+y..
	
//	EnvelopeComponent* env = handle->getParentComponent();
//	
//	if(env)
//	{
//		bounds.setPosition(env->convertDomainToPixels(domain), 
//						   env->convertValueToPixels(value));
//	}
}


void EnvelopeHandleComponentConstrainer::setAdjacentHandleLimits(int setLeftLimit, int setRightLimit)
{
	leftLimit = setLeftLimit;
	rightLimit = setRightLimit;
}

EnvelopeComponent::EnvelopeComponent()
	: minNumHandles(0),
	  maxNumHandles(0xffffff),
	  domainMin(0.0),
	  domainMax(1.0),
	  valueMin(0.0),
	  valueMax(1.0),
	  valueGrid((valueMax - valueMin) / 10.0),
	  domainGrid((domainMax - domainMin) / 16.0),
	  gridDisplayMode(GridNone),
	  gridQuantiseMode(GridNone),
	  draggingHandle(nullptr),
	  adjustingHandle(nullptr),
	  activeHandle(nullptr),
	  activeCurveHandle(nullptr),
	  curvePoints(64),
	  dahdsrMode(false)
    
    if (shouldLockValue)
    {
        setTopLeftPosition(getX(),
                           getParentComponent()->convertValueToPixels(value));
    }
	else {
		envChanged = true;
		value = getParentComponent()->convertPixelsToValue(getY());
	}

	if (envChanged == true) {
        ((EnvelopeComponent*)getParentComponent())->sendChangeMessage();
    }
	
#ifdef MYDEBUG
	printf("MyEnvelopeHandleComponent::updateTimeAndValue(%f, %f)\n", time, value);
#endif
}

void EnvelopeHandleComponent::updateLegend()
{	
	EnvelopeComponent *env = getParentComponent();
	EnvelopeLegendComponent* legend = env->getLegend();

	if(legend == 0) return;

	juce::String text;
	
	int width = getParentWidth();
	int places;

	if (width >= 115) {
		places = 3;
	} else if (width >= 100) {
		places = 2;
	} else {
		places = 1;
	}
	
	if (env->getDahdsrMode()) {
		int index = env->getHandleIndex(this);
		const auto dahdsr = env->getDahdsrParams();
		double safeTime = 0.0;
		switch (index)
		{
			case 1: safeTime = dahdsr.delaySeconds; break;
			case 2: safeTime = dahdsr.attackSeconds; break;
			case 3: safeTime = dahdsr.holdSeconds; break;
			case 4: safeTime = dahdsr.decaySeconds; break;
			case 5: safeTime = dahdsr.releaseSeconds; break;
			default: safeTime = 0.0; break;
		}

		if (index == 1) {
			text = "Delay time (s): " + juce::String(legend->mapTime(safeTime), places);
		} else if (index == 2) {
			text = "Attack time (s): " + juce::String(legend->mapTime(safeTime), places);
		} else if (index == 3) {
			text = "Hold time (s): " + juce::String(legend->mapTime(safeTime), places);
		} else if (index == 4) {
			text = "Decay time (s): " + juce::String(legend->mapTime(safeTime), places);
			text << ", Sustain level: " << juce::String(legend->mapValue(dahdsr.sustainLevel), places);
		} else if (index == 5) {
			text = "Release time (s): " + juce::String(legend->mapTime(safeTime), places);
		} else {
			text = "";
		}
	} else {
		if (width >= 165) {
			text << "Point ";
		} else if (width >= 140) {
			text << "Point ";
		} else if (width >= 115) {
			text << "Pt ";
		} else if (width >= 100) {
			text << "Pt ";
		} else if (width >= 85) {
			text << "Pt ";
		} else if (width >= 65) {
			text << "P ";
		}

		text << (getHandleIndex())
			<< ": "
			<< juce::String(legend->mapTime(time), places) << legend->getTimeUnits()
			<< ", "
			<< juce::String(legend->mapValue(value), places) << legend->getValueUnits();
	}
	
	getParentComponent()->setLegendText(text);
}

void EnvelopeHandleComponent::paint(juce::Graphics& g)
{
	if (shouldDraw) {
		EnvelopeComponent *env = getParentComponent();
		juce::Colour handleColour = (env != 0) ? findColour(EnvelopeComponent::Node)
			: juce::Colour(0xFF69B4FF);

		const bool hot = isMouseOverOrDragging(true);
		const float inset = hot ? 0.5f : 1.0f;
		const float size = (float)getWidth() - inset * 2.0f;

		g.setColour(handleColour.withAlpha(hot ? 1.0f : 0.95f));
		g.fillEllipse(inset, inset, size, size);
		g.setColour(findColour(EnvelopeComponent::NodeOutline).withAlpha(hot ? 1.0f : 0.85f));
		g.drawEllipse(inset, inset, size, size, hot ? 1.5f : 1.0f);
	}
}

void EnvelopeHandleComponent::moved()
{
	if(dontUpdateTimeAndValue == false)
		updateTimeAndValue();
}

void EnvelopeHandleComponent::mouseMove(const juce::MouseEvent& e)
{
	(void)e;
#ifdef MYDEBUG
	printf("MyEnvelopeHandleComponent::mouseMove\n");
#endif
}

void EnvelopeHandleComponent::mouseEnter(const juce::MouseEvent& e)
{
	(void)e;
#ifdef MYDEBUG
	printf("MyEnvelopeHandleComponent::mouseEnter\n");
#endif
	
	if (shouldDraw) {
		setMouseCursor(juce::MouseCursor::DraggingHandCursor);
		updateLegend();
	} else {
        setMouseCursor(juce::MouseCursor::NormalCursor);
    }
}

void EnvelopeHandleComponent::mouseExit(const juce::MouseEvent& e)
{
	(void)e;
#ifdef MYDEBUG
	printf("MyEnvelopeHandleComponent::mouseExit\n");
#endif
	
	getParentComponent()->setLegendTextToDefault();
}

void EnvelopeHandleComponent::mouseDown(const juce::MouseEvent& e)
{
#ifdef MYDEBUG
	printf("MyEnvelopeHandleComponent::mouseDown (%d, %d)\n", e.x, e.y);
#endif
	
	if (shouldDraw) {
		if(e.mods.isShiftDown()) {

			if(!shouldLockTime && !shouldLockValue)
			{
				getParentComponent()->setLegendTextToDefault();
				removeThisHandle();
			}

			return; // dont send drag msg

		} 
		//else if(e.mods.isCtrlDown())
		//{
		//	if(getParentComponent()->getAllowNodeEditing())
		//	{
		//		ignoreDrag = true;
		//		
		//		if(PopupComponent::getActivePopups() < 1)
		//		{
		//			EnvelopeNodePopup::create(this, getScreenX()+e.x, getScreenY()+e.y);
		//		}
		//	}
		//}
		else 
		{

			offsetX = e.x;
			offsetY = e.y;

			resizeLimits.setMinimumOnscreenAmounts(HANDLESIZE,HANDLESIZE,HANDLESIZE,HANDLESIZE);

			EnvelopeHandleComponent* previousHandle = getPreviousHandle();
			EnvelopeHandleComponent* nextHandle = getNextHandle();

			int leftLimit = previousHandle == 0 ? 0 : previousHandle->getX()+2;
			int rightLimit = nextHandle == 0 ? getParentWidth()-HANDLESIZE : nextHandle->getX()-2;
			//		int leftLimit = previousHandle == 0 ? 0 : previousHandle->getX();
			//		int rightLimit = nextHandle == 0 ? getParentWidth()-HANDLESIZE : nextHandle->getX();


			resizeLimits.setAdjacentHandleLimits(leftLimit, rightLimit);

			dragger.startDraggingComponent(this, e);//&resizeLimits);

		}

		getParentComponent()->setActiveHandle(this);
		getParentComponent()->setActiveCurveHandle(nullptr);
		getParentComponent()->sendStartDrag();
	}
}

void EnvelopeHandleComponent::mouseDrag(const juce::MouseEvent& e)
{
	if(ignoreDrag || !shouldDraw) return;
	
	dragger.dragComponent(this, e, &resizeLimits);
	
	updateLegend();
	getParentComponent()->repaint();
	
	lastX = getX();
	lastY = getY();
	
#ifdef MYDEBUG
	double domain = getParentComponent()->convertPixelsToDomain(getX());
	double value = getParentComponent()->convertPixelsToValue(getY());
	printf("MyEnvelopeHandleComponent::mouseDrag(%d, %d) [%f, %f] (%d, %d)\n", e.x, e.y, 
		   domain,
		   value,
		   getParentComponent()->convertDomainToPixels(domain),
		   getParentComponent()->convertValueToPixels(value));
#endif
	
}


void EnvelopeHandleComponent::mouseUp(const juce::MouseEvent& e)
{
	(void)e;
    EnvelopeComponent *env = getParentComponent();

#ifdef MYDEBUG
	printf("MyEnvelopeHandleComponent::mouseUp\n");
#endif
	
	if (!shouldDraw) {
		goto exit;
    }

	if(ignoreDrag)
	{
		ignoreDrag = false;
		goto exit;
	}
		
//	if(e.mods.isCtrlDown() == false)
//	{
		env->quantiseHandle(this);
//	}
	
	setMouseCursor(juce::MouseCursor::DraggingHandCursor);
	
	offsetX = 0;
	offsetY = 0;
    
exit:
	getParentComponent()->sendEndDrag();
	getParentComponent()->setActiveHandle(nullptr);
}


EnvelopeHandleComponent* EnvelopeHandleComponent::getPreviousHandle() const
{
	return getParentComponent()->getPreviousHandle(this);
}

EnvelopeHandleComponent* EnvelopeHandleComponent::getNextHandle() const
{
	return getParentComponent()->getNextHandle(this);
}

void EnvelopeHandleComponent::removeThisHandle()
{
	getParentComponent()->removeHandle(this);
}

int EnvelopeHandleComponent::getHandleIndex() const
{
	return getParentComponent()->getHandleIndex(const_cast<EnvelopeHandleComponent*>(this));
}

void EnvelopeHandleComponent::setTime(double timeToSet)
{
	bool oldDontUpdateTimeAndValue = dontUpdateTimeAndValue;
	dontUpdateTimeAndValue = true;
	
	time = constrainDomain(timeToSet);
	
	setTopLeftPosition(getParentComponent()->convertDomainToPixels(time), 
					   getY());
	
	dontUpdateTimeAndValue = oldDontUpdateTimeAndValue;
	
	getParentComponent()->repaint();
	((EnvelopeComponent*)getParentComponent())->sendChangeMessage();
}

void EnvelopeHandleComponent::setValue(double valueToSet)
{
	bool oldDontUpdateTimeAndValue = dontUpdateTimeAndValue;
	dontUpdateTimeAndValue = true;
	
	value = constrainValue(valueToSet);
	
	setTopLeftPosition(getX(), 
					   getParentComponent()->convertValueToPixels(value));
	
	dontUpdateTimeAndValue = oldDontUpdateTimeAndValue;
	
	getParentComponent()->repaint();
	((EnvelopeComponent*)getParentComponent())->sendChangeMessage();
}

void EnvelopeHandleComponent::setCurve(float curveToSet)
{
	curve = curveToSet;
	getParentComponent()->repaint();
	((EnvelopeComponent*)getParentComponent())->sendChangeMessage();
}

void EnvelopeHandleComponent::setTimeAndValue(double timeToSet, double valueToSet, double quantise)
{
	bool oldDontUpdateTimeAndValue = dontUpdateTimeAndValue;
	dontUpdateTimeAndValue = true;

#ifdef MYDEBUG
	printf("MyEnvelopeHandleComponent::setTimeAndValue original (%f, %f)\n", timeToSet, valueToSet);
#endif
	
	if(quantise > 0.0) {
		int steps;

		steps		= valueToSet / quantise;
		valueToSet	= steps * quantise;
		steps		= timeToSet  / quantise;
		timeToSet	= steps * quantise;
		
#ifdef MYDEBUG
		printf("MyEnvelopeHandleComponent::setTimeAndValue quantised (%f, %f)\n", timeToSet, valueToSet);
#endif
	}
	
//	valueToSet = getParentComponent()->quantiseValue(valueToSet);
//	timeToSet = getParentComponent()->quantiseDomain(timeToSet);
	
	value = constrainValue(valueToSet);
	time = constrainDomain(timeToSet);
	
	setTopLeftPosition(getParentComponent()->convertDomainToPixels(time), 
					   getParentComponent()->convertValueToPixels(value));
	
	dontUpdateTimeAndValue = oldDontUpdateTimeAndValue;
	
	getParentComponent()->repaint();
}

void EnvelopeHandleComponent::offsetTimeAndValue(double offsetTime, double offsetValue, double quantise)
{
	setTimeAndValue(time+offsetTime, value+offsetValue, quantise);
}


double EnvelopeHandleComponent::constrainDomain(double domainToConstrain) const
{ 
	EnvelopeHandleComponent* previousHandle = getPreviousHandle();
	EnvelopeHandleComponent* nextHandle = getNextHandle();

	int leftLimit = previousHandle == 0 ? 0 : previousHandle->getX();
	int rightLimit = nextHandle == 0 ? getParentWidth()-HANDLESIZE : nextHandle->getX();
	
	double left = getParentComponent()->convertPixelsToDomain(leftLimit);
	double right = getParentComponent()->convertPixelsToDomain(rightLimit);
	
	if(previousHandle != 0) left += FINETUNE;
	if(nextHandle != 0) right -= FINETUNE;
		
	return juce::jlimit(juce::jmin(left, right), juce::jmax(left, right), shouldLockTime ? time : domainToConstrain);
}

double EnvelopeHandleComponent::constrainValue(double valueToConstrain) const
{
	return getParentComponent()->constrainValue(shouldLockValue ? value : valueToConstrain);
}

void EnvelopeHandleComponent::lockTime(double timeToLock)
{
    setTime(timeToLock);
    shouldLockTime = true;
	recalculateShouldDraw();
}

void EnvelopeHandleComponent::lockValue(double valueToLock)
{
    setValue(valueToLock);
    shouldLockValue = true;
	recalculateShouldDraw();
}

void EnvelopeHandleComponent::unlockTime()
{
    shouldLockTime = false;
	recalculateShouldDraw();
}

void EnvelopeHandleComponent::unlockValue()
{
    shouldLockValue = false;
	recalculateShouldDraw();
}

void EnvelopeHandleComponent::recalculatePosition()
{
	bool oldDontUpdateTimeAndValue = dontUpdateTimeAndValue;
	dontUpdateTimeAndValue = true;
	setTopLeftPosition(getParentComponent()->convertDomainToPixels(time), 
					   getParentComponent()->convertValueToPixels(value));
	dontUpdateTimeAndValue = oldDontUpdateTimeAndValue;
	getParentComponent()->repaint();
}

void EnvelopeHandleComponent::recalculateShouldDraw() {
	shouldDraw = !shouldLockTime || !shouldLockValue;
}


EnvelopeComponent::EnvelopeComponent()
	: minNumHandles(0),
	  maxNumHandles(0xffffff),
	  domainMin(0.0),
	  domainMax(1.0),
	  valueMin(0.0),
	  valueMax(1.0),
	  valueGrid((valueMax - valueMin) / 10.0),
	  domainGrid((domainMax - domainMin) / 16.0),
	  gridDisplayMode(GridNone),
	  gridQuantiseMode(GridNone),
	  draggingHandle(nullptr),
	  adjustingHandle(nullptr),
	  activeHandle(nullptr),
	  activeCurveHandle(nullptr),
	  curvePoints(64),
	  dahdsrMode(false)
{
	stopTimer();
	setMouseCursor(juce::MouseCursor::NormalCursor);
	setBounds(0, 0, 200, 200); // non-zero size to start with
}

void EnvelopeComponent::timerCallback()
{
	// Self-driven repaint so the flow trail can decay smoothly even when the host/UI stops pushing updates.
	if (flowTrailLastSeenMs.empty())
	{
		stopTimer();
		return;
	}

	const double nowMs = juce::Time::getMillisecondCounterHiRes();
	const double tauMs = juce::jmax(1.0, (double) flowTrailTauMs);
	const double maxAgeMs = tauMs * 6.0; // ~1% remaining for an exponential, fine for linear too
	if (numFlowMarkers == 0 && flowTrailNewestMs > 0.0 && (nowMs - flowTrailNewestMs) > maxAgeMs)
	{
		// Trail is effectively invisible; stop repainting.
		std::fill(flowTrailLastSeenMs.begin(), flowTrailLastSeenMs.end(), 0.0);
		flowTrailNewestMs = 0.0;
		stopTimer();
		repaint();
		return;
	}

	repaint();
}

void EnvelopeComponent::ensureFlowRepaintTimerRunning()
{
	if (!isTimerRunning())
		startTimerHz(60);
}

EnvelopeComponent::~EnvelopeComponent()
{
	deleteAllChildren();
}

void EnvelopeComponent::setDomainRange(const double min, const double max)
{
	bool changed = false;
	
	if(domainMin != min)
	{
		changed = true;
		domainMin = min;
	}
	
	if(domainMax != max)
	{
		changed = true;
		domainMax = max;
	}
	
	if(changed == true)
	{
		recalculateHandles();
	}
}

void EnvelopeComponent::getDomainRange(double& min, double& max) const
{
	min = domainMin;
	max = domainMax;
}

void EnvelopeComponent::setValueRange(const double min, const double max)
{
	bool changed = false;
	
	if(valueMin != min)
	{
		changed = true;
		valueMin = min;
	}
	
	if(valueMax != max)
	{
		changed = true;
		valueMax = max;
	}
	
	if(changed == true)
	{
		recalculateHandles();
	}	
}

void EnvelopeComponent::getValueRange(double& min, double& max) const
{
	min = valueMin;
	max = valueMax;
}

void EnvelopeComponent::recalculateHandles()
{
	for(int i = 0; i < handles.size(); i++) 
	{
		handles.getUnchecked(i)->recalculatePosition();
	}

	applyDahdsrHandleLocks();
}

void EnvelopeComponent::applyDahdsrHandleLocks()
{
	if (!dahdsrMode)
		return;

	// Clear any previous constraints first.
	for (int i = 0; i < handles.size(); ++i)
	{
		auto* handle = handles.getUnchecked(i);
		handle->unlockTime();
		handle->unlockValue();
	}

	if (handles.size() == 0)
		return;

	// Always anchor start.
	handles.getUnchecked(0)->lockTime(0.0);
	handles.getUnchecked(0)->lockValue(0.0);

	// DAHDSR (6 points): start 0, delay end 0, peak 1, hold end 1, sustain editable, release end 0.
	if (handles.size() == 6)
	{
		handles.getUnchecked(1)->lockValue(0.0);
		handles.getUnchecked(2)->lockValue(1.0);
		handles.getUnchecked(3)->lockValue(1.0);
		handles.getUnchecked(5)->lockValue(0.0);
		return;
	}
}

EnvelopeHandleComponent* EnvelopeComponent::findHandle(double time) {
	EnvelopeHandleComponent* handle = nullptr;

	for (int i = 0; i < handles.size(); i++) {
		handle = handles.getUnchecked(i);
		if (handle->getTime() > time)
			break;
	}

	return handle;
}

void EnvelopeComponent::setGrid(const GridMode display, const GridMode quantise, const double domainQ, const double valueQ)
{
	if(quantise != GridLeaveUnchanged)
		gridQuantiseMode = quantise;
	
	if((display != GridLeaveUnchanged) && (display != gridDisplayMode))
	{
		gridDisplayMode = display;
		repaint();
	}
	
	if((domainQ > 0.0) && (domainQ != domainGrid))
	{
		domainGrid = domainQ;
		repaint();
	}
	
	if((valueQ > 0.0) && (valueQ != valueGrid))
	{
		valueGrid = valueQ;
		repaint();
	}
}

void EnvelopeComponent::paint(juce::Graphics& g)
{
	paintBackground(g);
	
	if(handles.size() > 0)
	{
		juce::Path path;
		
		
		EnvelopeHandleComponent* handle = handles.getUnchecked(0);
		path.startNewSubPath((handle->getX() + handle->getRight()) * 0.5f,
							 (handle->getY() + handle->getBottom()) * 0.5f);
		
		const float firstTime = handle->getTime();
		float time = firstTime;
		
		for(int i = 1; i < handles.size(); i++) 
		{
			handle = handles.getUnchecked(i);
			float halfWidth = handle->getWidth()*0.5f;
			float halfHeight = handle->getHeight()*0.5f;
			
			float nextTime = handle->getTime();
			float handleTime = nextTime - time;
			float timeInc = handleTime / curvePoints;
			
			for(int j = 0; j < curvePoints; j++)
			{
				float value = lookup(time);
				path.lineTo(convertDomainToPixels(time) + halfWidth, 
							convertValueToPixels(value) + halfHeight);
				time += timeInc;
			}
			
			path.lineTo((handle->getX() + handle->getRight()) * 0.5f,
						(handle->getY() + handle->getBottom()) * 0.5f);
			
			time = nextTime;
		}
		
		// Build a fill path (area under curve) and render it first
		juce::Path fillPath(path);
		fillPath.lineTo(handle->getRight(), getHeight());
		fillPath.lineTo(0, getHeight());
		fillPath.closeSubPath();

		{
			auto fillColour = findColour(LineBackground).withAlpha(0.26f);
			g.setColour(fillColour);
			g.fillPath(fillPath);
		}

		// VITAL-style trail: time-based persistence using per-x lastSeen timestamps.
		{
			juce::Graphics::ScopedSaveState state(g);
			g.reduceClipRegion(fillPath);
			auto markerColour = findColour(LineBackground);
			const int w = getWidth();
			if ((int)flowTrailLastSeenMs.size() == w)
			{
				const double nowMs = juce::Time::getMillisecondCounterHiRes();
				const double tauMs = juce::jmax(1.0, (double) flowTrailTauMs);
				const float invTau = 1.0f / (float) tauMs;
				const int step = juce::jmax(1, flowTrailPaintStepPx);
				for (int x = 0; x < w; x += step)
				{
					const double lastSeen = flowTrailLastSeenMs[(size_t)x];
					if (lastSeen <= 0.0)
						continue;
					const double ageMs = nowMs - lastSeen;
					if (ageMs <= 0.0)
					{
						g.setColour(markerColour.withAlpha(flowTrailMaxAlpha));
						g.fillRect((float) x, 0.0f, (float) step, (float) getHeight());
						continue;
					}
					// Linear fade is much cheaper than exp() and good enough for UI.
					const float a = juce::jlimit(0.0f, 1.0f, 1.0f - ((float) ageMs * invTau));
					if (a <= 0.001f)
						continue;
					g.setColour(markerColour.withAlpha(flowTrailMaxAlpha * a));
					g.fillRect((float) x, 0.0f, (float) step, (float) getHeight());
				}
			}
		}

		// Draw marker heads on top (glow band + center line)
		if (numFlowMarkers > 0)
		{
			juce::Graphics::ScopedSaveState state(g);
			g.reduceClipRegion(fillPath);
			auto markerColour = findColour(LineBackground);
			for (int i = 0; i < numFlowMarkers; ++i)
			{
				const double timeSeconds = flowMarkerTimesSeconds[i];
				if (timeSeconds < 0.0)
					continue;
				const float x = (float)convertDomainToPixels(timeSeconds) + (HANDLESIZE * 0.5f);

				const float outerHalfWidth = 10.0f;
				juce::Rectangle<float> outer(x - outerHalfWidth, 0.0f, outerHalfWidth * 2.0f, (float)getHeight());
				juce::ColourGradient outerGradient(
					markerColour.withAlpha(0.0f), outer.getX(), 0.0f,
					markerColour.withAlpha(0.0f), outer.getRight(), 0.0f,
					false);
				outerGradient.addColour(0.5, markerColour.withAlpha(0.18f));
				g.setGradientFill(outerGradient);
				g.fillRect(outer);

				const float innerHalfWidth = 5.0f;
				juce::Rectangle<float> inner(x - innerHalfWidth, 0.0f, innerHalfWidth * 2.0f, (float)getHeight());
				juce::ColourGradient innerGradient(
					markerColour.withAlpha(0.0f), inner.getX(), 0.0f,
					markerColour.withAlpha(0.0f), inner.getRight(), 0.0f,
					false);
				innerGradient.addColour(0.5, markerColour.withAlpha(0.32f));
				g.setGradientFill(innerGradient);
				g.fillRect(inner);

				g.setColour(markerColour.withAlpha(0.60f));
				g.drawLine(x, 0.0f, x, (float)getHeight(), 2.5f);
			}
		}

		// Curve stroke on top
		g.setColour(findColour(Line).withAlpha(0.95f));
		g.strokePath(path, juce::PathStrokeType(1.75f));
	}
}

inline double round(double a, double b) throw()
{
	double offset = a < 0 ? -0.5 : 0.5;
	int n = (int)(a / b + offset);
	return b * (double)n;
}

void EnvelopeComponent::paintBackground(juce::Graphics& g)
{
	g.setColour(findColour(Background));
	g.fillRect(0, 0, getWidth(), getHeight());
	
	// Subtle grid for a more modern look
	auto gridColour = findColour(GridLine).withAlpha(0.55f);
	g.setColour(gridColour);
	
	if((gridDisplayMode & GridValue) && (valueGrid > 0.0))
	{
		double value = valueMin;
		int idx = 0;
		
		while(value <= valueMax)
		{
			//g.drawHorizontalLine(convertValueToPixels(value) + HANDLESIZE/2, 0, getWidth());
			float y = convertValueToPixels(value) + HANDLESIZE/2;
			y = round(y, 1.f) + 0.5f;
			// Slightly emphasize every 4th line
			const float alpha = (idx % 4 == 0) ? 0.85f : 0.55f;
			g.setColour(findColour(GridLine).withAlpha(alpha));
			g.drawLine(0, y, (float)getWidth(), y, 1.0f);
			value += valueGrid;
			idx++;
		}
	}
	
	if((gridDisplayMode & GridDomain) && (domainGrid > 0.0))
	{
		double domain = domainMin;
		int idx = 0;
		
		while(domain <= domainMax)
		{
			const float alpha = (idx % 4 == 0) ? 0.85f : 0.55f;
			g.setColour(findColour(GridLine).withAlpha(alpha));
			g.drawVerticalLine((int)(convertDomainToPixels(domain) + HANDLESIZE/2), 0.0f, (float)getHeight());
			domain += domainGrid;
			idx++;
		}
	}
}

void EnvelopeComponent::setFlowMarkerTimesForUi(const double* timesSeconds, int numTimes)
{
	numFlowMarkers = juce::jmax(0, numTimes);
	if ((int)flowMarkerTimesSeconds.size() < numFlowMarkers)
		flowMarkerTimesSeconds.resize((size_t) numFlowMarkers, -1.0);
	for (int i = 0; i < numFlowMarkers; ++i)
		flowMarkerTimesSeconds[(size_t) i] = timesSeconds[i];

	if ((int)prevFlowMarkerX.size() < numFlowMarkers)
	{
		prevFlowMarkerX.resize((size_t) numFlowMarkers, 0.0f);
		prevFlowMarkerValid.resize((size_t) numFlowMarkers, 0);
		prevFlowMarkerTimeSeconds.resize((size_t) numFlowMarkers, 0.0);
	}

	const int w = getWidth();
	if (w > 0)
	{
		if ((int)flowTrailLastSeenMs.size() != w)
			flowTrailLastSeenMs.assign((size_t)w, 0.0);

		const double nowMs = juce::Time::getMillisecondCounterHiRes();
		flowTrailNewestMs = juce::jmax(flowTrailNewestMs, nowMs);
		for (int i = 0; i < numFlowMarkers; ++i)
		{
			const double t = flowMarkerTimesSeconds[i];
			if (t < 0.0)
			{
				prevFlowMarkerValid[(size_t) i] = 0;
				prevFlowMarkerTimeSeconds[(size_t) i] = 0.0;
				continue;
			}

			const float x = (float)convertDomainToPixels(t) + (HANDLESIZE * 0.5f);
			const int xi = (int)juce::jlimit(0.0f, (float)(w - 1), x);

			const bool canBridge = prevFlowMarkerValid[(size_t) i] != 0
				&& (t >= prevFlowMarkerTimeSeconds[(size_t) i])
				&& ((t - prevFlowMarkerTimeSeconds[(size_t) i]) <= (double)flowTrailMaxBridgeTimeSeconds);

			if (canBridge)
			{
				const float prevX = prevFlowMarkerX[(size_t) i];
				const int a = (int)juce::jlimit(0.0f, (float)(w - 1), juce::jmin(prevX, x));
				const int b = (int)juce::jlimit(0.0f, (float)(w - 1), juce::jmax(prevX, x));
				// Fill between frames to eliminate gaps
				for (int xx = a; xx <= b; ++xx)
					flowTrailLastSeenMs[(size_t)xx] = nowMs;
			}
			else
			{
				flowTrailLastSeenMs[(size_t)xi] = nowMs;
			}

			prevFlowMarkerX[(size_t) i] = x;
			prevFlowMarkerValid[(size_t) i] = 1;
			prevFlowMarkerTimeSeconds[(size_t) i] = t;
		}
	}
	ensureFlowRepaintTimerRunning();
	repaint();
}

void EnvelopeComponent::clearFlowMarkerTimesForUi()
{
	numFlowMarkers = 0;
	ensureFlowRepaintTimerRunning();
	repaint();
}

void EnvelopeComponent::resetFlowPersistenceForUi()
{
	numFlowMarkers = 0;
	std::fill(flowTrailLastSeenMs.begin(), flowTrailLastSeenMs.end(), 0.0);
	flowTrailNewestMs = 0.0;
	std::fill(prevFlowMarkerValid.begin(), prevFlowMarkerValid.end(), 0);
	std::fill(prevFlowMarkerTimeSeconds.begin(), prevFlowMarkerTimeSeconds.end(), 0.0);
	stopTimer();
	repaint();
}

void EnvelopeComponent::resized()
{	
#ifdef MYDEBUG
	printf("MyEnvelopeComponent::resized(%d, %d)\n", getWidth(), getHeight());
#endif
	if(handles.size() != 0) {
		for(int i = 0; i < handles.size(); i++) {
			EnvelopeHandleComponent* handle = handles.getUnchecked(i);
			
			bool oldDontUpdateTimeAndValue = handle->dontUpdateTimeAndValue;
			handle->dontUpdateTimeAndValue = true;
						
			handle->setTopLeftPosition(convertDomainToPixels(handle->getTime()),
									   convertValueToPixels(handle->getValue()));
			
			handle->dontUpdateTimeAndValue = oldDontUpdateTimeAndValue;
		}
	}	

	if (getWidth() > 0)
		flowTrailLastSeenMs.assign((size_t)getWidth(), 0.0);
	flowTrailNewestMs = 0.0;
	std::fill(prevFlowMarkerValid.begin(), prevFlowMarkerValid.end(), 0);
	std::fill(prevFlowMarkerTimeSeconds.begin(), prevFlowMarkerTimeSeconds.end(), 0.0);
}


void EnvelopeComponent::mouseEnter(const juce::MouseEvent& e) 
{
	EnvelopeHandleComponent* handle = findHandle(convertPixelsToDomain(e.x));
	EnvelopeHandleComponent* prevHandle = handle->getPreviousHandle();

	if (handle == nullptr || prevHandle == nullptr) {
        adjustable = false;
        setMouseCursor(juce::MouseCursor::NormalCursor);
        return;
    }

	auto handleBounds = handle->getBoundsInParent();
	auto prevHandleBounds = prevHandle->getBoundsInParent();
	
	auto minX = juce::jmin(handleBounds.getX(), prevHandleBounds.getX());
	auto maxX = juce::jmax(handleBounds.getRight(), prevHandleBounds.getRight());
	auto minY = juce::jmin(handleBounds.getY(), prevHandleBounds.getY());
	auto maxY = juce::jmax(handleBounds.getBottom(), prevHandleBounds.getBottom());

	auto rect = juce::Rectangle<int>(minX, minY, maxX - minX, maxY - minY);

	if (rect.contains(e.getPosition())) {
		adjustable = true;
        setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
    } else {
		adjustable = false;
        setMouseCursor(juce::MouseCursor::NormalCursor);
    }
}

void EnvelopeComponent::mouseMove(const juce::MouseEvent& e) 
{
	mouseEnter(e);
}

void EnvelopeComponent::mouseDown(const juce::MouseEvent& e)
{
#ifdef MYDEBUG
	printf("MyEnvelopeComponent::mouseDown(%d, %d)\n", e.x, e.y);
#endif
	
	if (adjustable) {
		adjustingHandle = findHandle(convertPixelsToDomain(e.x));
		prevCurveValue = adjustingHandle != nullptr ? adjustingHandle->getCurve() : 0.0f;
		setActiveCurveHandle(adjustingHandle);
		setActiveHandle(nullptr);
		sendStartDrag();
	}
}

void EnvelopeComponent::mouseDrag(const juce::MouseEvent& e)
{
#ifdef MYDEBUG
	printf("MyEnvelopeComponent::mouseDrag(%d, %d)\n", e.x, e.y);
#endif
	
	if (adjustable && adjustingHandle != nullptr) {
		EnvelopeHandleComponent* prevHandle = adjustingHandle->getPreviousHandle();
		// get distance as proportion of height
		double originalValue = e.getDistanceFromDragStartY() / (double) getHeight();
		double value = originalValue * 5;
		value = value * value;
		if (originalValue < 0) {
            value = -value;
        }
		// make the value change the prevValue rather than overwriting it
		if (prevHandle != nullptr && prevHandle->getValue() > adjustingHandle->getValue()) {
			value = -value;
        }
		value = juce::jlimit(-50.0, 50.0, (double) prevCurveValue + value);
		adjustingHandle->setCurve((float) value);
	} else if (draggingHandle != nullptr) {
		draggingHandle->mouseDrag(e.getEventRelativeTo(draggingHandle));
	}
}

void EnvelopeComponent::mouseUp(const juce::MouseEvent& e)
{
#ifdef MYDEBUG
	printf("MyEnvelopeComponent::mouseUp\n");
#endif

	bool wasAdjusting = (adjustingHandle != nullptr);
	adjustingHandle = nullptr;
	
	if(draggingHandle != 0) 
	{
		if(e.mods.isCtrlDown() == false)
			quantiseHandle(draggingHandle);
		
		setMouseCursor(juce::MouseCursor::DraggingHandCursor);
		draggingHandle->resetOffsets();
		draggingHandle = 0;
        sendEndDrag();
	} else {
		setMouseCursor(juce::MouseCursor::NormalCursor);
	}

	if (wasAdjusting) {
		sendEndDrag();
	}
	setActiveCurveHandle(nullptr);
}

void EnvelopeComponent::setActiveHandle(EnvelopeHandleComponent* handle)
{
	activeHandle = handle;
}

void EnvelopeComponent::setActiveCurveHandle(EnvelopeHandleComponent* handle)
{
	activeCurveHandle = handle;
}

void EnvelopeComponent::addListener (EnvelopeComponentListener* const listener)
{
	if (listener != 0)
        listeners.add (listener);
}

void EnvelopeComponent::removeListener (EnvelopeComponentListener* const listener)
{
	listeners.removeValue(listener);
}

void EnvelopeComponent::sendChangeMessage()
{
	for (int i = listeners.size(); --i >= 0;)
    {
        ((EnvelopeComponentListener*) listeners.getUnchecked (i))->envelopeChanged (this);
        i = juce::jmin (i, listeners.size());
    }
}

void EnvelopeComponent::sendStartDrag()
{
    for (int i = listeners.size(); --i >= 0;)
    {
        ((EnvelopeComponentListener*) listeners.getUnchecked (i))->envelopeStartDrag (this);
        i = juce::jmin (i, listeners.size());
    }
}

void EnvelopeComponent::sendEndDrag()
{
    for (int i = listeners.size(); --i >= 0;)
    {
        ((EnvelopeComponentListener*) listeners.getUnchecked (i))->envelopeEndDrag (this);
        i = juce::jmin (i, listeners.size());
    }
}

void EnvelopeComponent::clear()
{
	ScopedStructuralEdit edit(*this);
    int i = getNumHandles();
    
    while (i > 0)
        removeHandle(handles[--i]);
}

EnvelopeLegendComponent* EnvelopeComponent::getLegend()
{
	EnvelopeContainerComponent* container = 
	dynamic_cast <EnvelopeContainerComponent*> (getParentComponent());
	
	if(container == 0) return 0;
	
	return container->getLegendComponent();
}

void EnvelopeComponent::setLegendText(juce::String legendText)
{	
	EnvelopeLegendComponent* legend = getLegend();
	
	if(legend == 0) return;
	
	legend->setText(legendText);
}

void EnvelopeComponent::setLegendTextToDefault()
{
	EnvelopeLegendComponent* legend = getLegend();
	
	if(legend == 0) return;
	
	legend->setText();	
}

int EnvelopeComponent::getHandleIndex(EnvelopeHandleComponent* thisHandle) const
{
	return handles.indexOf(thisHandle);
}

EnvelopeHandleComponent* EnvelopeComponent::getHandle(const int index) const
{
	return handles[index];
}

EnvelopeHandleComponent* EnvelopeComponent::getPreviousHandle(const EnvelopeHandleComponent* thisHandle) const
{
	int thisHandleIndex = handles.indexOf(const_cast<EnvelopeHandleComponent*>(thisHandle));
	
	if(thisHandleIndex <= 0) 
		return 0;
	else 
		return handles.getUnchecked(thisHandleIndex-1);
}

EnvelopeHandleComponent* EnvelopeComponent::getNextHandle(const EnvelopeHandleComponent* thisHandle) const
{
	int thisHandleIndex = handles.indexOf(const_cast<EnvelopeHandleComponent*>(thisHandle));
	
	if(thisHandleIndex == -1 || thisHandleIndex >= handles.size()-1) 
		return 0;
	else
		return handles.getUnchecked(thisHandleIndex+1);
}


EnvelopeHandleComponent* EnvelopeComponent::addHandle(int newX, int newY, float curve)
{
#ifdef MYDEBUG
	printf("MyEnvelopeComponent::addHandle(%d, %d)\n", newX, newY);
#endif
	
	return addHandle(convertPixelsToDomain(newX), convertPixelsToValue(newY), curve);
}


EnvelopeHandleComponent* EnvelopeComponent::addHandle(double newDomain, double newValue, float curve)
{
#ifdef MYDEBUG
	printf("MyEnvelopeComponent::addHandle(%f, %f)\n", (float)newDomain, (float)newValue);
#endif

	if (!canEditStructure())
		return 0;
	
//	newDomain = quantiseDomain(newDomain);
//	newValue = quantiseValue(newValue);
	
	if(handles.size() < maxNumHandles) {
		int i;
		for(i = 0; i < handles.size(); i++) {
			EnvelopeHandleComponent* handle = handles.getUnchecked(i);
			if(handle->getTime() > newDomain)
				break;
		}
		
		EnvelopeHandleComponent* handle;
		addAndMakeVisible(handle = new EnvelopeHandleComponent());
		handle->setSize(HANDLESIZE, HANDLESIZE);
		handle->setTimeAndValue(newDomain, newValue, 0.0);	
		handle->setCurve(curve);
		handles.insert(i, handle);
		if (dahdsrMode) {
			if (i == 0) {
				handle->lockTime(0);
			}
			// Constraints are applied in applyAdsrHandleLocks() after handles are rebuilt.
		}
	//	sendChangeMessage();
		return handle;
	}
	else return 0;
}


void EnvelopeComponent::removeHandle(EnvelopeHandleComponent* thisHandle)
{
	if (!canEditStructure())
		return;

	if(handles.size() > minNumHandles) {
		handles.removeFirstMatchingValue(thisHandle);
		removeChildComponent(thisHandle);
		delete thisHandle;
		sendChangeMessage();
		repaint();
	}
}

void EnvelopeComponent::quantiseHandle(EnvelopeHandleComponent* thisHandle)
{
	if((gridQuantiseMode & GridDomain) && (domainGrid > 0.0))
	{
		double domain = round(thisHandle->getTime(), domainGrid);
		thisHandle->setTime(domain);
		
	}
	
	if((gridQuantiseMode & GridValue) && (valueGrid > 0.0))
	{
		double value = round(thisHandle->getValue(), valueGrid);
		thisHandle->setValue(value);
	}
}

double EnvelopeComponent::convertPixelsToDomain(int pixelsX, int pixelsXMax) const
{
	if(pixelsXMax < 0) 
		pixelsXMax = getWidth()-HANDLESIZE;
	
	return (double)pixelsX / pixelsXMax * (domainMax-domainMin) + domainMin;

}

double EnvelopeComponent::convertPixelsToValue(int pixelsY, int pixelsYMax) const
{
	if(pixelsYMax < 0)
		pixelsYMax = getHeight()-HANDLESIZE;
	
	return (double)(pixelsYMax - pixelsY) / pixelsYMax * (valueMax-valueMin) + valueMin;
}

double EnvelopeComponent::convertDomainToPixels(double domainValue) const
{
	return (domainValue - domainMin) / (domainMax - domainMin) * (getWidth() - HANDLESIZE);
}

double EnvelopeComponent::convertValueToPixels(double value) const
{	
	double pixelsYMax = getHeight()-HANDLESIZE;	
	return pixelsYMax-((value- valueMin) / (valueMax - valueMin) * pixelsYMax);
}

static inline float osciEvalCurve01(float curveValue, float pos)
{
	pos = juce::jlimit(0.0f, 1.0f, pos);
	if (std::abs(curveValue) <= 0.001f)
		return pos;
	const float denom = 1.0f - std::exp(curveValue);
	const float numer = 1.0f - std::exp(pos * curveValue);
	return (denom != 0.0f) ? (numer / denom) : pos;
}

static inline float osciLerp(float a, float b, float t) { return a + (b - a) * t; }

DahdsrParams EnvelopeComponent::getDahdsrParams() const
{
	DahdsrParams p;
	if (handles.size() != 6)
		return p;

	const auto t0 = handles.getUnchecked(0)->getTime();
	const auto t1 = handles.getUnchecked(1)->getTime();
	const auto t2 = handles.getUnchecked(2)->getTime();
	const auto t3 = handles.getUnchecked(3)->getTime();
	const auto t4 = handles.getUnchecked(4)->getTime();
	const auto t5 = handles.getUnchecked(5)->getTime();

	p.delaySeconds = juce::jmax(0.0, t1 - t0);
	p.attackSeconds = juce::jmax(0.0, t2 - t1);
	p.holdSeconds = juce::jmax(0.0, t3 - t2);
	p.decaySeconds = juce::jmax(0.0, t4 - t3);
	p.releaseSeconds = juce::jmax(0.0, t5 - t4);

	p.sustainLevel = juce::jlimit(0.0, 1.0, handles.getUnchecked(4)->getValue());

	// Segment curves are stored on the destination handle (incoming segment).
	p.attackCurve = handles.getUnchecked(2)->getCurve();
	p.decayCurve = handles.getUnchecked(4)->getCurve();
	p.releaseCurve = handles.getUnchecked(5)->getCurve();

	return p;
}

void EnvelopeComponent::setDahdsrParams(const DahdsrParams& params)
{
	if (!dahdsrMode)
		setDahdsrMode(true);

	ScopedStructuralEdit edit(*this);

	if (handles.size() != kDahdsrNumHandles)
	{
		clear();
		// Start at 0
		addHandle(0.0, 0.0, 0.0f);
		addHandle(0.0, 0.0, 0.0f);
		addHandle(0.0, 1.0, 0.0f);
		addHandle(0.0, 1.0, 0.0f);
		addHandle(0.0, 0.5, 0.0f);
		addHandle(0.0, 0.0, 0.0f);
	}

	double t = 0.0;
	const double delay = juce::jmax(0.0, params.delaySeconds);
	const double attack = juce::jmax(0.0, params.attackSeconds);
	const double hold = juce::jmax(0.0, params.holdSeconds);
	const double decay = juce::jmax(0.0, params.decaySeconds);
	const double release = juce::jmax(0.0, params.releaseSeconds);
	const double sustain = juce::jlimit(0.0, 1.0, params.sustainLevel);

	// Handle indices: 0 start, 1 delay end, 2 attack peak, 3 hold end, 4 sustain, 5 release end.
	handles.getUnchecked(0)->setTimeAndValue(t, 0.0, 0.0);

	t += delay;
	handles.getUnchecked(1)->setTimeAndValue(t, 0.0, 0.0);
	handles.getUnchecked(1)->setCurve(0.0f);

	t += attack;
	handles.getUnchecked(2)->setTimeAndValue(t, 1.0, 0.0);
	handles.getUnchecked(2)->setCurve(params.attackCurve);

	t += hold;
	handles.getUnchecked(3)->setTimeAndValue(t, 1.0, 0.0);
	handles.getUnchecked(3)->setCurve(0.0f);

	t += decay;
	handles.getUnchecked(4)->setTimeAndValue(t, sustain, 0.0);
	handles.getUnchecked(4)->setCurve(params.decayCurve);

	t += release;
	handles.getUnchecked(5)->setTimeAndValue(t, 0.0, 0.0);
	handles.getUnchecked(5)->setCurve(params.releaseCurve);

	applyDahdsrHandleLocks();
	repaint();
}

float EnvelopeComponent::lookup(const float time) const
{
	if(handles.size() < 1)
	{
		return 0.f;
	}
	else 
	{
		EnvelopeHandleComponent *first = handles.getUnchecked(0);
		EnvelopeHandleComponent *last = handles.getUnchecked(handles.size()-1);
		
		if(time <= first->getTime())
		{
			return first->getValue();
		}
		else if(time >= last->getTime())
		{
			return last->getValue();
		}
		else
		{
			// DAHDSR-only lookup used for drawing.
			const auto p = getDahdsrParams();
			const float tRel = time - (float) first->getTime();
			if (tRel <= 0.0f)
				return (float) first->getValue();

			const float d0 = (float) p.delaySeconds;
			const float d1 = d0 + (float) p.attackSeconds;
			const float d2 = d1 + (float) p.holdSeconds;
			const float d3 = d2 + (float) p.decaySeconds;
			const float d4 = d3 + (float) p.releaseSeconds;

			auto evalSeg = [](float start, float end, float elapsed, float duration, float curve)
			{
				if (duration <= 0.0f)
					return end;
				const float pos = juce::jlimit(0.0f, 1.0f, elapsed / duration);
				const float shaped = osciEvalCurve01(curve, pos);
				return osciLerp(start, end, shaped);
			};

			if (tRel < d0)
				return 0.0f;
			if (tRel < d1)
				return evalSeg(0.0f, 1.0f, tRel - d0, (float) p.attackSeconds, p.attackCurve);
			if (tRel < d2)
				return 1.0f;
			if (tRel < d3)
				return evalSeg(1.0f, (float) p.sustainLevel, tRel - d2, (float) p.decaySeconds, p.decayCurve);
			if (tRel < d4)
				return evalSeg((float) p.sustainLevel, 0.0f, tRel - d3, (float) p.releaseSeconds, p.releaseCurve);
			return 0.0f;
		}
	}
}

void EnvelopeComponent::setMinMaxNumHandles(int min, int max)
{
	// DAHDSR mode uses a fixed, deterministic handle layout.
	// Avoid accidental random handle insertion/removal from external callers.
	if (dahdsrMode && !canEditStructure())
		return;

	if(min <= max) {
		minNumHandles = min;
		maxNumHandles = max;
	} else {
		minNumHandles = max;
		maxNumHandles = min;
	}
	
	juce::Random rand(juce::Time::currentTimeMillis());
	
	if(handles.size() < minNumHandles) {
		int num = minNumHandles-handles.size();
		
		for(int i = 0; i < num; i++) {
			double randX = rand.nextDouble() * (domainMax-domainMin) + domainMin;
			double randY = rand.nextDouble() * (valueMax-valueMin) + valueMin;
						
			addHandle(randX, randY, 0.0f);
		}
		
	} else if(handles.size() > maxNumHandles) {
		int num = handles.size()-maxNumHandles;
		
		for(int i = 0; i < num; i++) {
			removeHandle(handles.getLast());
		}
	}
	
}

double EnvelopeComponent::constrainDomain(double domainToConstrain) const
{
	return juce::jlimit(domainMin, domainMax, domainToConstrain); 
}

double EnvelopeComponent::constrainValue(double valueToConstrain) const	
{ 
	return juce::jlimit(valueMin, valueMax, valueToConstrain); 
}


//double EnvelopeComponent::quantiseDomain(double value)
//{
//	if((gridQuantiseMode & GridDomain) && (domainGrid > 0.0))
//		return round(value, domainGrid);
//	else
//		return value;
//}
//
//double EnvelopeComponent::quantiseValue(double value)
//{
//	if((gridQuantiseMode & GridValue) && (valueGrid > 0.0))
//		return round(value, valueGrid);
//	else
//		return value;
//}

EnvelopeLegendComponent::EnvelopeLegendComponent(juce::String _defaultText)
:	defaultText(_defaultText)
{
	addAndMakeVisible(text = new juce::Label("legend", defaultText));
	setBounds(0, 0, 100, 16); // the critical thing is the height which should stay constant
}

EnvelopeLegendComponent::~EnvelopeLegendComponent()
{
	deleteAllChildren();
}

EnvelopeComponent* EnvelopeLegendComponent::getEnvelopeComponent() const
{
	EnvelopeContainerComponent* parent = dynamic_cast<EnvelopeContainerComponent*> (getParentComponent());
	
	if(parent == 0) return 0;
	
	return parent->getEnvelopeComponent();
}

void EnvelopeComponent::setDahdsrMode(const bool dahdsrMode) {
	if (this->dahdsrMode == dahdsrMode)
		return;

	this->dahdsrMode = dahdsrMode;
	if (this->dahdsrMode)
	{
		ScopedStructuralEdit edit(*this);
		// DAHDSR mode is always a fixed 6-handle envelope.
		minNumHandles = kDahdsrNumHandles;
		maxNumHandles = kDahdsrNumHandles;

		if (handles.size() != kDahdsrNumHandles)
		{
			// Deterministic default shape; callers (e.g. MidiComponent) typically
			// follow up with setDahdsrParams() from processor parameters.
			clear();
			addHandle(0.0, 0.0, 0.0f);
			addHandle(0.0, 0.0, 0.0f);
			addHandle(0.0, 1.0, 0.0f);
			addHandle(0.0, 1.0, 0.0f);
			addHandle(0.0, 0.5, 0.0f);
			addHandle(0.0, 0.0, 0.0f);
		}

		applyDahdsrHandleLocks();
		repaint();
	}
	else
	{
		ScopedStructuralEdit edit(*this);
		minNumHandles = 0;
		maxNumHandles = 0xffffff;
		for (int i = 0; i < handles.size(); ++i)
		{
			auto* handle = handles.getUnchecked(i);
			handle->unlockTime();
			handle->unlockValue();
		}
		repaint();
	}
}

bool EnvelopeComponent::getDahdsrMode() const {
    return dahdsrMode;
}

void EnvelopeLegendComponent::resized()
{
	text->setBounds(0, 0, getWidth(), getHeight());
}

void EnvelopeLegendComponent::paint(juce::Graphics& g)
{
	EnvelopeComponent *env = getEnvelopeComponent();
	juce::Colour backColour = findColour(EnvelopeComponent::LegendBackground);
	
	g.setColour(backColour);
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

double EnvelopeLegendComponent::mapValue(double value)
{
	return value;
}

double EnvelopeLegendComponent::mapTime(double time)
{
	return time;
}

EnvelopeContainerComponent::EnvelopeContainerComponent(juce::String defaultText)
{
	addAndMakeVisible(legend = new EnvelopeLegendComponent(defaultText));
	addAndMakeVisible(envelope = new EnvelopeComponent());
}

EnvelopeContainerComponent::~EnvelopeContainerComponent()
{
	deleteAllChildren();
}

void EnvelopeContainerComponent::resized()
{
	int legendHeight = legend->getHeight();
	
	envelope->setBounds(0, 
						0, 
						getWidth(), 
						getHeight()-legendHeight);
	
	legend->setBounds(0, 
					  getHeight()-legendHeight, 
					  getWidth(), 
					  legend->getHeight());
}

void EnvelopeContainerComponent::setLegendComponent(EnvelopeLegendComponent* newLegend)
{
	deleteAndZero(legend);
	addAndMakeVisible(legend = newLegend);
}

#endif // gpl