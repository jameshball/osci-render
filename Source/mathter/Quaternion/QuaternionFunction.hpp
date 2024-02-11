// L=============================================================================
// L This software is distributed under the MIT license.
// L Copyright 2021 Péter Kardos
// L=============================================================================

#pragma once

#include "QuaternionImpl.hpp"

namespace mathter {

/// <summary> The euclidean length of the vector of the 4 elements of the quaternion. </summary>
template <class T, bool Packed>
T Abs(const Quaternion<T, Packed>& q) {
	return Length(q.vec);
}

/// <summary> Negates the imaginary values of the quaternion. </summary>
template <class T, bool Packed>
Quaternion<T, Packed> Conjugate(const Quaternion<T, Packed>& q) {
	return Quaternion<T, Packed>{ q.vec * Vector<T, 4, Packed>{ T(1), T(-1), T(-1), T(-1) } };
}

/// <summary> Natural quaternion exponentiation, base e. </summary>
template <class T, bool Packed>
Quaternion<T, Packed> Exp(const Quaternion<T, Packed>& q) {
	auto a = q.ScalarPart();
	auto v = q.VectorPart();
	T mag = Length(v);
	T es = exp(a);

	Quaternion<T, Packed> ret = { std::cos(mag), v * (std::sin(mag) / mag) };
	ret *= es;

	return ret;
}

/// <summary> Natural quaternion logarithm, base e. </summary>
template <class T, bool Packed>
Quaternion<T, Packed> Log(const Quaternion<T, Packed>& q) {
	auto magq = Length(q);
	auto vn = Normalize(q.VectorPart());

	Quaternion ret = { std::log(magq), vn * std::acos(q.s / magq) };
	return ret;
}

/// <summary> Raises <paramref name="q"/> to the power of <paramref name="a"/>. </summary>
template <class T, bool Packed>
Quaternion<T, Packed> Pow(const Quaternion<T, Packed>& q, T a) {
	return Exp(a * Log(q));
}

/// <summary> Returns the square of the absolute value. </summary>
/// <remarks> Just like complex numbers, it's the square of the length of the vector formed by the coefficients.
///			This is much faster than <see cref="Length">. </remarks>
template <class T, bool Packed>
T LengthSquared(const Quaternion<T, Packed>& q) {
	return LengthSquared(q.vec);
}

/// <summary> Returns the absolute value of the quaternion. </summary>
/// <remarks> Just like complex numbers, it's the length of the vector formed by the coefficients. </remarks>
template <class T, bool Packed>
T Length(const Quaternion<T, Packed>& q) {
	return Abs(q);
}

/// <summary> Returns the unit quaternion of the same direction. Does not change this object. </summary>
template <class T, bool Packed>
Quaternion<T, Packed> Normalize(const Quaternion<T, Packed>& q) {
	return Quaternion<T, Packed>{ Normalize(q.vec) };
}

/// <summary> Returns the quaternion of opposite rotation. </summary>
template <class T, bool Packed>
Quaternion<T, Packed> Inverse(const Quaternion<T, Packed>& q) {
	return Conjugate(q);
}

/// <summary> Check if the quaternion is a unit quaternion, with some tolerance for floats. </summary>
template <class T, bool Packed>
bool IsNormalized(const Quaternion<T, Packed>& q) {
	return IsNormalized(q.vec);
}


} // namespace mathter