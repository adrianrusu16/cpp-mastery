#ifndef INTBUFFER_H
#define INTBUFFER_H

#include <cstddef>

class IntBuffer {
public:
    explicit IntBuffer(std::size_t size);

    ~IntBuffer();

    IntBuffer(const IntBuffer& other);

    IntBuffer& operator=(const IntBuffer& other);

    IntBuffer(IntBuffer&& other) noexcept;

    IntBuffer& operator=(IntBuffer&& other) noexcept;

    [[nodiscard]]
    std::size_t size() const noexcept;

    int& operator[](std::size_t index) noexcept;

    const int& operator[](std::size_t index) const noexcept;

private:
    int* data_;
    std::size_t size_;
};

#endif