// L=============================================================================
// L This software is distributed under the MIT license.
// L Copyright 2021 Péter Kardos
// L=============================================================================

#pragma once

#include "MatrixImpl.hpp"

#include <utility>


namespace mathter {


//------------------------------------------------------------------------------
// Matrix-matrix multiplication
//------------------------------------------------------------------------------

namespace impl {
	template <class T, class U, int Rows1, int Match, int Columns2, eMatrixOrder Order, bool Packed, int... MatchIndices>
	inline auto SmallProductRowRR(const Matrix<T, Rows1, Match, Order, eMatrixLayout::ROW_MAJOR, Packed>& lhs,
								  const Matrix<U, Match, Columns2, Order, eMatrixLayout::ROW_MAJOR, Packed>& rhs,
								  int row,
								  std::integer_sequence<int, MatchIndices...>) {
		return (... + (rhs.stripes[MatchIndices] * lhs(row, MatchIndices)));
	}

	template <class T, class U, int Rows1, int Match, int Columns2, eMatrixOrder Order, bool Packed, int... RowIndices>
	inline auto SmallProductRR(const Matrix<T, Rows1, Match, Order, eMatrixLayout::ROW_MAJOR, Packed>& lhs,
							   const Matrix<U, Match, Columns2, Order, eMatrixLayout::ROW_MAJOR, Packed>& rhs,
							   std::integer_sequence<int, RowIndices...>) {
		using V = traits::MatMulElemT<T, U>;
		using ResultT = Matrix<V, Rows1, Columns2, Order, eMatrixLayout::ROW_MAJOR, Packed>;
		return ResultT{ ResultT::FromStripes, SmallProductRowRR(lhs, rhs, RowIndices, std::make_integer_sequence<int, Match>{})... };
	}

	template <class T, class U, int Rows1, int Match, int Columns2, eMatrixOrder Order, eMatrixLayout Layout2, bool Packed, int... MatchIndices>
	inline auto SmallProductRowCC(const Matrix<T, Rows1, Match, Order, eMatrixLayout::COLUMN_MAJOR, Packed>& lhs,
								  const Matrix<U, Match, Columns2, Order, Layout2, Packed>& rhs,
								  int col,
								  std::integer_sequence<int, MatchIndices...>) {
		return (... + (lhs.stripes[MatchIndices] * rhs(MatchIndices, col)));
	}

