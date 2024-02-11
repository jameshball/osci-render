// L=============================================================================
// L This software is distributed under the MIT license.
// L Copyright 2021 Péter Kardos
// L=============================================================================

#pragma once

#include "Vector.hpp"

#include <array>
#include <cmath>
#include <limits>


namespace mathter {


//------------------------------------------------------------------------------
// Shapes
//------------------------------------------------------------------------------

template <class T, int Dim>
class Hyperplane;

template <class T, int Dim>
class Line {
	using VectorT = Vector<T, Dim>;

public:
	/// <summary> Does not zero-initialize members. </summary>
	Line() = default;

	/// <summary> Construct a line through <paramref name="base"/> in given <paramref name="direction"/>. </summary>
	/// <param name="base"> Any point in 3D space. </param>
	/// <param name="direction"> Must be normalized. </param>
	Line(const VectorT& base, const VectorT& direction) : direction(direction), base(base) {
		assert(IsNormalized(direction));
	}

	/// <summary> Constructs a line through both points. </summary>
	/// <param name="point1"> Base of the line. </param>
	/// <param name="point2"> Specifies direction only. </param>
	static Line Through(const VectorT& point1, const VectorT& point2) {
		return Line(point1, SafeNormalize(point2 - point1));
	}

	/// <summary> A 2D plane and line are equivalent, converts representation. Only for 2D. </summary>
	Line(const Hyperplane<T, 2>& plane);

	/// <summary> Return the signed direction of the line (as given in constructor). </summary>
	VectorT Direction() const { return direction; }

	/// <summary> Returns the base point or point1 as given in constructor. </summary>
	VectorT Base() const { return base; }

	/// <summary> Returns the point at <paramref name="param"/> distance from the base point along direction. </summary>
	VectorT PointAt(T param) const { return base + param * direction; }

public:
	VectorT direction, base;
};



template <class T, int Dim>
class LineSegment {
	using VectorT = Vector<T, Dim>;

public:
	LineSegment() : point1(0), point2(0) {
		point2(0) = 1;
	}
	LineSegment(const VectorT& base, const VectorT& direction, T length) {
		point1 = base;
		point2 = base + direction * length;
	}
	LineSegment(const VectorT& point1, const VectorT& point2) : point1(point1), point2(point2) {}

	T Length() const { return mathter::Length(point2 - point1); }
	VectorT Direction() const { return Normalize(point2 - point1); }
	VectorT Start() const { return point1; }
	VectorT End() const { return point2; }
	VectorT Interpol(T t) const { return t * point2 + (T(1) - t) * point1; }

	mathter::Line<T, Dim> Line() const {
		return mathter::Line<T, Dim>{ point1, Direction() };
	}

public:
	VectorT point1, point2;
};


template <class T, int Dim>
class Ray : protected Line<T, Dim> {
public:
	// Inhreitance is protected to deny casting.
	// Casting is bad 'cause we don't want to implicit cast a Ray to a Line and Intersect() it with a plane.
	using Line<T, Dim>::Line;
	using Line<T, Dim>::Through;
	using Line<T, Dim>::Direction;
	using Line<T, Dim>::Base;
	using Line<T, Dim>::PointAt;
	using Line<T, Dim>::direction;
	using Line<T, Dim>::base;
	mathter::Line<T, Dim> Line() const {
		return static_cast<mathter::Line<T, Dim>>(*this);
	}
};



template <class T, int Dim>
class Hyperplane {
	using VectorT = Vector<T, Dim>;

public:
	Hyperplane() : normal(0), scalar(0) { normal(0) = 1; }
	Hyperplane(const VectorT& base, const VectorT& normal) : normal(normal) {
		assert(IsNormalized(normal));
		scalar = Dot(normal, base);
	}
	Hyperplane(const VectorT& normal, T scalar) : normal(normal), scalar(scalar) {
		assert(IsNormalized(normal));
	}
	Hyperplane(const Line<T, 2>& line) {
		static_assert(Dim == 2, "Plane dimension must be two, which is a line.");
		normal = { -line.Direction()(1), line.Direction()(0) };
		scalar = Dot(normal, line.Base());
	}

