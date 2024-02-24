// L=============================================================================
// L This software is distributed under the MIT license.
// L Copyright 2021 Péter Kardos
// L=============================================================================

#pragma once

#include "../Common/Definitions.hpp"
#include "../Common/DeterministicInitializer.hpp"
#include "../Common/Traits.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <type_traits>
#include <utility>

#if MATHTER_USE_XSIMD
#include <xsimd/xsimd.hpp>
#endif


namespace mathter {


//------------------------------------------------------------------------------
// Swizzle
//------------------------------------------------------------------------------


/// <summary> Enables element swizzling (reordering elements) for vectors. </summary>
/// <remarks>
/// To access swizzlers, use the xx, xy, xyz and similar elements of vectors.
/// Swizzlers can be used with assignments, concatenation, casting and constructors.
/// To perform arithmetic, cast swizzlers to corresponding vector type.
/// </remarks>
template <class T, int Dim, bool Packed, int... Indices>
class Swizzle {
	static constexpr int IndexTable[] = { Indices... };
	T* data() { return reinterpret_cast<T*>(this); }
	const T* data() const { return reinterpret_cast<const T*>(this); }

public:
	Swizzle(const Swizzle&) = delete;
	Swizzle(Swizzle&&) = delete;

	/// <summary> Builds the swizzled vector object. </summary>
	template <class T2, bool Packed2>
	operator Vector<T2, sizeof...(Indices), Packed2>() const;

	/// <summary> Sets the parent vector's elements from the right-side argument. </summary>
	/// <remarks>
	/// Example: b = {1,2,3}; a.yxz = b; -> a contains {2,1,3}.
	/// You don't have to worry about aliasing (a.xyz = a is totally fine).
	/// </remarks>
	template <class T2, bool Packed2>
	Swizzle& operator=(const Vector<T2, sizeof...(Indices), Packed2>& rhs);

	/// <summary> Sets the parent vector's elements from the right-side argument. </summary>
	/// <remarks>
	/// Example: b = {1,2,3}; a.yxz = b.xyz; -> a contains {2,1,3}.
	/// You don't have to worry about aliasing (a.xyz = a.zyx is totally fine).
	/// </remarks>
	template <class T2, int Dim2, bool Packed2, int... Indices2, typename std::enable_if<sizeof...(Indices) == sizeof...(Indices2), int>::type = 0>
	Swizzle& operator=(const Swizzle<T2, Dim2, Packed2, Indices2...>& rhs) {
		*this = Vector<T, sizeof...(Indices2), false>(rhs);
		return *this;
	}

	/// <summary> Returns the nth element of the swizzled vector. Example: v.zxy[2] returns y. </summary>
	T& operator[](int idx) {
		assert(idx < Dim);
		return data()[IndexTable[idx]];
	}
	/// <summary> Returns the nth element of the swizzled vector. Example: v.zxy[2] returns y. </summary>
	T operator[](int idx) const {
		assert(idx < Dim);
		return data()[IndexTable[idx]];
	}

	/// <summary> Returns the nth element of the swizzled vector. Example: v.zxy(2) returns y. </summary>
	T& operator()(int idx) {
		assert(idx < Dim);
		return data()[IndexTable[idx]];
	}
	/// <summary> Returns the nth element of the swizzled vector. Example: v.zxy(2) returns y. </summary>
	T operator()(int idx) const {
		assert(idx < Dim);
		return data()[IndexTable[idx]];
	}

