#pragma once

#include <cstddef>
#include <memory>
#include <stdexcept>
#include <utility>

namespace cpp_mastery {

template<typename T>
class Vector {
private:
    using Allocator = std::allocator<T>;
    using AllocatorTraits = std::allocator_traits<Allocator>;

public:
    using value_type = T;
    using size_type = std::size_t;
    using reference = T&;
    using const_reference = const T&;
    using pointer = T*;
    using const_pointer = const T*;
    using iterator = pointer;
    using const_iterator = const_pointer;

    Vector() noexcept = default;
    ~Vector();

    Vector(const Vector& other);
    Vector& operator=(const Vector& other);

    Vector(Vector&& other) noexcept;
    Vector& operator=(Vector&& other) noexcept;

    [[nodiscard]] size_type size() const noexcept;
    [[nodiscard]] size_type capacity() const noexcept;
    [[nodiscard]] size_type max_size() const noexcept;
    [[nodiscard]] bool empty() const noexcept;

    reference operator[](size_type index) noexcept;
    const_reference operator[](size_type index) const noexcept;
    reference at(size_type index);
    const_reference at(size_type index) const;
    reference front() noexcept;
    const_reference front() const noexcept;
    reference back() noexcept;
    const_reference back() const noexcept;
    pointer data() noexcept;
    const_pointer data() const noexcept;

    iterator begin() noexcept;
    const_iterator begin() const noexcept;
    const_iterator cbegin() const noexcept;
    iterator end() noexcept;
    const_iterator end() const noexcept;
    const_iterator cend() const noexcept;

    void clear() noexcept;
    void reserve(size_type new_capacity);
    void resize(size_type new_size);
    void resize(size_type new_size, const T& value);
    void pop_back() noexcept;

    void push_back(const T& value);
    void push_back(T&& value);

    template<typename... Args>
    reference emplace_back(Args&&... args);

private:
    static constexpr size_type initial_capacity = 4;

    static void destroy_range_reverse(
        pointer elements,
        size_type first,
        size_type last) noexcept;

    void destroy_elements() noexcept;
    void deallocate_storage() noexcept;
    void release_storage() noexcept;
    void shrink_to(size_type new_size) noexcept;
    void swap_storage(Vector& other) noexcept;

    void commit_reallocation(
        pointer new_data,
        size_type new_size,
        size_type new_capacity) noexcept;

    [[nodiscard]] size_type next_capacity() const;

    template<typename ConstructElement>
    void grow_to(size_type new_size, ConstructElement&& construct_element);

    template<typename ConstructElement>
    void reallocate_and_grow(
        size_type new_size,
        ConstructElement&& construct_element);

    template<typename... Args>
    reference grow_and_emplace(Args&&... args);

    [[no_unique_address]] Allocator allocator_{};
    pointer data_ = nullptr;
    size_type size_ = 0;
    size_type capacity_ = 0;
};

} // namespace cpp_mastery

#include <cpp_mastery/vector/Vector.tpp>
