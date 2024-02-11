// L=============================================================================
// L This software is distributed under the MIT license.
// L Copyright 2021 Péter Kardos
// L=============================================================================

#pragma once

#include "MatrixArithmetic.hpp"
#include "MatrixImpl.hpp"


namespace mathter {


/// <summary> Returns the trace (sum of diagonal elements) of the matrix. </summary>
template <class T, int Dim, eMatrixOrder Order, eMatrixLayout Layout, bool Packed>
T Trace(const Matrix<T, Dim, Dim, Order, Layout, Packed>& m) {
	T sum = m(0, 0);
	for (int i = 1; i < Dim; ++i) {
		sum += m(i, i);
	}
	return sum;
}


/// <summary> Returns the determinant of a 2x2 matrix. </summary>
template <class T, eMatrixOrder Order, eMatrixLayout Layout, bool Packed>
T Determinant(const Matrix<T, 2, 2, Order, Layout, Packed>& m) {
	return m(0, 0) * m(1, 1) - m(1, 0) * m(0, 1);
}

/// <summary> Returns the determinant of a 3x3matrix. </summary>
template <class T, eMatrixOrder Order, eMatrixLayout Layout, bool Packed>
T Determinant(const Matrix<T, 3, 3, Order, Layout, Packed>& m) {
	using Vec3 = Vector<T, 3, false>;

	Vec3 r0_zyx = m.stripes[0].zyx;
	Vec3 r1_xzy = m.stripes[1].xzy;
	Vec3 r1_yxz = m.stripes[1].yxz;
	Vec3 r2_yxz = m.stripes[2].yxz;
	Vec3 r2_xzy = m.stripes[2].xzy;

	T det = Dot(r0_zyx, r1_xzy * r2_yxz - r1_yxz * r2_xzy);

	return det;
}

/// <summary> Returns the determinant of a 4x4matrix. </summary>
template <class T, eMatrixOrder Order, eMatrixLayout Layout, bool Packed>
T Determinant(const Matrix<T, 4, 4, Order, Layout, Packed>& m) {
	using Vec3 = Vector<T, 3, false>;
	using Vec4 = Vector<T, 4, false>;

	Vec4 evenPair = { 1, -1, -1, 1 };
	Vec4 oddPair = { -1, 1, 1, -1 };

	const Vec4& r0 = m.stripes[0];
	const Vec4& r1 = m.stripes[1];
	const Vec4& r2 = m.stripes[2];
	const Vec4& r3 = m.stripes[3];

	Vec4 r2_zwzw = r2.zwzw;
	Vec4 r0_yyxx = r0.yyxx;
	Vec4 r1_wwxy = r1.wwxy;
	Vec4 r2_xyzz = r2.xyzz;
	Vec4 r3_wwww = r3.wwww;
	Vec4 r1_zzxy = r1.zzxy;
	Vec4 r0_yxyx = r0.yxyx;
	Vec4 r3_xxyy = r3.xxyy;
	Vec4 r1_wzwz = r1.wzwz;
	Vec4 r2_xyww = r2.xyww;
	Vec4 r3_zzzz = r3.zzzz;

	Vec3 r2_yxz = r2.yxz;
	Vec3 r3_xzy = r3.xzy;
	Vec3 r2_xzy = r2.xzy;
	Vec3 r3_yxz = r3.yxz;
	Vec3 r2_yxw = r2.yxw;
	Vec3 r1_zyx = r1.zyx;
	Vec3 r3_yxw = r3.yxw;
	Vec3 r2_xwy = r2.xwy;
	Vec3 r3_xwy = r3.xwy;
	Vec3 r1_wyx = r1.wyx;
	T r0_w = r0.w;
	T r0_z = r0.z;

	T det = Dot(evenPair, r0_yyxx * r1_wzwz * r2_zwzw * r3_xxyy)
			+ Dot(oddPair, r0_yxyx * r1_wwxy * r2_xyww * r3_zzzz)
			+ Dot(evenPair, r0_yxyx * r1_zzxy * r2_xyzz * r3_wwww)
			+ (r0_w * Dot(r1_zyx, r2_yxz * r3_xzy - r2_xzy * r3_yxz))
			+ (r0_z * Dot(r1_wyx, r2_xwy * r3_yxw - r2_yxw * r3_xwy));

	return det;
}


/// <summary> Returns the determinant of the matrix. </summary>
template <class T, int Dim, eMatrixOrder Order, eMatrixLayout Layout, bool Packed>
T Determinant(const Matrix<T, Dim, Dim, Order, Layout, Packed>& m) {
	// only works if L's diagonal is 1s
	int parity;
	auto [L, U, P] = DecomposeLUP(m, parity);
	T prod = U(0, 0);
	for (int i = 1; i < U.RowCount(); ++i) {
		prod *= U(i, i);
	}
	return parity * prod;
}

/// <summary> Transposes the matrix in-place. </summary>
template <class T, int Rows, int Columns, eMatrixOrder Order, eMatrixLayout Layout, bool Packed>
Matrix<T, Columns, Rows, Order, Layout, Packed> Transpose(const Matrix<T, Rows, Columns, Order, Layout, Packed>& m) {
	Matrix<T, Columns, Rows, Order, Layout, Packed> result;
	for (int i = 0; i < m.RowCount(); ++i) {
		for (int j = 0; j < m.ColumnCount(); ++j) {
			result(j, i) = m(i, j);
		}
	}
	return result;
}


/// <summary> Returns the inverse of a 2x2 matrix. </summary>
template <class T, eMatrixOrder Order, eMatrixLayout Layout, bool Packed>
auto Inverse(const Matrix<T, 2, 2, Order, Layout, Packed>& m) {
	Matrix<T, 2, 2, Order, Layout, Packed> result;

	const auto& r0 = m.stripes[0];
	const auto& r1 = m.stripes[1];

	result.stripes[0] = { r1.y, -r0.y };
	result.stripes[1] = { -r1.x, r0.x };

	auto det = T(1) / (r0.x * r1.y - r0.y * r1.x);
	result *= det;

	return result;
}


/// <summary> Returns the inverse of a 3x3 matrix. </summary>
template <class T, eMatrixOrder Order, eMatrixLayout Layout, bool Packed>
auto Inverse(const Matrix<T, 3, 3, Order, Layout, Packed>& m) {
	Matrix<T, 3, 3, Order, Layout, Packed> result;

	using Vec3 = Vector<T, 3, false>;

	// This code below uses notation for row-major matrices' stripes.
	// It, however, "magically" works for column-major layout as well.
	Vec3 r0_zxy = m.stripes[0].zxy;
	Vec3 r0_yzx = m.stripes[0].yzx;
	Vec3 r1_yzx = m.stripes[1].yzx;
	Vec3 r1_zxy = m.stripes[1].zxy;
	Vec3 r2_zxy = m.stripes[2].zxy;
	Vec3 r2_yzx = m.stripes[2].yzx;

	Vec3 c0 = r1_yzx * r2_zxy - r1_zxy * r2_yzx;
	Vec3 c1 = r0_zxy * r2_yzx - r0_yzx * r2_zxy;
	Vec3 c2 = r0_yzx * r1_zxy - r0_zxy * r1_yzx;

	Vec3 r0_zyx = m.stripes[0].zyx;
	Vec3 r1_xzy = m.stripes[1].xzy;
	Vec3 r1_yxz = m.stripes[1].yxz;
	Vec3 r2_yxz = m.stripes[2].yxz;
	Vec3 r2_xzy = m.stripes[2].xzy;

	result.stripes[0] = { c0[0], c1[0], c2[0] };
	result.stripes[1] = { c0[1], c1[1], c2[1] };
	result.stripes[2] = { c0[2], c1[2], c2[2] };

	T det = T(1) / Dot(r0_zyx, r1_xzy * r2_yxz - r1_yxz * r2_xzy);

	result.stripes[0] *= det;
	result.stripes[1] *= det;
	result.stripes[2] *= det;

	return result;
}


/// <summary> Returns the inverse of a 4x4 matrix. </summary>
template <class T, eMatrixOrder Order, eMatrixLayout Layout, bool Packed>
auto Inverse(const Matrix<T, 4, 4, Order, Layout, Packed>& m) {
	Matrix<T, 4, 4, Order, Layout, Packed> result;

	using Vec3 = Vector<T, 3, false>;
	using Vec4 = Vector<T, 4, false>;

	Vec4 even = { 1, -1, 1, -1 };
	Vec4 odd = { -1, 1, -1, 1 };
	Vec4 evenPair = { 1, -1, -1, 1 };
	Vec4 oddPair = { -1, 1, 1, -1 };

	const Vec4& r0 = m.stripes[0];
	const Vec4& r1 = m.stripes[1];
	const Vec4& r2 = m.stripes[2];
	const Vec4& r3 = m.stripes[3];

	Vec4 r0_wwwz = r0.wwwz;
	Vec4 r0_yxxx = r0.yxxx;
	Vec4 r0_zzyy = r0.zzyy;
	Vec4 r1_wwwz = r1.wwwz;
	Vec4 r1_yxxx = r1.yxxx;
	Vec4 r1_zzyy = r1.zzyy;
	Vec4 r2_wwwz = r2.wwwz;
	Vec4 r2_yxxx = r2.yxxx;
	Vec4 r2_zzyy = r2.zzyy;
	Vec4 r3_wwwz = r3.wwwz;
	Vec4 r3_yxxx = r3.yxxx;
	Vec4 r3_zzyy = r3.zzyy;

	Vec4 r0_wwwz_r1_yxxx = r0_wwwz * r1_yxxx;
	Vec4 r0_wwwz_r1_zzyy = r0_wwwz * r1_zzyy;
	Vec4 r0_yxxx_r1_wwwz = r0_yxxx * r1_wwwz;
	Vec4 r0_yxxx_r1_zzyy = r0_yxxx * r1_zzyy;
	Vec4 r0_zzyy_r1_wwwz = r0_zzyy * r1_wwwz;
	Vec4 r0_zzyy_r1_yxxx = r0_zzyy * r1_yxxx;
	Vec4 r2_wwwz_r3_yxxx = r2_wwwz * r3_yxxx;
	Vec4 r2_wwwz_r3_zzyy = r2_wwwz * r3_zzyy;
	Vec4 r2_yxxx_r3_wwwz = r2_yxxx * r3_wwwz;
	Vec4 r2_yxxx_r3_zzyy = r2_yxxx * r3_zzyy;
	Vec4 r2_zzyy_r3_wwwz = r2_zzyy * r3_wwwz;
	Vec4 r2_zzyy_r3_yxxx = r2_zzyy * r3_yxxx;

	Vec4 c0 = odd * (r1_wwwz * r2_zzyy_r3_yxxx - r1_zzyy * r2_wwwz_r3_yxxx - r1_wwwz * r2_yxxx_r3_zzyy + r1_yxxx * r2_wwwz_r3_zzyy + r1_zzyy * r2_yxxx_r3_wwwz - r1_yxxx * r2_zzyy_r3_wwwz);
	Vec4 c1 = even * (r0_wwwz * r2_zzyy_r3_yxxx - r0_zzyy * r2_wwwz_r3_yxxx - r0_wwwz * r2_yxxx_r3_zzyy + r0_yxxx * r2_wwwz_r3_zzyy + r0_zzyy * r2_yxxx_r3_wwwz - r0_yxxx * r2_zzyy_r3_wwwz);
	Vec4 c2 = odd * (r0_wwwz_r1_zzyy * r3_yxxx - r0_zzyy_r1_wwwz * r3_yxxx - r0_wwwz_r1_yxxx * r3_zzyy + r0_yxxx_r1_wwwz * r3_zzyy + r0_zzyy_r1_yxxx * r3_wwwz - r0_yxxx_r1_zzyy * r3_wwwz);
	Vec4 c3 = even * (r0_wwwz_r1_zzyy * r2_yxxx - r0_zzyy_r1_wwwz * r2_yxxx - r0_wwwz_r1_yxxx * r2_zzyy + r0_yxxx_r1_wwwz * r2_zzyy + r0_zzyy_r1_yxxx * r2_wwwz - r0_yxxx_r1_zzyy * r2_wwwz);

	result.stripes[0] = { c0[0], c1[0], c2[0], c3[0] };
	result.stripes[1] = { c0[1], c1[1], c2[1], c3[1] };
	result.stripes[2] = { c0[2], c1[2], c2[2], c3[2] };
	result.stripes[3] = { c0[3], c1[3], c2[3], c3[3] };

	Vec4 r2_zwzw = r2.zwzw;
	Vec4 r0_yyxx = r0.yyxx;
	Vec4 r1_wwxy = r1.wwxy;
	Vec4 r2_xyzz = r2.xyzz;
	Vec4 r3_wwww = r3.wwww;
	Vec4 r1_zzxy = r1.zzxy;
	Vec4 r0_yxyx = r0.yxyx;
	Vec4 r3_xxyy = r3.xxyy;
	Vec4 r1_wzwz = r1.wzwz;
	Vec4 r2_xyww = r2.xyww;
	Vec4 r3_zzzz = r3.zzzz;

	Vec3 r2_yxz = r2.yxz;
	Vec3 r3_xzy = r3.xzy;
	Vec3 r2_xzy = r2.xzy;
	Vec3 r3_yxz = r3.yxz;
	Vec3 r2_yxw = r2.yxw;
	Vec3 r1_zyx = r1.zyx;
	Vec3 r3_yxw = r3.yxw;
	Vec3 r2_xwy = r2.xwy;
	Vec3 r3_xwy = r3.xwy;
	Vec3 r1_wyx = r1.wyx;
	T r0_w = r0.w;
	T r0_z = r0.z;

	T det = Dot(evenPair, r0_yyxx * r1_wzwz * r2_zwzw * r3_xxyy)
			+ Dot(oddPair, r0_yxyx * r1_wwxy * r2_xyww * r3_zzzz)
			+ Dot(evenPair, r0_yxyx * r1_zzxy * r2_xyzz * r3_wwww)
			+ (r0_w * Dot(r1_zyx, r2_yxz * r3_xzy - r2_xzy * r3_yxz))
			+ (r0_z * Dot(r1_wyx, r2_xwy * r3_yxw - r2_yxw * r3_xwy));

	T invDet = 1 / det;

	result.stripes[0] *= invDet;
	result.stripes[1] *= invDet;
	result.stripes[2] *= invDet;
	result.stripes[3] *= invDet;

	return result;
}



/// <summary> Returns the inverse of the matrix. </summary>
template <class T, int Dim, eMatrixOrder Order, eMatrixLayout Layout, bool Packed>
Matrix<T, Dim, Dim, Order, Layout, Packed> Inverse(const Matrix<T, Dim, Dim, Order, Layout, Packed>& m) {
	Matrix<T, Dim, Dim, Order, Layout, Packed> ret;

	auto LUP = DecomposeLUP(m);

	Vector<T, Dim, Packed> b(0);
	Vector<T, Dim, Packed> x;
	for (int col = 0; col < Dim; ++col) {
		b(std::max(0, col - 1)) = 0;
		b(col) = 1;
		x = LUP.Solve(b);
		for (int i = 0; i < Dim; ++i) {
			ret(i, col) = x(i);
		}
	}

	return ret;
}

/// <summary> Calculates the square of the Frobenius norm of the matrix. </summary>
template <class T, int Rows, int Columns, eMatrixOrder Order, eMatrixLayout Layout, bool Packed>
T NormSquared(const Matrix<T, Rows, Columns, Order, Layout, Packed>& m) {
	T sum = T(0);
	for (auto& stripe : m.stripes) {
		sum += LengthSquared(stripe);
	}
	return sum;
}

/// <summary> Calculates the Frobenius norm of the matrix. </summary>
template <class T, int Rows, int Columns, eMatrixOrder Order, eMatrixLayout Layout, bool Packed>
T Norm(const Matrix<T, Rows, Columns, Order, Layout, Packed>& m) {
	return sqrt(NormSquared(m));
}



} // namespace mathter