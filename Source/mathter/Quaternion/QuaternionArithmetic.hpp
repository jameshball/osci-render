// L=============================================================================
// L This software is distributed under the MIT license.
// L Copyright 2021 Péter Kardos
// L=============================================================================

#pragma once

#include "QuaternionImpl.hpp"

namespace mathter {


namespace impl {

	template <class T, bool Packed>
	Quaternion<T, Packed> Product(const Quaternion<T, Packed>& lhs, const Quaternion<T, Packed>& rhs) {
		if constexpr (IsBatched<T, 4, Packed>()) {
			using Vec4 = Vector<T, 4, Packed>;
			const auto lhsv = lhs.vec;
			const auto rhsv = rhs.vec;
			const Vec4 s1 = { -1, 1, -1, 1 };
			const Vec4 s2 = { -1, 1, 1, -1 };
			const Vec4 s3 = { -1, -1, 1, 1 };

			// [ 3, 2, 1, 0 ]
			// [ 0, 3, 2, 1 ]
			const Vec4 t0 = lhsv.xxxx;
			const Vec4 t1 = rhsv.xyzw;

			const Vec4 t2 = lhsv.yyyy;
			const Vec4 t3 = rhsv.yxwz;

			const Vec4 t4 = lhsv.zzzz;
			const Vec4 t5 = rhsv.zwxy;

			const Vec4 t6 = lhsv.wwww;
			const Vec4 t7 = rhsv.wzyx;

			const auto m0 = t0 * t1;
			const auto m1 = s1 * t2 * t3;
			const auto m2 = s2 * t4 * t5;
			const auto m3 = s3 * t6 * t7;

			const auto s = m0 + m1 + m2 + m3;

			const auto w = lhs.s * rhs.s - lhs.x * rhs.x - lhs.y * rhs.y - lhs.z * rhs.z;
			const auto x = lhs.s * rhs.x + lhs.x * rhs.s + lhs.y * rhs.z - lhs.z * rhs.y;
			const auto y = lhs.s * rhs.y - lhs.x * rhs.z + lhs.y * rhs.s + lhs.z * rhs.x;
			const auto z = lhs.s * rhs.z + lhs.x * rhs.y - lhs.y * rhs.x + lhs.z * rhs.s;

			return Quaternion<T, Packed>{ s };
		}
		else {
			const auto w = lhs.s * rhs.s - lhs.x * rhs.x - lhs.y * rhs.y - lhs.z * rhs.z;
			const auto x = lhs.s * rhs.x + lhs.x * rhs.s + lhs.y * rhs.z - lhs.z * rhs.y;
			const auto y = lhs.s * rhs.y - lhs.x * rhs.z + lhs.y * rhs.s + lhs.z * rhs.x;
			const auto z = lhs.s * rhs.z + lhs.x * rhs.y - lhs.y * rhs.x + lhs.z * rhs.s;
			return { w, x, y, z };
		}
	}
} // namespace impl



template <class T, bool Packed>
Quaternion<T, Packed>& operator+=(Quaternion<T, Packed>& lhs, const Quaternion<T, Packed>& rhs) {
	lhs.vec += rhs.vec;
	return lhs;
} // Helpers to write quaternion in paper-style such as (1 + 2_i + 3_j + 4_k). Slow performance, be careful.
template <class T, bool Packed>
Quaternion<T, Packed>& operator-=(Quaternion<T, Packed>& lhs, const Quaternion<T, Packed>& rhs) {
	lhs.vec -= rhs.vec;
	return lhs;
}

template <class T, bool Packed>
Quaternion<T, Packed>& operator*=(Quaternion<T, Packed>& lhs, const Quaternion<T, Packed>& rhs) {
	lhs = impl::Product(lhs, rhs);
	return lhs;
}

template <class T, bool Packed>
Quaternion<T, Packed>& operator*=(Quaternion<T, Packed>& lhs, T s) {
	lhs.vec *= s;
	return lhs;
}

template <class T, bool Packed>
Quaternion<T, Packed>& operator/=(Quaternion<T, Packed>& lhs, T s) {
	lhs *= T(1) / s;
	return lhs;
}

template <class T, bool Packed>
Quaternion<T, Packed> operator+(const Quaternion<T, Packed>& lhs, const Quaternion<T, Packed>& rhs) {
	Quaternion<T, Packed> copy(lhs);
	copy += rhs;
	return copy;
}

template <class T, bool Packed>
Quaternion<T, Packed> operator-(const Quaternion<T, Packed>& lhs, const Quaternion<T, Packed>& rhs) {
	Quaternion<T, Packed> copy(lhs);
	copy -= rhs;
	return copy;
}

template <class T, bool Packed>
Quaternion<T, Packed> operator*(const Quaternion<T, Packed>& lhs, const Quaternion<T, Packed>& rhs) {
	Quaternion<T, Packed> copy(lhs);
	copy *= rhs;
	return copy;
}

template <class T, bool Packed>
Quaternion<T, Packed> operator*(const Quaternion<T, Packed>& lhs, T s) {
	Quaternion<T, Packed> copy(lhs);
	copy *= s;
	return copy;
}

template <class T, bool Packed>
Quaternion<T, Packed> operator/(const Quaternion<T, Packed>& lhs, T s) {
	Quaternion<T, Packed> copy(lhs);
	copy /= s;
	return copy;
}

template <class T, bool Packed>
Quaternion<T, Packed> operator+(const Quaternion<T, Packed>& arg) {
	return arg;
}

template <class T, bool Packed>
Quaternion<T, Packed> operator-(const Quaternion<T, Packed>& arg) {
	return Quaternion(-arg.vec);
}


/// <summary> Multiplies all coefficients of the quaternion by <paramref name="s"/>. </summary>
template <class T, bool Packed, class U, class = typename std::enable_if<!std::is_same<U, Quaternion<T, Packed>>::value>::type>
Quaternion<T, Packed> operator*(U s, const Quaternion<T, Packed>& rhs) {
	return rhs * s;
}
/// <summary> Divides all coefficients of the quaternion by <paramref name="s"/>. </summary>
template <class T, bool Packed, class U, class = typename std::enable_if<!std::is_same<U, Quaternion<T, Packed>>::value>::type>
Quaternion<T, Packed> operator/(U s, const Quaternion<T, Packed>& rhs) {
	return rhs / s;
}

/// <summary> Adds a real to the real part of the quaternion. </summary>
template <class T, bool Packed, class U, class = typename std::enable_if<!traits::IsQuaternion<U>::value>::type>
Quaternion<T, Packed> operator+(const U& lhs, const Quaternion<T, Packed>& rhs) {
	return Quaternion<T, Packed>(rhs.w + lhs, rhs.x, rhs.y, rhs.z);
}



} // namespace mathter