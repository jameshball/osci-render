// L=============================================================================
// L This software is distributed under the MIT license.
// L Copyright 2021 Péter Kardos
// L=============================================================================

#pragma once

#include "../Matrix/MatrixFunction.hpp"
#include "../Matrix/MatrixImpl.hpp"
#include "../Quaternion/QuaternionImpl.hpp"
#include "../Vector.hpp"
#include "IdentityBuilder.hpp"

#include <stdexcept>


namespace mathter {


template <class T>
class Rotation3DAxisBuilder {
public:
	Rotation3DAxisBuilder(const T& angle, int axis) : angle(angle), axis(axis) {}
	Rotation3DAxisBuilder& operator=(const Rotation3DAxisBuilder&) = delete;

	template <class U, eMatrixOrder Order, eMatrixLayout Layout, bool MPacked>
	operator Matrix<U, 4, 4, Order, Layout, MPacked>() const {
		Matrix<U, 4, 4, Order, Layout, MPacked> m;
		Set(m);
		return m;
	}

	template <class U, eMatrixOrder Order, eMatrixLayout Layout, bool MPacked>
	operator Matrix<U, 3, 3, Order, Layout, MPacked>() const {
		Matrix<U, 3, 3, Order, Layout, MPacked> m;
		Set(m);
		return m;
	}

	template <class U, eMatrixLayout Layout, bool MPacked>
	operator Matrix<U, 3, 4, eMatrixOrder::PRECEDE_VECTOR, Layout, MPacked>() const {
		Matrix<U, 3, 4, eMatrixOrder::PRECEDE_VECTOR, Layout, MPacked> m;
		Set(m);
		return m;
	}

	template <class U, eMatrixLayout Layout, bool MPacked>
	operator Matrix<U, 4, 3, eMatrixOrder::FOLLOW_VECTOR, Layout, MPacked>() const {
		Matrix<U, 4, 3, eMatrixOrder::FOLLOW_VECTOR, Layout, MPacked> m;
		Set(m);
		return m;
	}

	template <class U, bool QPacked>
	operator Quaternion<U, QPacked>() const;

private:
	template <class U, int Rows, int Columns, eMatrixOrder Order, eMatrixLayout Layout, bool MPacked>
	void Set(Matrix<U, Rows, Columns, Order, Layout, MPacked>& m) const {
		T C = cos(angle);
		T S = sin(angle);

		auto elem = [&m](int i, int j) -> U& {
			return Order == eMatrixOrder::FOLLOW_VECTOR ? m(i, j) : m(j, i);
		};

		assert(0 <= axis && axis < 3);

		// Indices according to follow vector order
		if (axis == 0) {
			// Rotate around X
			elem(0, 0) = U(1);
			elem(0, 1) = U(0);
			elem(0, 2) = U(0);
			elem(1, 0) = U(0);
			elem(1, 1) = U(C);
			elem(1, 2) = U(S);
			elem(2, 0) = U(0);
			elem(2, 1) = U(-S);
			elem(2, 2) = U(C);
		}
		else if (axis == 1) {
			// Rotate around Y
			elem(0, 0) = U(C);
			elem(0, 1) = U(0);
			elem(0, 2) = U(-S);
			elem(1, 0) = U(0);
			elem(1, 1) = U(1);
			elem(1, 2) = U(0);
			elem(2, 0) = U(S);
			elem(2, 1) = U(0);
			elem(2, 2) = U(C);
		}
		else {
			// Rotate around Z
			elem(0, 0) = U(C);
			elem(0, 1) = U(S);
			elem(0, 2) = U(0);
			elem(1, 0) = U(-S);
			elem(1, 1) = U(C);
			elem(1, 2) = U(0);
			elem(2, 0) = U(0);
			elem(2, 1) = U(0);
			elem(2, 2) = U(1);
		}

		// Rest
		for (int j = 0; j < m.ColumnCount(); ++j) {
			for (int i = (j < 3 ? 3 : 0); i < m.RowCount(); ++i) {
				m(i, j) = U(j == i);
			}
		}
	}

