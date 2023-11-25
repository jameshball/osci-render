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

#include "Env.h"

Env::Env(std::vector<double> levels, 
	std::vector<double> times, 
	EnvCurveList const& curves, 
	const int releaseNode, 
	const int loopNode) throw()
	:	levels_(levels),
	times_(times),
	curves_(curves),
	releaseNode_(releaseNode),
	loopNode_(loopNode)
{
}

Env& Env::operator=(Env const& other) throw() {
	if (this != &other) {
        levels_ = other.levels_;
        times_ = other.times_;
        curves_ = other.curves_;
        releaseNode_ = other.releaseNode_;
        loopNode_ = other.loopNode_;
    }
    return *this;
}

bool Env::operator==(const Env& other) const throw() {
	// approximate comparison
	bool levelsEqual = true;
	for (int i = 0; i < levels_.size(); i++) {
        if (std::abs(levels_[i] - other.levels_[i]) > 0.001) {
            levelsEqual = false;
            break;
        }
    }

	// approximate comparison
	bool timesEqual = true;
	for (int i = 0; i < times_.size(); i++) {
        if (std::abs(times_[i] - other.times_[i]) > 0.001) {
            timesEqual = false;
            break;
        }
    }

	// approximate comparison
	bool curvesEqual = true;
	for (int i = 0; i < curves_.size(); i++) {
        if (std::abs(curves_[i].getCurve() - other.curves_[i].getCurve()) > 0.001) {
            curvesEqual = false;
            break;
        }
    }

	return levelsEqual && timesEqual && curvesEqual;
}

Env::~Env() throw() {}

double Env::duration() const throw() {
	return std::reduce(times_.begin(), times_.end());
}

Env Env::levelScale(const double scale) const throw()
{
	std::vector<double> newLevels;
	for (auto level : levels_) {
        newLevels.push_back(level * scale);
    }
	return Env(newLevels,
		times_, 
		curves_,
		releaseNode_,
		loopNode_);
}

Env Env::levelBias(const double bias) const throw()
{
	std::vector<double> newLevels;
	for (auto level : levels_) {
        newLevels.push_back(level + bias);
    }
	return Env(newLevels,
		times_, 
		curves_,
		releaseNode_,
		loopNode_);
}

Env Env::timeScale(const double scale) const throw()
{
	std::vector<double> newTimes;
    for (auto time : times_) {
        newTimes.push_back(time * scale);
    }
	return Env(levels_,
		newTimes, 
		curves_,
		releaseNode_,
		loopNode_);
}

inline double linlin(const double input, 
	const double inLow, const double inHigh,
	const double outLow, const double outHigh) throw()
{
	double inRange = inHigh - inLow;
	double outRange = outHigh - outLow;
	return (input - inLow) * outRange / inRange + outLow;
}

inline double linexp(const double input, 
	const double inLow, const double inHigh,
	const double outLow, const double outHigh) throw()
{
	double outRatio = outHigh / outLow;
	double reciprocalInRange = 1.0 / (inHigh - inLow);
	double negInLowOverInRange = reciprocalInRange * -inLow;
	return outLow * pow(outRatio, input * reciprocalInRange + negInLowOverInRange);	
}

inline double linsin(const double input, 
	const double inLow, const double inHigh,
	const double outLow, const double outHigh) throw()
{
	double inRange = inHigh - inLow;
	double outRange = outHigh - outLow;

	double inPhase = (input - inLow) * std::numbers::pi / inRange + std::numbers::pi;
#ifndef UGEN_ANDROID
	double cosInPhase = std::cos(inPhase) * 0.5 + 0.5;
#else
	double cosInPhase = cos(inPhase) * 0.5 + 0.5;
#endif

	return cosInPhase * outRange + outLow;	
}

inline float linwelch(const float input, 
	const float inLow, const float inHigh,
	const float outLow, const float outHigh) throw()
{
	float inRange = inHigh - inLow;
	float outRange = outHigh - outLow;
	float inPos = (input - inLow) / inRange;

#ifndef UGEN_ANDROID
	if (outLow < outHigh)
		return outLow + outRange * std::sin(std::numbers::pi * inPos / 2);
	else
		return outHigh - outRange * std::sin(std::numbers::pi / 2 - std::numbers::pi * inPos / 2);
#else
	if (outLow < outHigh)
		return outLow + outRange * sinf(piOverTwo * inPos);
	else
		return outHigh - outRange * sinf(piOverTwo - piOverTwo * inPos);	
#endif
}

