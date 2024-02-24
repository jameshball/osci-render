// L=============================================================================
// L This software is distributed under the MIT license.
// L Copyright 2021 Péter Kardos
// L=============================================================================

#pragma once

#include "../Matrix/MatrixImpl.hpp"
#include "../Vector.hpp"


namespace mathter {


template <class T>
class Rotation2DBuilder {
public:
	Rotation2DBuilder(const T& angle) : angle(angle) {}
	Rotation2DBuilder& operator=(const Rotation2DBuilder&) = delete;

	template <class U, eMatrixOrder Order, eMatrixLayout Layout, bool MPacked>
	operator Matrix<U, 3, 3, Order, Layout, MPacked>() const {
		Matrix<U, 3, 3, Order, Layout, MPacked> m;
		Set(m);
		return m;
	}

	template <class U, eMatrixOrder Order, eMatrixLayout Layout, bool MPacked>
	operator Matrix<U, 2, 2, Order, Layout, MPacked>() const {
		Matrix<U, 2, 2, Order, Layout, MPacked> m;
		Set(m);
		return m;
	}

	template <class U, eMatrixLayout Layout, bool MPacked>
	operator Matrix<U, 2, 3, eMatrixOrder::PRECEDE_VECTOR, Layout, MPacked>() const {
		Matrix<U, 2, 3, eMatrixOrder::PRECEDE_VECTOR, Layout, MPacked> m;
		Set(m);
		return m;
	}

	template <class U, eMatrixLayout Layout, bool MPacked>
	operator Matrix<U, 3, 2, eMatrixOrder::FOLLOW_VECTOR, Layout, MPacked>() const {
		Matrix<U, 3, 2, eMatrixOrder::FOLLOW_VECTOR, Layout, MPacked> m;
		Set(m);
		return m;
	}

private:
	template <class U, int Rows, int Columns, eMatrixOrder Order, eMatrixLayout Layout, bool MPacked>
	void Set(Matrix<U, Rows, Columns, Order, Layout, MPacked>& m) const {
		T C = cos(angle);
		T S = sin(angle);

		auto elem = [&m](int i, int j) -> U& {
			return Order == eMatrixOrder::FOLLOW_VECTOR ? m(i, j) : m(j, i);
		};

		// Indices according to follow vector order
		elem(0, 0) = U(C);
		elem(0, 1) = U(S);
		elem(1, 0) = U(-S);
		elem(1, 1) = U(C);

		// Rest
		for (int j = 0; j < m.ColumnCount(); ++j) {
			for (int i = (j < 2 ? 2 : 0); i < m.RowCount(); ++i) {
				m(i, j) = U(j == i);
			}
		}
	}

	const T angle;
};


/// <summary> Creates a 2D rotation matrix. </summary>
/// <param name="angle"> Counter-clockwise angle in radians. </param>
template <class T>
auto Rotation(const T& angle) {
	return Rotation2DBuilder{ angle };
}


} // namespace mathter