// L=============================================================================
// L This software is distributed under the MIT license.
// L Copyright 2021 Péter Kardos
// L=============================================================================

#pragma once


#include "../Common/Definitions.hpp"
#include "../Common/Traits.hpp"
#include "../Vector.hpp"

#include <array>


namespace mathter {


//------------------------------------------------------------------------------
// Matrix base class only allocating the memory
//------------------------------------------------------------------------------

template <class T, int Rows, int Columns, eMatrixOrder Order = eMatrixOrder::FOLLOW_VECTOR, eMatrixLayout Layout = eMatrixLayout::ROW_MAJOR, bool Packed = false>
class MatrixData {
public:
	/// <summary> Returns the number of columns of the matrix. </summary>
	constexpr int ColumnCount() const {
		return Columns;
	}
	/// <summary> Returns the number of rows of the matrix. </summary>
	constexpr int RowCount() const {
		return Rows;
	}
	/// <summary> Returns the number of columns of the matrix. </summary>
	constexpr int Width() const {
		return Columns;
	}
	/// <summary> Returns the number of rows of the matrix. </summary>
	constexpr int Height() const {
		return Rows;
	}
	// Rows equal height, Columns equal width, row-major has column-sized stripes
	static constexpr int StripeDim = Layout == eMatrixLayout::ROW_MAJOR ? Columns : Rows;
	static constexpr int StripeCount = Layout == eMatrixLayout::ROW_MAJOR ? Rows : Columns;

	using StripeVecT = Vector<T, StripeDim, Packed>;
	std::array<StripeVecT, StripeCount> stripes;

protected:
	// Get element
	inline T& GetElement(int row, int col) {
		assert(row < RowCount());
		assert(col < ColumnCount());
		if constexpr (Layout == eMatrixLayout::ROW_MAJOR) {
			return stripes[row][col];
		}
		else {
			return stripes[col][row];
		}
	}
	inline T GetElement(int row, int col) const {
		assert(row < RowCount());
		assert(col < ColumnCount());
		if constexpr (Layout == eMatrixLayout::ROW_MAJOR) {
			return stripes[row][col];
		}
		else {
			return stripes[col][row];
		}
	}
};


//------------------------------------------------------------------------------
// Submatrix helper
//------------------------------------------------------------------------------

template <class MatrixT, int SRows, int SColumns>
class SubmatrixHelper {
	friend MatrixT;
	using Props = traits::MatrixTraits<MatrixT>;
	template <class, int, int>
	friend class SubmatrixHelper;
	static constexpr int VecDim = std::max(SRows, SColumns);
	static constexpr bool VectorAssignable = std::min(SRows, SColumns) == 1;

protected:
	SubmatrixHelper(MatrixT& mat, int row, int col) : mat(mat), row(row), col(col) {}

public:
	SubmatrixHelper(const SubmatrixHelper& rhs) = delete;
	SubmatrixHelper(SubmatrixHelper&& rhs) : mat(rhs.mat), row(rhs.row), col(rhs.col) {}


	template <class U, eMatrixOrder UOrder, eMatrixLayout ULayout, bool UPacked>
	operator Matrix<U, SRows, SColumns, UOrder, ULayout, UPacked>() const {
		Matrix<U, SRows, SColumns, UOrder, ULayout, UPacked> ret;
		for (int i = 0; i < SRows; ++i) {
			for (int j = 0; j < SColumns; ++j) {
				ret(i, j) = (*this)(i, j);
			}
		}
		return ret;
	}

	template <class U, bool Packed2, class = typename std::enable_if<VectorAssignable, U>::type>
	operator Vector<U, VecDim, Packed2>() const {
		Vector<U, std::max(SRows, SColumns), Packed2> v;
		int k = 0;
		for (int i = 0; i < SRows; ++i) {
			for (int j = 0; j < SColumns; ++j) {
				v(k) = (*this)(i, j);
				++k;
			}
		}
		return v;
	}


	template <class U, eMatrixOrder UOrder, eMatrixLayout ULayout, bool UPacked>
	SubmatrixHelper& operator=(const Matrix<U, SRows, SColumns, UOrder, ULayout, UPacked>& rhs) {
		static_assert(!std::is_const<MatrixT>::value, "Cannot assign to submatrix of const matrix.");

		// If aliasing happens, the same matrix is copied to itself with no side-effects.
		for (int i = 0; i < SRows; ++i) {
			for (int j = 0; j < SColumns; ++j) {
				mat(row + i, col + j) = rhs(i, j);
			}
		}
		return *this;
	}


