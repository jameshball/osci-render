// L=============================================================================
// L This software is distributed under the MIT license.
// L Copyright 2021 Péter Kardos
// L=============================================================================

#pragma once


#include "../Matrix/MatrixImpl.hpp"
#include "ZeroBuilder.hpp"


namespace mathter {


class ZeroBuilder {
public:
	ZeroBuilder() = default;
	ZeroBuilder& operator=(const ZeroBuilder&) = delete;

	template <class T, int Rows, int Columns, eMatrixOrder Order, eMatrixLayout Layout, bool Packed>
	operator Matrix<T, Rows, Columns, Order, Layout, Packed>() const {
		Matrix<T, Rows, Columns, Order, Layout, Packed> m;
		Set(m);
		return m;
	}

private:
	template <class T, int Rows, int Columns, eMatrixOrder Order, eMatrixLayout Layout, bool Packed>
	void Set(Matrix<T, Rows, Columns, Order, Layout, Packed>& m) const {
		for (auto& stripe : m.stripes) {
			Fill(stripe, T(0));
		}
	}
};


/// <summary> Creates a matrix with all elements zero. </summary>
inline auto Zero() {
	return ZeroBuilder{};
}


} // namespace mathter