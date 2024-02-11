// L=============================================================================
// L This software is distributed under the MIT license.
// L Copyright 2021 Péter Kardos
// L=============================================================================

#pragma once

#include "MatrixImpl.hpp"

namespace mathter {


namespace impl {
	template <class MatrixDestT, class MatrixSourceT>
	struct ReinterpretCompatible : std::false_type {};

	template <class T1,
			  class T2,
			  int Rows,
			  int Columns,
			  eMatrixOrder Order1,
			  eMatrixOrder Order2,
			  eMatrixLayout Layout1,
			  eMatrixLayout Layout2,
			  bool Packed1,
			  bool Packed2>
	struct ReinterpretCompatible<Matrix<T1, Rows, Columns, Order1, Layout1, Packed1>, Matrix<T2, Rows, Columns, Order2, Layout2, Packed2>> {
		static constexpr bool value = std::is_convertible_v<T2, T1>;
	};

	template <class MatrixDestT, class MatrixSourceT>
	struct RepresentationCompatible : std::false_type {};

	template <class T1,
			  class T2,
			  int Rows,
			  int Columns,
			  eMatrixOrder Order1,
			  eMatrixLayout Layout1,
			  eMatrixLayout Layout2,
			  bool Packed1,
			  bool Packed2>
	struct RepresentationCompatible<Matrix<T1, Rows, Columns, Order1, Layout1, Packed1>, Matrix<T2, Columns, Rows, traits::OppositeOrder<Order1>::value, Layout2, Packed2>> {
		static constexpr bool value = std::is_convertible_v<T2, T1>;
	};

	template <class T1,
			  class T2,
			  int Rows,
			  int Columns,
			  eMatrixOrder Order1,
			  eMatrixLayout Layout1,
			  eMatrixLayout Layout2,
			  bool Packed1,
			  bool Packed2>
	struct RepresentationCompatible<Matrix<T1, Rows, Columns, Order1, Layout1, Packed1>, Matrix<T2, Rows, Columns, Order1, Layout2, Packed2>> {
		static constexpr bool value = std::is_convertible_v<T2, T1>;
	};
} // namespace impl



/// <summary> Changes the type, order and layout of the matrix, but the elements stay at the same place. </summary>
template <class MatrixDestT, class MatrixSourceT>
auto matrix_reinterpret_cast(const MatrixSourceT& source) -> std::enable_if_t<impl::ReinterpretCompatible<MatrixDestT, MatrixSourceT>::value, MatrixDestT> {
	MatrixDestT dest;
	for (int i = 0; i < source.RowCount(); ++i) {
		for (int j = 0; j < source.ColumnCount(); ++j) {
			dest(i, j) = typename traits::MatrixTraits<MatrixDestT>::Type(source(i, j));
		}
	}
	return dest;
}


/// <summary> Changes the type, order and layout of the matrix.
///		The elements are transposed according to the change in order. </summary>
template <class MatrixDestT, class MatrixSourceT>
auto matrix_representation_cast(const MatrixSourceT& source) -> std::enable_if_t<impl::RepresentationCompatible<MatrixDestT, MatrixSourceT>::value, MatrixDestT> {
	MatrixDestT dest;
	for (int i = 0; i < source.RowCount(); ++i) {
		for (int j = 0; j < source.ColumnCount(); ++j) {
			if constexpr (traits::MatrixTraits<MatrixDestT>::Order == traits::MatrixTraits<MatrixSourceT>::Order) {
				dest(i, j) = typename traits::MatrixTraits<MatrixDestT>::Type(source(i, j));
			}
			else {
				dest(j, i) = typename traits::MatrixTraits<MatrixDestT>::Type(source(i, j));
			}
		}
	}
	return dest;
}


} // namespace mathter