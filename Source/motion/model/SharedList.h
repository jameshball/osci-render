#pragma once

#include <compare>
#include <cstddef>
#include <initializer_list>
#include <iterator>
#include <memory>
#include <type_traits>
#include <vector>

namespace motion {
// A list whose items stay shared between copies until one is changed. Copying
// the list copies pointers, so undo snapshots and previews share every item
// they did not change. Reads look like a vector's but are const; a write goes
// through change(), which copies that one item if another list still holds it.
// A reference taken before change() keeps reading the item as it was.
template <typename T>
class SharedList {
    using Items = std::vector<std::shared_ptr<const T>>;

public:
    class const_iterator {
    public:
        using iterator_category = std::random_access_iterator_tag;
        using iterator_concept = std::random_access_iterator_tag;
        using value_type = T;
        using difference_type = std::ptrdiff_t;
        using pointer = const T*;
        using reference = const T&;

        const_iterator() = default;
        reference operator*() const { return **position; }
        pointer operator->() const { return position->get(); }
        reference operator[](difference_type offset) const { return *position[offset]; }
        const_iterator& operator++() { ++position; return *this; }
        const_iterator operator++(int) { auto copy = *this; ++position; return copy; }
        const_iterator& operator--() { --position; return *this; }
        const_iterator operator--(int) { auto copy = *this; --position; return copy; }
        const_iterator& operator+=(difference_type offset) { position += offset; return *this; }
        const_iterator& operator-=(difference_type offset) { position -= offset; return *this; }
        friend const_iterator operator+(const_iterator it, difference_type offset) { return it += offset; }
        friend const_iterator operator+(difference_type offset, const_iterator it) { return it += offset; }
        friend const_iterator operator-(const_iterator it, difference_type offset) { return it -= offset; }
        friend difference_type operator-(const const_iterator& a, const const_iterator& b) { return a.position - b.position; }
        friend bool operator==(const const_iterator& a, const const_iterator& b) { return a.position == b.position; }
        friend auto operator<=>(const const_iterator& a, const const_iterator& b) { return a.position <=> b.position; }

    private:
        friend class SharedList;
        explicit const_iterator(typename Items::const_iterator at) : position(at) {}
        typename Items::const_iterator position;
    };
    using iterator = const_iterator;
    using value_type = T;
    using size_type = std::size_t;

    SharedList() = default;
    SharedList(std::initializer_list<T> values) {
        items.reserve(values.size());
        for (const auto& value : values) { push_back(value); }
    }

    const_iterator begin() const { return const_iterator(items.cbegin()); }
    const_iterator end() const { return const_iterator(items.cend()); }
    std::size_t size() const { return items.size(); }
    bool empty() const { return items.empty(); }
    const T& operator[](std::size_t index) const { return *items[index]; }
    const T& front() const { return *items.front(); }
    const T& back() const { return *items.back(); }

    // The item at `index`, ready to write: copied first if anything else
    // shares it, so the change reaches only this list.
    T& change(std::size_t index) {
        auto& item = items[index];
        if (item.use_count() > 1) { item = std::make_shared<T>(*item); }
        // Every item is created non-const by this class, so writing is defined.
        return const_cast<T&>(*item);
    }
    T& change(const_iterator at) { return change(static_cast<std::size_t>(at - begin())); }
    // The first item with this id, ready to write; null when none has it.
    template <typename Id>
    T* changeById(Id id) {
        for (std::size_t index = 0; index < items.size(); ++index) {
            if (items[index]->id == id) { return &change(index); }
        }
        return nullptr;
    }

    // Changes every item `needs` picks; the rest stay shared.
    template <typename Needs, typename Apply>
    void changeEach(Needs&& needs, Apply&& apply) {
        for (std::size_t index = 0; index < items.size(); ++index) {
            if (needs(*items[index])) { apply(change(index)); }
        }
    }

    template <typename Apply>
    void changeAll(Apply&& apply) {
        for (std::size_t index = 0; index < items.size(); ++index) { apply(change(index)); }
    }

    void reserve(std::size_t count) { items.reserve(count); }
    void clear() { items.clear(); }
    // New items share one copy of `value` until each is changed.
    void resize(std::size_t count, const T& value = T {}) { items.resize(count, std::make_shared<T>(value)); }
    void push_back(T value) { items.push_back(std::make_shared<T>(std::move(value))); }
    const_iterator insert(const_iterator at, T value) {
        return const_iterator(items.insert(at.position, std::make_shared<T>(std::move(value))));
    }
    const_iterator erase(const_iterator at) { return const_iterator(items.erase(at.position)); }
    template <typename Predicate>
    std::size_t eraseIf(Predicate&& predicate) {
        return std::erase_if(items, [&predicate](const auto& item) { return predicate(*item); });
    }
    // Moves the item at `from` so it ends up at `to`, sharing it unchanged.
    void move(std::size_t from, std::size_t to) {
        auto item = std::move(items[from]);
        items.erase(items.begin() + static_cast<std::ptrdiff_t>(from));
        items.insert(items.begin() + static_cast<std::ptrdiff_t>(to), std::move(item));
    }

    // True when both lists hold this very item, unchanged.
    bool shares(std::size_t index, const SharedList& other) const {
        for (const auto& item : other.items) {
            if (item == items[index]) { return true; }
        }
        return false;
    }

    friend bool operator==(const SharedList& a, const SharedList& b) {
        if (a.items.size() != b.items.size()) { return false; }
        for (std::size_t index = 0; index < a.items.size(); ++index) {
            if (a.items[index] != b.items[index] && !(*a.items[index] == *b.items[index])) { return false; }
        }
        return true;
    }

private:
    Items items;
};

// Item `index` for lookups that serve both const and mutable callers: read
// from a const list, changed (copied if shared) through a mutable one. Find
// the match read-only first, so only the item returned is copied.
template <typename List>
auto& itemAt(List& list, std::size_t index) {
    if constexpr (std::is_const_v<List>) {
        return list[index];
    } else {
        return list.change(index);
    }
}
}
