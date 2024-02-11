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
class TranslationBuilder {
public:
	TranslationBuilder(const Vector<T, Dim, Packed>& translation) : translation(translation) {}
	TranslationBuilder& operator=(const TranslationBuilder&) = delete;

	template <class U, eMatrixOrder Order, eMatrixLayout Layout, bool MPacked>
	operator Matrix<U, Dim + 1, Dim + 1, Order, Layout, MPacked>() const {
		Matrix<U, Dim + 1, Dim + 1, Order, Layout, MPacked> m;
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
		if constexpr (Order == eMatrixOrder::FOLLOW_VECTOR) {
			for (int i = 0; i < translation.Dimension(); ++i) {
				m(Rows - 1, i) = U(translation(i));
			}
		}
		else {
			for (int i = 0; i < translation.Dimension(); ++i) {
				m(i, Columns - 1) = U(translation(i));
			}
		}
	}

	const Vector<T, Dim, Packed> translation;
};


/// <summary> Creates a translation matrix. </summary>
/// <param name="translation"> The movement vector. </param>
template <class T, int Dim, bool Packed>
auto Translation(const Vector<T, Dim, Packed>& translation) {
	return TranslationBuilder{ translation };
}

/// <summary> Creates a translation matrix. </summary>
/// <param name="coordinates"> A list of scalars that specify movement along repsective axes. </param>
template <class... Args, typename std::enable_if<(traits::All<traits::IsScalar, typename std::decay<Args>::type...>::value), int>::type = 0>
auto Translation(const Args&... coordinates) {
	using PromotedT = decltype((0 + ... + coordinates));
	return TranslationBuilder{ Vector<PromotedT, sizeof...(coordinates)>(coordinates...) };
}


} // namespace mathter