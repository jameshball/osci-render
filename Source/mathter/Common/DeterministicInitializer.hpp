// L=============================================================================
// L This software is distributed under the MIT license.
// L Copyright 2021 Péter Kardos
// L=============================================================================

#pragma once

#include <complex>
#include <limits>
#include <type_traits>

namespace mathter::impl {


/// <summary> Return a value initialized to zero, or, if not possible, a value-initialized object. </summary>
/// <remarks> If you want special treatment for a particular type, specialize this method. </remarks>
template <class T>
struct NullScalarInitializer {
	static constexpr T Get() {
		if constexpr (std::is_convertible_v<decltype(0), T>) {
			return T(0);
		}
		else {
			return T{};
		}
	}
};


/// <summary> Return a value initialized to zero, or, if not possible, a value-initialized object. </summary>
template <class T>
constexpr T NullScalar() {
	return NullScalarInitializer<T>::Get();
}


/// <summary> Initializes an object of type T to invalid (NaN) value. </summary>
/// <remarks> Returns a signaling NaN,
///		if not possible, a quiet NaN,
///		if not possible, an infinity,
///		if not possible, the highest value for T,
///		if not possible, then a null-initialized value.
///	If you want special treatment for a particular type, specialize this method. </remarks>
template <class T>
struct InvalidScalarInitializer {
	static constexpr T Get() {
		if constexpr (std::numeric_limits<T>::is_specialized) {
			if constexpr (std::numeric_limits<T>::has_signaling_NaN) {
				return std::numeric_limits<T>::signaling_NaN();
			}
			else if constexpr (std::numeric_limits<T>::has_quiet_NaN) {
				return std::numeric_limits<T>::quiet_NaN();
			}
			else if constexpr (std::numeric_limits<T>::has_infinity) {
				return std::numeric_limits<T>::infinity();
			}
			else {
				return std::numeric_limits<T>::max();
			}
		}
		return NullScalar<T>();
	}
};


/// <summary> Return a signaling NaN complex number. Same as the unspecialized version. </summary>
template <class T>
struct InvalidScalarInitializer<std::complex<T>> {
	static constexpr std::complex<T> Get() {
		return { InvalidScalarInitializer<T>::Get(), InvalidScalarInitializer<T>::Get() };
	}
};


/// <summary> Returns a signaling NaN,
///		if not possible, a quiet NaN,
///		if not possible, an infinity,
///		if not possible, the highest value for T,
///		if not possible, then a null-initialized value. </summary>
template <class T>
constexpr T InvalidScalar() {
	return InvalidScalarInitializer<T>::Get();
}


#if !defined(MATHTER_NULL_INITIALIZE) && !defined(MATHTER_INVALID_INITIALIZE) && !defined(MATHTER_DONT_INITIALIZE)
#ifdef NDEBUG
#define MATHTER_DONT_INITIALIZE 1
#else
#define MATHTER_INVALID_INITIALIZE 1
#endif
#endif


#if defined(MATHTER_NULL_INITIALIZE)
#define MATHTER_SCALAR_INIT_EXPRESSION(T) mathter::impl::NullScalar<T>()
#elif defined(MATHTER_INVALID_INITIALIZE)
#define MATHTER_SCALAR_INIT_EXPRESSION(T) mathter::impl::InvalidScalar<T>()
#elif defined(MATHTER_DONT_INITIALIZE)
#define MATHTER_SCALAR_INIT_EXPRESSION(T)
#else
#error Set at least one of the MATHTER_NULL/INVALID/DONT_INITIALIZE macros.
#endif

#if defined(MATHTER_NULL_INITIALIZE) || defined(MATHTER_INVALID_INITIALIZE)
#define MATHTER_VECTOR_INITIALIZER(T) : Vector(MATHTER_SCALAR_INIT_EXPRESSION(T))
#else
#define MATHTER_VECTOR_INITIALIZER(T)
#endif


} // namespace mathter::impl