	const float angle;
	const int axis;
};



/// <summary> Rotates around coordinate axis. </summary>
/// <param name="axis"> 0 for X, 1 for Y, 2 for Z and so on... </param>
/// <param name="angle"> Angle of rotation in radians. </param>
/// <remarks> Positive angles rotate according to the right-hand rule in right-handed
///		coordinate systems (left-handed according to left-hand rule).
template <class T>
auto RotationAxis(T angle, int axis) {
	return Rotation3DAxisBuilder(angle, axis);
}

/// <summary> Rotates around coordinate axis. </summary>
/// <typeparam name="Axis"> 0 for X, 1 for Y, 2 for Z and so on... </typeparam>
/// <param name="angle"> Angle of rotation in radians. </param>
/// <remarks> Positive angles rotate according to the right-hand rule in right-handed
///		coordinate systems (left-handed according to left-hand rule).
template <int Axis, class T>
auto RotationAxis(T angle) {
	return Rotation3DAxisBuilder(angle, Axis);
}


/// <summary> Rotates around the X axis according to the right (left) hand rule in right (left) handed systems. </summary>
/// <param name="angle"> Angle of rotation in radians. </param>
template <class T>
auto RotationX(T angle) {
	return RotationAxis<0>(angle);
}

/// <summary> Rotates around the Y axis according to the right (left) hand rule in right (left) handed systems. </summary>
/// <param name="angle"> Angle of rotation in radians. </param>
template <class T>
auto RotationY(T angle) {
	return RotationAxis<1>(angle);
}

/// <summary> Rotates around the Z axis according to the right (left) hand rule in right (left) handed systems. </summary>
/// <param name="angle"> Angle of rotation in radians. </param>
template <class T>
auto RotationZ(T angle) {
	return RotationAxis<2>(angle);
}



template <class T>
class Rotation3DTriAxisBuilder {
public:
	Rotation3DTriAxisBuilder(const std::array<T, 3>& angles, std::array<int, 3> axes) : angles(angles), axes(axes) {}
	Rotation3DTriAxisBuilder& operator=(const Rotation3DTriAxisBuilder&) = delete;

	template <class U, eMatrixOrder Order, eMatrixLayout Layout, bool MPacked>
	operator Matrix<U, 4, 4, Order, Layout, MPacked>() const {
		Matrix<U, 4, 4, Order, Layout, MPacked> m;
		Set(m);
		return m;
	}

	template <class U, eMatrixOrder Order, eMatrixLayout Layout, bool MPacked>
	operator Matrix<U, 3, 3, Order, Layout, MPacked>() const {
		Matrix<U, 3, 3, Order, Layout, MPacked> m;
		Set(m);
		return m;
	}

	template <class U, eMatrixLayout Layout, bool MPacked>
	operator Matrix<U, 3, 4, eMatrixOrder::PRECEDE_VECTOR, Layout, MPacked>() const {
		Matrix<U, 3, 4, eMatrixOrder::PRECEDE_VECTOR, Layout, MPacked> m;
		Set(m);
		return m;
	}

	template <class U, eMatrixLayout Layout, bool MPacked>
	operator Matrix<U, 4, 3, eMatrixOrder::FOLLOW_VECTOR, Layout, MPacked>() const {
		Matrix<U, 4, 3, eMatrixOrder::FOLLOW_VECTOR, Layout, MPacked> m;
		Set(m);
		return m;
	}

	template <class U, bool QPacked>
	operator Quaternion<U, QPacked>() const;

private:
	template <class U, int Rows, int Columns, eMatrixOrder Order, eMatrixLayout Layout, bool MPacked>
	void Set(Matrix<U, Rows, Columns, Order, Layout, MPacked>& m) const {
		using MatT = Matrix<U, 3, 3, Order, Layout, MPacked>;
		if constexpr (Order == eMatrixOrder::FOLLOW_VECTOR) {
			m.template Submatrix<3, 3>(0, 0) = MatT(RotationAxis(angles[0], axes[0])) * MatT(RotationAxis(angles[1], axes[1])) * MatT(RotationAxis(angles[2], axes[2]));
		}
		else {
			m.template Submatrix<3, 3>(0, 0) = MatT(RotationAxis(angles[2], axes[2])) * MatT(RotationAxis(angles[1], axes[1])) * MatT(RotationAxis(angles[0], axes[0]));
		}

		// Rest
		for (int j = 0; j < m.ColumnCount(); ++j) {
			for (int i = (j < 3 ? 3 : 0); i < m.RowCount(); ++i) {
				m(i, j) = U(j == i);
			}
		}
	}