	/// <summary> Builds the swizzled vector object. </summary>
	template <bool Packed2 = false>
	const auto ToVector() const {
		return Vector<T, Dim, Packed2>(*this);
	}
};

//------------------------------------------------------------------------------
// Vectorization utilities
//------------------------------------------------------------------------------

template <class T, int Dim, bool Packed>
constexpr int ExtendedDim() {
	if constexpr (!Packed) {
		if (Dim == 3) {
			return 4;
		}
		if (Dim == 6 || Dim == 7) {
			return 8;
		}
	}
	return Dim;
}


template <class T, int Dim, bool Packed>
constexpr int IsBatched() {
#if MATHTER_USE_XSIMD
	if constexpr (!Packed) {
		constexpr auto extendedSize = ExtendedDim<T, Dim, Packed>();
		using BatchT = typename xsimd::make_sized_batch<T, extendedSize>::type;
		return !std::is_void_v<BatchT>;
	}
#endif
	return false;
}


template <class T, int Dim, bool Packed>
struct BatchTHelper {
	static auto GetType() {
		if constexpr (IsBatched<T, Dim, Packed>()) {
#if MATHTER_USE_XSIMD
			using B = typename xsimd::make_sized_batch<T, ExtendedDim<T, Dim, Packed>()>::type;
			return static_cast<B*>(nullptr);
#else
			return static_cast<void*>(nullptr);
#endif
		}
		else {
			return static_cast<void*>(nullptr);
		}
	}
};


template <class T, int Dim, bool Packed>
using Batch = std::decay_t<std::remove_pointer_t<decltype(BatchTHelper<T, Dim, Packed>::GetType())>>;


template <class T, int Dim, bool Packed>
constexpr int Alignment() {
	if constexpr (IsBatched<T, Dim, Packed>()) {
		using B = Batch<T, Dim, Packed>;
		static_assert(!std::is_void_v<B>, "IsBatched should prevent this case from ever happening.");
		return alignof(B);
	}
	return alignof(T);
}


//------------------------------------------------------------------------------
// VectorData
//------------------------------------------------------------------------------

// General case
template <class T, int Dim, bool Packed>
struct VectorData {
	VectorData() {}
	union {
		/// <summary> A potentially larger array extended to the next SIMD size. </summary>
		alignas(Alignment<T, Dim, Packed>()) std::array<T, ExtendedDim<T, Dim, Packed>()> extended;
	};
};


// Small vectors with x,y,z,w members
template <class T, bool Packed>
struct VectorData<T, 2, Packed> {
	VectorData() {}
	VectorData(const VectorData& rhs) {
		extended = rhs.extended;
	}
	VectorData& operator=(const VectorData& rhs) {
		extended = rhs.extended;
		return *this;
	}
	union {
		/// <summary> A potentially larger array extended to the next SIMD size. </summary>
		alignas(Alignment<T, 2, Packed>()) std::array<T, ExtendedDim<T, 2, Packed>()> extended;
		struct {
			T x, y;
		};
#define Dim 2
#include "../Swizzle/Swizzle_2.inc.hpp"
#undef Dim
	};
};


template <class T, bool Packed>
struct VectorData<T, 3, Packed> {
	VectorData() {}
	VectorData(const VectorData& rhs) {
		extended = rhs.extended;
	}
	VectorData& operator=(const VectorData& rhs) {
		extended = rhs.extended;
		return *this;
	}
	union {
		/// <summary> A potentially larger array extended to the next SIMD size. </summary>
		alignas(Alignment<T, 3, Packed>()) std::array<T, ExtendedDim<T, 3, Packed>()> extended;
		struct {
			T x, y, z;
		};
#define Dim 3
#include "../Swizzle/Swizzle_4.inc.hpp"
#undef Dim
	};
};


template <class T, bool Packed>
struct VectorData<T, 4, Packed> {
	VectorData() {}
	VectorData(const VectorData& rhs) {
		extended = rhs.extended;
	}
	VectorData& operator=(const VectorData& rhs) {
		extended = rhs.extended;
		return *this;
	}
	union {
		/// <summary> A potentially larger array extended to the next SIMD size. </summary>
		alignas(Alignment<T, 4, Packed>()) std::array<T, ExtendedDim<T, 4, Packed>()> extended;
		struct {
			T x, y, z, w;
		};
#define Dim 4
#include "../Swizzle/Swizzle_4.inc.hpp"
#undef Dim
	};
};


//------------------------------------------------------------------------------
// Tuple utilities
//------------------------------------------------------------------------------

template <class Indexable, size_t... Indices>
auto AsTuple(const Indexable& value, std::index_sequence<Indices...>) {
	return std::tuple{ value[Indices]... };
}

template <class T, int Dim, bool Packed>
auto AsTuple(const Vector<T, Dim, Packed>& value, std::nullptr_t) {
	return AsTuple(value, std::make_index_sequence<Dim>());
}

template <class T, int Dim, bool Packed, int... Indices>
auto AsTuple(const Swizzle<T, Dim, Packed, Indices...>& value, std::nullptr_t) {
	return AsTuple(value, std::make_index_sequence<sizeof...(Indices)>());
}

template <class T>
auto AsTuple(const T& value, const void*) {
	return std::tuple{ value };
}

template <class T>
auto AsTuple(const T& value) {
	return AsTuple(value, nullptr);
}


//------------------------------------------------------------------------------
// General vector class
//------------------------------------------------------------------------------


/// <summary> Represents a vector in N-dimensional space. </summary>
/// <typeparam name="T"> The scalar type on which the vector is based.
///	You can use builtin floating point or integer types. User-defined types and std::complex
/// may also work, but are not yet officially supported. </typeparam>
/// <typeparam name="Dim"> The dimension of the vector-space. Must be a positive integer.
/// Dynamically sized vectors are not supported yet, but you'll have to use
/// <see cref="mathter::DYNAMIC"/> to define dynamically sized vectors. </typeparam>
///	<typeparam name="Packed"> Set to true to tightly pack vector elements and
/// avoid padding of the vector struct. Disables SIMD optimizations. </typeparam>
/// <remarks>
/// There is not much extraordinary to vectors, they work as you would expect.
/// - you can use common vector space airhtmetic
/// - you have common function like normalization
/// - you can multiply them with <see cref="mathter::Matrix"/> from either side
/// - you can concatenate and swizzle them.
/// </remarks>
template <class T, int Dim, bool Packed = false>
class Vector : public VectorData<T, Dim, Packed> {
	static_assert(Dim >= 1, "Dimension must be positive integer.");

public:
	using VectorData<T, Dim, Packed>::extended;

