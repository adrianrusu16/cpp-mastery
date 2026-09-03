#include "IntBuffer.h"

#include <algorithm>
#include <utility>

IntBuffer::IntBuffer(const std::size_t size)
    : data_{new int[size]},
      size_{size}
{
}

IntBuffer::~IntBuffer()
{
    delete[] data_;
}

IntBuffer::IntBuffer(const IntBuffer& other)
    : data_{new int[other.size_]},
      size_{other.size_}
{
    std::copy_n(
        other.data_,
        other.size_,
        data_
    );
}

IntBuffer& IntBuffer::operator=(const IntBuffer& other)
{
    if (this == &other) {
        return *this;
    }

    auto* new_data =
        new int[other.size_];

    std::copy_n(
        other.data_,
        other.size_,
        new_data
    );

    delete[] data_;

    data_ = new_data;
    size_ = other.size_;

    return *this;
}

IntBuffer::IntBuffer(IntBuffer&& other) noexcept
    : data_{std::exchange(other.data_, nullptr)},
      size_{std::exchange(other.size_, 0)}
{
}

IntBuffer& IntBuffer::operator=(IntBuffer&& other) noexcept
{
    if (this == &other) {
        return *this;
    }

    delete[] data_;

    data_ =
        std::exchange(other.data_, nullptr);

    size_ =
        std::exchange(other.size_, 0);

    return *this;
}

std::size_t IntBuffer::size() const noexcept
{
    return size_;
}

int& IntBuffer::operator[](const std::size_t index) noexcept
{
    return data_[index];
}

const int& IntBuffer::operator[](
    const std::size_t index) const noexcept
{
    return data_[index];
}