float Env::lookup(float time) const throw()
{
	const int numTimes = times_.size();
	const int numLevels = levels_.size();
	const int lastLevel = numLevels-1;

	if(numLevels < 1) return 0.f;
	if(time <= 0.f || numTimes == 0) return levels_[0];

	float lastTime = 0.f;
	float stageTime = 0.f;
	int stageIndex = 0;

	while(stageTime < time && stageIndex < numTimes)
	{
		lastTime = stageTime;
		stageTime += times_[stageIndex];
		stageIndex++;
	}

	if(stageIndex > numTimes) return levels_[lastLevel];

	float level0 = levels_[stageIndex-1];
	float level1 = levels_[stageIndex];

	int curveIndex = (stageIndex - 1) % curves_.size();

	EnvCurve::CurveType type = curves_.data[curveIndex].getType();
	float curveValue = curves_.data[curveIndex].getCurve();

	if((lastTime - stageTime)==0.f)
	{
		return level1;
	}
	else if(type == EnvCurve::Linear)
	{
		return linlin(time, lastTime, stageTime, level0, level1);
	}
	else if(type == EnvCurve::Numerical)
	{
		if(abs(curveValue) <= 0.001)
		{
			return linlin(time, lastTime, stageTime, level0, level1);
		}
		else
		{			
			float pos = (time-lastTime) / (stageTime-lastTime);
#ifndef UGEN_ANDROID
			float denom = 1.f - std::exp(curveValue);
			float numer = 1.f - std::exp(pos * curveValue);
#else
			// android
			float denom = 1.f - expf(curveValue);
			float numer = 1.f - expf(pos * curveValue);
#endif
			return level0 + (level1 - level0) * (numer/denom);
		}
	}
	else if(type == EnvCurve::Sine)
	{
		return linsin(time, lastTime, stageTime, level0, level1);
	}
	else if(type == EnvCurve::Exponential)
	{
		return linexp(time, lastTime, stageTime, level0, level1);
	}
	else if(type == EnvCurve::Welch)
	{
		return linwelch(time, lastTime, stageTime, level0, level1);
	}
	else
	{
		// Empty or Step
		return level1;
	}
}

Env Env::linen(const double attackTime, 
	const double sustainTime, 
	const double releaseTime, 
	const double sustainLevel,
	EnvCurve const& curve) throw()
{
	return Env({ 0.0, sustainLevel, sustainLevel, 0.0 },
		{ attackTime, sustainTime, releaseTime },
		curve);
}

Env Env::triangle(const double duration,
	const double level) throw() {
	const double durationHalved = duration * 0.5;
	return Env({ 0.0, level, 0.0 },
		{ durationHalved, durationHalved });
}

Env Env::sine(const double duration, 
	const double level) throw()
{
	const double durationHalved = duration * 0.5;
	return Env({ 0.0, level, 0.0 },
		{ durationHalved, durationHalved },
		EnvCurve::Sine);	
}

Env Env::perc(const double attackTime, 
	const double releaseTime, 
	const double level, 
	EnvCurve const& curve) throw()
{
	return Env({ 0.0, level, 0.0 },
		{ attackTime, releaseTime },
		curve);	
}

Env Env::adsr(const double attackTime, 
	const double decayTime, 
	const double sustainLevel, 
	const double releaseTime,
	const double level, 
	EnvCurveList const& curves) throw()
{
	return Env({ 0.0, level, (level * sustainLevel), 0.0 },
		{ attackTime, decayTime, releaseTime },
		curves, 2);
}

Env Env::asr(const double attackTime, 
	const double sustainLevel, 
	const double releaseTime, 
	const double level, 
	EnvCurve const& curve) throw()
{
	return Env({ 0.0, (level * sustainLevel), 0.0 },
		{ attackTime, releaseTime },
		curve, 1);
}

#endif // gpl