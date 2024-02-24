// L=============================================================================
// L This software is distributed under the MIT license.
// L Copyright 2021 Péter Kardos
// L=============================================================================

#pragma once

#include "QuaternionImpl.hpp"

namespace mathter {

namespace quat_literals {
	inline Quaternion<long double> operator"" _i(unsigned long long int arg) {
		return Quaternion<long double>(0, (long double)arg, 0, 0);
	}
	inline Quaternion<long double> operator"" _j(unsigned long long int arg) {
		return Quaternion<long double>(0, 0, (long double)arg, 0);
	}
	inline Quaternion<long double> operator"" _k(unsigned long long int arg) {
		return Quaternion<long double>(0, 0, 0, (long double)arg);
	}

	inline Quaternion<long double> operator"" _i(long double arg) {
		return Quaternion<long double>(0, arg, 0, 0);
	}
	inline Quaternion<long double> operator"" _j(long double arg) {
		return Quaternion<long double>(0, 0, arg, 0);
	}
	inline Quaternion<long double> operator"" _k(long double arg) {
		return Quaternion<long double>(0, 0, 0, arg);
	}
} // namespace quat_literals

} // namespace mathter