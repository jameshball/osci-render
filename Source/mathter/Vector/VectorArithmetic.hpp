// L=============================================================================
// L This software is distributed under the MIT license.
// L Copyright 2021 Péter Kardos
// L=============================================================================

#pragma once

#include "VectorImpl.hpp"

#include <functional>


namespace mathter {


//------------------------------------------------------------------------------
// Utility
//------------------------------------------------------------------------------

template <class T, int Dim, bool Packed, class Fun, size_t... Indices>
inline auto DoElementwiseOp(const Vector<T, Dim, Packed>& lhs, const Vector<T, Dim, Packed>& rhs, Fun&& fun, std::index_sequence<Indices...>) {
	return Vector<T, Dim, Packed>{ fun(lhs[Indices], rhs[Indices])... };
}

template <class T, int Dim, bool Packed, class Fun>
inline auto DoBinaryOp(const Vector<T, Dim, Packed>& lhs, const Vector<T, Dim, Packed>& rhs, Fun&& fun) {
	if constexpr (IsBatched<T, Dim, Packed>()) {
		using B = Batch<T, Dim, Packed>;
		return Vector<T, Dim, Packed>{ fun(B::load_unaligned(lhs.data()), B::load_unaligned(rhs.data())) };
	}
	else {
		return DoElementwiseOp(lhs, rhs, fun, std::make_index_sequence<Dim>{});
	}
}

template <class T, int Dim, bool Packed, class S, class Fun, size_t... Indices, std::enable_if_t<!traits::IsVector<S>::value, int> = 0>
inline auto DoElementwiseOp(const Vector<T, Dim, Packed>& lhs, const S& rhs, Fun&& fun, std::index_sequence<Indices...>) {
	using R = std::common_type_t<T, S>;
	return Vector<R, Dim, Packed>{ fun(R(lhs[Indices]), R(rhs))... };
}

template <class T, int Dim, bool Packed, class S, class Fun, std::enable_if_t<!traits::IsVector<S>::value, int> = 0>
inline auto DoBinaryOp(const Vector<T, Dim, Packed>& lhs, const S& rhs, Fun&& fun) {
	using R = std::common_type_t<T, S>;
	if constexpr (IsBatched<R, Dim, Packed>()
				  && IsBatched<T, Dim, Packed>()
				  && std::is_convertible_v<Batch<T, Dim, Packed>, Batch<R, Dim, Packed>>) {
		using TB = Batch<T, Dim, Packed>;
		using RB = Batch<T, Dim, Packed>;
		return Vector<T, Dim, Packed>{ fun(RB(TB::load_unaligned(lhs.data())), RB(R(rhs))) };
	}
	else {
		return DoElementwiseOp(lhs, rhs, fun, std::make_index_sequence<Dim>{});
	}
}


//------------------------------------------------------------------------------
// Vector arithmetic
//------------------------------------------------------------------------------

/// <summary> Elementwise (Hadamard) vector product. </summary>
template <class T, int Dim, bool Packed>
inline auto operator*(const Vector<T, Dim, Packed>& lhs, const Vector<T, Dim, Packed>& rhs) {
	return DoBinaryOp(lhs, rhs, std::multiplies{});
}

/// <summary> Elementwise vector division. </summary>
template <class T, int Dim, bool Packed>
inline Vector<T, Dim, Packed> operator/(const Vector<T, Dim, Packed>& lhs, const Vector<T, Dim, Packed>& rhs) {
	return DoBinaryOp(lhs, rhs, std::divides{});
}

/// <summary> Elementwise vector addition. </summary>
template <class T, int Dim, bool Packed>
inline Vector<T, Dim, Packed> operator+(const Vector<T, Dim, Packed>& lhs, const Vector<T, Dim, Packed>& rhs) {
	return DoBinaryOp(lhs, rhs, std::plus{});
}

/// <summary> Elementwise vector subtraction. </summary>
template <class T, int Dim, bool Packed>
inline Vector<T, Dim, Packed> operator-(const Vector<T, Dim, Packed>& lhs, const Vector<T, Dim, Packed>& rhs) {
	return DoBinaryOp(lhs, rhs, std::minus{});
}

//------------------------------------------------------------------------------
// Vector assign arithmetic
//------------------------------------------------------------------------------

/// <summary> Elementwise (Hadamard) vector product. </summary>
template <class T, int Dim, bool Packed>
inline Vector<T, Dim, Packed>& operator*=(Vector<T, Dim, Packed>& lhs, const Vector<T, Dim, Packed>& rhs) {
	return lhs = lhs * rhs;
}

/// <summary> Elementwise vector division. </summary>
template <class T, int Dim, bool Packed>
inline Vector<T, Dim, Packed>& operator/=(Vector<T, Dim, Packed>& lhs, const Vector<T, Dim, Packed>& rhs) {
	return lhs = lhs / rhs;
}

/// <summary> Elementwise vector addition. </summary>
template <class T, int Dim, bool Packed>
inline Vector<T, Dim, Packed>& operator+=(Vector<T, Dim, Packed>& lhs, const Vector<T, Dim, Packed>& rhs) {
	return lhs = lhs + rhs;
}

/// <summary> Elementwise vector subtraction. </summary>
template <class T, int Dim, bool Packed>
inline Vector<T, Dim, Packed>& operator-=(Vector<T, Dim, Packed>& lhs, const Vector<T, Dim, Packed>& rhs) {
	return lhs = lhs - rhs;
}


//------------------------------------------------------------------------------
// Scalar arithmetic
//------------------------------------------------------------------------------

/// <summary> Scales the vector by <paramref name="rhs"/>. </summary>
template <class T, int Dim, bool Packed, class U, class = std::enable_if_t<std::is_convertible_v<U, T>>>
inline auto operator*(const Vector<T, Dim, Packed>& lhs, U rhs) {
	return DoBinaryOp(lhs, rhs, std::multiplies{});
}

/// <summary> Scales the vector by 1/<paramref name="rhs"/>. </summary>
template <class T, int Dim, bool Packed, class U, class = std::enable_if_t<std::is_convertible_v<U, T>>>
inline auto operator/(const Vector<T, Dim, Packed>& lhs, U rhs) {
	return DoBinaryOp(lhs, rhs, std::divides{});
}

/// <summary> Adds <paramref name="rhs"/> to each element of the vector. </summary>
template <class T, int Dim, bool Packed, class U, class = std::enable_if_t<std::is_convertible_v<U, T>>>
inline auto operator+(const Vector<T, Dim, Packed>& lhs, U rhs) {
	return DoBinaryOp(lhs, rhs, std::plus{});
}

/// <summary> Subtracts <paramref name="rhs"/> from each element of the vector. </summary>
template <class T, int Dim, bool Packed, class U, class = std::enable_if_t<std::is_convertible_v<U, T>>>
inline auto operator-(const Vector<T, Dim, Packed>& lhs, U rhs) {
	return DoBinaryOp(lhs, rhs, std::minus{});
}


/// <summary> Scales vector by <paramref name="lhs"/>. </summary>
template <class T, int Dim, bool Packed, class U, class = std::enable_if_t<std::is_convertible_v<U, T>>>
inline Vector<T, Dim, Packed> operator*(U lhs, const Vector<T, Dim, Packed>& rhs) { return rhs * lhs; }
/// <summary> Adds <paramref name="lhs"/> to all elements of the vector. </summary>
template <class T, int Dim, bool Packed, class U, class = std::enable_if_t<std::is_convertible_v<U, T>>>
inline Vector<T, Dim, Packed> operator+(U lhs, const Vector<T, Dim, Packed>& rhs) { return rhs + lhs; }
/// <summary> Makes a vector with <paramref name="lhs"/> as all elements, then subtracts <paramref name="rhs"> from it. </summary>
template <class T, int Dim, bool Packed, class U, class = std::enable_if_t<std::is_convertible_v<U, T>>>
inline Vector<T, Dim, Packed> operator-(U lhs, const Vector<T, Dim, Packed>& rhs) { return Vector<T, Dim, Packed>(lhs) - rhs; }
/// <summary> Makes a vector with <paramref name="lhs"/> as all elements, then divides it by <paramref name="rhs">. </summary>
template <class T, int Dim, bool Packed, class U, class = std::enable_if_t<std::is_convertible_v<U, T>>>
inline Vector<T, Dim, Packed> operator/(U lhs, const Vector<T, Dim, Packed>& rhs) {
	Vector<T, Dim, Packed> copy(lhs);
	copy /= rhs;
	return copy;
}


//------------------------------------------------------------------------------
// Scalar assign arithmetic
//------------------------------------------------------------------------------

/// <summary> Scales the vector by <paramref name="rhs"/>. </summary>
template <class T, int Dim, bool Packed, class U, class = std::enable_if_t<std::is_convertible_v<U, T>>>
inline Vector<T, Dim, Packed>& operator*=(Vector<T, Dim, Packed>& lhs, U rhs) {
	return lhs = lhs * rhs;
}

/// <summary> Scales the vector by 1/<paramref name="rhs"/>. </summary>
template <class T, int Dim, bool Packed, class U, class = std::enable_if_t<std::is_convertible_v<U, T>>>
inline Vector<T, Dim, Packed>& operator/=(Vector<T, Dim, Packed>& lhs, U rhs) {
	return lhs = lhs / rhs;
}

/// <summary> Adds <paramref name="rhs"/> to each element of the vector. </summary>
template <class T, int Dim, bool Packed, class U, class = std::enable_if_t<std::is_convertible_v<U, T>>>
inline Vector<T, Dim, Packed>& operator+=(Vector<T, Dim, Packed>& lhs, U rhs) {
	return lhs = lhs + rhs;
}

/// <summary> Subtracts <paramref name="rhs"/> from each element of the vector. </summary>
template <class T, int Dim, bool Packed, class U, class = std::enable_if_t<std::is_convertible_v<U, T>>>
inline Vector<T, Dim, Packed>& operator-=(Vector<T, Dim, Packed>& lhs, U rhs) {
	return lhs = lhs - rhs;
}


//------------------------------------------------------------------------------
// Extra
//------------------------------------------------------------------------------

/// <summary> Return (a*b)+c. Performs MAD or FMA if supported by target architecture. </summary>
template <class T, int Dim, bool Packed>
inline Vector<T, Dim, Packed> MultiplyAdd(const Vector<T, Dim, Packed>& a, const Vector<T, Dim, Packed>& b, const Vector<T, Dim, Packed>& c) {
	return a * b + c;
}

/// <summary> Negates all elements of the vector. </summary>
template <class T, int Dim, bool Packed>
inline Vector<T, Dim, Packed> operator-(const Vector<T, Dim, Packed>& arg) {
	return arg * T(-1);
}

/// <summary> Optional plus sign, leaves the vector as is. </summary>
template <class T, int Dim, bool Packed>
inline Vector<T, Dim, Packed> operator+(const Vector<T, Dim, Packed>& arg) {
	return arg;
}

//------------------------------------------------------------------------------
// Swizzle-vector
//------------------------------------------------------------------------------

template <class T, int Dim, bool Packed, int SwDim, bool SwPacked, int... Indices>
auto operator*(const Vector<T, Dim, Packed>& v, const Swizzle<T, SwDim, SwPacked, Indices...>& s) -> std::enable_if_t<Dim == sizeof...(Indices), Vector<T, Dim, Packed>> {
	return v * Vector<T, Dim, SwPacked>(s);
}

template <class T, int Dim, bool Packed, int SwDim, bool SwPacked, int... Indices>
auto operator/(const Vector<T, Dim, Packed>& v, const Swizzle<T, SwDim, SwPacked, Indices...>& s) -> std::enable_if_t<Dim == sizeof...(Indices), Vector<T, Dim, Packed>> {
	return v / Vector<T, Dim, SwPacked>(s);
}

template <class T, int Dim, bool Packed, int SwDim, bool SwPacked, int... Indices>
auto operator+(const Vector<T, Dim, Packed>& v, const Swizzle<T, SwDim, SwPacked, Indices...>& s) -> std::enable_if_t<Dim == sizeof...(Indices), Vector<T, Dim, Packed>> {
	return v + Vector<T, Dim, SwPacked>(s);
}

template <class T, int Dim, bool Packed, int SwDim, bool SwPacked, int... Indices>
auto operator-(const Vector<T, Dim, Packed>& v, const Swizzle<T, SwDim, SwPacked, Indices...>& s) -> std::enable_if_t<Dim == sizeof...(Indices), Vector<T, Dim, Packed>> {
	return v - Vector<T, Dim, SwPacked>(s);
}



template <class T, int Dim, bool Packed, int SwDim, bool SwPacked, int... Indices>
auto operator*(const Swizzle<T, SwDim, SwPacked, Indices...>& s, const Vector<T, Dim, Packed>& v) -> std::enable_if_t<Dim == sizeof...(Indices), Vector<T, Dim, Packed>> {
	return Vector<T, Dim, SwPacked>(s) * v;
}

template <class T, int Dim, bool Packed, int SwDim, bool SwPacked, int... Indices>
auto operator/(const Swizzle<T, SwDim, SwPacked, Indices...>& s, const Vector<T, Dim, Packed>& v) -> std::enable_if_t<Dim == sizeof...(Indices), Vector<T, Dim, Packed>> {
	return Vector<T, Dim, SwPacked>(s) / v;
}

template <class T, int Dim, bool Packed, int SwDim, bool SwPacked, int... Indices>
auto operator+(const Swizzle<T, SwDim, SwPacked, Indices...>& s, const Vector<T, Dim, Packed>& v) -> std::enable_if_t<Dim == sizeof...(Indices), Vector<T, Dim, Packed>> {
	return Vector<T, Dim, SwPacked>(s) + v;
}

template <class T, int Dim, bool Packed, int SwDim, bool SwPacked, int... Indices>
auto operator-(const Swizzle<T, SwDim, SwPacked, Indices...>& s, const Vector<T, Dim, Packed>& v) -> std::enable_if_t<Dim == sizeof...(Indices), Vector<T, Dim, Packed>> {
	return Vector<T, Dim, SwPacked>(s) - v;
}



template <class T, int Dim, bool Packed, int SwDim, bool SwPacked, int... Indices>
auto operator*=(Vector<T, Dim, Packed>& v, const Swizzle<T, SwDim, SwPacked, Indices...>& s) -> std::enable_if_t<Dim == sizeof...(Indices), Vector<T, Dim, Packed>>& {
	return v *= Vector<T, Dim, SwPacked>(s);
}

template <class T, int Dim, bool Packed, int SwDim, bool SwPacked, int... Indices>
auto operator/=(Vector<T, Dim, Packed>& v, const Swizzle<T, SwDim, SwPacked, Indices...>& s) -> std::enable_if_t<Dim == sizeof...(Indices), Vector<T, Dim, Packed>>& {
	return v /= Vector<T, Dim, SwPacked>(s);
}

template <class T, int Dim, bool Packed, int SwDim, bool SwPacked, int... Indices>
auto operator+=(Vector<T, Dim, Packed>& v, const Swizzle<T, SwDim, SwPacked, Indices...>& s) -> std::enable_if_t<Dim == sizeof...(Indices), Vector<T, Dim, Packed>>& {
	return v += Vector<T, Dim, SwPacked>(s);
}

template <class T, int Dim, bool Packed, int SwDim, bool SwPacked, int... Indices>
auto operator-=(Vector<T, Dim, Packed>& v, const Swizzle<T, SwDim, SwPacked, Indices...>& s) -> std::enable_if_t<Dim == sizeof...(Indices), Vector<T, Dim, Packed>>& {
	return v -= Vector<T, Dim, SwPacked>(s);
}



template <class T, int Dim, bool Packed, int SwDim, bool SwPacked, int... Indices>
auto operator*=(Swizzle<T, SwDim, SwPacked, Indices...>& s, const Vector<T, Dim, Packed>& v) -> std::enable_if_t<Dim == sizeof...(Indices), Swizzle<T, SwDim, SwPacked, Indices...>>& {
	return s = Vector<T, Dim, SwPacked>(s) * v;
}

template <class T, int Dim, bool Packed, int SwDim, bool SwPacked, int... Indices>
auto operator/=(Swizzle<T, SwDim, SwPacked, Indices...>& s, const Vector<T, Dim, Packed>& v) -> std::enable_if_t<Dim == sizeof...(Indices), Swizzle<T, SwDim, SwPacked, Indices...>>& {
	return s = Vector<T, Dim, SwPacked>(s) / v;
}

template <class T, int Dim, bool Packed, int SwDim, bool SwPacked, int... Indices>
auto operator+=(Swizzle<T, SwDim, SwPacked, Indices...>& s, const Vector<T, Dim, Packed>& v) -> std::enable_if_t<Dim == sizeof...(Indices), Swizzle<T, SwDim, SwPacked, Indices...>>& {
	return s = Vector<T, Dim, SwPacked>(s) + v;
}

template <class T, int Dim, bool Packed, int SwDim, bool SwPacked, int... Indices>
auto operator-=(Swizzle<T, SwDim, SwPacked, Indices...>& s, const Vector<T, Dim, Packed>& v) -> std::enable_if_t<Dim == sizeof...(Indices), Swizzle<T, SwDim, SwPacked, Indices...>>& {
	return s = Vector<T, Dim, SwPacked>(s) - v;
}


//------------------------------------------------------------------------------
// Swizzle-swizzle
//------------------------------------------------------------------------------


template <class T1, int Dim1, bool Packed1, int... Indices1, class T2, int Dim2, bool Packed2, int... Indices2>
auto operator*(const Swizzle<T1, Dim1, Packed1, Indices1...>& s1, const Swizzle<T2, Dim2, Packed2, Indices2...>& s2) {
	static_assert(sizeof...(Indices1) == sizeof...(Indices2));
	constexpr bool Packed = Packed1 && Packed2;
	using V1 = Vector<T1, sizeof...(Indices1), Packed>;
	using V2 = Vector<T2, sizeof...(Indices2), Packed>;
	return V1(s1) * V2(s2);
}

template <class T1, int Dim1, bool Packed1, int... Indices1, class T2, int Dim2, bool Packed2, int... Indices2>
auto operator/(const Swizzle<T1, Dim1, Packed1, Indices1...>& s1, const Swizzle<T2, Dim2, Packed2, Indices2...>& s2) {
	static_assert(sizeof...(Indices1) == sizeof...(Indices2));
	constexpr bool Packed = Packed1 && Packed2;
	using V1 = Vector<T1, sizeof...(Indices1), Packed>;
	using V2 = Vector<T2, sizeof...(Indices2), Packed>;
	return V1(s1) / V2(s2);
}

template <class T1, int Dim1, bool Packed1, int... Indices1, class T2, int Dim2, bool Packed2, int... Indices2>
auto operator+(const Swizzle<T1, Dim1, Packed1, Indices1...>& s1, const Swizzle<T2, Dim2, Packed2, Indices2...>& s2) {
	static_assert(sizeof...(Indices1) == sizeof...(Indices2));
	constexpr bool Packed = Packed1 && Packed2;
	using V1 = Vector<T1, sizeof...(Indices1), Packed>;
	using V2 = Vector<T2, sizeof...(Indices2), Packed>;
	return V1(s1) + V2(s2);
}

template <class T1, int Dim1, bool Packed1, int... Indices1, class T2, int Dim2, bool Packed2, int... Indices2>
auto operator-(const Swizzle<T1, Dim1, Packed1, Indices1...>& s1, const Swizzle<T2, Dim2, Packed2, Indices2...>& s2) {
	static_assert(sizeof...(Indices1) == sizeof...(Indices2));
	constexpr bool Packed = Packed1 && Packed2;
	using V1 = Vector<T1, sizeof...(Indices1), Packed>;
	using V2 = Vector<T2, sizeof...(Indices2), Packed>;
	return V1(s1) - V2(s2);
}


template <class T1, int Dim1, bool Packed1, int... Indices1, class T2, int Dim2, bool Packed2, int... Indices2>
auto& operator*=(Swizzle<T1, Dim1, Packed1, Indices1...>& s1, const Swizzle<T2, Dim2, Packed2, Indices2...>& s2) {
	return s1 = s1 * s2;
}

template <class T1, int Dim1, bool Packed1, int... Indices1, class T2, int Dim2, bool Packed2, int... Indices2>
auto& operator/=(Swizzle<T1, Dim1, Packed1, Indices1...>& s1, const Swizzle<T2, Dim2, Packed2, Indices2...>& s2) {
	return s1 = s1 / s2;
}

template <class T1, int Dim1, bool Packed1, int... Indices1, class T2, int Dim2, bool Packed2, int... Indices2>
auto& operator+=(Swizzle<T1, Dim1, Packed1, Indices1...>& s1, const Swizzle<T2, Dim2, Packed2, Indices2...>& s2) {
	return s1 = s1 + s2;
}

template <class T1, int Dim1, bool Packed1, int... Indices1, class T2, int Dim2, bool Packed2, int... Indices2>
auto& operator-=(Swizzle<T1, Dim1, Packed1, Indices1...>& s1, const Swizzle<T2, Dim2, Packed2, Indices2...>& s2) {
	return s1 = s1 - s2;
}

//------------------------------------------------------------------------------
// Swizzle-scalar
//------------------------------------------------------------------------------

template <class T, int Dim, bool Packed, int... Indices, class U, class = std::enable_if_t<std::is_convertible_v<U, T>>>
auto operator*(const Swizzle<T, Dim, Packed, Indices...>& lhs, U rhs) {
	using VectorT = Vector<T, sizeof...(Indices), Packed>;
	return VectorT(lhs) * rhs;
}

template <class T, int Dim, bool Packed, int... Indices, class U, class = std::enable_if_t<std::is_convertible_v<U, T>>>
auto operator/(const Swizzle<T, Dim, Packed, Indices...>& lhs, U rhs) {
	using VectorT = Vector<T, sizeof...(Indices), Packed>;
	return VectorT(lhs) / rhs;
}

template <class T, int Dim, bool Packed, int... Indices, class U, class = std::enable_if_t<std::is_convertible_v<U, T>>>
auto operator+(const Swizzle<T, Dim, Packed, Indices...>& lhs, U rhs) {
	using VectorT = Vector<T, sizeof...(Indices), Packed>;
	return VectorT(lhs) + rhs;
}

template <class T, int Dim, bool Packed, int... Indices, class U, class = std::enable_if_t<std::is_convertible_v<U, T>>>
auto operator-(const Swizzle<T, Dim, Packed, Indices...>& lhs, U rhs) {
	using VectorT = Vector<T, sizeof...(Indices), Packed>;
	return VectorT(lhs) - rhs;
}



template <class T, int Dim, bool Packed, int... Indices, class U, class = std::enable_if_t<std::is_convertible_v<U, T>>>
auto operator*(U lhs, const Swizzle<T, Dim, Packed, Indices...>& rhs) {
	return rhs * lhs;
}

template <class T, int Dim, bool Packed, int... Indices, class U, class = std::enable_if_t<std::is_convertible_v<U, T>>>
auto operator/(U lhs, const Swizzle<T, Dim, Packed, Indices...>& rhs) {
	using VectorT = Vector<T, sizeof...(Indices), Packed>;
	return lhs / VectorT(rhs);
}

template <class T, int Dim, bool Packed, int... Indices, class U, class = std::enable_if_t<std::is_convertible_v<U, T>>>
auto operator+(U lhs, const Swizzle<T, Dim, Packed, Indices...>& rhs) {
	return rhs + lhs;
}

template <class T, int Dim, bool Packed, int... Indices, class U, class = std::enable_if_t<std::is_convertible_v<U, T>>>
auto operator-(U lhs, const Swizzle<T, Dim, Packed, Indices...>& rhs) {
	using VectorT = Vector<T, sizeof...(Indices), Packed>;
	return lhs - VectorT(rhs);
}



template <class T, int Dim, bool Packed, int... Indices, class U, class = std::enable_if_t<std::is_convertible_v<U, T>>>
auto& operator*=(Swizzle<T, Dim, Packed, Indices...>& lhs, U rhs) {
	using VectorT = Vector<T, sizeof...(Indices), Packed>;
	lhs = VectorT(lhs) * rhs;
	return lhs;
}

template <class T, int Dim, bool Packed, int... Indices, class U, class = std::enable_if_t<std::is_convertible_v<U, T>>>
auto& operator/=(Swizzle<T, Dim, Packed, Indices...>& lhs, U rhs) {
	using VectorT = Vector<T, sizeof...(Indices), Packed>;
	lhs = VectorT(lhs) / rhs;
	return lhs;
}

template <class T, int Dim, bool Packed, int... Indices, class U, class = std::enable_if_t<std::is_convertible_v<U, T>>>
auto& operator+=(Swizzle<T, Dim, Packed, Indices...>& lhs, U rhs) {
	using VectorT = Vector<T, sizeof...(Indices), Packed>;
	lhs = VectorT(lhs) + rhs;
	return lhs;
}

template <class T, int Dim, bool Packed, int... Indices, class U, class = std::enable_if_t<std::is_convertible_v<U, T>>>
auto& operator-=(Swizzle<T, Dim, Packed, Indices...>& lhs, U rhs) {
	using VectorT = Vector<T, sizeof...(Indices), Packed>;
	lhs = VectorT(lhs) - rhs;
	return lhs;
}



} // namespace mathter