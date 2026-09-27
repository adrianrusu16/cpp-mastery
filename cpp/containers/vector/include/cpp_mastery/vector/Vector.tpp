#pragma once

namespace cpp_mastery {

template<typename T>
Vector<T>::~Vector()
{
    release_storage();
}

template<typename T>
Vector<T>::Vector(const Vector& other)
    : allocator_{AllocatorTraits::select_on_container_copy_construction(other.allocator_)}
{
    if (other.size_ == 0) {
        return;
    }

    data_ = AllocatorTraits::allocate(allocator_, other.size_);
    capacity_ = other.size_;

    size_type constructed = 0;
    try {
        for (; constructed < other.size_; ++constructed) {
            std::construct_at(data_ + constructed, other.data_[constructed]);
        }
    } catch (...) {
        destroy_range_reverse(data_, 0, constructed);
        AllocatorTraits::deallocate(allocator_, data_, capacity_);
        data_ = nullptr;
        capacity_ = 0;
        throw;
    }

    size_ = other.size_;
}

template<typename T>
auto Vector<T>::operator=(const Vector& other) -> Vector&
{
    if (this == &other) {
        return *this;
    }

    Vector temporary{other};
    swap_storage(temporary);
    return *this;
}

template<typename T>
Vector<T>::Vector(Vector&& other) noexcept
    : allocator_{std::move(other.allocator_)},
      data_{std::exchange(other.data_, nullptr)},
      size_{std::exchange(other.size_, 0)},
      capacity_{std::exchange(other.capacity_, 0)}
{
}

template<typename T>
auto Vector<T>::operator=(Vector&& other) noexcept -> Vector&
{
    if (this == &other) {
        return *this;
    }

    release_storage();
    allocator_ = std::move(other.allocator_);
    data_ = std::exchange(other.data_, nullptr);
    size_ = std::exchange(other.size_, 0);
    capacity_ = std::exchange(other.capacity_, 0);
    return *this;
}

template<typename T>
auto Vector<T>::size() const noexcept -> size_type
{
    return size_;
}

template<typename T>
auto Vector<T>::capacity() const noexcept -> size_type
{
    return capacity_;
}

template<typename T>
auto Vector<T>::max_size() const noexcept -> size_type
{
    return AllocatorTraits::max_size(allocator_);
}

template<typename T>
bool Vector<T>::empty() const noexcept
{
    return size_ == 0;
}

template<typename T>
auto Vector<T>::operator[](const size_type index) noexcept -> reference
{
    return data_[index];
}

template<typename T>
auto Vector<T>::operator[](const size_type index) const noexcept -> const_reference
{
    return data_[index];
}

template<typename T>
auto Vector<T>::at(const size_type index) -> reference
{
    if (index >= size_) {
        throw std::out_of_range{"Vector::at index out of range"};
    }
    return data_[index];
}

template<typename T>
auto Vector<T>::at(const size_type index) const -> const_reference
{
    if (index >= size_) {
        throw std::out_of_range{"Vector::at index out of range"};
    }
    return data_[index];
}

template<typename T>
auto Vector<T>::front() noexcept -> reference
{
    return data_[0];
}

template<typename T>
auto Vector<T>::front() const noexcept -> const_reference
{
    return data_[0];
}

template<typename T>
auto Vector<T>::back() noexcept -> reference
{
    return data_[size_ - 1];
}

template<typename T>
auto Vector<T>::back() const noexcept -> const_reference
{
    return data_[size_ - 1];
}

template<typename T>
auto Vector<T>::data() noexcept -> pointer
{
    return data_;
}

template<typename T>
auto Vector<T>::data() const noexcept -> const_pointer
{
    return data_;
}

template<typename T>
auto Vector<T>::begin() noexcept -> iterator
{
    return data_;
}

template<typename T>
auto Vector<T>::begin() const noexcept -> const_iterator
{
    return data_;
}

template<typename T>
auto Vector<T>::cbegin() const noexcept -> const_iterator
{
    return begin();
}

template<typename T>
auto Vector<T>::end() noexcept -> iterator
{
    return data_ == nullptr ? nullptr : data_ + size_;
}

template<typename T>
auto Vector<T>::end() const noexcept -> const_iterator
{
    return data_ == nullptr ? nullptr : data_ + size_;
}

template<typename T>
auto Vector<T>::cend() const noexcept -> const_iterator
{
    return end();
}

template<typename T>
void Vector<T>::clear() noexcept
{
    destroy_elements();
    size_ = 0;
}

template<typename T>
void Vector<T>::reserve(const size_type new_capacity)
{
    if (new_capacity <= capacity_) {
        return;
    }
    if (new_capacity > max_size()) {
        throw std::length_error{"Vector capacity exceeds max_size"};
    }

    pointer new_data = AllocatorTraits::allocate(allocator_, new_capacity);
    size_type relocated = 0;

    try {
        for (; relocated < size_; ++relocated) {
            std::construct_at(
                new_data + relocated,
                std::move_if_noexcept(data_[relocated]));
        }
    } catch (...) {
        destroy_range_reverse(new_data, 0, relocated);
        AllocatorTraits::deallocate(allocator_, new_data, new_capacity);
        throw;
    }

    commit_reallocation(new_data, size_, new_capacity);
}

template<typename T>
void Vector<T>::resize(const size_type new_size)
{
    if (new_size <= size_) {
        shrink_to(new_size);
        return;
    }

    grow_to(new_size, [](pointer slot) {
        std::construct_at(slot);
    });
}

template<typename T>
void Vector<T>::resize(const size_type new_size, const T& value)
{
    if (new_size <= size_) {
        shrink_to(new_size);
        return;
    }

    grow_to(new_size, [&value](pointer slot) {
        std::construct_at(slot, value);
    });
}

template<typename T>
void Vector<T>::pop_back() noexcept
{
    std::destroy_at(data_ + (size_ - 1));
    --size_;
}

template<typename T>
void Vector<T>::push_back(const T& value)
{
    emplace_back(value);
}

template<typename T>
void Vector<T>::push_back(T&& value)
{
    emplace_back(std::move(value));
}

template<typename T>
template<typename... Args>
auto Vector<T>::emplace_back(Args&&... args) -> reference
{
    if (size_ == capacity_) {
        return grow_and_emplace(std::forward<Args>(args)...);
    }

    pointer new_element = data_ + size_;
    std::construct_at(new_element, std::forward<Args>(args)...);
    ++size_;
    return *new_element;
}

template<typename T>
void Vector<T>::destroy_range_reverse(
    pointer elements,
    const size_type first,
    size_type last) noexcept
{
    while (last > first) {
        --last;
        std::destroy_at(elements + last);
    }
}

template<typename T>
void Vector<T>::destroy_elements() noexcept
{
    destroy_range_reverse(data_, 0, size_);
}

template<typename T>
void Vector<T>::deallocate_storage() noexcept
{
    if (data_ != nullptr) {
        AllocatorTraits::deallocate(allocator_, data_, capacity_);
    }
}

template<typename T>
void Vector<T>::release_storage() noexcept
{
    destroy_elements();
    deallocate_storage();
    data_ = nullptr;
    size_ = 0;
    capacity_ = 0;
}

template<typename T>
void Vector<T>::shrink_to(const size_type new_size) noexcept
{
    destroy_range_reverse(data_, new_size, size_);
    size_ = new_size;
}

template<typename T>
void Vector<T>::swap_storage(Vector& other) noexcept
{
    using std::swap;
    swap(data_, other.data_);
    swap(size_, other.size_);
    swap(capacity_, other.capacity_);
}

template<typename T>
void Vector<T>::commit_reallocation(
    pointer new_data,
    const size_type new_size,
    const size_type new_capacity) noexcept
{
    destroy_elements();
    deallocate_storage();
    data_ = new_data;
    size_ = new_size;
    capacity_ = new_capacity;
}

template<typename T>
auto Vector<T>::next_capacity() const -> size_type
{
    const size_type maximum = max_size();

    if (capacity_ == 0) {
        return maximum < initial_capacity ? maximum : initial_capacity;
    }
    if (capacity_ >= maximum) {
        throw std::length_error{"Vector has reached max_size"};
    }
    if (capacity_ > maximum / 2) {
        return maximum;
    }
    return capacity_ * 2;
}

template<typename T>
template<typename ConstructElement>
void Vector<T>::grow_to(
    const size_type new_size,
    ConstructElement&& construct_element)
{
    if (new_size > max_size()) {
        throw std::length_error{"Vector::resize exceeds max_size"};
    }

    if (new_size > capacity_) {
        reallocate_and_grow(new_size, std::forward<ConstructElement>(construct_element));
        return;
    }

    size_type constructed = size_;
    try {
        for (; constructed < new_size; ++constructed) {
            construct_element(data_ + constructed);
        }
    } catch (...) {
        destroy_range_reverse(data_, size_, constructed);
        throw;
    }

    size_ = new_size;
}

template<typename T>
template<typename ConstructElement>
void Vector<T>::reallocate_and_grow(
    const size_type new_size,
    ConstructElement&& construct_element)
{
    pointer new_data = AllocatorTraits::allocate(allocator_, new_size);
    size_type tail_constructed = 0;
    size_type relocated = 0;

    try {
        for (; tail_constructed < new_size - size_; ++tail_constructed) {
            construct_element(new_data + size_ + tail_constructed);
        }

        for (; relocated < size_; ++relocated) {
            std::construct_at(
                new_data + relocated,
                std::move_if_noexcept(data_[relocated]));
        }
    } catch (...) {
        destroy_range_reverse(new_data, 0, relocated);
        destroy_range_reverse(new_data, size_, size_ + tail_constructed);
        AllocatorTraits::deallocate(allocator_, new_data, new_size);
        throw;
    }

    commit_reallocation(new_data, new_size, new_size);
}

template<typename T>
template<typename... Args>
auto Vector<T>::grow_and_emplace(Args&&... args) -> reference
{
    const size_type new_capacity = next_capacity();
    if (new_capacity <= capacity_) {
        throw std::length_error{"Vector cannot grow further"};
    }

    pointer new_data = AllocatorTraits::allocate(allocator_, new_capacity);
    size_type relocated = 0;
    bool appended_constructed = false;

    try {
        std::construct_at(new_data + size_, std::forward<Args>(args)...);
        appended_constructed = true;

        for (; relocated < size_; ++relocated) {
            std::construct_at(
                new_data + relocated,
                std::move_if_noexcept(data_[relocated]));
        }
    } catch (...) {
        destroy_range_reverse(new_data, 0, relocated);
        if (appended_constructed) {
            std::destroy_at(new_data + size_);
        }
        AllocatorTraits::deallocate(allocator_, new_data, new_capacity);
        throw;
    }

    commit_reallocation(new_data, size_ + 1, new_capacity);
    return data_[size_ - 1];
}

} // namespace cpp_mastery
