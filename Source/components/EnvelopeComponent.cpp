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

static void updateIfApproxEqual(osci::FloatParameter* parameter, float newValue)
{
	if (parameter == nullptr) {
		return;
	}

	if (std::abs(parameter->getValueUnnormalised() - newValue) > 0.000001f) {
		parameter->setUnnormalisedValueNotifyingHost(newValue);
	}
}

static constexpr double kFlowTrailTauMs = 180.0;

#include <algorithm>
#include <cmath>
#include <limits>

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
}

class EnvelopeComponent::EnvelopeGridRenderer
{
public:
	explicit EnvelopeGridRenderer(EnvelopeComponent& owner) : env(owner) {}

	void invalidate() { dirty = true; }

	void resized() { dirty = true; }

	void paint(juce::Graphics& g)
	{
		const int width = env.getWidth();
		const int height = env.getHeight();
		if (width <= 0 || height <= 0)
			return;

		const bool sizeChanged = (cacheWidth != width || cacheHeight != height);
		if (sizeChanged || dirty || !cache.isValid())
		{
			cache = juce::Image(juce::Image::ARGB, width, height, true);
			cacheWidth = width;
			cacheHeight = height;
			dirty = false;

			juce::Graphics gg(cache);
			gg.setColour(env.findColour(EnvelopeComponent::Background));
			gg.fillRect(0, 0, width, height);

			if (env.gridDisplayMode & EnvelopeComponent::GridDomain)
			{
				if (width > HANDLESIZE && env.domainMax > env.domainMin)
				{
					const double domainRange = env.domainMax - env.domainMin;
					const double secondsPerPixel = domainRange / (double)(width - HANDLESIZE);
					const double targetPx = 70.0; // desired spacing (fewer markers sooner)
					const double idealStep = secondsPerPixel * targetPx;

					// Base at 8s, then quarter each time: 8s -> 2s -> 0.5s -> 0.125s ...
					constexpr double kBaseStep = 8.0;
					const double ratio = juce::jmax(idealStep / kBaseStep, 1.0e-9);
					const double level = std::log(ratio) / std::log(0.25);
					const double lowerIndex = std::floor(level);
					const double upperIndex = lowerIndex + 1.0;
					const double lowerStep = kBaseStep * std::pow(0.25, lowerIndex);
					const double upperStep = kBaseStep * std::pow(0.25, upperIndex);
					const double stepDelta = juce::jmax(1.0e-9, lowerStep - upperStep);
					const float t = (float)juce::jlimit(0.0, 1.0, (lowerStep - idealStep) / stepDelta);

					auto gridColour = env.findColour(EnvelopeComponent::GridLine);
					const float baseAlpha = 0.42f;
					const float lowerAlpha = baseAlpha;
					const float upperAlpha = baseAlpha * t;

					auto formatTimeLabel = [](double seconds) {
						if (seconds >= 1.0) {
							if (seconds >= 10.0)
								return juce::String((int)std::round(seconds)) + "s";
							return juce::String(seconds, 1) + "s";
						}
						const int ms = (int)std::round(seconds * 1000.0);
						return juce::String(ms) + "ms";
					};

					auto drawVerticalGrid = [&](double step, float alpha, double majorStep) {
						if (step <= 0.0 || alpha <= 0.001f)
							return;

						const double start = std::ceil(env.domainMin / step) * step;
						const double end = env.domainMax + (step * 0.5);
						const double pxStep = step / secondsPerPixel;
						const float lineVis = (float)juce::jlimit(0.0, 1.0, (pxStep - 40.0) / 40.0);
						const float labelVis = lineVis;

						juce::Font labelFont(11.0f, juce::Font::bold);
						const float labelY = (float)height - 2.0f - labelFont.getHeight();

						for (double domain = start; domain <= end; domain += step)
						{
							if (majorStep > 0.0) {
								const double rem = std::fmod(domain, majorStep);
								if (rem < (step * 0.001) || (majorStep - rem) < (step * 0.001))
									continue;
							}
							const float x = (float)(env.convertDomainToPixels(domain) + (HANDLESIZE * 0.5f));
							if (x < -1.0f || x > (float)width + 1.0f)
								continue;
							gg.setColour(gridColour.withAlpha(alpha * lineVis));
							gg.drawVerticalLine((int)std::round(x), 0.0f, (float)height);

							if (labelVis > 0.01f) {
								const juce::String label = formatTimeLabel(domain);
								gg.setFont(labelFont);
								const float textAlpha = juce::jlimit(0.0f, 1.0f, alpha * 1.25f * labelVis);
								gg.setColour(juce::Colours::black.withAlpha(textAlpha * 0.65f));
								gg.drawText(label, (int)x + 5, (int)labelY + 1, 70, (int)labelFont.getHeight(), juce::Justification::left);
								gg.setColour(gridColour.withAlpha(textAlpha));
								gg.drawText(label, (int)x + 4, (int)labelY, 70, (int)labelFont.getHeight(), juce::Justification::left);
							}
						}
					};

					drawVerticalGrid(lowerStep, lowerAlpha, 0.0);
					drawVerticalGrid(upperStep, upperAlpha, lowerStep);
				}
			}
		}

		g.drawImageAt(cache, 0, 0);
	}

private:
	EnvelopeComponent& env;
	juce::Image cache;
	int cacheWidth = 0;
	int cacheHeight = 0;
	bool dirty = true;
};

class EnvelopeComponent::EnvelopeFlowTrailRenderer
{
public:
	explicit EnvelopeFlowTrailRenderer(EnvelopeComponent& owner) : env(owner) {}

	bool hasTrailData() const { return !flowTrailLastSeenMs.empty(); }

	bool shouldStopRepaint(double nowMs, double tauMs)
	{
		const double maxAgeMs = tauMs * 6.0;
		if (numFlowMarkers == 0 && flowTrailNewestMs > 0.0 && (nowMs - flowTrailNewestMs) > maxAgeMs)
		{
			std::fill(flowTrailLastSeenMs.begin(), flowTrailLastSeenMs.end(), 0.0);
			flowTrailNewestMs = 0.0;
			return true;
		}
		return false;
	}