	const VectorT& Normal() const { return normal; }
	T Scalar() const { return scalar; }

	template <bool Packed>
	T Distance(const Vector<T, Dim, Packed>& point) {
		return Dot(point, normal) - scalar;
	}

private:
	VectorT normal;
	T scalar;
};


template <class T, int Dim>
Line<T, Dim>::Line(const Hyperplane<T, 2>& plane) {
	static_assert(Dim == 2, "Line dimension must be two, since it a plane in 2 dimensional space.");

	// Intersect plane's line with line through origo perpendicular to plane to find suitable base
	T a = plane.Normal()(0);
	T b = plane.Normal()(1);
	T d = plane.Scalar();
	T div = (a * a + b * b);
	base = { a * d / div, b * d / div };
	direction = { b, -a };
}


template <class T>
class Triangle3D {
public:
	Triangle3D() = default;
	Triangle3D(const Vector<T, 3, false>& a, const Vector<T, 3, false>& b, const Vector<T, 3, false>& c) : a(a), b(b), c(c) {}
	Vector<T, 3, false> a, b, c; // Corners of the traingle.
};


//------------------------------------------------------------------------------
// Intersections
//------------------------------------------------------------------------------


template <class T, class U>
class Intersection;



template <class T, class U>
auto Intersect(const T& t, const U& u) {
	return Intersection<T, U>(t, u);
}


// Plane-line intersection
template <class T, int Dim>
class Intersection<Hyperplane<T, Dim>, Line<T, Dim>> {
protected:
	using PlaneT = Hyperplane<T, Dim>;
	using LineT = Line<T, Dim>;
	using VectorT = Vector<T, Dim>;

public:
	Intersection(const PlaneT& plane, const LineT& line);

	bool Intersecting() const { return !std::isinf(param); }
	VectorT Point() const { return line.PointAt(param); }
	T LineParameter() const { return param; }

private:
	LineT line;
	T param;
};

template <class T, int Dim>
class Intersection<Line<T, Dim>, Hyperplane<T, Dim>> : public Intersection<Hyperplane<T, Dim>, Line<T, Dim>> {
	using PlaneT = Hyperplane<T, Dim>;
	using LineT = Line<T, Dim>;

public:
	Intersection(const LineT& line, const PlaneT& plane) : Intersection<Hyperplane<T, Dim>, Line<T, Dim>>(plane, line) {}
};



// Plane-line segment intersection
template <class T, int Dim>
class Intersection<Hyperplane<T, Dim>, LineSegment<T, Dim>> {
	using PlaneT = Hyperplane<T, Dim>;
	using LineT = LineSegment<T, Dim>;
	using VectorT = Vector<T, Dim>;

public:
	Intersection(const PlaneT& plane, const LineT& line) {
		lineSegment = line;
		auto intersection = Intersect(plane, line.Line());
		param = intersection.LineParameter() / line.Length();
	}