	const std::array<T, 3> angles;
	const std::array<int, 3> axes;
};



/// <summary> Rotates around three axes in succession. </summary>
/// <remarks> Axes: 0 for X, 1 for Y and 2 for Z.
///		Angles in radians. Each rotation according to the right (and left) hand rule in right (and left) handed systems. </remarks>
template <int FirstAxis, int SecondAxis, int ThirdAxis, class T>
auto RotationAxis3(T angle0, T angle1, T angle2) {
	return Rotation3DTriAxisBuilder(std::array<T, 3>{ angle0, angle1, angle2 }, std::array<int, 3>{ FirstAxis, SecondAxis, ThirdAxis });
}

/// <summary> Rotation matrix from Euler angles. Rotations are Z-X-Z. </summary>
/// <param name="z1"> Angle of the first rotation around Z in radians. </param>
/// <param name="x2"> Angle of the second rotation around X in radians. </param>
/// <param name="z3"> Angle of the third rotation around Z in radians. </param>
/// <remarks> Each rotation according to the right (and left) hand rule in right (and left) handed systems. </remarks>
template <class T>
auto RotationEuler(T z1, T x2, T z3) {
	return RotationAxis3<2, 0, 2>(z1, x2, z3);
}

/// <summary> Rotation matrix from roll-pitch-yaw angles. Rotations are X-Y-Z. </summary>
/// <param name="z1"> Angle of the first rotation around X in radians. </param>
/// <param name="x2"> Angle of the second rotation around Y in radians. </param>
/// <param name="z3"> Angle of the third rotation around Z in radians. </param>
/// /// <remarks> Each rotation according to the right (and left) hand rule in right (and left) handed systems. </remarks>
template <class T>
auto RotationRPY(T x1, T y2, T z3) {
	return RotationAxis3<0, 1, 2>(x1, y2, z3);
}



template <class T, bool Packed>
class Rotation3DAxisAngleBuilder {
public:
	Rotation3DAxisAngleBuilder(const Vector<T, 3, Packed>& axis, T angle) : axis(axis), angle(angle) {}
	Rotation3DAxisAngleBuilder& operator=(const Rotation3DAxisAngleBuilder&) = delete;

	template <class U, eMatrixOrder Order, eMatrixLayout Layout, bool MPacked>
	operator Matrix<U, 4, 4, Order, Layout, MPacked>() const {
		Matrix<U, 4, 4, Order, Layout, MPacked> m;
		Set(m);
		return m;
	}

	template <class U, eMatrixOrder Order, eMatrixLayout Layout, bool MPacked>
	operator Matrix<U, 3, 3, Order, Layout, MPacked>() const {
		Matrix<U, 3, 3, Order, Layout, MPacked> m;
		Set(m);
		return m;
	}

	template <class U, eMatrixLayout Layout, bool MPacked>
	operator Matrix<U, 3, 4, eMatrixOrder::PRECEDE_VECTOR, Layout, MPacked>() const {
		Matrix<U, 3, 4, eMatrixOrder::PRECEDE_VECTOR, Layout, MPacked> m;
		Set(m);
		return m;
	}

	template <class U, eMatrixLayout Layout, bool MPacked>
	operator Matrix<U, 4, 3, eMatrixOrder::FOLLOW_VECTOR, Layout, MPacked>() const {
		Matrix<U, 4, 3, eMatrixOrder::FOLLOW_VECTOR, Layout, MPacked> m;
		Set(m);
		return m;
	}