	template <class T, class U, int Rows1, int Match, int Columns2, eMatrixOrder Order, eMatrixLayout Layout2, bool Packed, int... ColIndices>
	inline auto SmallProductCC(const Matrix<T, Rows1, Match, Order, eMatrixLayout::COLUMN_MAJOR, Packed>& lhs,
							   const Matrix<U, Match, Columns2, Order, Layout2, Packed>& rhs,
							   std::integer_sequence<int, ColIndices...>) {
		using V = traits::MatMulElemT<T, U>;
		using ResultT = Matrix<V, Rows1, Columns2, Order, eMatrixLayout::COLUMN_MAJOR, Packed>;
		return ResultT{ ResultT::FromStripes, SmallProductRowCC(lhs, rhs, ColIndices, std::make_integer_sequence<int, Match>{})... };
	}
} // namespace impl


template <class T, class U, int Rows1, int Match, int Columns2, eMatrixOrder Order, bool Packed>
inline auto operator*(const Matrix<T, Rows1, Match, Order, eMatrixLayout::ROW_MAJOR, Packed>& lhs,
					  const Matrix<U, Match, Columns2, Order, eMatrixLayout::ROW_MAJOR, Packed>& rhs) {
	if constexpr (Rows1 <= 4 && Match <= 4 && Columns2 <= 4) {
		return impl::SmallProductRR(lhs, rhs, std::make_integer_sequence<int, Rows1>{});
	}
	else {
		using V = traits::MatMulElemT<T, U>;
		Matrix<V, Rows1, Columns2, Order, eMatrixLayout::ROW_MAJOR, Packed> result;
		for (int i = 0; i < Rows1; ++i) {
			result.stripes[i] = rhs.stripes[0] * lhs(i, 0);
		}
		for (int i = 0; i < Rows1; ++i) {
			for (int j = 1; j < Match; ++j) {
				result.stripes[i] += rhs.stripes[j] * lhs(i, j);
			}
		}
		return result;
	}
}

template <class T, class U, int Rows1, int Match, int Columns2, eMatrixOrder Order, bool Packed>
inline auto operator*(const Matrix<T, Rows1, Match, Order, eMatrixLayout::ROW_MAJOR, Packed>& lhs,
					  const Matrix<U, Match, Columns2, Order, eMatrixLayout::COLUMN_MAJOR, Packed>& rhs) {
	using V = traits::MatMulElemT<T, U>;
	Matrix<V, Rows1, Columns2, Order, eMatrixLayout::ROW_MAJOR, Packed> result;

	for (int j = 0; j < Columns2; ++j) {
		for (int i = 0; i < Rows1; ++i) {
			result(i, j) = Dot(lhs.stripes[i], rhs.stripes[j]);
		}
	}

	return result;
}

template <class T, class U, int Rows1, int Match, int Columns2, eMatrixOrder Order, bool Packed>
inline auto operator*(const Matrix<T, Rows1, Match, Order, eMatrixLayout::COLUMN_MAJOR, Packed>& lhs,
					  const Matrix<U, Match, Columns2, Order, eMatrixLayout::COLUMN_MAJOR, Packed>& rhs) {
	if constexpr (Rows1 <= 4 && Match <= 4 && Columns2 <= 4) {
		return impl::SmallProductCC(lhs, rhs, std::make_integer_sequence<int, Columns2>{});
	}
	else {
		using V = traits::MatMulElemT<T, U>;
		Matrix<V, Rows1, Columns2, Order, eMatrixLayout::COLUMN_MAJOR, Packed> result;
		for (int j = 0; j < Columns2; ++j) {
			result.stripes[j] = lhs.stripes[0] * rhs(0, j);
		}
		for (int i = 1; i < Match; ++i) {
			for (int j = 0; j < Columns2; ++j) {
				result.stripes[j] += lhs.stripes[i] * rhs(i, j);
			}
		}
		return result;
	}
}

template <class T, class U, int Rows1, int Match, int Columns2, eMatrixOrder Order, bool Packed>
inline auto operator*(const Matrix<T, Rows1, Match, Order, eMatrixLayout::COLUMN_MAJOR, Packed>& lhs,
					  const Matrix<U, Match, Columns2, Order, eMatrixLayout::ROW_MAJOR, Packed>& rhs) {
	// CC algorithm is completely fine for COL_MAJOR x ROW_MAJOR.
	// See that rhs is only indexed per-element, so its layout does not matter.
	if constexpr (Rows1 <= 4 && Match <= 4 && Columns2 <= 4) {
		return impl::SmallProductCC(lhs, rhs, std::make_integer_sequence<int, Columns2>{});
	}
	else {
		using V = traits::MatMulElemT<T, U>;
		Matrix<V, Rows1, Columns2, Order, eMatrixLayout::COLUMN_MAJOR, Packed> result;
		for (int j = 0; j < Columns2; ++j) {
			result.stripes[j] = lhs.stripes[0] * rhs(0, j);
		}
		for (int i = 1; i < Match; ++i) {
			for (int j = 0; j < Columns2; ++j) {
				result.stripes[j] += lhs.stripes[i] * rhs(i, j);
			}
		}
		return result;
	}
}


// Assign-multiply
template <class T1, class T2, int Dim, eMatrixOrder Order, eMatrixLayout Layout1, eMatrixLayout Layout2, bool Packed>
inline Matrix<T1, Dim, Dim, Order, Layout1, Packed>& operator*=(Matrix<T1, Dim, Dim, Order, Layout1, Packed>& lhs, const Matrix<T2, Dim, Dim, Order, Layout2, Packed>& rhs) {
	lhs = lhs * rhs;
	return lhs;
}

//------------------------------------------------------------------------------
// Matrix-matrix addition & subtraction
//------------------------------------------------------------------------------

namespace impl {
	template <class T, class U, int Rows, int Columns, eMatrixOrder Order, eMatrixLayout SameLayout, bool Packed, int... StripeIndices>
	inline auto SmallAdd(const Matrix<T, Rows, Columns, Order, SameLayout, Packed>& lhs,
						 const Matrix<U, Rows, Columns, Order, SameLayout, Packed>& rhs,
						 std::integer_sequence<int, StripeIndices...>) {
		using V = traits::MatMulElemT<T, U>;
		using ResultT = Matrix<V, Rows, Columns, Order, SameLayout, Packed>;
		return ResultT{ ResultT::FromStripes, (lhs.stripes[StripeIndices] + rhs.stripes[StripeIndices])... };
	}