	void resized(int width)
	{
		if (width > 0)
			flowTrailLastSeenMs.assign((size_t)width, 0.0);
		flowTrailNewestMs = 0.0;
		std::fill(prevFlowMarkerValid.begin(), prevFlowMarkerValid.end(), 0);
		std::fill(prevFlowMarkerTimeSeconds.begin(), prevFlowMarkerTimeSeconds.end(), 0.0);
		std::fill(prevFlowMarkerUpdateMs.begin(), prevFlowMarkerUpdateMs.end(), 0.0);
	}

	void setFlowMarkerTimes(const double* timesSeconds, int numTimes)
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
			prevFlowMarkerUpdateMs.resize((size_t) numFlowMarkers, 0.0);
		}

		const int w = env.getWidth();
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
					prevFlowMarkerUpdateMs[(size_t) i] = 0.0;
					continue;
				}

				const float x = (float)env.convertDomainToPixels(t) + (HANDLESIZE * 0.5f);
				const int xi = (int)juce::jlimit(0.0f, (float)(w - 1), x);

				const bool canBridge = prevFlowMarkerValid[(size_t) i] != 0
					&& (t >= prevFlowMarkerTimeSeconds[(size_t) i])
					&& ((t - prevFlowMarkerTimeSeconds[(size_t) i]) <= (double)EnvelopeComponent::kFlowTrailMaxBridgeTimeSeconds);

				if (canBridge)
				{
					const double prevUpdateMs = prevFlowMarkerUpdateMs[(size_t) i] > 0.0
						? prevFlowMarkerUpdateMs[(size_t) i]
						: nowMs;
					const float prevX = prevFlowMarkerX[(size_t) i];
					const int a = (int)juce::jlimit(0.0f, (float)(w - 1), juce::jmin(prevX, x));
					const int b = (int)juce::jlimit(0.0f, (float)(w - 1), juce::jmax(prevX, x));
					// Fill between frames with interpolated timestamps (tail older -> head newer).
					const int denom = juce::jmax(1, b - a);
					for (int xx = a; xx <= b; ++xx)
					{
						const double u = (double)(xx - a) / (double)denom;
						const double tMs = prevUpdateMs + u * (nowMs - prevUpdateMs);
						flowTrailLastSeenMs[(size_t)xx] = juce::jmax(flowTrailLastSeenMs[(size_t)xx], tMs);
					}
				}
				else
				{
					flowTrailLastSeenMs[(size_t)xi] = juce::jmax(flowTrailLastSeenMs[(size_t)xi], nowMs);
				}

				prevFlowMarkerX[(size_t) i] = x;
				prevFlowMarkerValid[(size_t) i] = 1;
				prevFlowMarkerTimeSeconds[(size_t) i] = t;
				prevFlowMarkerUpdateMs[(size_t) i] = nowMs;
			}
		}
	}

	void clear()
	{
		numFlowMarkers = 0;
	}

	void reset()
	{
		numFlowMarkers = 0;
		std::fill(flowTrailLastSeenMs.begin(), flowTrailLastSeenMs.end(), 0.0);
		flowTrailNewestMs = 0.0;
		std::fill(prevFlowMarkerValid.begin(), prevFlowMarkerValid.end(), 0);
		std::fill(prevFlowMarkerTimeSeconds.begin(), prevFlowMarkerTimeSeconds.end(), 0.0);
		std::fill(prevFlowMarkerUpdateMs.begin(), prevFlowMarkerUpdateMs.end(), 0.0);
	}

	void paintTrail(juce::Graphics& g, const juce::Path& fillPath, float& outGlowStrength)
	{
		// Fixed (non-live) tuning: keep it simple and bright.
		const double tauMs = kFlowTrailTauMs; // decay time constant
		const float maxTrailAlpha = 1.0f;  // allow fully bright
		const float alphaFloor = 0.10f;    // keep it from going too dark

		juce::Graphics::ScopedSaveState state(g);
		g.reduceClipRegion(fillPath);
		const auto trailColour = env.findColour(EnvelopeComponent::Line)
			.interpolatedWith(juce::Colours::white, 0.99f);
		const int w = env.getWidth();
		if ((int)flowTrailLastSeenMs.size() == w && w > 0)
		{
			const double nowMs = juce::Time::getMillisecondCounterHiRes();
			const float invTau = 1.0f / (float) juce::jmax(1.0, tauMs);

			if (flowTrailStripWidth != w || !flowTrailStrip.isValid())
			{
				flowTrailStrip = juce::Image(juce::Image::ARGB, w, 1, true);
				flowTrailStripWidth = w;
			}

			juce::Image::BitmapData bd(flowTrailStrip, juce::Image::BitmapData::writeOnly);
			for (int x = 0; x < w; ++x)
			{
				const double lastSeen = flowTrailLastSeenMs[(size_t) x];
				if (lastSeen <= 0.0)
				{
					bd.setPixelColour(x, 0, juce::Colours::transparentBlack);
					continue;
				}
				const double ageMs = nowMs - lastSeen;
				const float a = (ageMs <= 0.0)
					? 1.0f
					: juce::jlimit(0.0f, 1.0f, 1.0f - ((float) ageMs * invTau));
				// Cheap easing to soften the tail without pow().
				const float shaped = a * (2.0f - a);
				const float finalA = juce::jlimit(0.0f, 1.0f, maxTrailAlpha * (alphaFloor + (1.0f - alphaFloor) * shaped));
				outGlowStrength = juce::jmax(outGlowStrength, finalA);
				bd.setPixelColour(x, 0, trailColour.withAlpha(finalA));
			}

			g.drawImage(flowTrailStrip,
				0, 0, w, env.getHeight(),
				0, 0, w, 1);
		}
	}

	void paintMarkerHeads(juce::Graphics& g, const juce::Path& fillPath)
	{
		if (numFlowMarkers <= 0)
			return;

		juce::Graphics::ScopedSaveState state(g);
		g.reduceClipRegion(fillPath);
		auto markerColour = env.findColour(EnvelopeComponent::LineBackground);
		for (int i = 0; i < numFlowMarkers; ++i)
		{
			const double timeSeconds = flowMarkerTimesSeconds[i];
			if (timeSeconds < 0.0)
				continue;
			const float x = (float)env.convertDomainToPixels(timeSeconds) + (HANDLESIZE * 0.5f);

			const float outerHalfWidth = 10.0f;
			juce::Rectangle<float> outer(x - outerHalfWidth, 0.0f, outerHalfWidth * 2.0f, (float)env.getHeight());
			juce::ColourGradient outerGradient(
				markerColour.withAlpha(0.0f), outer.getX(), 0.0f,
				markerColour.withAlpha(0.0f), outer.getRight(), 0.0f,
				false);
			outerGradient.addColour(0.5, markerColour.withAlpha(0.18f));
			g.setGradientFill(outerGradient);
			g.fillRect(outer);

			const float innerHalfWidth = 5.0f;
			juce::Rectangle<float> inner(x - innerHalfWidth, 0.0f, innerHalfWidth * 2.0f, (float)env.getHeight());
			juce::ColourGradient innerGradient(
				markerColour.withAlpha(0.0f), inner.getX(), 0.0f,
				markerColour.withAlpha(0.0f), inner.getRight(), 0.0f,
				false);
			innerGradient.addColour(0.5, markerColour.withAlpha(0.32f));
			g.setGradientFill(innerGradient);
			g.fillRect(inner);

			g.setColour(markerColour.withAlpha(0.60f));
			g.drawLine(x, 0.0f, x, (float)env.getHeight(), 2.5f);
		}
	}

