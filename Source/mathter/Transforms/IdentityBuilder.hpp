// L=============================================================================
// L This software is distributed under the MIT license.
// L Copyright 2021 Péter Kardos
// L=============================================================================

#pragma once

#include "../Matrix/MatrixImpl.hpp"
#include "../Quaternion/QuaternionImpl.hpp"
#include "ZeroBuilder.hpp"


namespace mathter {


class IdentityBuilder {
public:
	IdentityBuilder() = default;
	IdentityBuilder& operator=(const IdentityBuilder&) = delete;

	template <class T, int Rows, int Columns, eMatrixOrder Order, eMatrixLayout Layout, bool Packed>
	operator Matrix<T, Rows, Columns, Order, Layout, Packed>() const {
		Matrix<T, Rows, Columns, Order, Layout, Packed> m;
		Set(m);
		return m;
	}

	template <class T, bool Packed>
	operator Quaternion<T, Packed>() const {
		return Quaternion<T, Packed>{ 1, 0, 0, 0 };
	}

private:
	template <class T, int Rows, int Columns, eMatrixOrder Order, eMatrixLayout Layout, bool Packed>
	void Set(Matrix<T, Rows, Columns, Order, Layout, Packed>& m) const {
		m = Zero();
		for (int i = 0; i < std::min(Rows, Columns); ++i) {
			m(i, i) = T(1);
		}
	}
};


/// <summary> Creates an identity matrix or identity quaternion. </summary>
/// <remarks> If the matrix is not square, it will look like a truncated larger square identity matrix. </remarks>
inline auto Identity() {
	return IdentityBuilder{};
}


} // namespace mathter