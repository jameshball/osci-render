// L=============================================================================
// L This software is distributed under the MIT license.
// L Copyright 2021 Péter Kardos
// L=============================================================================

#pragma once

#include "../Matrix/MatrixImpl.hpp"
#include "../Vector.hpp"
#include "IdentityBuilder.hpp"


namespace mathter {


template <class T, int Dim, bool Packed>
class ScaleBuilder {
public:
	ScaleBuilder(const Vector<T, Dim, Packed>& scale) : scale(scale) {}
	ScaleBuilder& operator=(const ScaleBuilder&) = delete;

	template <class U, eMatrixOrder Order, eMatrixLayout Layout, bool MPacked>
	operator Matrix<U, Dim + 1, Dim + 1, Order, Layout, MPacked>() const {
		Matrix<U, Dim + 1, Dim + 1, Order, Layout, MPacked> m;
		Set(m);
		return m;
	}

	template <class U, eMatrixOrder Order, eMatrixLayout Layout, bool MPacked>
	operator Matrix<U, Dim, Dim, Order, Layout, MPacked>() const {
		Matrix<U, Dim, Dim, Order, Layout, MPacked> m;
		Set(m);
		return m;
	}

	template <class U, eMatrixLayout Layout, bool MPacked>
	operator Matrix<U, Dim, Dim + 1, eMatrixOrder::PRECEDE_VECTOR, Layout, MPacked>() const {
		Matrix<U, Dim, Dim + 1, eMatrixOrder::PRECEDE_VECTOR, Layout, MPacked> m;
		Set(m);
		return m;
	}

	template <class U, eMatrixLayout Layout, bool MPacked>
	operator Matrix<U, Dim + 1, Dim, eMatrixOrder::FOLLOW_VECTOR, Layout, MPacked>() const {
		Matrix<U, Dim + 1, Dim, eMatrixOrder::FOLLOW_VECTOR, Layout, MPacked> m;
		Set(m);
		return m;
	}


private:
	template <class U, int Rows, int Columns, eMatrixOrder Order, eMatrixLayout Layout, bool MPacked>
	void Set(Matrix<U, Rows, Columns, Order, Layout, MPacked>& m) const {
		m = Identity();
		int i;
		for (i = 0; i < scale.Dimension(); ++i) {
			m(i, i) = std::move(scale(i));
		}
		for (; i < std::min(Rows, Columns); ++i) {
			m(i, i) = T(1);
		}
	}

	const Vector<T, Dim, Packed> scale;
};


/// <summary> Creates a scaling matrix. </summary>
/// <param name="scale"> A vector containing the scales of respective axes. </summary>
/// <remarks> The vector's dimension must be less than or equal to the matrix dimension. </remarks>
template <class Vt, int Vdim, bool Vpacked>
auto Scale(const Vector<Vt, Vdim, Vpacked>& scale) {
	return ScaleBuilder{ scale };
}


/// <summary> Creates a scaling matrix. </summary>
/// <param name="scales"> A list of scalars corresponding to scaling on respective axes. </summary>
/// <remarks> The number of arguments must be less than or equal to the matrix dimension. </remarks>
template <class... Args, typename std::enable_if<(traits::All<traits::IsScalar, typename std::decay<Args>::type...>::value), int>::type = 0>
auto Scale(Args&&... scales) {
	using PromotedT = decltype((0 + ... + scales));
	return ScaleBuilder{ Vector<PromotedT, sizeof...(scales)>(scales...) };
}



} // namespace mathter