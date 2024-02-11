// L=============================================================================
// L This software is distributed under the MIT license.
// L Copyright 2021 Péter Kardos
// L=============================================================================

#pragma once


#include "../Common/MathUtil.hpp"
#include "../Common/Traits.hpp"
#include "../Matrix.hpp"
#include "../Utility.hpp"
#include "../Vector.hpp"

#include <cmath>
#include <limits>


namespace mathter {


/// <summary> Allows you to do quaternion math and represent rotation in a compact way. </summary>
/// <typeparam name="T"> The scalar type of w, x, y and z. Use a builtin or custom floating or fixed point type. </typeparam>
/// <typeparam name="Packed"> If true, tightly packs quaternion members and disables padding due to overalignment in arrays.
///		Disables SIMD optimization. </typeparam>
/// <remarks>
/// These are plain mathematical quaternions, so expect the operations to work as mathematically defined.
/// There are helper functions to represent rotation with quaternions.
/// </remarks>
template <class T, bool Packed = false>
class Quaternion {
public:
	union {
		struct {
			T s, i, j, k;
		};
		struct {
			T w, x, y, z;
		};
		Vector<T, 4, Packed> vec;
	};

	//-----------------------------------------------
	// Constructors
	//-----------------------------------------------
	/// <summary> Does NOT zero-initialize values. </summary>
	Quaternion() : vec() {}

	Quaternion(const Quaternion& rhs) : vec(rhs.vec) {}

	/// <summary> Set values directly. </summary>
	Quaternion(T scalar, T x, T y, T z) : w(scalar), x(x), y(y), z(z) {}

	/// <summary> Sets the scalar part (w) and the vector part (xyz). This is not <see cref="AxisAngle"/> rotation. </summary>
	Quaternion(T scalar, const Vector<T, 3, true>& vector) : w(scalar), x(vector.x), y(vector.y), z(vector.z) {}

	/// <summary> Sets the scalar part (w) and the vector part (xyz). This is not <see cref="AxisAngle"/> rotation. </summary>
	Quaternion(T scalar, const Vector<T, 3, false>& vector) : w(scalar), x(vector.x), y(vector.y), z(vector.z) {}

	/// <summary> Sets the scalar part to zero, and the vector part to given argument. </summary>
	explicit Quaternion(const Vector<T, 3, true>& vector) : Quaternion(0, vector) {}

	/// <summary> Sets the scalar part to zero, and the vector part to given argument. </summary>
	explicit Quaternion(const Vector<T, 3, false>& vector) : Quaternion(0, vector) {}

	template <class U, bool P>
	Quaternion(const Quaternion<U, P>& rhs) : vec(rhs.vec) {}

	/// <summary> Convert a rotation matrix to equivalent quaternion. </summary>
	/// <remarks> Matrix must be in SO(3). </remarks>
	template <class U, eMatrixOrder Order, eMatrixLayout Layout, bool PackedA>
	explicit Quaternion(const Matrix<U, 3, 3, Order, Layout, PackedA>& rhs) {
		FromMatrix(rhs);
	}
	/// <summary> Convert a rotation matrix to equivalent quaternion. </summary>
	/// <remarks> Matrix must be in SO(3). Translation part is ignored. </remarks>
	template <class U, eMatrixLayout Layout, bool PackedA>
	explicit Quaternion(const Matrix<U, 3, 4, eMatrixOrder::PRECEDE_VECTOR, Layout, PackedA>& rhs) {
		FromMatrix(rhs);
	}
	/// <summary> Convert a rotation matrix to equivalent quaternion. </summary>
	/// <remarks> Matrix must be in SO(3). Translation part is ignored. </remarks>
	template <class U, eMatrixLayout Layout, bool PackedA>
	explicit Quaternion(const Matrix<U, 4, 3, eMatrixOrder::FOLLOW_VECTOR, Layout, PackedA>& rhs) {
		FromMatrix(rhs);
	}
	/// <summary> Convert a rotation matrix to equivalent quaternion. </summary>
	/// <remarks> Matrix must be in SO(3). Translation part is ignored. </remarks>
	template <class U, eMatrixOrder Order, eMatrixLayout Layout, bool PackedA>
	explicit Quaternion(const Matrix<U, 4, 4, Order, Layout, PackedA>& rhs) {
		FromMatrix(rhs);
	}

