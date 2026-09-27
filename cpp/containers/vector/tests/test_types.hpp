#pragma once

#include <stdexcept>
#include <string>
#include <utility>

namespace cpp_mastery::vector_tests {

struct ThrowOnCopy {
    static inline int live_count = 0;
    static inline int copies_before_throw = -1;

    int value = 0;


    explicit ThrowOnCopy(const int initial_value)
        : value{initial_value} {
        ++live_count;
    }


    ThrowOnCopy(const ThrowOnCopy &other)
        : value{other.value} {
        if (copies_before_throw == 0) {
            throw std::runtime_error{
                "intentional copy failure"
            };
        }

        if (copies_before_throw > 0) {
            --copies_before_throw;
        }

        ++live_count;
    }


    ThrowOnCopy(ThrowOnCopy &&other) noexcept
        : value{other.value} {
        other.value = -1;
        ++live_count;
    }


    ~ThrowOnCopy() {
        --live_count;
    }
};

struct PopBackProbe {
    static inline int live_count = 0;
    static inline int destructor_calls = 0;

    int value;

    explicit PopBackProbe(const int initial_value)
        : value{initial_value} {
        ++live_count;
    }

    PopBackProbe(const PopBackProbe &other)
        : value{other.value} {
        ++live_count;
    }

    PopBackProbe(PopBackProbe &&other) noexcept
        : value{other.value} {
        ++live_count;
    }

    ~PopBackProbe() {
        --live_count;
        ++destructor_calls;
    }
};

struct EmplaceProbe {
    static inline int direct_constructions = 0;
    static inline int copies = 0;
    static inline int moves = 0;

    int id;
    std::string name;

    EmplaceProbe(
        const int initial_id,
        std::string initial_name
    )
        : id{initial_id},
          name{std::move(initial_name)} {
        ++direct_constructions;
    }

    EmplaceProbe(const EmplaceProbe &other)
        : id{other.id},
          name{other.name} {
        ++copies;
    }

    EmplaceProbe(EmplaceProbe &&other) noexcept
        : id{other.id},
          name{std::move(other.name)} {
        ++moves;
    }
};

struct ResizeProbe {
    static inline int live_count = 0;
    static inline int destructor_calls = 0;

    int value;

    ResizeProbe()
        : value{0} {
        ++live_count;
    }

    explicit ResizeProbe(const int initial_value)
        : value{initial_value} {
        ++live_count;
    }

    ResizeProbe(const ResizeProbe &other)
        : value{other.value} {
        ++live_count;
    }

    ResizeProbe(ResizeProbe &&other) noexcept
        : value{other.value} {
        ++live_count;
    }

    ~ResizeProbe() {
        --live_count;
        ++destructor_calls;
    }
};

struct ThrowOnDefault {
    static inline int live_count = 0;
    static inline int destructor_calls = 0;
    static inline int defaults_before_throw = -1;

    int value = 0;

    ThrowOnDefault() {
        if (defaults_before_throw == 0) {
            throw std::runtime_error{
                "intentional default construction failure"
            };
        }

        if (defaults_before_throw > 0) {
            --defaults_before_throw;
        }

        ++live_count;
    }

    explicit ThrowOnDefault(const int initial_value)
        : value{initial_value} {
        ++live_count;
    }

    ThrowOnDefault(const ThrowOnDefault &other)
        : value{other.value} {
        ++live_count;
    }

    ThrowOnDefault(ThrowOnDefault &&other) noexcept
        : value{other.value} {
        ++live_count;
    }

    ~ThrowOnDefault() {
        --live_count;
        ++destructor_calls;
    }
};

struct ResizeRelocationProbe {
    static inline int live_count = 0;
    static inline int destructor_calls = 0;
    static inline int copies = 0;
    static inline int moves = 0;
    static inline int copies_before_throw = -1;

    int value = 0;

    ResizeRelocationProbe() {
        ++live_count;
    }

    explicit ResizeRelocationProbe(const int initial_value)
        : value{initial_value} {
        ++live_count;
    }

    ResizeRelocationProbe(
        const ResizeRelocationProbe &other
    )
        : value{other.value} {
        if (copies_before_throw == 0) {
            throw std::runtime_error{
                "intentional relocation copy failure"
            };
        }

        if (copies_before_throw > 0) {
            --copies_before_throw;
        }

        ++copies;
        ++live_count;
    }

    /*
     * Deliberately NOT noexcept.
     *
     * Because copying is available,
     * move_if_noexcept should prefer copying.
     */
    ResizeRelocationProbe(
        ResizeRelocationProbe &&other
    )
        : value{other.value} {
        ++moves;
        ++live_count;

        other.value = -1;
    }

    ~ResizeRelocationProbe() {
        --live_count;
        ++destructor_calls;
    }
};

struct ResizeFillProbe {
    static inline int live_count = 0;
    static inline int destructor_calls = 0;
    static inline int copies = 0;
    static inline int copies_before_throw = -1;

    int value = 0;

    ResizeFillProbe() {
        ++live_count;
    }

    explicit ResizeFillProbe(const int initial_value)
        : value{initial_value} {
        ++live_count;
    }

    ResizeFillProbe(const ResizeFillProbe &other)
        : value{other.value} {
        if (copies_before_throw == 0) {
            throw std::runtime_error{
                "intentional fill copy failure"
            };
        }

        if (copies_before_throw > 0) {
            --copies_before_throw;
        }

        ++copies;
        ++live_count;
    }

    ResizeFillProbe(ResizeFillProbe &&other) noexcept
        : value{other.value} {
        other.value = -1;
        ++live_count;
    }

    ~ResizeFillProbe() {
        --live_count;
        ++destructor_calls;
    }
};

} // namespace cpp_mastery::vector_tests
