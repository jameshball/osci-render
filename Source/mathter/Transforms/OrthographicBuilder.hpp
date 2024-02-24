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
class OrthographicBuilder {
	static_assert(!std::is_integral_v<T>);
	using VectorT = Vector<T, Dim, Packed>;

public:
	OrthographicBuilder(const VectorT& minBounds, const VectorT& maxBounds, T projNearPlane, T projFarPlane)
		: minBounds(minBounds), maxBounds(maxBounds), projNearPlane(projNearPlane), projFarPlane(projFarPlane) {}
	OrthographicBuilder& operator=(const OrthographicBuilder&) = delete;

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
		using VectorT = Vector<T, Dim, Packed>;

		VectorT volumeSize = maxBounds - minBounds;
		VectorT scale = T(2) / volumeSize;
		scale[scale.Dimension() - 1] *= T(0.5) * (projFarPlane - projNearPlane);
		VectorT offset = -(maxBounds + minBounds) / T(2) * scale;
		offset[offset.Dimension() - 1] += (projFarPlane + projNearPlane) / 2;

		m = Identity();
		for (int i = 0; i < scale.Dimension(); ++i) {
			m(i, i) = scale(i);
			(Order == eMatrixOrder::FOLLOW_VECTOR ? m(scale.Dimension(), i) : m(i, scale.Dimension())) = offset(i);
		}
	}

	const Vector<T, Dim, Packed> minBounds, maxBounds;
	T projNearPlane, projFarPlane;
};


/// <summary> Creates an orthographics projection matrix. The volume before projection
///		is an axis-aligned hypercube and it is projected onto a unit hypercube. </summary>
/// <param name="minBounds"> The "left" corner of the hypercube. </param>
/// <param name="maxBounds"> The "right" corner of the hypercube. </param>
/// <param name="projNearPlane"> The lower bound of the last axis of the projected volume (Z axis in 3D). </param>
/// <param name="projFarPlane"> The upper bound of the last axis of the projected volume (Z axis in 3D). </param>
/// <remarks> After projection, all axes range from -1 to 1, except for the last axis, which is specified explicitly. </remarks>
template <class T, int Dim, bool Packed>
auto Orthographic(const Vector<T, Dim, Packed>& minBounds, const Vector<T, Dim, Packed>& maxBounds, T projNearPlane, T projFarPlane) {
	if constexpr (std::is_integral_v<T>) {
		using VectorT = Vector<float, Dim, false>;
		return OrthographicBuilder(VectorT(minBounds), VectorT(maxBounds), float(projNearPlane), float(projFarPlane));
	}
	else {
		return OrthographicBuilder(minBounds, maxBounds, projNearPlane, projFarPlane);
	}
}


} // namespace mathter