private:
	EnvelopeComponent& env;
	int numFlowMarkers = 0;
	std::vector<double> flowMarkerTimesSeconds;
	std::vector<double> flowTrailLastSeenMs; // per-x timestamp; 0 means "never"
	std::vector<float> prevFlowMarkerX;
	std::vector<char> prevFlowMarkerValid;
	std::vector<double> prevFlowMarkerTimeSeconds;
	std::vector<double> prevFlowMarkerUpdateMs;
	double flowTrailNewestMs = 0.0;
	juce::Image flowTrailStrip;
	int flowTrailStripWidth = 0;
};

class EnvelopeComponent::EnvelopeInteraction
{
public:
	explicit EnvelopeInteraction(EnvelopeComponent& owner) : env(owner) {}

	void setHoverHandleFromChild(EnvelopeHandleComponent* handle)
	{
		isMouseOver = true;
		if (hoverHandle != handle)
		{
			hoverHandle = handle;
			env.repaint();
		}
	}

	void clearHoverHandleFromChild(EnvelopeHandleComponent* handle)
	{
		if (hoverHandle == handle)
		{
			hoverHandle = nullptr;
			env.repaint();
		}
	}

	void mouseEnter(const juce::MouseEvent& e)
	{
		isMouseOver = true;
		const auto previousHover = hoverHandle;
		const int previousCurveHover = hoverCurveHandleIndex;

		EnvelopeHandleComponent* nearestNode = nullptr;
		float nearestNodeDist = 0.0f;
		int nearestCurve = -1;
		float nearestCurveDist = 0.0f;
		getClosestTargets(e.getPosition(), nearestNode, nearestNodeDist, nearestCurve, nearestCurveDist);

		if (nearestCurve >= 1 && nearestCurveDist <= nearestNodeDist)
		{
			hoverHandle = nullptr;
			hoverCurveHandleIndex = nearestCurve;
		}
		else
		{
			hoverHandle = nearestNode;
			hoverCurveHandleIndex = -1;
		}

		if (previousHover != hoverHandle || previousCurveHover != hoverCurveHandleIndex)
			env.repaint();
	}

	void mouseMove(const juce::MouseEvent& e)
	{
		mouseEnter(e);
	}

	void mouseExit(const juce::MouseEvent& e)
	{
		juce::ignoreUnused(e);
		isMouseOver = false;
		hoverHandle = nullptr;
		hoverCurveHandleIndex = -1;
		env.repaint();
	}

	void mouseDown(const juce::MouseEvent& e)
	{
		EnvelopeHandleComponent* nearestNode = nullptr;
		float nearestNodeDist = 0.0f;
		int nearestCurve = -1;
		float nearestCurveDist = 0.0f;
		getClosestTargets(e.getPosition(), nearestNode, nearestNodeDist, nearestCurve, nearestCurveDist);

		if (nearestCurve >= 1 && nearestCurveDist <= nearestNodeDist)
		{
			auto* curveHandle = env.handles.getUnchecked(nearestCurve);
			activeCurveHandleIndex = nearestCurve;
			curveDragLastDistanceNorm = 0.0;
			env.setActiveCurveHandle(curveHandle);
			env.setActiveHandle(nullptr);
			env.setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
			env.sendStartDrag();
			return;
		}

		if (nearestNode != nullptr)
		{
			draggingHandle = nearestNode;
			env.setActiveHandle(nearestNode);
			env.setActiveCurveHandle(nullptr);
			nearestNode->mouseDown(e.getEventRelativeTo(nearestNode));
		}
	}

	void mouseDoubleClick(const juce::MouseEvent& e)
	{
		EnvelopeHandleComponent* nearestNode = nullptr;
		float nearestNodeDist = 0.0f;
		int nearestCurve = -1;
		float nearestCurveDist = 0.0f;
		getClosestTargets(e.getPosition(), nearestNode, nearestNodeDist, nearestCurve, nearestCurveDist);

		if (nearestCurve < 1 || nearestCurveDist > nearestNodeDist)
			return;

		auto* curveHandle = env.handles.getUnchecked(nearestCurve);
		env.setActiveCurveHandle(curveHandle);
		env.setActiveHandle(nullptr);
		env.sendStartDrag();
		curveHandle->setCurve(0.0f);
		env.sendChangeMessage();
		env.sendEndDrag();
		env.setActiveCurveHandle(nullptr);
		env.setMouseCursor(juce::MouseCursor::NormalCursor);
	}