	template <class U, bool QPacked>
	operator Quaternion<U, QPacked>() const;

private:
	template <class U, int Rows, int Columns, eMatrixOrder Order, eMatrixLayout Layout, bool MPacked>
	void Set(Matrix<U, Rows, Columns, Order, Layout, MPacked>& m) const {
		assert(IsNormalized(axis));

		T C = cos(angle);
		T S = sin(angle);

		// 3x3 rotation sub-matrix
		using RotMat = Matrix<U, 3, 3, eMatrixOrder::FOLLOW_VECTOR, Layout, Packed>;
		Matrix<U, 3, 1, eMatrixOrder::FOLLOW_VECTOR, Layout, Packed> u(axis(0), axis(1), axis(2));
		RotMat cross = {
			U(0), -u(2), u(1),
			u(2), U(0), -u(0),
			-u(1), u(0), U(0)
		};
		RotMat rot = C * RotMat(Identity()) + S * cross + (1 - C) * (u * Transpose(u));


		// Elements
		auto elem = [&m](int i, int j) -> U& {
			return Order == eMatrixOrder::PRECEDE_VECTOR ? m(i, j) : m(j, i);
		};
		for (int j = 0; j < 3; ++j) {
			for (int i = 0; i < 3; ++i) {
				elem(i, j) = rot(i, j);
			}
		}

		// Rest
		for (int j = 0; j < m.Width(); ++j) {
			for (int i = (j < 3 ? 3 : 0); i < m.Height(); ++i) {
				m(i, j) = U(j == i);
			}
		}
	}

	const Vector<T, 3, Packed> axis;
	const T angle;
};

/// <summary> Rotates around an arbitrary axis. </summary>
/// <param name="axis"> Axis of rotation, must be normalized. </param>
/// <param name="angle"> Angle of rotation in radians. </param>
/// <remarks> Right-hand (left-hand) rule is followed in right-handed (left-handed) systems. </remarks>
template <class T, bool Vpacked, class U>
auto RotationAxisAngle(const Vector<T, 3, Vpacked>& axis, U angle) {
	return Rotation3DAxisAngleBuilder(axis, T(angle));
}


/// <summary> Determines if the matrix is a proper rotation matrix. </summary>
/// <remarks> Proper rotation matrices are orthogonal and have a determinant of +1. </remarks>
template <class T, int Rows, int Columns, eMatrixOrder Order, eMatrixLayout Layout, bool Packed>
bool IsRotationMatrix3D(const Matrix<T, Rows, Columns, Order, Layout, Packed>& m) {
	static_assert(Rows == 3 || Rows == 4);
	static_assert(Columns == 3 || Columns == 4);
	Vector<T, 3> rows[3] = {
		{ m(0, 0), m(0, 1), m(0, 2) },
		{ m(1, 0), m(1, 1), m(1, 2) },
		{ m(2, 0), m(2, 1), m(2, 2) },
	};
	return (std::abs(Dot(rows[0], rows[1])) + std::abs(Dot(rows[0], rows[2])) + std::abs(Dot(rows[1], rows[2]))) < T(0.0005) // rows are orthogonal to each other
		   && IsNormalized(rows[0]) && IsNormalized(rows[1]) && IsNormalized(rows[2]) // all rows are normalized
		   && Determinant(Matrix<T, 3, 3, Order, Layout, Packed>(m.template Submatrix<3, 3>(0, 0))) > 0; // not an improper rotation
}


template <class T>
template <class U, bool QPacked>
Rotation3DAxisBuilder<T>::operator Quaternion<U, QPacked>() const {
	using QuatT = Quaternion<U, QPacked>;
	switch (axis) {
		case 0: return QuatT(RotationAxisAngle(Vector<U, 3, QPacked>(1, 0, 0), angle));
		case 1: return QuatT(RotationAxisAngle(Vector<U, 3, QPacked>(0, 1, 0), angle));
		case 2: return QuatT(RotationAxisAngle(Vector<U, 3, QPacked>(0, 0, 1), angle));
	}
	assert(false);
	throw std::invalid_argument("Axis must be 0, 1, or 2.");
}

template <class T>
template <class U, bool QPacked>
Rotation3DTriAxisBuilder<T>::operator Quaternion<U, QPacked>() const {
	using QuatT = Quaternion<U, QPacked>;
	return QuatT(RotationAxis(angles[2], axes[2])) * QuatT(RotationAxis(angles[1], axes[1])) * QuatT(RotationAxis(angles[0], axes[0]));
}

template <class T, bool Packed>
template <class U, bool QPacked>
Rotation3DAxisAngleBuilder<T, Packed>::operator Quaternion<U, QPacked>() const {
	auto halfAngle = U(angle) * U(0.5);
	return Quaternion(std::cos(halfAngle), Vector<U, 3, QPacked>(axis) * std::sin(halfAngle));
}



} // namespace mathter