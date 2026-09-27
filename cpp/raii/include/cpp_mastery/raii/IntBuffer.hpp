#pragma once

#include <cstddef>
#include <vector>

namespace cpp_mastery {

class IntBuffer {
public:
    explicit IntBuffer(std::size_t size);

    [[nodiscard]] std::size_t size() const noexcept;

    int& operator[](std::size_t index) noexcept;
    const int& operator[](std::size_t index) const noexcept;

private:
    std::vector<int> data_;
};

} // namespace cpp_mastery