	//--------------------------------------------
	// Properties
	//--------------------------------------------

	/// <summary> Returns the number of dimensions of the vector. </summary>
	constexpr int Dimension() const {
		return Dim;
	}

	//--------------------------------------------
	// Basic constructors
	//--------------------------------------------

	/// <summary> Constructs the vector. Does NOT zero-initialize elements. </summary>
	Vector() MATHTER_VECTOR_INITIALIZER(T) {}
	Vector(const Vector&) = default;
	Vector& operator=(const Vector&) = default;

	/// <summary> Constructs the vector by converting elements of <paramref name="other"/>. </summary>
	template <class T2, bool Packed2, std::enable_if_t<std::is_convertible_v<T2, T>, int> = 0>
	Vector(const Vector<T2, Dim, Packed2>& other) {
		for (int i = 0; i < Dim; ++i) {
			data()[i] = (T)other.data()[i];
		}
	}

	/// <summary> Construct the vector using the SIMD batch type as content. </summary>
	template <class B, std::enable_if_t<std::is_same_v<B, Batch<T, Dim, Packed>>, int> = 0>
	explicit Vector(const B& batch) {
		batch.store_unaligned(data());
	}

	//--------------------------------------------
	// Homogeneous up- and downcast
	//--------------------------------------------

	/// <summary> Creates a homogeneous vector by appending a 1. </summary>
	template <class T2, bool Packed2, class = typename std::enable_if<(Dim >= 2), T2>::type>
	explicit Vector(const Vector<T2, Dim - 1, Packed2>& rhs) : Vector(rhs, 1) {}

	/// <summary> Truncates last coordinate of homogenous vector to create non-homogeneous. </summary>
	template <class T2, bool Packed2>
	explicit Vector(const Vector<T2, Dim + 1, Packed2>& rhs) : Vector(rhs.data()) {}


	//--------------------------------------------
	// Data constructors
	//--------------------------------------------

	/// <summary> Constructs the vector from an array of elements. </summary>
	/// <remarks> The number of elements must be the same as the vector's dimension. </remarks>
	template <class U, std::enable_if_t<std::is_convertible_v<U, T>, int> = 0>
	explicit Vector(const U* elements) {
		std::copy(elements, elements + Dimension(), begin());
	}

	/// <summary> Sets all elements to the same value. </summary>
	template <class U, std::enable_if_t<std::is_convertible_v<U, T>, int> = 0>
	explicit Vector(U all) {
		if constexpr (IsBatched<T, Dim, Packed>()) {
			Batch<T, Dim, Packed>(T(all)).store_unaligned(data());
		}
		else {
			std::fill(begin(), end(), T(all));
		}
	}

	/// <summary> Initializes the vector by concatenating given scalar, vector or swizzle arguments. </summary>
	/// <remarks> Sum of the dimension of arguments must equal vector dimension.
	///		Types of arguments may differ from vector's underlying type, in which case cast is forced without a warning. </remarks>
	template <class... Args, typename std::enable_if<(sizeof...(Args) > 1), int>::type = 0>
	Vector(const Args&... mixed) {
		auto scalars = std::tuple_cat(AsTuple(mixed)...);
		auto fun = [this](const auto&... args) {
			extended = { T(args)... };
		};
		std::apply(fun, scalars);
	}


