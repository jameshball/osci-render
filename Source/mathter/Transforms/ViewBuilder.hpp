// L=============================================================================
// L This software is distributed under the MIT license.
// L Copyright 2021 Péter Kardos
// L=============================================================================

#pragma once

#include "../Matrix/MatrixImpl.hpp"
#include "../Vector.hpp"


namespace mathter {


template <class T, int Dim, bool Packed>
class ViewBuilder {
	using VectorT = Vector<T, Dim, Packed>;

public:
	ViewBuilder(const VectorT& eye, const VectorT& target, const std::array<VectorT, size_t(Dim - 2)>& bases, const std::array<bool, Dim>& flipAxes)
		: eye(eye), target(target), bases(bases), flipAxes(flipAxes) {}
	ViewBuilder& operator=(const ViewBuilder&) = delete;

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
	void Set(Matrix<U, Rows, Columns, Order, Layout, MPacked>& matrix) const {
		VectorT columns[Dim];
		std::array<const VectorT*, Dim - 1> crossTable = {};
		for (int i = 0; i < (int)bases.size(); ++i) {
			crossTable[i] = &bases[i];
		}
		crossTable.back() = &columns[Dim - 1];
		auto elem = [&matrix](int i, int j) -> U& {
			return Order == eMatrixOrder::FOLLOW_VECTOR ? matrix(i, j) : matrix(j, i);
		};

		// calculate columns of the rotation matrix
		int j = Dim - 1;
		columns[j] = Normalize(eye - target); // right-handed: camera look towards -Z
		do {
			--j;

			columns[Dim - j - 2] = Normalize(Cross(crossTable));

			// shift bases
			for (int s = 0; s < j; ++s) {
				crossTable[s] = crossTable[s + 1];
			}
			crossTable[j] = &columns[Dim - j - 2];
		} while (j > 0);

		// flip columns
		for (int i = 0; i < Dim; ++i) {
			if (flipAxes[i]) {
				columns[i] *= -T(1);
			}
		}

		// copy columns to matrix
		for (int i = 0; i < Dim; ++i) {
			for (int j = 0; j < Dim; ++j) {
				elem(i, j) = columns[j][i];
			}
		}

		// calculate translation of the matrix
		for (int j = 0; j < Dim; ++j) {
			elem(Dim, j) = -Dot(eye, columns[j]);
		}

		// clear additional elements
		constexpr int AuxDim = Rows < Columns ? Rows : Columns;
		if (AuxDim > Dim) {
			for (int i = 0; i < Dim; ++i) {
				elem(i, AuxDim - 1) = 0;
			}
			elem(Dim, AuxDim - 1) = 1;
		}
	}

	const VectorT eye;
	const VectorT target;
	const std::array<VectorT, size_t(Dim - 2)> bases;
	const std::array<bool, Dim> flipAxes;
};


/// <summary> Creates a general, n-dimensional camera look-at matrix. </summary>
/// <param name="eye"> The camera's position. </param>
/// <param name="target"> The camera's target. </param>
/// <param name="bases"> Basis vectors fixing the camera's orientation. </param>
/// <param name="flipAxis"> Set any element to true to flip an axis in camera space. </param>
/// <remarks> The camera looks down the vector going from <paramref name="eye"/> to
///		<paramref name="target"/>, but it can still rotate around that vector. To fix the rotation,
///		an "up" vector must be provided in 3 dimensions. In higher dimensions,
///		we need multiple up vectors. Unfortunately I can't remember how these
///		basis vectors are used, but they are orthogonalized to each-other and to the look vector.
///		I can't remember the order of orthogonalization. </remarks>
template <class T, int Dim, bool Packed, size_t BaseDim, size_t FlipDim>
auto LookAt(const Vector<T, Dim, Packed>& eye,
			const Vector<T, Dim, Packed>& target,
			const std::array<Vector<T, Dim, Packed>, BaseDim>& bases,
			const std::array<bool, FlipDim>& flipAxes) {
	static_assert(BaseDim == Dim - 2, "You must provide 2 fewer bases than the dimension of the transform.");
	static_assert(Dim == FlipDim, "You must provide the same number of flips as the dimension of the transform.");
	return ViewBuilder<T, Dim, Packed>(eye, target, bases, flipAxes);
}


/// <summary> Creates a 2D look-at matrix. </summary>
/// <param name="eye"> The camera's position. </param>
/// <param name="target"> The camera's target. </param>
/// <param name="positiveYForward"> True if the camera looks towards +Y in camera space, false if -Y. </param>
/// <param name="flipX"> True to flip X in camera space. </param>
template <class T, bool Packed>
auto LookAt(const Vector<T, 2, Packed>& eye, const Vector<T, 2, Packed>& target, bool positiveYForward, bool flipX) {
	return LookAt(eye, target, std::array<Vector<T, 2, Packed>, 0>{}, std::array{ flipX, positiveYForward });
}


/// <summary> Creates a 3D look-at matrix. </summary>
/// <param name="eye"> The camera's position. </param>
/// <param name="target"> The camera's target. </param>
/// <param name="up"> Up direction in world space. </param>
/// <param name="positiveZForward"> True if the camera looks towards +Z in camera space, false if -Z. </param>
/// <param name="flipX"> True to flip X in camera space. </param>
/// <param name="flipY"> True to flip Y in camera space. </param>
/// <remarks> The camera space X is selected to be orthogonal to both the look direction and the <paramref name="up"/> vector.
///		Afterwards, the <paramref name="up"/> vector is re-orthogonalized to the camera-space Z and X vectors. </remarks>
template <class T, bool Packed>
auto LookAt(const Vector<T, 3, Packed>& eye, const Vector<T, 3, Packed>& target, const Vector<T, 3, Packed>& up, bool positiveZForward, bool flipX, bool flipY) {
	return LookAt(eye, target, std::array<Vector<T, 3, Packed>, 1>{ up }, std::array<bool, 3>{ flipX, flipY, positiveZForward });
}



} // namespace mathter