	bool Intersecting() const { return T(0) <= param && param <= T(1); }
	VectorT Point() const { return lineSegment.Interpol(param); }
	T InterpolParameter() const { return param; }
	T LineParameter() const { return param * lineSegment.Length(); }

private:
	LineT lineSegment;
	T param;
};

template <class T, int Dim>
class Intersection<LineSegment<T, Dim>, Hyperplane<T, Dim>> : public Intersection<Hyperplane<T, Dim>, LineSegment<T, Dim>> {
	using PlaneT = Hyperplane<T, Dim>;
	using LineT = LineSegment<T, Dim>;

public:
	Intersection(const LineT& line, const PlaneT& plane) : Intersection<Hyperplane<T, Dim>, LineSegment<T, Dim>>(plane, line) {}
};



template <class T, int Dim>
Intersection<Hyperplane<T, Dim>, Line<T, Dim>>::Intersection(const PlaneT& plane, const LineT& line) {
	this->line = line;

	// We have to solve the system of linear equations for x,y,z,t
	//	|d |	|a b c 0|	|x|
	//	|px|	|1 0 0 p|	|y|
	//	|py|  =	|0 1 0 q| *	|z|
	//	|pz|	|0 0 1 r|	|t|
	//   b    =     A     *  x
	// where [px,py,pz] + t*[p,q,r] = [x,y,z] is the line's equation
	// and ax + by + cz + d = 0 is the plane's equation

	Vector<T, Dim + 1> b;
	Vector<T, Dim + 1> A_inv_t;

	// Fill up 'b'
	b = -plane.Scalar() | line.Base();

	// Fill up 'A_inv_t', which is the last line of A^-1, used to calculate 't'
	A_inv_t = 1 | plane.Normal();

	// Compute result of the equation
	T scaler = Dot(line.Direction(), plane.Normal());
	T x_t = Dot(A_inv_t, b);
	T t = x_t / scaler;
	param = -t;
}



// 2D line intersection
// with lines
template <class T>
class Intersection<Line<T, 2>, Line<T, 2>> {
	using LineT = Line<T, 2>;

public:
	Intersection(const LineT& l1, const LineT& l2) {
		line2 = l2;
		auto intersection = Intersect(Hyperplane<T, 2>(l1), l2);
		param2 = intersection.LineParameter();
		param1 = std::isinf(param2) ? std::numeric_limits<T>::infinity() : Length(intersection.Point() - l1.Base());
	}

	bool Intersecting() const { return !std::isinf(param1); }
	T LineParameter1() const { return param1; }
	T LineParameter2() const { return param2; }
	Vector<T, 2> Point() const { return line2.PointAt(param2); }

private:
	T param1, param2;
	LineT line2;
};

// with hyperplanes
template <class T>
class Intersection<Hyperplane<T, 2>, Hyperplane<T, 2>> : public Intersection<Line<T, 2>, Line<T, 2>> {
public:
	Intersection(const Hyperplane<T, 2>& p1, const Hyperplane<T, 2>& p2) : Intersection<Line<T, 2>, Line<T, 2>>(p1, p2) {}
};


// line segments
template <class T>
class Intersection<LineSegment<T, 2>, LineSegment<T, 2>> {
public:
	Intersection(const LineSegment<T, 2>& l1, const LineSegment<T, 2>& l2) {
		lineSegment1 = l1;
		lineSegment2 = l2;
		auto intersection = Intersect(l1.Line(), l2.Line());
		if (intersection.Intersecting()) {
			param1 = intersection.LineParameter1() / l1.Length();
			param2 = intersection.LineParameter2() / l2.Length();
		}
		else {
			param1 = param2 = std::numeric_limits<T>::infinity();
		}
	}

	bool Intersecting() const {
		return (T(0) <= param1 && param1 <= T(1))
			   && (T(0) <= param2 && param2 <= T(1));
	}
	Vector<T, 2> Point() const { return lineSegment1.Interpol(param1); }
	T InterpolParameter1() const { return param1; }
	T InterpolParameter2() const { return param2; }
	T LineParameter1() const { return param1 * lineSegment1.Length(); }
	T LineParameter2() const { return param2 * lineSegment2.Length(); }

private:
	T param1;
	T param2;
	LineSegment<T, 2> lineSegment1;
	LineSegment<T, 2> lineSegment2;
};


// line segment vs line2d
template <class T>
class Intersection<LineSegment<T, 2>, Line<T, 2>> {
public:
	Intersection(const LineSegment<T, 2>& line1, const Line<T, 2>& line2) {
		auto inter = Intersect(line1.Line(), line2);
		if (inter.Intersecting() && inter.LineParameter1() < line1.Length()) {
			param1 = inter.LineParameter1();
			param2 = inter.LineParameter2();
		}
		else {
			param1 = param2 = std::numeric_limits<T>::infinity();
		}
	}