	template <class T, class U, int Rows, int Columns, eMatrixOrder Order, eMatrixLayout SameLayout, bool Packed, int... StripeIndices>
	inline auto SmallSub(const Matrix<T, Rows, Columns, Order, SameLayout, Packed>& lhs,
						 const Matrix<U, Rows, Columns, Order, SameLayout, Packed>& rhs,
						 std::integer_sequence<int, StripeIndices...>) {
		using V = traits::MatMulElemT<T, U>;
		using ResultT = Matrix<V, Rows, Columns, Order, SameLayout, Packed>;
		return ResultT{ ResultT::FromStripes, (lhs.stripes[StripeIndices] - rhs.stripes[StripeIndices])... };
	}
} // namespace impl

// Same layout
template <class T, class U, int Rows, int Columns, eMatrixOrder Order, eMatrixLayout SameLayout, bool Packed>
inline auto operator+(const Matrix<T, Rows, Columns, Order, SameLayout, Packed>& lhs,
					  const Matrix<U, Rows, Columns, Order, SameLayout, Packed>& rhs) {
	using V = traits::MatMulElemT<T, U>;

	if constexpr (Rows * Columns == 4) {
		Matrix<V, Rows, Columns, Order, SameLayout, Packed> result;
		for (int i = 0; i < result.RowCount(); ++i) {
			for (int j = 0; j < result.ColumnCount(); ++j) {
				result(i, j) = lhs(i, j) + rhs(i, j);
			}
		}
		return result;
	}
	else if constexpr (Rows <= 4 && Columns <= 4) {
		return impl::SmallAdd(lhs, rhs, std::make_integer_sequence<int, std::decay_t<decltype(lhs)>::StripeCount>{});
	}
	else {
		Matrix<V, Rows, Columns, Order, SameLayout, Packed> result;
		for (int i = 0; i < result.StripeCount; ++i) {
			result.stripes[i] = lhs.stripes[i] + rhs.stripes[i];
		}
		return result;
	}
}

template <class T, class U, int Rows, int Columns, eMatrixOrder Order, eMatrixLayout SameLayout, bool Packed>
inline auto operator-(const Matrix<T, Rows, Columns, Order, SameLayout, Packed>& lhs,
					  const Matrix<U, Rows, Columns, Order, SameLayout, Packed>& rhs) {
	using V = traits::MatMulElemT<T, U>;

	if constexpr (Rows * Columns == 4) {
		Matrix<V, Rows, Columns, Order, SameLayout, Packed> result;
		for (int i = 0; i < result.RowCount(); ++i) {
			for (int j = 0; j < result.ColumnCount(); ++j) {
				result(i, j) = lhs(i, j) - rhs(i, j);
			}
		}
		return result;
	}
	else if constexpr (Rows <= 4 && Columns <= 4) {
		return impl::SmallSub(lhs, rhs, std::make_integer_sequence<int, std::decay_t<decltype(lhs)>::StripeCount>{});
	}
	else {
		Matrix<V, Rows, Columns, Order, SameLayout, Packed> result;
		for (int i = 0; i < result.StripeCount; ++i) {
			result.stripes[i] = lhs.stripes[i] - rhs.stripes[i];
		}
		return result;
	}
}


// Add & sub opposite layout
template <class T, class U, int Rows, int Columns, eMatrixOrder Order, eMatrixLayout Layout1, eMatrixLayout Layout2, bool Packed, class = typename std::enable_if<Layout1 != Layout2>::type>
inline auto operator+(const Matrix<T, Rows, Columns, Order, Layout1, Packed>& lhs,
					  const Matrix<U, Rows, Columns, Order, Layout2, Packed>& rhs) {
	using V = traits::MatMulElemT<T, U>;
	Matrix<V, Rows, Columns, Order, Layout1, Packed> result;
	for (int i = 0; i < result.RowCount(); ++i) {
		for (int j = 0; j < result.ColumnCount(); ++j) {
			result(i, j) = lhs(i, j) + rhs(i, j);
		}
	}
	return result;
}

template <class T, class U, int Rows, int Columns, eMatrixOrder Order, eMatrixLayout Layout1, eMatrixLayout Layout2, bool Packed, class = typename std::enable_if<Layout1 != Layout2>::type>
inline auto operator-(const Matrix<T, Rows, Columns, Order, Layout1, Packed>& lhs,
					  const Matrix<U, Rows, Columns, Order, Layout2, Packed>& rhs) {
	using V = traits::MatMulElemT<T, U>;
	Matrix<V, Rows, Columns, Order, Layout1, Packed> result;
	for (int i = 0; i < result.RowCount(); ++i) {
		for (int j = 0; j < result.ColumnCount(); ++j) {
			result(i, j) = lhs(i, j) - rhs(i, j);
		}
	}
	return result;
}



/// <summary> Performs matrix addition and stores result in this. </summary>
template <class T, class U, int Rows, int Columns, eMatrixOrder Order, eMatrixLayout Layout1, eMatrixLayout Layout2, bool Packed>
inline Matrix<U, Rows, Columns, Order, Layout1, Packed>& operator+=(
	Matrix<T, Rows, Columns, Order, Layout1, Packed>& lhs,
	const Matrix<U, Rows, Columns, Order, Layout2, Packed>& rhs) {
	lhs = lhs + rhs;
	return lhs;
}

/// <summary> Performs matrix subtraction and stores result in this. </summary>
template <class T, class U, int Rows, int Columns, eMatrixOrder Order, eMatrixLayout Layout1, eMatrixLayout Layout2, bool Packed>
inline Matrix<U, Rows, Columns, Order, Layout1, Packed>& operator-=(
	Matrix<T, Rows, Columns, Order, Layout1, Packed>& lhs,
	const Matrix<U, Rows, Columns, Order, Layout2, Packed>& rhs) {
	lhs = lhs - rhs;
	return lhs;
}



//------------------------------------------------------------------------------
// Matrix-Scalar arithmetic
//------------------------------------------------------------------------------


// Scalar multiplication
/// <summary> Multiplies all elements of the matrix by scalar. </summary>
template <class T, int Rows, int Columns, eMatrixOrder Order, eMatrixLayout Layout, bool Packed, class U, std::enable_if_t<std::is_convertible_v<U, T>, int> = 0>
inline Matrix<T, Rows, Columns, Order, Layout, Packed>& operator*=(Matrix<T, Rows, Columns, Order, Layout, Packed>& mat, U s) {
	for (auto& stripe : mat.stripes) {
		stripe *= s;
	}
	return mat;
}
/// <summary> Divides all elements of the matrix by scalar. </summary>
template <class T, int Rows, int Columns, eMatrixOrder Order, eMatrixLayout Layout, bool Packed, class U, std::enable_if_t<std::is_convertible_v<U, T>, int> = 0>
inline Matrix<T, Rows, Columns, Order, Layout, Packed>& operator/=(Matrix<T, Rows, Columns, Order, Layout, Packed>& mat, U s) {
	mat *= U(1) / s;
	return mat;
}

template <class T, int Rows, int Columns, eMatrixOrder Order, eMatrixLayout Layout, bool Packed, class U, std::enable_if_t<std::is_convertible_v<U, T>, int> = 0>
Matrix<T, Rows, Columns, Order, Layout, Packed> operator*(const Matrix<T, Rows, Columns, Order, Layout, Packed>& mat, U s) {
	Matrix<T, Rows, Columns, Order, Layout, Packed> copy(mat);
	copy *= s;
	return copy;
}

template <class T, int Rows, int Columns, eMatrixOrder Order, eMatrixLayout Layout, bool Packed, class U, std::enable_if_t<std::is_convertible_v<U, T>, int> = 0>
Matrix<T, Rows, Columns, Order, Layout, Packed> operator/(const Matrix<T, Rows, Columns, Order, Layout, Packed>& mat, U s) {
	Matrix<T, Rows, Columns, Order, Layout, Packed> copy(mat);
	copy /= s;
	return copy;
}

template <class T, int Rows, int Columns, eMatrixOrder Order, eMatrixLayout Layout, bool Packed, class U, std::enable_if_t<std::is_convertible_v<U, T>, int> = 0>
Matrix<T, Rows, Columns, Order, Layout, Packed> operator*(U s, const Matrix<T, Rows, Columns, Order, Layout, Packed>& mat) {
	return mat * s;
}

template <class T, int Rows, int Columns, eMatrixOrder Order, eMatrixLayout Layout, bool Packed, class U, std::enable_if_t<std::is_convertible_v<U, T>, int> = 0>
Matrix<T, Rows, Columns, Order, Layout, Packed> operator/(U s, const Matrix<T, Rows, Columns, Order, Layout, Packed>& mat) {
	Matrix<T, Rows, Columns, Order, Layout, Packed> result;
	for (int i = 0; i < Matrix<T, Rows, Columns, Order, Layout, Packed>::StripeCount; ++i) {
		result.stripes[i] = T(s) / mat.stripes[i];
	}
	return result;
}


//------------------------------------------------------------------------------
// Elementwise multiply and divide
//------------------------------------------------------------------------------
template <class T, class T2, int Rows, int Columns, eMatrixOrder Order, eMatrixLayout Layout, bool Packed>
auto MulElementwise(const Matrix<T, Rows, Columns, Order, Layout, Packed>& lhs, const Matrix<T2, Rows, Columns, Order, Layout, Packed>& rhs) {
	Matrix<T, Rows, Columns, Order, Layout, Packed> result;
	for (int i = 0; i < result.StripeCount; ++i) {
		result.stripes[i] = lhs.stripes[i] * rhs.stripes[i];
	}
	return result;
}

template <class T, class T2, int Rows, int Columns, eMatrixOrder Order, eMatrixLayout Layout, bool Packed>
auto MulElementwise(const Matrix<T, Rows, Columns, Order, Layout, Packed>& lhs, const Matrix<T2, Rows, Columns, Order, traits::OppositeLayout<Layout>::value, Packed>& rhs) {
	Matrix<T, Rows, Columns, Order, Layout, Packed> result;
	for (int i = 0; i < Rows; ++i) {
		for (int j = 0; j < Columns; ++j) {
			result(i, j) = lhs(i, j) * rhs(i, j);
		}
	}
	return result;
}

template <class T, class T2, int Rows, int Columns, eMatrixOrder Order, eMatrixLayout Layout, bool Packed>
auto DivElementwise(const Matrix<T, Rows, Columns, Order, Layout, Packed>& lhs, const Matrix<T2, Rows, Columns, Order, Layout, Packed>& rhs) {
	Matrix<T, Rows, Columns, Order, Layout, Packed> result;
	for (int i = 0; i < result.StripeCount; ++i) {
		result.stripes[i] = lhs.stripes[i] / rhs.stripes[i];
	}
	return result;
}

template <class T, class T2, int Rows, int Columns, eMatrixOrder Order, eMatrixLayout Layout, bool Packed>
auto DivElementwise(const Matrix<T, Rows, Columns, Order, Layout, Packed>& lhs, const Matrix<T2, Rows, Columns, Order, traits::OppositeLayout<Layout>::value, Packed>& rhs) {
	Matrix<T, Rows, Columns, Order, Layout, Packed> result;
	for (int i = 0; i < Rows; ++i) {
		for (int j = 0; j < Columns; ++j) {
			result(i, j) = lhs(i, j) / rhs(i, j);
		}
	}
	return result;
}



//------------------------------------------------------------------------------
// Unary signs
//------------------------------------------------------------------------------
template <class T, int Rows, int Columns, eMatrixOrder Order, eMatrixLayout Layout, bool Packed>
auto operator+(const Matrix<T, Rows, Columns, Order, Layout, Packed>& mat) {
	return Matrix<T, Rows, Columns, Order, Layout, Packed>(mat);
}

template <class T, int Rows, int Columns, eMatrixOrder Order, eMatrixLayout Layout, bool Packed>
auto operator-(const Matrix<T, Rows, Columns, Order, Layout, Packed>& mat) {
	return Matrix<T, Rows, Columns, Order, Layout, Packed>(mat) * T(-1);
}



} // namespace mathter