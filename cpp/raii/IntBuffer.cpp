#include "IntBuffer.h"

IntBuffer::IntBuffer(const std::size_t size)
    : data_(size)
{
}

std::size_t IntBuffer::size() const noexcept
{
    return data_.size();
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