	bool Intersecting() const { return !isinf(param1); }
	Vector<T, 2> Point() const { return line1.Line().PointAt(param1); }
	T LineParameter1() const { return param1; }
	T InterpolParameter1() const { return param1 / line1.Length(); }
	T LineParameter2() const { return param2; }

private:
	T param1;
	T param2;
	LineSegment<T, 2> line1;
};

template <class T>
class Intersection<Line<T, 2>, LineSegment<T, 2>> : private Intersection<LineSegment<T, 2>, Line<T, 2>> {
public:
	Intersection(const Line<T, 2>& line1, const LineSegment<T, 2>& line2)
		: Intersection<LineSegment<T, 2>, Line<T, 2>>(line2, line1) {}

	using Intersection<LineSegment<T, 2>, Line<T, 2>>::Intersecting;
	using Intersection<LineSegment<T, 2>, Line<T, 2>>::Point;

	T LineParameter1() const { return Intersection<LineSegment<T, 2>, Line<T, 2>>::LineParameter2(); }
	T InterpolParameter2() const { return Intersection<LineSegment<T, 2>, Line<T, 2>>::InterpolParameter1(); }
	T LineParameter2() const { return Intersection<LineSegment<T, 2>, Line<T, 2>>::LineParameter1(); }
};


// Ray-triangle intersection (M�ller-Trumbore algorithm)
template <class T>
class Intersection<Ray<T, 3>, Triangle3D<T>> {
	using VectorT = Vector<T, 3, false>;

public:
	Intersection(const Ray<T, 3>& ray, const Triangle3D<T>& triangle);

	bool IsIntersecting() const { return intersecting; }
	VectorT Point() const { return point; }

	template <class U>
	U Interpolate(const U& a, const U& b, const U& c) const;

	T GetT() const { return t; }
	T GetU() const { return u; }
	T GetV() const { return v; }

private:
	T t, u, v;
	bool intersecting;
	VectorT point;
};

template <class T>
Intersection<Ray<T, 3>, Triangle3D<T>>::Intersection(const Ray<T, 3>& ray, const Triangle3D<T>& triangle) {
	constexpr T EPSILON = T(0.00000001);

	VectorT edge1 = triangle.b - triangle.a;
	VectorT edge2 = triangle.c - triangle.a;

	VectorT h = Cross(ray.Direction(), edge2);
	T a = Dot(edge1, h);

	if (std::abs(a) < EPSILON) {
		intersecting = false;
		return;
	}

	T f = T(1) / a;
	VectorT s = ray.Base() - triangle.a;
	u = f * Dot(s, h);

	if (u < T(0) || u > T(1)) {
		intersecting = false;
		return;
	}

	VectorT q = Cross(s, edge1);
	v = f * Dot(ray.Direction(), q);

	if (v < 0.0 || u + v > 1.0) {
		intersecting = false;
		return;
	}

	t = f * Dot(edge2, q);
	intersecting = t > EPSILON;
	if (intersecting) {
		point = ray.PointAt(t);
	}
}


template <class T>
template <class U>
U Intersection<Ray<T, 3>, Triangle3D<T>>::Interpolate(const U& a, const U& b, const U& c) const {
	T w = T(1) - u - v;
	return u * b + v * c + w * a;
}


template <class T, int Dim, int Order>
class BezierCurve {
	static_assert(Order >= 1, "Bezier curve must have order n>=1.");

public:
	using VectorT = Vector<T, Dim, false>;

	VectorT operator()(T t) const {
		return EvalInterpolRecurse(t);
	}

protected:
	VectorT EvalInterpolRecurse(T t) const;

public:
	std::array<VectorT, Order + 1> p;
};


template <class T, int Dim, int Order>
auto BezierCurve<T, Dim, Order>::EvalInterpolRecurse(T t) const -> VectorT {
	std::array<VectorT, Order + 1> reduction = p;

	T u = T(1) - t;

	for (int i = Order; i >= 1; --i) {
		for (int j = 1; j <= i; ++j) {
			reduction[j - 1] = u * reduction[j - 1] + t * reduction[j];
		}
	}

	return reduction[0];
}



} // namespace mathter