	void mouseDrag(const juce::MouseEvent& e)
	{
		if (activeCurveHandleIndex >= 1 && activeCurveHandleIndex < env.handles.size())
		{
			auto* curveHandle = env.handles.getUnchecked(activeCurveHandleIndex);
			auto* prevHandle = curveHandle->getPreviousHandle();
			// Incremental drag: sensitivity depends on *current* curve value.
			// This guarantees slow control near linear (0), even if you start from an extreme curve.
			constexpr double kCurveRange = 50.0;
			constexpr double kMinSpeed = 0.22;      // slow near 0
			constexpr double kMaxSpeed = 3.2;       // fast at extremes
			constexpr double kExpK = 3.0;           // exponential shaping
			constexpr double kBase = 65.0;          // base units per full-height drag

			const double distNorm = e.getDistanceFromDragStartY() / (double)env.getHeight();
			const double dy = distNorm - curveDragLastDistanceNorm;
			curveDragLastDistanceNorm = distNorm;

			const double currentCurve = (double)curveHandle->getCurve();
			const double curviness = juce::jlimit(0.0, 1.0, std::abs(currentCurve) / kCurveRange);
			const double expNorm = (std::exp(kExpK * curviness) - 1.0) / (std::exp(kExpK) - 1.0);
			const double speed = kMinSpeed + (kMaxSpeed - kMinSpeed) * expNorm;

			double delta = dy * kBase * speed;

			// invert if segment slopes downward
			if (prevHandle != nullptr && prevHandle->getValue() > curveHandle->getValue())
				delta = -delta;

			const double next = juce::jlimit(-50.0, 50.0, currentCurve + delta);
			curveHandle->setCurve((float)next);
			return;
		}
		if (draggingHandle != nullptr) {
			draggingHandle->mouseDrag(e.getEventRelativeTo(draggingHandle));
		}
	}

	void mouseUp(const juce::MouseEvent& e)
	{
		const bool wasAdjusting = (activeCurveHandleIndex >= 1);
		activeCurveHandleIndex = -1;
		curveDragLastDistanceNorm = 0.0;

		if(draggingHandle != 0)
		{
			if(e.mods.isCtrlDown() == false)
				env.quantiseHandle(draggingHandle);
			draggingHandle->resetOffsets();
			draggingHandle = 0;
			env.sendEndDrag();
		}

		if (wasAdjusting) {
			env.sendEndDrag();
			env.setMouseCursor(juce::MouseCursor::NormalCursor);
		}
		env.setActiveCurveHandle(nullptr);
	}

	bool isCurveHandleIndex(int handleIndex) const
	{
		// Curves only for Attack (2), Decay (4), Release (5)
		return handleIndex == 2 || handleIndex == 4 || handleIndex == 5;
	}

	juce::Point<float> getCurveHandlePosition(int handleIndex) const
	{
		if (handleIndex <= 0 || handleIndex >= env.handles.size())
			return {};
		if (!isCurveHandleIndex(handleIndex))
			return {};

		const double t0 = env.handles.getUnchecked(handleIndex - 1)->getTime();
		const double t1 = env.handles.getUnchecked(handleIndex)->getTime();
		const double midTime = (t0 + t1) * 0.5;
		const float value = env.lookup((float)midTime);
		const float x = (float)env.convertDomainToPixels(midTime) + (HANDLESIZE * 0.5f);
		const float y = (float)env.convertValueToPixels(value) + (HANDLESIZE * 0.5f);
		return { x, y };
	}

	bool getHoverRing(juce::Point<float>& outCenter, float& outRadius, bool& outIsCurve) const
	{
		outCenter = {};
		outRadius = 0.0f;
		outIsCurve = false;

		if (hoverCurveHandleIndex >= 1)
		{
			outCenter = getCurveHandlePosition(hoverCurveHandleIndex);
			outRadius = EnvelopeComponent::kHoverRingRadius;
			outIsCurve = true;
			return true;
		}

		if (hoverHandle != nullptr)
		{
			const auto bounds = hoverHandle->getBoundsInParent().toFloat();
			outCenter = bounds.getCentre();
			outRadius = EnvelopeComponent::kHoverRingRadius;
			outIsCurve = false;
			return true;
		}

		return false;
	}

	EnvelopeHandleComponent* getHoverHandle() const { return hoverHandle; }
	int getHoverCurveHandleIndex() const { return hoverCurveHandleIndex; }
	EnvelopeHandleComponent* getDraggingHandle() const { return draggingHandle; }
	int getActiveCurveHandleIndex() const { return activeCurveHandleIndex; }
	bool isMouseOverComponent() const { return isMouseOver; }

private:
	EnvelopeHandleComponent* findClosestHandle(juce::Point<int> pos) const
	{
		EnvelopeHandleComponent* closest = nullptr;
		float bestDist = std::numeric_limits<float>::max();
		constexpr float kSwitchThresholdPx = 5.0f;
		// Iterate right-to-left so "right-most" wins when nearly overlapping.
		for (int i = env.handles.size() - 1; i >= 1; --i)
		{
			auto* handle = env.handles.getUnchecked(i);
			const auto bounds = handle->getBoundsInParent();
			const auto center = bounds.getCentre().toFloat();
			const float d = center.getDistanceFrom(pos.toFloat());
			if (closest == nullptr || d < bestDist - kSwitchThresholdPx)
			{
				bestDist = d;
				closest = handle;
			}
		}
		return closest;
	}

	int findClosestCurveHandleIndex(juce::Point<int> pos) const
	{
		int closestIndex = -1;
		float bestDist = std::numeric_limits<float>::max();
		for (int i = 1; i < env.handles.size(); ++i)
		{
			if (!isCurveHandleIndex(i))
				continue;
			const auto center = getCurveHandlePosition(i);
			const float d = center.getDistanceFrom(pos.toFloat());
			if (d < bestDist)
			{
				bestDist = d;
				closestIndex = i;
			}
		}
		return closestIndex;
	}

