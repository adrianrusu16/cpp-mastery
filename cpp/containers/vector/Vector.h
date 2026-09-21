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
        if (size_ == capacity_) {
            grow_and_append(value);
            return;
        }

        std::construct_at(
            data_ + size_,
            value
        );

        ++size_;
    }


    void push_back(T &&value) {
        if (size_ == capacity_) {
            grow_and_append(
                std::move(value)
            );

            return;
        }

        std::construct_at(
            data_ + size_,
            std::move(value)
        );

        ++size_;
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


    template<typename U>
    void grow_and_append(U &&value) {
        const std::size_t new_capacity =
                next_capacity();

        if (new_capacity == 0) {
            throw std::length_error{
                "Vector cannot allocate storage"
            };
        }

        T *new_elements =
                AllocatorTraits::allocate(
                    allocator_,
                    new_capacity
                );

        std::size_t relocated = 0;
        bool appended_constructed = false;

        try {
            /*
             * Construct the new element BEFORE relocating
             * existing elements.
             *
             * This makes:
             *
             *     v.push_back(v[0]);
             *
             * safe even when value aliases our old storage.
             */
            std::construct_at(
                new_elements + size_,
                std::forward<U>(value)
            );

            appended_constructed = true;

            /*
             * Now relocate the old objects.
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
            /*
             * Only [0, relocated) contains successfully
             * constructed relocated elements.
             */
            while (relocated > 0) {
                --relocated;

                std::destroy_at(
                    new_elements + relocated
                );
            }

            /*
             * The appended object is at a separate location:
             * new_elements[size_].
             */
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

        /*
         * Everything succeeded.
         *
         * Only now do we destroy/deallocate the old state.
         */
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
    }


    [[no_unique_address]]
    Allocator allocator_{};

    T *data_ = nullptr;
    std::size_t size_ = 0;
    std::size_t capacity_ = 0;
};


#endif
