#ifndef VECTOR_H
#define VECTOR_H

#include <cstddef>
#include <memory>
#include <stdexcept>
#include <utility>


template<typename T>
class Vector {
private:
    using Allocator = std::allocator<T>;
    using AllocatorTraits = std::allocator_traits<Allocator>;

public:
    Vector() noexcept = default;


    ~Vector() {
        clear();

        if (data_ != nullptr) {
            AllocatorTraits::deallocate(
                allocator_,
                data_,
                capacity_
            );
        }
    }


    Vector(const Vector &other)
        : allocator_{
            AllocatorTraits::select_on_container_copy_construction(
                other.allocator_
            )
        } {
        if (other.size_ == 0) {
            return;
        }

        data_ = AllocatorTraits::allocate(
            allocator_,
            other.size_
        );

        capacity_ = other.size_;

        std::size_t constructed = 0;

        try {
            for (; constructed < other.size_; ++constructed) {
                std::construct_at(
                    data_ + constructed,
                    other.data_[constructed]
                );
            }
        } catch (...) {
            while (constructed > 0) {
                --constructed;

                std::destroy_at(
                    data_ + constructed
                );
            }

            AllocatorTraits::deallocate(
                allocator_,
                data_,
                capacity_
            );

            data_ = nullptr;
            capacity_ = 0;

            throw;
        }

        size_ = other.size_;
    }

    Vector &operator=(const Vector &other) {
        if (this == &other) {
            return *this;
        }

        Vector temporary{other};
        swap(temporary);

        return *this;
    }

    Vector(Vector &&other) noexcept
        : allocator_{std::move(other.allocator_)},
          data_{std::exchange(other.data_, nullptr)},
          size_{std::exchange(other.size_, 0)},
          capacity_{std::exchange(other.capacity_, 0)} {
    }

    Vector &operator=(Vector &&other) noexcept {
        if (this == &other) {
            return *this;
        }

        clear();

        if (data_ != nullptr) {
            AllocatorTraits::deallocate(
                allocator_,
                data_,
                capacity_
            );
        }

        allocator_ = std::move(other.allocator_);

        data_ = std::exchange(
            other.data_,
            nullptr
        );

        size_ = std::exchange(
            other.size_,
            0
        );

        capacity_ = std::exchange(
            other.capacity_,
            0
        );

        return *this;
    }


    [[nodiscard]]
    std::size_t size() const noexcept {
        return size_;
    }


    [[nodiscard]]
    std::size_t capacity() const noexcept {
        return capacity_;
    }


    [[nodiscard]]
    bool empty() const noexcept {
        return size_ == 0;
    }


    T &operator[](const std::size_t index) noexcept {
        return data_[index];
    }


    const T &operator[](const std::size_t index) const noexcept {
        return data_[index];
    }


    void clear() noexcept {
        for (std::size_t i = size_; i > 0; --i) {
            std::destroy_at(
                data_ + (i - 1)
            );
        }

        size_ = 0;
    }


    void reserve(const std::size_t new_capacity) {
        if (new_capacity <= capacity_) {
            return;
        }

        if (new_capacity >
            AllocatorTraits::max_size(allocator_)) {
            throw std::length_error{
                "Vector capacity exceeds max_size"
            };
        }

        T *new_elements =
                AllocatorTraits::allocate(
                    allocator_,
                    new_capacity
                );

        std::size_t constructed = 0;

        try {
            for (; constructed < size_; ++constructed) {
                std::construct_at(
                    new_elements + constructed,
                    std::move_if_noexcept(
                        data_[constructed]
                    )
                );
            }
        } catch (...) {
            while (constructed > 0) {
                --constructed;

                std::destroy_at(
                    new_elements + constructed
                );
            }

            AllocatorTraits::deallocate(
                allocator_,
                new_elements,
                new_capacity
            );

            throw;
        }

        destroy_old_elements();

        if (data_ != nullptr) {
            AllocatorTraits::deallocate(
                allocator_,
                data_,
                capacity_
            );
        }

        data_ = new_elements;
        capacity_ = new_capacity;
    }


    void push_back(const T &value) {
        emplace_back(value);
    }

    void push_back(T &&value) {
        emplace_back(std::move(value));
    }

    template<typename... Args>
    T &emplace_back(Args &&... args) {
        if (size_ == capacity_) {
            return grow_and_emplace(
                std::forward<Args>(args)...
            );
        }

        T *new_element = data_ + size_;

        std::construct_at(
            new_element,
            std::forward<Args>(args)...
        );

        ++size_;

        return *new_element;
    }

    T &at(const std::size_t index) {
        if (index >= size_) {
            throw std::out_of_range{
                "Vector::at index out of range"
            };
        }

        return data_[index];
    }


    const T &at(const std::size_t index) const {
        if (index >= size_) {
            throw std::out_of_range{
                "Vector::at index out of range"
            };
        }

        return data_[index];
    }

private:
    void swap(Vector &other) noexcept {
        using std::swap;

        swap(data_, other.data_);
        swap(size_, other.size_);
        swap(capacity_, other.capacity_);
    }

    [[nodiscard]]
    std::size_t next_capacity() const {
        const std::size_t max_capacity =
                AllocatorTraits::max_size(allocator_);

        if (capacity_ == 0) {
            if (max_capacity < 4) {
                return max_capacity;
            }

            return 4;
        }

        if (capacity_ >= max_capacity) {
            throw std::length_error{
                "Vector has reached max_size"
            };
        }

        /*
         * Avoid overflow and, if doubling would exceed
         * max_size, grow directly to max_size.
         */
        if (capacity_ > max_capacity / 2) {
            return max_capacity;
        }

        return capacity_ * 2;
    }


    void destroy_old_elements() noexcept {
        for (std::size_t i = size_; i > 0; --i) {
            std::destroy_at(
                data_ + (i - 1)
            );
        }
    }

    template<typename... Args>
    T &grow_and_emplace(Args &&... args) {
        const std::size_t new_capacity =
                next_capacity();

        T *new_elements =
                AllocatorTraits::allocate(
                    allocator_,
                    new_capacity
                );

        std::size_t relocated = 0;
        bool appended_constructed = false;

        try {
            /*
             * Construct the NEW element first.
             *
             * This matters if one of args aliases something
             * inside our existing Vector.
             */
            std::construct_at(
                new_elements + size_,
                std::forward<Args>(args)...
            );

            appended_constructed = true;

            /*
             * Relocate the existing elements afterward.
             */
            for (; relocated < size_; ++relocated) {
                std::construct_at(
                    new_elements + relocated,
                    std::move_if_noexcept(
                        data_[relocated]
                    )
                );
            }
        } catch (...) {
            while (relocated > 0) {
                --relocated;

                std::destroy_at(
                    new_elements + relocated
                );
            }

            if (appended_constructed) {
                std::destroy_at(
                    new_elements + size_
                );
            }

            AllocatorTraits::deallocate(
                allocator_,
                new_elements,
                new_capacity
            );

            throw;
        }

        destroy_old_elements();

        if (data_ != nullptr) {
            AllocatorTraits::deallocate(
                allocator_,
                data_,
                capacity_
            );
        }

        data_ = new_elements;
        capacity_ = new_capacity;

        ++size_;

        return data_[size_ - 1];
    }


    [[no_unique_address]]
    Allocator allocator_{};

    T *data_ = nullptr;
    std::size_t size_ = 0;
    std::size_t capacity_ = 0;
};


#endif
