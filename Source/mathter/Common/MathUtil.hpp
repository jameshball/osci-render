// L=============================================================================
// L This software is distributed under the MIT license.
// L Copyright 2021 Péter Kardos
// L=============================================================================

#pragma once

#include <cmath>

namespace mathter::impl {


template <class T>
T sign(T arg) {
	return T(arg > T(0)) - (arg < T(0));
}

template <class T>
T sign_nonzero(T arg) {
	return std::copysign(T(1), arg);
}

template <class T>
constexpr T ConstexprExp10(int exponent) {
	return exponent == 0 ? T(1) : T(10) * ConstexprExp10<T>(exponent - 1);
}

template <class T>
constexpr T ConstexprAbs(T arg) {
	return arg >= T(0) ? arg : -arg;
}


} // namespace mathter::impl