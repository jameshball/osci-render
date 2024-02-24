// L=============================================================================
// L This software is distributed under the MIT license.
// L Copyright 2021 Péter Kardos
// L=============================================================================

#pragma once

#include <iterator>
#include <limits>


namespace mathter::impl {

// Helper for writing for loops as for (auto i : Range(0,10))
template <class T>
class RangeHelper {
public:
	class iterator {
		friend class RangeHelper;
		iterator(T value, T step) : value(value), step(step) {}

	public:
		iterator() : value(std::numeric_limits<T>::lowest()) {}

		using value_type = T;
		using difference_type = ptrdiff_t;
		using reference = T&;
		using pointer = T*;
		using iterator_category = std::forward_iterator_tag;

		void operator++() {
			value += step;
		}
		T operator*() const {
			return value;
		}
		bool operator==(const iterator& rhs) const {
			return value == rhs.value;
		}
		bool operator!=(const iterator& rhs) const {
			return !(*this == rhs);
		}

	private:
		T value;
		T step;
	};

	RangeHelper(T first, T last, T step) : first(first), last(last), step(step) {}

	iterator begin() const { return iterator(first, step); }
	iterator end() const { return iterator(last, step); }
	iterator cbegin() const { return iterator(first, step); }
	iterator cend() const { return iterator(last, step); }

private:
	T first, last, step;
};


template <class T>
RangeHelper<T> Range(T first, T last, T step) {
	return RangeHelper<T>(first, last, step);
}

template <class T>
RangeHelper<T> Range(T first, T last) {
	T step = last >= first ? T(1) : T(-1);
	return Range(first, last, step);
}

template <class T>
RangeHelper<T> Range(T last) {
	T first = T(0);
	T step = last >= first ? T(1) : T(-1);
	return Range(first, last, step);
}

} // namespace mathter::impl