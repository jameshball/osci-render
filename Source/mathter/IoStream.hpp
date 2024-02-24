// L=============================================================================
// L This software is distributed under the MIT license.
// L Copyright 2021 Péter Kardos
// L=============================================================================

#pragma once

#include "Vector.hpp"

#include <iostream>

namespace mathter {

enum class eEnclosingBracket {
	NONE,
	PARANTHESE,
	BRACKET,
	BRACE,
};

/// <summary> Prints the vector like [1,2,3]. </summary>
template <class T, int Dim, bool Packed>
std::ostream& operator<<(std::ostream& os, const mathter::Vector<T, Dim, Packed>& v) {
	os << "[";
	for (int x = 0; x < Dim; ++x) {
		os << v(x) << (x == Dim - 1 ? "" : ", ");
	}
	os << "]";
	return os;
}


namespace impl {
	template <class T>
	struct dependent_false {
		static constexpr bool value = false;
	};
	template <class T>
	constexpr bool dependent_false_v = dependent_false<T>::value;

	template <class AritT, typename std::enable_if<std::is_integral<AritT>::value && std::is_signed<AritT>::value, int>::type = 0>
	AritT strtonum(const char* str, const char** end) {
		AritT value;
		value = (AritT)strtoll(str, (char**)end, 10);
		return value;
	}
	template <class AritT, typename std::enable_if<std::is_integral<AritT>::value && !std::is_signed<AritT>::value, int>::type = 0>
	AritT strtonum(const char* str, const char** end) {
		AritT value;
		value = (AritT)strtoull(str, (char**)end, 10);
		return value;
	}
	template <class AritT, typename std::enable_if<std::is_floating_point<AritT>::value, int>::type = 0>
	AritT strtonum(const char* str, const char** end) {
		AritT value;
		value = (AritT)strtold(str, (char**)end);
		return value;
	}

	inline const char* StripSpaces(const char* str) {
		while (*str != '\0' && isspace(*str))
			++str;
		return str;
	};

} // namespace impl

/// <summary> Parses a vector from a string. </summary>
template <class T, int Dim, bool Packed>
Vector<T, Dim, Packed> strtovec(const char* str, const char** end) {
	Vector<T, Dim, Packed> ret;

	const char* strproc = str;

	// parse initial bracket if any
	strproc = impl::StripSpaces(strproc);
	if (*strproc == '\0') {
		*end = str;
		return ret;
	}

	char startBracket = *strproc;
	char endBracket;
	bool hasBrackets = false;
	switch (startBracket) {
		case '(':
			endBracket = ')';
			hasBrackets = true;
			++strproc;
			break;
		case '[':
			endBracket = ']';
			hasBrackets = true;
			++strproc;
			break;
		case '{':
			endBracket = '}';
			hasBrackets = true;
			++strproc;
			break;
	}

	// parse elements
	for (int i = 0; i < Dim; ++i) {
		const char* elemend;
		T elem = impl::strtonum<T>(strproc, &elemend);
		if (elemend == strproc) {
			*end = str;
			return ret;
		}
		else {
			ret[i] = elem;
			strproc = elemend;
		}
		strproc = impl::StripSpaces(strproc);
		if (*strproc == ',') {
			++strproc;
		}
	}

	// parse ending bracket corresponding to initial bracket
	if (hasBrackets) {
		strproc = impl::StripSpaces(strproc);
		if (*strproc != endBracket) {
			*end = str;
			return ret;
		}
		++strproc;
	}

	*end = strproc;
	return ret;
}

template <class VectorT>
VectorT strtovec(const char* str, const char** end) {
	static_assert(traits::IsVector<VectorT>::value, "This type is not a Vector, dumbass.");

	return strtovec<
		typename traits::VectorTraits<VectorT>::Type,
		traits::VectorTraits<VectorT>::Dim,
		traits::VectorTraits<VectorT>::Packed>(str, end);
}



template <class T, int Rows, int Columns, eMatrixOrder Order, eMatrixLayout Layout, bool Packed>
std::ostream& operator<<(std::ostream& os, const Matrix<T, Rows, Columns, Order, Layout, Packed>& mat) {
	os << "[";
	for (int i = 0; i < mat.Height(); ++i) {
		for (int j = 0; j < mat.Width(); ++j) {
			os << mat(i, j) << (j == mat.Width() - 1 ? "" : ", ");
		}
		if (i < Rows - 1) {
			os << "; ";
		}
	}
	os << "]";
	return os;
}


template <class T, int Rows, int Columns, eMatrixOrder Order, eMatrixLayout Layout, bool Packed>
Matrix<T, Rows, Columns, Order, Layout, Packed> strtomat(const char* str, const char** end) {
	using MatrixT = Matrix<T, Rows, Columns, Order, Layout, Packed>;
	using VectorT = Vector<T, Columns, Packed>;
	MatrixT ret;

	const char* strproc = str;

	// parse initial bracket if any
	strproc = impl::StripSpaces(strproc);
	if (*strproc == '\0') {
		*end = str;
		return ret;
	}

	char startBracket = *strproc;
	char endBracket;
	bool hasBrackets = false;
	switch (startBracket) {
		case '(':
			endBracket = ')';
			hasBrackets = true;
			++strproc;
			break;
		case '[':
			endBracket = ']';
			hasBrackets = true;
			++strproc;
			break;
		case '{':
			endBracket = '}';
			hasBrackets = true;
			++strproc;
			break;
	}

	// parse rows
	for (int i = 0; i < Rows; ++i) {
		const char* rowend;
		VectorT row = strtovec<VectorT>(strproc, &rowend);
		if (rowend == strproc) {
			*end = str;
			return ret;
		}
		else {
			ret.Row(i) = row;
			strproc = rowend;
		}
		strproc = impl::StripSpaces(strproc);
		if (i < Rows - 1) {
			if (*strproc == ';') {
				++strproc;
			}
			else {
				*end = str;
				return ret;
			}
		}
	}

	// parse ending bracket corresponding to initial bracket
	if (hasBrackets) {
		strproc = impl::StripSpaces(strproc);
		if (*strproc != endBracket) {
			*end = str;
			return ret;
		}
		++strproc;
	}

	*end = strproc;
	return ret;
}

template <class MatrixT>
MatrixT strtomat(const char* str, const char** end) {
	static_assert(traits::IsMatrix<MatrixT>::value, "This type if not a matrix, dumbass.");

	return strtomat<
		typename traits::MatrixTraits<MatrixT>::Type,
		traits::MatrixTraits<MatrixT>::Rows,
		traits::MatrixTraits<MatrixT>::Columns,
		traits::MatrixTraits<MatrixT>::Order,
		traits::MatrixTraits<MatrixT>::Layout,
		traits::MatrixTraits<MatrixT>::Packed>(str, end);
}



template <class T, bool Packed>
std::ostream& operator<<(std::ostream& os, const Quaternion<T, Packed>& q) {
	os << "["
	   << q.Angle() * T(180.0) / T(3.1415926535897932384626)
	   << " deg @ "
	   << q.Axis()
	   << "]";
	return os;
}


} // namespace mathter