	// From vector if applicable (for 1*N and N*1 submatrices)
	template <class U, bool Packed, class = typename std::enable_if<VectorAssignable, U>::type>
	SubmatrixHelper& operator=(const Vector<U, VecDim, Packed>& v) {
		static_assert(!std::is_const<MatrixT>::value, "Cannot assign to submatrix of const matrix.");

		int k = 0;
		for (int i = 0; i < SRows; ++i) {
			for (int j = 0; j < SColumns; ++j) {
				mat(row + i, col + j) = v(k);
				++k;
			}
		}
		return *this;
	}


	template <class MatrixU>
	SubmatrixHelper& operator=(const SubmatrixHelper<MatrixU, SRows, SColumns>& rhs) {
		static_assert(!std::is_const<MatrixT>::value, "Cannot assign to submatrix of const matrix.");

		// If *this and rhs reference the same matrix, aliasing must be resolved.
		if ((void*)&mat == (void*)&rhs.mat) {
			Matrix<typename traits::MatrixTraits<MatrixU>::Type,
				   SRows,
				   SColumns,
				   traits::MatrixTraits<MatrixU>::Order,
				   traits::MatrixTraits<MatrixU>::Layout,
				   traits::MatrixTraits<MatrixU>::Packed>
				tmpmat;
			tmpmat = rhs;
			operator=(tmpmat);
		}
		else {
			for (int i = 0; i < SRows; ++i) {
				for (int j = 0; j < SColumns; ++j) {
					mat(row + i, col + j) = rhs(i, j);
				}
			}
		}
		return *this;
	}
	SubmatrixHelper& operator=(const SubmatrixHelper& rhs) {
		static_assert(!std::is_const<MatrixT>::value, "Cannot assign to submatrix of const matrix.");
		return operator=<MatrixT>(rhs);
	}

	typename Props::Type& operator()(int row, int col) {
		return mat(this->row + row, this->col + col);
	}

	typename Props::Type operator()(int row, int col) const {
		return mat(this->row + row, this->col + col);
	}

private:
	MatrixT& mat;
	int row = -1, col = -1;
};


//------------------------------------------------------------------------------
// Matrix class providing the common interface for all matrices
//------------------------------------------------------------------------------

template <class T, int Rows, int Columns, eMatrixOrder Order = eMatrixOrder::FOLLOW_VECTOR, eMatrixLayout Layout = eMatrixLayout::ROW_MAJOR, bool Packed = false>
class Matrix : public MatrixData<T, Rows, Columns, Order, Layout, Packed> {
	static_assert(Columns >= 1 && Rows >= 1, "Dimensions must be positive integers.");

	static constexpr int VecDim = std::max(Rows, Columns);
	static constexpr bool VectorAssignable = std::min(Rows, Columns) == 1;

protected:
	using MatrixData<T, Rows, Columns, Order, Layout, Packed>::GetElement;

	template <class T2, int Rows2, int Columns2, eMatrixOrder Order2, eMatrixLayout Layout2, bool Packed2>
	friend class Matrix;

public:
	using MatrixData<T, Rows, Columns, Order, Layout, Packed>::RowCount;
	using MatrixData<T, Rows, Columns, Order, Layout, Packed>::ColumnCount;
	using typename MatrixData<T, Rows, Columns, Order, Layout, Packed>::StripeVecT;
	using MatrixData<T, Rows, Columns, Order, Layout, Packed>::stripes;
	using MatrixData<T, Rows, Columns, Order, Layout, Packed>::StripeCount;
	struct FromStripes_ {};
	static constexpr FromStripes_ FromStripes = {};

	//--------------------------------------------
	// Constructors
	//--------------------------------------------

	Matrix() = default;

	// From same multiplication order
	template <class T2, eMatrixLayout Layout2, bool Packed2>
	Matrix(const Matrix<T2, Rows, Columns, Order, Layout2, Packed2>& rhs) {
		for (int i = 0; i < RowCount(); ++i) {
			for (int j = 0; j < ColumnCount(); ++j) {
				(*this)(i, j) = rhs(i, j);
			}
		}
	}