	void getClosestTargets(juce::Point<int> pos,
		EnvelopeHandleComponent*& outNode,
		float& outNodeDist,
		int& outCurveIndex,
		float& outCurveDist) const
	{
		outNode = findClosestHandle(pos);
		outNodeDist = std::numeric_limits<float>::max();
		if (outNode != nullptr)
			outNodeDist = outNode->getBoundsInParent().getCentre().toFloat().getDistanceFrom(pos.toFloat());

		outCurveIndex = findClosestCurveHandleIndex(pos);
		outCurveDist = std::numeric_limits<float>::max();
		if (outCurveIndex >= 1)
			outCurveDist = getCurveHandlePosition(outCurveIndex).getDistanceFrom(pos.toFloat());
	}

	EnvelopeComponent& env;
	EnvelopeHandleComponent* draggingHandle = nullptr;
	EnvelopeHandleComponent* hoverHandle = nullptr;
	int hoverCurveHandleIndex = -1;
	int activeCurveHandleIndex = -1;
	bool isMouseOver = false;
	double curveDragLastDistanceNorm = 0.0;
};


EnvelopeHandleComponent::EnvelopeHandleComponent()
	: dontUpdateTimeAndValue(false),
	  lastX(-1),
	  lastY(-1),
	  shouldLockTime(false),
	  shouldLockValue(false),
	  shouldDraw(!shouldLockTime || !shouldLockValue),
	  curve(0.0f),
	  ignoreDrag(false)
{
	setRepaintsOnMouseActivity(true);

	if (shouldDraw) {
	}
	resetOffsets();
}

EnvelopeComponent* EnvelopeHandleComponent::getParentComponent() const
{
	return (EnvelopeComponent*)Component::getParentComponent();
}

void EnvelopeHandleComponent::updateTimeAndValue()
{
	bool envChanged = false;
	EnvelopeComponent* env = getParentComponent();

	if (shouldLockTime)
	{
		setTopLeftPosition(env->convertDomainToPixels(time),
					   getY());
	}
	else {
		envChanged = true;
		time = env->convertPixelsToDomain(getX());
	}

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
		env->sendChangeMessage();
	}

#ifdef MYDEBUG
	printf("MyEnvelopeHandleComponent::updateTimeAndValue(%f, %f)\n", time, value);
#endif
}

void EnvelopeHandleComponent::updateLegend()
{
	EnvelopeComponent *env = getParentComponent();
	EnvelopeLegendComponent* legend = env->getLegend();

	if (legend == 0) return;

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

	int index = env->getHandleIndex(this);
	const auto dahdsr = env->getDahdsrParams();
	const std::array<double, kDahdsrHandles> safeTimes = {
		0.0,
		dahdsr.delaySeconds,
		dahdsr.attackSeconds,
		dahdsr.holdSeconds,
		dahdsr.decaySeconds,
		dahdsr.releaseSeconds
	};

	if (index > 0 && index < (int)kLegendLabels.size())
	{
		text = kLegendLabels[(size_t)index] + juce::String(legend->mapTime(safeTimes[(size_t)index]), places);
		if (index == 4)
			text << ", Sustain level: " << juce::String(legend->mapValue(dahdsr.sustainLevel), places);
	}
	else
	{
		text = "";
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
		const float scale = hot ? 0.78f : 0.72f;
		const float size = (float)getWidth() * scale;
		const float inset = ((float)getWidth() - size) * 0.5f;

		g.setColour(handleColour.withAlpha(hot ? 1.0f : 0.95f));
		g.fillEllipse(inset, inset, size, size);
		g.setColour(findColour(EnvelopeComponent::NodeOutline).withAlpha(hot ? 1.0f : 0.85f));
		g.drawEllipse(inset, inset, size, size, 1.75f);
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
	
	if (auto* env = getParentComponent())
		env->setHoverHandleFromChild(this);

	if (shouldDraw) {
		updateLegend();
    }
}

void EnvelopeHandleComponent::mouseExit(const juce::MouseEvent& e)
{
	(void)e;
#ifdef MYDEBUG
	printf("MyEnvelopeHandleComponent::mouseExit\n");
#endif
	if (auto* env = getParentComponent())
		env->clearHoverHandleFromChild(this);
	
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
		else 
		{

			offsetX = e.x;
			offsetY = e.y;

			dragger.startDraggingComponent(this, e);

		}

		getParentComponent()->setActiveHandle(this);
		getParentComponent()->setActiveCurveHandle(nullptr);
		getParentComponent()->sendStartDrag();
	}
}

void EnvelopeHandleComponent::mouseDrag(const juce::MouseEvent& e)
{
	if(ignoreDrag || !shouldDraw) return;
	
	dragger.dragComponent(this, e, nullptr);

	const int maxX = getParentWidth() - HANDLESIZE;
	const int maxY = getParentHeight() - HANDLESIZE;
	const int minX = [&]() {
		if (auto* prev = getPreviousHandle()) {
			return prev->getX();
		}
		return 0;
	}();
	const int clampedX = juce::jlimit(minX, juce::jmax(minX, maxX), getX());
	const int clampedY = juce::jlimit(0, juce::jmax(0, maxY), getY());
	setTopLeftPosition(clampedX, clampedY);
	
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
	value = constrainValue(valueToSet);
	time = constrainDomain(timeToSet);
	
	setTopLeftPosition(getParentComponent()->convertDomainToPixels(time), 
					   getParentComponent()->convertValueToPixels(value));
	
	dontUpdateTimeAndValue = oldDontUpdateTimeAndValue;
	
	getParentComponent()->repaint();
}

