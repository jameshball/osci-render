// L=============================================================================
// L This software is distributed under the MIT license.
// L Copyright 2021 Péter Kardos
// L=============================================================================

#pragma once

#include "../Matrix/MatrixImpl.hpp"
#include "../Vector.hpp"


namespace mathter {


template <class T>
class ShearBuilder {
public:
	ShearBuilder(T slope, int principalAxis, int modulatorAxis) : slope(slope), principalAxis(principalAxis), modulatorAxis(modulatorAxis) {}
	ShearBuilder& operator=(const ShearBuilder&) = delete;

	template <class U, int Rows, int Columns, eMatrixOrder Order, eMatrixLayout Layout, bool MPacked>
	operator Matrix<U, Rows, Columns, Order, Layout, MPacked>() const {
		Matrix<U, Rows, Columns, Order, Layout, MPacked> m;
		Set(m);
		return m;
	}

private:
	template <class U, int Rows, int Columns, eMatrixOrder Order, eMatrixLayout Layout, bool MPacked>
	void Set(Matrix<U, Rows, Columns, Order, Layout, MPacked>& m) const {
		assert(principalAxis != modulatorAxis);
		m = Identity();
		if constexpr (Order == eMatrixOrder::FOLLOW_VECTOR) {
			assert(modulatorAxis < Rows);
			assert(principalAxis < Columns);
			m(modulatorAxis, principalAxis) = slope;
		}
		else {
			assert(principalAxis < Rows);
			assert(modulatorAxis < Columns);
			m(principalAxis, modulatorAxis) = slope;
		}
	}

	const T slope;
	const int principalAxis;
	const int modulatorAxis;
};


/// <summary> Creates a shear matrix. </summary>
/// <param name="slope"> Strength of the shear. </param>
/// <param name="principalAxis"> Points are moved along this axis. </param>
/// <param name="modulatorAxis"> The displacement of points is proportional to this coordinate's value. </param>
/// <remarks> The formula for displacement along the pricipal axis is
///		<paramref name="slope"/>&ast;pos[<paramref name="modulatorAxis"/>]. </remarks>
template <class T>
auto Shear(T slope, int principalAxis, int modulatorAxis) {
	return ShearBuilder(slope, principalAxis, modulatorAxis);
}


} // namespace mathter