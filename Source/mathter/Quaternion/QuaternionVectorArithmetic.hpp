// L=============================================================================
// L This software is distributed under the MIT license.
// L Copyright 2021 Péter Kardos
// L=============================================================================

#pragma once

#include "QuaternionImpl.hpp"

namespace mathter {


/// <summary> Rotates (and scales) vector by quaternion. </summary>
template <class T, bool QPacked, bool PackedA>
Vector<T, 3, PackedA> operator*(const Quaternion<T, QPacked>& q, const Vector<T, 3, PackedA>& vec) {
	// sandwich product
	return Vector<T, 3, PackedA>(q * Quaternion<T, QPacked>(vec) * Inverse(q));
}

/// <summary> Rotates (and scales) vector by quaternion. </summary>
template <class T, bool QPacked, bool PackedA>
Vector<T, 3, PackedA> operator*(const Vector<T, 3, PackedA>& vec, const Quaternion<T, QPacked>& q) {
	// sandwich product
	return Vector<T, 3, PackedA>(q * Quaternion<T, QPacked>(vec) * Inverse(q));
}

/// <summary> Rotates (and scales) vector by quaternion. </summary>
template <class T, bool QPacked, bool PackedA>
Vector<T, 3, PackedA>& operator*=(Vector<T, 3, PackedA>& vec, const Quaternion<T, QPacked>& q) {
	// sandwich product
	return vec = Vector<T, 3, PackedA>(q * Quaternion<T, QPacked>(vec) * Inverse(q));
}


} // namespace mathter