void EnvelopeHandleComponent::setTimeAndValueUnclamped(double timeToSet, double valueToSet)
{
	bool oldDontUpdateTimeAndValue = dontUpdateTimeAndValue;
	dontUpdateTimeAndValue = true;

	time = timeToSet;
	value = valueToSet;

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
	const int leftLimit = 0;
	const int rightLimit = getParentWidth() - HANDLESIZE;

	const double left = getParentComponent()->convertPixelsToDomain(leftLimit);
	const double right = getParentComponent()->convertPixelsToDomain(rightLimit);

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


EnvelopeComponent::EnvelopeComponent(OscirenderAudioProcessor& processorToUse)
	: minNumHandles(kDahdsrNumHandles),
	  maxNumHandles(kDahdsrNumHandles),
	  domainMin(0.0),
	  domainMax(1.0),
	  valueMin(0.0),
	  valueMax(1.0),
	  valueGrid((valueMax - valueMin) / 10.0),
	  domainGrid((domainMax - domainMin) / 16.0),
	  gridDisplayMode(GridNone),
	  gridQuantiseMode(GridNone),
	  activeHandle(nullptr),
	  activeCurveHandle(nullptr),
	  curvePoints(64),
	  processor(processorToUse)
{
	stopTimer();
	setBounds(0, 0, 200, 200); // non-zero size to start with
	gridRenderer = std::make_unique<EnvelopeGridRenderer>(*this);
	flowTrailRenderer = std::make_unique<EnvelopeFlowTrailRenderer>(*this);
	interaction = std::make_unique<EnvelopeInteraction>(*this);

	timeParams = { nullptr, processor.delayTime, processor.attackTime, processor.holdTime, processor.decayTime, processor.releaseTime };
	valueParams = { nullptr, nullptr, nullptr, nullptr, processor.sustainLevel, nullptr };
	curveParams = { nullptr, nullptr, processor.attackShape, nullptr, processor.decayShape, processor.releaseShape };

	DahdsrParams params;
	params.delaySeconds = processor.delayTime->getValueUnnormalised();
	params.attackSeconds = processor.attackTime->getValueUnnormalised();
	params.holdSeconds = processor.holdTime->getValueUnnormalised();
	params.decaySeconds = processor.decayTime->getValueUnnormalised();
	params.sustainLevel = processor.sustainLevel->getValueUnnormalised();
	params.releaseSeconds = processor.releaseTime->getValueUnnormalised();
	params.attackCurve = processor.attackShape->getValueUnnormalised();
	params.decayCurve = processor.decayShape->getValueUnnormalised();
	params.releaseCurve = processor.releaseShape->getValueUnnormalised();
	setDahdsrParams(params);
}

void EnvelopeComponent::timerCallback()
{
	// Self-driven repaint so the flow trail can decay smoothly even when the host/UI stops pushing updates.
	if (!flowTrailRenderer || !flowTrailRenderer->hasTrailData())
	{
		stopTimer();
		return;
	}

	const double nowMs = juce::Time::getMillisecondCounterHiRes();
	const double tauMs = juce::jmax(1.0, kFlowTrailTauMs);
	if (flowTrailRenderer->shouldStopRepaint(nowMs, tauMs))
	{
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
		if (gridRenderer)
			gridRenderer->invalidate();
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
		if (gridRenderer)
			gridRenderer->invalidate();
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
	// Clear any previous constraints first.
	for (int i = 0; i < handles.size(); ++i)
	{
		auto* handle = handles.getUnchecked(i);
		handle->unlockTime();
		handle->unlockValue();
	}

	if (handles.size() == 0)
		return;

	// DAHDSR (6 points): start 0, delay end 0, peak 1, hold end 1, sustain editable, release end 0.
	if (handles.size() == kDahdsrHandles)
	{
		for (int i = 0; i < handles.size(); ++i)
		{
			auto* handle = handles.getUnchecked(i);
			if (i == 0)
				handle->lockTime(0.0);
			const double lockedValue = kLockedValues[(size_t)i];
			if (std::isfinite(lockedValue))
				handle->lockValue(lockedValue);
		}
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

void EnvelopeComponent::setHoverHandleFromChild(EnvelopeHandleComponent* handle)
{
	if (interaction)
		interaction->setHoverHandleFromChild(handle);
}

void EnvelopeComponent::clearHoverHandleFromChild(EnvelopeHandleComponent* handle)
{
	if (interaction)
		interaction->clearHoverHandleFromChild(handle);
}

void EnvelopeComponent::setGrid(const GridMode display, const GridMode quantise, const double domainQ, const double valueQ)
{
	if(quantise != GridLeaveUnchanged)
		gridQuantiseMode = quantise;
	
	if((display != GridLeaveUnchanged) && (display != gridDisplayMode))
	{
		gridDisplayMode = display;
		if (gridRenderer)
			gridRenderer->invalidate();
		repaint();
	}
	
	if((domainQ > 0.0) && (domainQ != domainGrid))
	{
		domainGrid = domainQ;
		if (gridRenderer)
			gridRenderer->invalidate();
		repaint();
	}
	
	if((valueQ > 0.0) && (valueQ != valueGrid))
	{
		valueGrid = valueQ;
		if (gridRenderer)
			gridRenderer->invalidate();
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
			// Adaptive sampling: longer segments and more extreme curve values get more points.
			const float x0 = (float) convertDomainToPixels(time);
			const float x1 = (float) convertDomainToPixels(nextTime);
			const float dxPx = std::abs(x1 - x0);
			const float curveAmount = std::abs(handle->getCurve()); // 0..~50
			const float curveFactor = 1.0f + (curveAmount / 20.0f); // 1..~3.5
			const int steps = juce::jlimit(32, 900, (int) std::round(juce::jmax(1.0f, dxPx) * 0.45f * curveFactor));
			const float timeInc = handleTime / (float) steps;
			
			for(int j = 0; j < steps; j++)
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

		float trailGlowStrength = 0.0f;
		if (flowTrailRenderer)
			flowTrailRenderer->paintTrail(g, fillPath, trailGlowStrength);

		// Use trail strength to gently brighten/glow the curve.
		if (trailGlowStrength > 0.01f)
		{
			const float glow = juce::jlimit(0.0f, 1.0f, trailGlowStrength);
			const float glowThickness = kCurveStrokeThickness + (2.0f * glow);
			g.setColour(findColour(Line).withAlpha(0.25f + 0.55f * glow));
			g.strokePath(path, juce::PathStrokeType(glowThickness));
		}

		if (flowTrailRenderer)
			flowTrailRenderer->paintMarkerHeads(g, fillPath);

		// Curve stroke on top
		g.setColour(findColour(Line).withAlpha(0.90f));
		g.strokePath(path, juce::PathStrokeType(kCurveStrokeThickness));
	}
}

void EnvelopeComponent::paintOverChildren(juce::Graphics& g)
{
	if (handles.size() == 0)
		return;
	if (!interaction)
		return;

	const auto lineColour = findColour(Line);
	const auto outlineColour = findColour(NodeOutline);

	// Curve shape handles (one per segment)
	for (int i = 1; i < handles.size(); ++i)
	{
		if (!interaction->isCurveHandleIndex(i))
			continue;
		const auto center = interaction->getCurveHandlePosition(i);
		const float r = kCurveHandleRadius;
		g.setColour(lineColour.withAlpha(0.95f));
		g.fillEllipse(center.x - r, center.y - r, r * 2.0f, r * 2.0f);
		g.setColour(outlineColour.withAlpha(0.9f));
		g.drawEllipse(center.x - r, center.y - r, r * 2.0f, r * 2.0f, kHandleOutlineThickness);
	}

	// White ring around closest handle (position or curve)
	if (interaction->isMouseOverComponent())
	{
		juce::Point<float> center;
		float ringR = 0.0f;
		bool isCurve = false;
		if (interaction->getHoverRing(center, ringR, isCurve))
		{
			g.setColour(juce::Colours::white.withAlpha(kHoverRingAlpha));
			g.drawEllipse(center.x - ringR, center.y - ringR, ringR * 2.0f, ringR * 2.0f, kHoverRingThickness);

			const bool isDraggingThis = (isCurve && interaction->getActiveCurveHandleIndex() == interaction->getHoverCurveHandleIndex())
				|| (!isCurve && interaction->getDraggingHandle() == interaction->getHoverHandle());
			if (isDraggingThis)
			{
				// Slightly larger outer ring, but keep strokes overlapping (no gap).
				const float outerR = ringR + 2.0f;
				g.setColour(juce::Colours::white.withAlpha(kHoverOuterRingAlpha));
				g.drawEllipse(center.x - outerR, center.y - outerR, outerR * 2.0f, outerR * 2.0f, kHoverOuterRingThickness);
			}
		}
	}
}

static double roundToMultiple(double a, double b) noexcept
{
	double offset = a < 0 ? -0.5 : 0.5;
	int n = (int)(a / b + offset);
	return b * (double)n;
}

void EnvelopeComponent::paintBackground(juce::Graphics& g)
{
	if (gridRenderer)
		gridRenderer->paint(g);
}

void EnvelopeComponent::setFlowMarkerTimesForUi(const double* timesSeconds, int numTimes)
{
	if (flowTrailRenderer)
		flowTrailRenderer->setFlowMarkerTimes(timesSeconds, numTimes);
	ensureFlowRepaintTimerRunning();
	repaint();
}

void EnvelopeComponent::clearFlowMarkerTimesForUi()
{
	if (flowTrailRenderer)
		flowTrailRenderer->clear();
	ensureFlowRepaintTimerRunning();
	repaint();
}

void EnvelopeComponent::resetFlowPersistenceForUi()
{
	if (flowTrailRenderer)
		flowTrailRenderer->reset();
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
	{
		if (flowTrailRenderer)
			flowTrailRenderer->resized(getWidth());
	}
	if (gridRenderer)
		gridRenderer->resized();
}


void EnvelopeComponent::mouseEnter(const juce::MouseEvent& e) 
{
	if (interaction)
		interaction->mouseEnter(e);
}

void EnvelopeComponent::mouseMove(const juce::MouseEvent& e) 
{
	if (interaction)
		interaction->mouseMove(e);
}

void EnvelopeComponent::mouseExit(const juce::MouseEvent& e)
{
	if (interaction)
		interaction->mouseExit(e);
}

void EnvelopeComponent::lookAndFeelChanged()
{
	if (gridRenderer)
		gridRenderer->invalidate();
	repaint();
}

void EnvelopeComponent::mouseDown(const juce::MouseEvent& e)
{
#ifdef MYDEBUG
	printf("MyEnvelopeComponent::mouseDown(%d, %d)\n", e.x, e.y);
#endif
	if (interaction)
		interaction->mouseDown(e);
}

void EnvelopeComponent::mouseDoubleClick(const juce::MouseEvent& e)
{
	if (interaction)
		interaction->mouseDoubleClick(e);
}

void EnvelopeComponent::mouseDrag(const juce::MouseEvent& e)
{
#ifdef MYDEBUG
	printf("MyEnvelopeComponent::mouseDrag(%d, %d)\n", e.x, e.y);
#endif
	if (interaction)
		interaction->mouseDrag(e);
}

void EnvelopeComponent::mouseUp(const juce::MouseEvent& e)
{
#ifdef MYDEBUG
	printf("MyEnvelopeComponent::mouseUp\n");
#endif
	if (interaction)
		interaction->mouseUp(e);
}

void EnvelopeComponent::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
	juce::ignoreUnused(e);

	if (wheel.deltaY == 0.0f)
		return;

	const double minDomain = osci_audio::kEnvelopeZoomMinSeconds;
	const double maxDomain = osci_audio::kEnvelopeZoomMaxSeconds;
	const double base = 0.98;
	const double zoomFactor = std::pow(base, wheel.deltaY * 20.0);

	const double newMax = juce::jlimit(minDomain, maxDomain, domainMax * zoomFactor);
	setDomainRange(0.0, newMax);
}

void EnvelopeComponent::setActiveHandle(EnvelopeHandleComponent* handle)
{
	activeHandle = handle;
}

void EnvelopeComponent::setActiveCurveHandle(EnvelopeHandleComponent* handle)
{
	activeCurveHandle = handle;
}

void EnvelopeComponent::sendChangeMessage()
{
	pushParamsForActiveHandle();
}

void EnvelopeComponent::sendStartDrag()
{
	beginGestureForActiveHandle();
}

void EnvelopeComponent::sendEndDrag()
{
	endGestureForActiveHandle();
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
		if (i == 0) {
			handle->lockTime(0);
		}
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
		double domain = roundToMultiple(thisHandle->getTime(), domainGrid);
		thisHandle->setTime(domain);
		
	}
	
	if((gridQuantiseMode & GridValue) && (valueGrid > 0.0))
	{
		double value = roundToMultiple(thisHandle->getValue(), valueGrid);
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

	const double delay = juce::jmax(0.0, params.delaySeconds);
	const double attack = juce::jmax(0.0, params.attackSeconds);
	const double hold = juce::jmax(0.0, params.holdSeconds);
	const double decay = juce::jmax(0.0, params.decaySeconds);
	const double release = juce::jmax(0.0, params.releaseSeconds);
	const double sustain = juce::jlimit(0.0, 1.0, params.sustainLevel);

	const std::array<double, kDahdsrHandles> handleTimes = {
		0.0,
		delay,
		delay + attack,
		delay + attack + hold,
		delay + attack + hold + decay,
		delay + attack + hold + decay + release
	};
	const std::array<double, kDahdsrHandles> handleValues = {
		0.0,
		0.0,
		1.0,
		1.0,
		sustain,
		0.0
	};
	const std::array<float, kDahdsrHandles> handleCurves = {
		0.0f,
		0.0f,
		params.attackCurve,
		0.0f,
		params.decayCurve,
		params.releaseCurve
	};

	for (int i = 0; i < handles.size(); ++i)
	{
		handles.getUnchecked(i)->setTimeAndValueUnclamped(handleTimes[(size_t)i], handleValues[(size_t)i]);
		handles.getUnchecked(i)->setCurve(handleCurves[(size_t)i]);
	}

	applyDahdsrHandleLocks();
	repaint();
}

void EnvelopeComponent::beginGestureForActiveHandle()
{
	if (auto* curveHandle = getActiveCurveHandle())
	{
		const int curveHandleIndex = getHandleIndex(curveHandle);
		withParamAtIndex(curveParams, curveHandleIndex, [](auto* param) { param->beginChangeGesture(); });
		return;
	}

	if (auto* handle = getActiveHandle())
	{
		const int handleIndex = getHandleIndex(handle);
		withParamAtIndex(timeParams, handleIndex, [](auto* param) { param->beginChangeGesture(); });
		withParamAtIndex(valueParams, handleIndex, [](auto* param) { param->beginChangeGesture(); });
	}
}

void EnvelopeComponent::endGestureForActiveHandle()
{
	if (auto* curveHandle = getActiveCurveHandle())
	{
		const int curveHandleIndex = getHandleIndex(curveHandle);
		withParamAtIndex(curveParams, curveHandleIndex, [](auto* param) { param->endChangeGesture(); });
		return;
	}

	if (auto* handle = getActiveHandle())
	{
		const int handleIndex = getHandleIndex(handle);
		withParamAtIndex(timeParams, handleIndex, [](auto* param) { param->endChangeGesture(); });
		withParamAtIndex(valueParams, handleIndex, [](auto* param) { param->endChangeGesture(); });
	}
}

void EnvelopeComponent::pushParamsForActiveHandle()
{
	const auto* curveHandle = getActiveCurveHandle();
	const auto* handle = getActiveHandle();
	if (curveHandle == nullptr && handle == nullptr) {
		return;
	}

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
		0.0f,
		0.0f,
		0.0f,
		0.0f,
		(float)params.sustainLevel,
		0.0f
	};
	const std::array<float, kDahdsrNumHandles> curveValues = {
		0.0f,
		0.0f,
		params.attackCurve,
		0.0f,
		params.decayCurve,
		params.releaseCurve
	};

	if (curveHandle != nullptr)
	{
		const int curveHandleIndex = getHandleIndex(const_cast<EnvelopeHandleComponent*>(curveHandle));
		withParamAtIndex(curveParams, curveHandleIndex, [&](auto* param) {
			updateIfApproxEqual(param, curveValues[(size_t)curveHandleIndex]);
		});
		return;
	}

	if (handle != nullptr)
	{
		const int handleIndex = getHandleIndex(const_cast<EnvelopeHandleComponent*>(handle));
		withParamAtIndex(timeParams, handleIndex, [&](auto* param) {
			updateIfApproxEqual(param, timeValues[(size_t)handleIndex]);
		});
		withParamAtIndex(valueParams, handleIndex, [&](auto* param) {
			updateIfApproxEqual(param, valueValues[(size_t)handleIndex]);
		});
	}
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

			if (tRel < d0)
				return 0.0f;
			if (tRel < d1)
				return osci_audio::evalSegment(0.0f, 1.0f, tRel - d0, (float) p.attackSeconds, p.attackCurve);
			if (tRel < d2)
				return 1.0f;
			if (tRel < d3)
				return osci_audio::evalSegment(1.0f, (float) p.sustainLevel, tRel - d2, (float) p.decaySeconds, p.decayCurve);
			if (tRel < d4)
				return osci_audio::evalSegment((float) p.sustainLevel, 0.0f, tRel - d3, (float) p.releaseSeconds, p.releaseCurve);
			return 0.0f;
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

EnvelopeContainerComponent::EnvelopeContainerComponent(OscirenderAudioProcessor& processor, juce::String defaultText)
{
	addAndMakeVisible(legend = new EnvelopeLegendComponent(defaultText));
	addAndMakeVisible(envelope = new EnvelopeComponent(processor));
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

#endif // gpl