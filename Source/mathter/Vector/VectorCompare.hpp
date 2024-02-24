// L=============================================================================
// L This software is distributed under the MIT license.
// L Copyright 2021 Péter Kardos
// L=============================================================================

#pragma once

#include "VectorImpl.hpp"

namespace mathter {

/// <summary> Exactly compares two vectors. </summary>
/// <remarks> &lt;The usual warning about floating point numbers&gt; </remarks>
template <class T, int Dim, bool Packed>
bool operator==(const Vector<T, Dim, Packed>& lhs, const Vector<T, Dim, Packed>& rhs) {
	bool same = lhs[0] == rhs[0];
	for (int i = 1; i < Dim; ++i) {
		same = same && lhs[i] == rhs[i];
	}
	return same;
}

/// <summary> Exactly compares two vectors. </summary>
/// <remarks> &lt;The usual warning about floating point numbers&gt; </remarks>
template <class T, int Dim, bool Packed>
bool operator!=(const Vector<T, Dim, Packed>& lhs, const Vector<T, Dim, Packed>& rhs) {
	return !operator==(rhs);
}

} // namespace mathter