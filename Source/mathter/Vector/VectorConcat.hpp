// L=============================================================================
// L This software is distributed under the MIT license.
// L Copyright 2021 Péter Kardos
// L=============================================================================

#pragma once

#include "VectorImpl.hpp"

namespace mathter {


/// <summary> Concatenates the arguments, and returns the concatenated vector. </summary>
template <class T, int Dim, bool Packed, class U>
mathter::Vector<T, Dim + 1, Packed> operator|(const mathter::Vector<T, Dim, Packed>& lhs, U rhs) {
	mathter::Vector<T, Dim + 1, Packed> ret;
	for (int i = 0; i < Dim; ++i) {
		ret(i) = lhs(i);
	}
	ret(Dim) = rhs;
	return ret;
}

/// <summary> Concatenates the arguments, and returns the concatenated vector. </summary>
template <class T1, int Dim1, class T2, int Dim2, bool Packed>
mathter::Vector<T1, Dim1 + Dim2, Packed> operator|(const mathter::Vector<T1, Dim1, Packed>& lhs, const mathter::Vector<T2, Dim2, Packed>& rhs) {
	mathter::Vector<T1, Dim1 + Dim2, Packed> ret;
	for (int i = 0; i < Dim1; ++i) {
		ret(i) = lhs(i);
	}
	for (int i = 0; i < Dim2; ++i) {
		ret(Dim1 + i) = rhs(i);
	}
	return ret;
}

/// <summary> Concatenates the arguments, and returns the concatenated vector. </summary>
template <class T, int Dim, bool Packed, class U>
mathter::Vector<T, Dim + 1, Packed> operator|(U lhs, const mathter::Vector<T, Dim, Packed>& rhs) {
	mathter::Vector<T, Dim + 1, Packed> ret;
	ret(0) = lhs;
	for (int i = 0; i < Dim; ++i) {
		ret(i + 1) = rhs(i);
	}
	return ret;
}

/// <summary> Concatenates the arguments, and returns the concatenated vector. </summary>
template <class VectorData1, int... Indices1, class VectorData2, int... Indices2>
auto operator|(const Swizzle<VectorData1, Indices1...>& lhs, const Swizzle<VectorData2, Indices2...>& rhs) {
	using TS1 = typename traits::VectorTraits<VectorData1>::Type;
	using TS2 = typename traits::VectorTraits<VectorData2>::Type;
	return Vector<TS1, sizeof...(Indices1), false>(lhs) | Vector<TS2, sizeof...(Indices2), false>(rhs);
}
/// <summary> Concatenates the arguments, and returns the concatenated vector. </summary>
template <class VectorData1, int... Indices1, class T2, int Dim, bool Packed>
auto operator|(const Swizzle<VectorData1, Indices1...>& lhs, const Vector<T2, Dim, Packed>& rhs) {
	using TS = typename traits::VectorTraits<VectorData1>::Type;
	return Vector<TS, sizeof...(Indices1), Packed>(lhs) | rhs;
}
/// <summary> Concatenates the arguments, and returns the concatenated vector. </summary>
template <class VectorData1, int... Indices1, class T2, int Dim, bool Packed>
auto operator|(const Vector<T2, Dim, Packed>& lhs, const Swizzle<VectorData1, Indices1...>& rhs) {
	using TS = typename traits::VectorTraits<VectorData1>::Type;
	return lhs | Vector<TS, sizeof...(Indices1), false>(rhs);
}
/// <summary> Concatenates the arguments, and returns the concatenated vector. </summary>
template <class VectorData1, int... Indices1, class U>
auto operator|(const Swizzle<VectorData1, Indices1...>& lhs, U rhs) {
	using TS = typename traits::VectorTraits<VectorData1>::Type;
	return Vector<TS, sizeof...(Indices1), false>(lhs) | rhs;
}
/// <summary> Concatenates the arguments, and returns the concatenated vector. </summary>
template <class VectorData1, int... Indices1, class U>
auto operator|(U lhs, const Swizzle<VectorData1, Indices1...>& rhs) {
	using TS = typename traits::VectorTraits<VectorData1>::Type;
	return lhs | Vector<TS, sizeof...(Indices1), false>(rhs);
}


} // namespace mathter