	template <class H, class... Args,
			  typename std::enable_if<traits::All<traits::IsScalar, H, Args...>::value, int>::type = 0,
			  typename std::enable_if<1 + sizeof...(Args) == Rows * Columns, int>::type = 0>
	Matrix(H h, Args... args) {
		Assign<0, 0>(h, args...);
	}

	// From vector if applicable (for 1*N and N*1 matrices)
	template <class T2, bool Packed2, class = typename std::enable_if<VectorAssignable, T2>::type>
	Matrix(const Vector<T2, VecDim, Packed2>& v) {
		for (int i = 0; i < v.Dimension(); ++i) {
			(*this)(i) = v(i);
		}
	}

	/// <summary> Used by internal methods. </summary>
	template <class... Stripes>
	Matrix(FromStripes_, Stripes... stripes)
		: MatrixData<T, Rows, Columns, Order, Layout, Packed>{ std::forward<Stripes>(stripes)... } {}

	//--------------------------------------------
	// Accessors
	//--------------------------------------------

	// General matrix indexing
	inline T& operator()(int row, int col) {
		return GetElement(row, col);
	}
	inline T operator()(int row, int col) const {
		return GetElement(row, col);
	}

	// Column and row vector simple indexing
	template <class Q = T>
	inline typename std::enable_if<(Columns == 1 && Rows > 1) || (Columns > 1 && Rows == 1), Q>::type& operator()(int idx) {
		return GetElement(Rows == 1 ? 0 : idx, Columns == 1 ? 0 : idx);
	}
	template <class Q = T>
	inline typename std::enable_if<(Columns == 1 && Rows > 1) || (Columns > 1 && Rows == 1), Q>::type operator()(int idx) const {
		return GetElement(Rows == 1 ? 0 : idx, Columns == 1 ? 0 : idx);
	}

	// Submatrices
	/// <summary> DEPRECATED: I plan to replace it with a nicer MatrixView like std::string_view. </summary>
	template <int Subrows, int Subcolumns>
	mathter::SubmatrixHelper<Matrix, Subrows, Subcolumns> Submatrix(int rowIdx, int colIdx) {
		assert(Subrows + rowIdx <= Rows);
		assert(Subcolumns + colIdx <= Columns);

		return SubmatrixHelper<Matrix, Subrows, Subcolumns>(*this, rowIdx, colIdx);
	}

	template <int Subrows, int Subcolumns>
	/// <summary> DEPRECATED: I plan to replace it with a nicer MatrixView like std::string_view. </summary>
	mathter::SubmatrixHelper<const Matrix, Subrows, Subcolumns> Submatrix(int rowIdx, int colIdx) const {
		assert(Subrows + rowIdx <= Rows);
		assert(Subcolumns + colIdx <= Columns);

		return SubmatrixHelper<const Matrix, Subrows, Subcolumns>(*this, rowIdx, colIdx);
	}

	/// <summary> Return the submatrix corresponding to the specified column. </summary>
	auto Column(int colIdx) {
		return Submatrix<Rows, 1>(0, colIdx);
	}
	/// <summary> Return the submatrix corresponding to the specified row. </summary>
	auto Row(int rowIdx) {
		return Submatrix<1, Columns>(rowIdx, 0);
	}
	/// <summary> Return the submatrix corresponding to the specified column. </summary>
	auto Column(int colIdx) const {
		return Submatrix<Rows, 1>(0, colIdx);
	}
	/// <summary> Return the submatrix corresponding to the specified row. </summary>
	auto Row(int rowIdx) const {
		return Submatrix<1, Columns>(rowIdx, 0);
	}

	// Conversion to vector if applicable
	template <class T2, bool Packed2, class = typename std::enable_if<VectorAssignable, T2>::type>
	operator Vector<T2, VecDim, Packed2>() const {
		Vector<T2, std::max(Rows, Columns), Packed2> v;
		int k = 0;
		for (int i = 0; i < Rows; ++i) {
			for (int j = 0; j < Columns; ++j) {
				v(k) = (*this)(i, j);
				++k;
			}
		}
		return v;
	}

protected:
	template <int i, int j, class Head, class... Args>
	void Assign(Head head, Args... args) {
		(*this)(i, j) = (T)head;
		Assign<((j != Columns - 1) ? i : (i + 1)), ((j + 1) % Columns)>(args...);
	}

	template <int, int>
	void Assign() {}
}; // namespace mathter


} // namespace mathter