	explicit Quaternion(const Vector<T, 4, Packed>& vec) : vec(vec) {}

	//-----------------------------------------------
	// Assignment
	//-----------------------------------------------
	Quaternion& operator=(const Quaternion& rhs) {
		vec = rhs.vec;
		return *this;
	}

	/// <summary> Convert from quaternion with different base type and packing. </summary>
	template <class U, bool P>
	Quaternion& operator=(const Quaternion<U, P>& rhs) {
		vec = rhs.vec;
		return *this;
	}


	/// <summary> Convert a rotation matrix to equivalent quaternion. </summary>
	/// <remarks> Matrix must be in SO(3). </remarks>
	template <class U, eMatrixOrder Order, eMatrixLayout Layout, bool PackedA>
	Quaternion& operator=(const Matrix<U, 3, 3, Order, Layout, PackedA>& rhs) {
		FromMatrix(rhs);
		return *this;
	}
	/// <summary> Convert a rotation matrix to equivalent quaternion. </summary>
	/// <remarks> Matrix must be in SO(3). Translation part is ignored. </remarks>
	template <class U, eMatrixLayout Layout, bool PackedA>
	Quaternion& operator=(const Matrix<U, 3, 4, eMatrixOrder::PRECEDE_VECTOR, Layout, PackedA>& rhs) {
		FromMatrix(rhs);
		return *this;
	}
	/// <summary> Convert a rotation matrix to equivalent quaternion. </summary>
	/// <remarks> Matrix must be in SO(3). Translation part is ignored. </remarks>
	template <class U, eMatrixLayout Layout, bool PackedA>
	Quaternion& operator=(const Matrix<U, 4, 3, eMatrixOrder::FOLLOW_VECTOR, Layout, PackedA>& rhs) {
		FromMatrix(rhs);
		return *this;
	}
	/// <summary> Convert a rotation matrix to equivalent quaternion. </summary>
	/// <remarks> Matrix must be in SO(3). Translation part is ignored. </remarks>
	template <class U, eMatrixOrder Order, eMatrixLayout Layout, bool PackedA>
	Quaternion& operator=(const Matrix<U, 4, 4, Order, Layout, PackedA>& rhs) {
		FromMatrix(rhs);
		return *this;
	}

	//-----------------------------------------------
	// Functions
	//-----------------------------------------------

	/// <summary> Returns the scalar part (w) of (w + xi + yj + zk). </summary>
	const T ScalarPart() const {
		return s;
	}
	/// <summary> Returns the vector part (x, y, z) of (w + xi + yj + zk). </summary>
	const Vector<T, 3, Packed> VectorPart() const {
		return { x, y, z };
	}

	/// <summary> Returns the angle of the rotation represented by quaternion. </summary>
	/// <remarks> Only valid for unit quaternions. </remarks>
	const T Angle() const {
		return impl::sign_nonzero(s) * 2 * std::acos(std::clamp(std::abs(s) / Length(vec), T(-1), T(1)));
	}
	/// <summary> Returns the axis of rotation represented by quaternion. </summary>
	/// <remarks> Only valid for unit quaternions. Returns (1,0,0) for near 180 degree rotations. </remarks>
	const Vector<T, 3, Packed> Axis() const {
		auto direction = VectorPart();
		return SafeNormalize(direction);
	}

	//-----------------------------------------------
	// Matrix conversions
	//-----------------------------------------------

