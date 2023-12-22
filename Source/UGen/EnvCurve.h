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

#ifndef _UGEN_ugen_EnvCurve_H_
#define _UGEN_ugen_EnvCurve_H_

#include <vector>
#include <JuceHeader.h>

/** Specify curved sections of breakpoint envelopes.
Curve type may be linear or from a selection of curve types, or a numerical
value which specifies the degree of curvature.

@see Env, EnvGen */
class EnvCurve
{
public:

	/// @name Construction and destruction
	/// @{

	enum CurveType
	{
		Empty,
		Numerical,
		Step,
		Linear,
		Exponential,
		Sine,
		Welch
	};

	EnvCurve(float curve) throw()				: type_(Numerical), curve_(curve)			{ }
	//EnvCurve(double curve) throw()				: type_(Numerical), curve_((float)curve)	{ }
	EnvCurve(CurveType type = Empty) throw()	: type_(type), curve_(0.f)					{ }

	/// @} <!-- end Construction and destruction ------------------------------------------- -->

	/// @name Curve access
	/// @{

	CurveType getType() const throw()				{ return type_;	 }
	float getCurve() const throw()					{ return curve_; }
	void setType(const CurveType newType) throw()	{ type_ = newType;	 }
	void setCurve(const float newCurve) throw()		{ curve_ = newCurve; }

	bool equalsInfinity() const throw()	{ return type_ == Numerical && curve_ == INFINITY; }

	bool operator==(EnvCurve const& other) const
	{
		if (type_ != other.type_)
			return false;

		if (type_ == Numerical)
			return curve_ == other.curve_;

		return true;
	}

	bool operator!=(EnvCurve const& other) const { return !(*this==other); }


	/// @} <!-- end Curve access ----------------------------------------------------------- -->

private:
	CurveType type_;
	float curve_;	
};

/** An array of EnvCurve objects.

@see EnvCurve, Env, EnvGen */
class EnvCurveList
{
public:

	/// @name Construction and destruction
	/// @{

	EnvCurveList(EnvCurve const& i00) throw();
	EnvCurveList(const std::vector<EnvCurve>&) throw();

	EnvCurveList(EnvCurve::CurveType type, const int size = 1) throw();
	EnvCurveList(const double curve, const int size = 1) throw();

	~EnvCurveList() throw();

	EnvCurveList(EnvCurveList const& copy) throw();
	EnvCurveList& operator= (EnvCurveList const& other) throw();
	bool operator== (EnvCurveList const& other) const throw();


	/// @} <!-- end Construction and destruction ------------------------------------------- -->

	/// @name Array access and manipulation
	/// @{

	EnvCurve& operator[] (const int index) throw();
	const EnvCurve& operator[] (const int index) const throw();

	inline int size() const throw()									{ return data.size();		}
	inline const std::vector<EnvCurve> const getData() throw()					{ return data;		}
	inline const std::vector<EnvCurve> const getData() const throw()			{ return data;		}

	EnvCurveList blend(EnvCurveList const& other, float fraction) const throw();

	/// @} <!-- end Array access and manipulation ----------------------------------------------- -->

	static EnvCurve empty;
	std::vector<EnvCurve> data;
};


#endif // _UGEN_ugen_EnvCurve_H_