	//--------------------------------------------
	// Accessors
	//--------------------------------------------

	/// <summary> Returns the nth element of the vector. </summary>
	T operator[](int idx) const {
		return data()[idx];
	}
	/// <summary> Returns the nth element of the vector. </summary>
	T& operator[](int idx) {
		return data()[idx];
	}

	/// <summary> Returns the nth element of the vector. </summary>
	T operator()(int idx) const {
		return data()[idx];
	}
	/// <summary> Returns the nth element of the vector. </summary>
	T& operator()(int idx) {
		return data()[idx];
	}

	/// <summary> Returns an iterator to the first element. </summary>
	auto cbegin() const {
		return extended.cbegin();
	}
	/// <summary> Returns an iterator to the first element. </summary>
	auto begin() const {
		return extended.begin();
	}
	/// <summary> Returns an iterator to the first element. </summary>
	auto begin() {
		return extended.begin();
	}
	/// <summary> Returns an iterator to the end of the vector (works like STL). </summary>
	auto cend() const {
		return cbegin() + Dimension();
	}
	/// <summary> Returns an iterator to the end of the vector (works like STL). </summary>
	auto end() const {
		return begin() + Dimension();
	}
	/// <summary> Returns an iterator to the end of the vector (works like STL). </summary>
	auto end() {
		return begin() + Dimension();
	}

	/// <summary> Returns a pointer to the underlying array of elements. </summary>
	auto Data() const {
		return data();
	}
	/// <summary> Returns a pointer to the underlying array of elements. </summary>
	auto Data() {
		return data();
	}

	/// <summary> Returns a pointer to the underlying array of elements. </summary>
	auto data() const {
		return extended.data();
	}
	/// <summary> Returns a pointer to the underlying array of elements. </summary>
	auto data() {
		return extended.data();
	}
};



template <class T, T... Indices>
struct SwizzleGenerator {
	template <T... EmptyList>
	static constexpr unsigned get_helper(unsigned, unsigned, const void*) {
		return 0;
	}
	template <T First, T... Rest>
	static constexpr unsigned get_helper(unsigned idx, unsigned size, std::nullptr_t) {
		return idx == 0 ? First : get_helper<Rest...>(idx - 1, size - 1, nullptr);
	}
	static constexpr unsigned get(unsigned idx, unsigned size) {
		return get_helper<Indices...>(idx, size, nullptr);
	}
};


template <class T, int Dim, bool Packed, int... Indices>
template <class T2, bool Packed2>
Swizzle<T, Dim, Packed, Indices...>::operator Vector<T2, sizeof...(Indices), Packed2>() const {
	constexpr auto Dim2 = int(sizeof...(Indices));
	using V = Vector<T2, Dim2, Packed2>;

#if MATHTER_USE_XSIMD
	if constexpr (IsBatched<T, Dim, Packed>() && Dim2 <= Dim) {
		using TI = traits::same_size_int_t<T>;
		static_assert(!std::is_void_v<TI> && sizeof(TI) == sizeof(T));
		using B = Batch<T, Dim, Packed>;
		constexpr auto batchSize = B::size;
		using BI = xsimd::batch<TI, typename B::arch_type>;
		const auto batch = B::load_unaligned(data());
		using G = SwizzleGenerator<TI, TI(Indices)...>;
		constexpr auto first = G::get(0, 9);
		const auto mask = xsimd::make_batch_constant<BI, G>();
		const auto swizzled = xsimd::swizzle(batch, mask);
		if constexpr (std::is_convertible_v<decltype(swizzled), Vector<T, Dim2, Packed>>) {
			return V{ Vector<T, Dim2, Packed>(swizzled) };
		}
		else {
			alignas(decltype(swizzled)) std::array<T, batchSize> extended;
			swizzled.store_aligned(extended.data());
			return V{ Vector<T, Dim2, Packed>(extended.data()) };
		}
	}
#endif
	return V(data()[Indices]...);
}


template <class T, int Dim, bool Packed, int... Indices>
template <class T2, bool Packed2>
Swizzle<T, Dim, Packed, Indices...>& Swizzle<T, Dim, Packed, Indices...>::operator=(const Vector<T2, sizeof...(Indices), Packed2>& rhs) {
	if (data() != rhs.data()) {
		std::tie((*this)[Indices]...) = AsTuple(rhs);
	}
	else {
		Vector<T, sizeof...(Indices), false> tmp = rhs;
		*this = tmp;
	}
	return *this;
}



} // namespace mathter