	/// <summary> Creates a rotation matrix equivalent to the quaternion. </summary>
	template <class U, eMatrixOrder Order, eMatrixLayout Layout, bool PackedA>
	explicit operator Matrix<U, 3, 3, Order, Layout, PackedA>() const {
		return ToMatrix<U, 3, 3, Order, Layout, PackedA>();
	}
	/// <summary> Creates a rotation matrix equivalent to the quaternion. </summary>
	template <class U, eMatrixLayout Layout, bool PackedA>
	explicit operator Matrix<U, 3, 4, eMatrixOrder::PRECEDE_VECTOR, Layout, PackedA>() const {
		return ToMatrix<U, 3, 4, eMatrixOrder::PRECEDE_VECTOR, Layout, PackedA>();
	}
	/// <summary> Creates a rotation matrix equivalent to the quaternion. </summary>
	template <class U, eMatrixLayout Layout, bool PackedA>
	explicit operator Matrix<U, 4, 3, eMatrixOrder::FOLLOW_VECTOR, Layout, PackedA>() const {
		return ToMatrix<U, 4, 3, eMatrixOrder::FOLLOW_VECTOR, Layout, PackedA>();
	}
	/// <summary> Creates a rotation matrix equivalent to the quaternion. </summary>
	template <class U, eMatrixOrder Order, eMatrixLayout Layout, bool PackedA>
	explicit operator Matrix<U, 4, 4, Order, Layout, PackedA>() const {
		return ToMatrix<U, 4, 4, Order, Layout, PackedA>();
	}

	//-----------------------------------------------
	// Truncate to vector
	//-----------------------------------------------

	/// <summary> Truncates the quaternion to the vector part (x, y, z). </summary>
	template <class U, bool PackedA>
	explicit operator Vector<U, 3, PackedA>() const {
		return { x, y, z };
	}

protected:
	//-----------------------------------------------
	// Matrix conversion helpers
	//-----------------------------------------------
	template <class U, int Rows, int Columns, eMatrixOrder Order, eMatrixLayout Layout, bool PackedA>
	Matrix<U, Rows, Columns, Order, Layout, PackedA> ToMatrix() const {
		assert(IsNormalized(vec));
		Matrix<U, Rows, Columns, Order, Layout, PackedA> mat;
		auto elem = [&mat](int i, int j) -> U& {
			return Order == eMatrixOrder::PRECEDE_VECTOR ? mat(i, j) : mat(j, i);
		};
		elem(0, 0) = 1 - 2 * (j * j + k * k);
		elem(0, 1) = 2 * (i * j - k * s);
		elem(0, 2) = 2 * (i * k + j * s);
		elem(1, 0) = 2 * (i * j + k * s);
		elem(1, 1) = 1 - 2 * (i * i + k * k);
		elem(1, 2) = 2 * (j * k - i * s);
		elem(2, 0) = 2 * (i * k - j * s);
		elem(2, 1) = 2 * (j * k + i * s);
		elem(2, 2) = 1 - 2 * (i * i + j * j);

		// Rest
		for (int j = 0; j < mat.Width(); ++j) {
			for (int i = (j < 3 ? 3 : 0); i < mat.Height(); ++i) {
				mat(i, j) = T(j == i);
			}
		}

		return mat;
	}

	template <class U, int Rows, int Columns, eMatrixOrder Order, eMatrixLayout Layout, bool PackedA>
	void FromMatrix(const Matrix<U, Rows, Columns, Order, Layout, PackedA>& mat) {
		assert(IsRotationMatrix3D(mat));
		auto elem = [&mat](int i, int j) -> U {
			return Order == eMatrixOrder::PRECEDE_VECTOR ? mat(i, j) : mat(j, i);
		};
		w = std::sqrt(1 + elem(0, 0) + elem(1, 1) + elem(2, 2)) * T(0.5);
		T div = T(1) / (T(4) * w);
		x = (elem(2, 1) - elem(1, 2)) * div;
		y = (elem(0, 2) - elem(2, 0)) * div;
		z = (elem(1, 0) - elem(0, 1)) * div;
	}
};


} // namespace mathter