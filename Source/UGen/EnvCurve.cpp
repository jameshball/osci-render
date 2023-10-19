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

#include "EnvCurve.h"

EnvCurveList::EnvCurveList(EnvCurve const& i00) throw() {
	data.push_back(i00);
}

EnvCurveList::EnvCurveList(std::vector<EnvCurve> curves) throw() {
	data = curves;
}

EnvCurveList::EnvCurveList(EnvCurve::CurveType type, const int size) throw(){
	for(int i = 0; i < size; i++)
		data.push_back(EnvCurve(type));
}

EnvCurveList::EnvCurveList(const double curve, const int size) throw()
{
	for(int i = 0; i < size; i++)
		data.push_back(EnvCurve(curve));
}

EnvCurveList::~EnvCurveList() throw() {}

EnvCurveList::EnvCurveList(EnvCurveList const& copy) throw() : data(copy.data) {}

EnvCurveList& EnvCurveList::operator= (EnvCurveList const& other) throw()
{
	if (this != &other) {		
		data = other.data;
	}

	return *this;
}

EnvCurve& EnvCurveList::operator[] (const int index) throw() {
	return data[index % size()];
}

const EnvCurve& EnvCurveList::operator[] (const int index) const throw() {
	return data[index % size()];
}

EnvCurveList EnvCurveList::blend(EnvCurveList const& other, float fraction) const throw() {
	const int maxSize = juce::jmax(size(), other.size());
	const int minSize = juce::jmin(size(), other.size());
	fraction = juce::jmax(0.f, fraction);
	fraction = juce::jmin(1.f, fraction);

	EnvCurveList newList(0.0, maxSize);

	for(int i = 0; i < minSize; i++)
	{
		EnvCurve thisCurve = data[i];
		EnvCurve otherCurve = other.data[i];

		if((thisCurve.getType() == EnvCurve::Numerical) && (otherCurve.getType() == EnvCurve::Numerical))
		{
			float thisValue = thisCurve.getCurve();
			float otherValue = otherCurve.getCurve();
			float newValue = thisValue * (1.f-fraction) + otherValue * fraction;
			newList.data[i] = EnvCurve(newValue);
		}
		else
		{
			if(fraction < 0.5f)
				newList.data[i] = thisCurve;
			else
				newList.data[i] = otherCurve;
		}
	}

	// should really turn this loop "inside out" save doing the if() for each iteration
	for(int i = minSize; i < maxSize; i++)
	{
		EnvCurve thisCurve = data[i];
		EnvCurve otherCurve = other.data[i];

		if(size() < other.size())
		{
			if(fraction < 0.5f)
				newList.data[i] = EnvCurve(EnvCurve::Linear);
			else
				newList.data[i] = otherCurve;
		}
		else
		{
			if(fraction < 0.5f)
				newList.data[i] = thisCurve;
			else
				newList.data[i] = EnvCurve(EnvCurve::Linear);
		}
	}

	return newList;
}