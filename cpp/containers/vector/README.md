# Allocator-backed `Vector<T>` lab

The largest lab in the repository: a custom contiguous container built to study the mechanics normally hidden behind `std::vector`.

It is a learning implementation, not a standard-library replacement.

## What it exercises

- allocator-backed raw storage;
- explicit object lifetime with `std::construct_at` / `std::destroy_at`;
- Rule of Five behavior;
- copy-and-swap assignment;
- geometric growth for append operations;
- alias-safe `push_back` / `emplace_back`;
- checked and unchecked element access;
- pointer iterators and ranges integration;
- both `resize` overloads;
- exception rollback during partial construction and relocation;
- `std::move_if_noexcept` as an exception-guarantee tool.

## Structure

```text
include/cpp_mastery/vector/
├── Vector.hpp          declarations and public API
└── Vector.tpp          template definitions
examples/
└── vector_demo.cpp     aliasing, ranges, and alignment demo
tests/
├── copy_tests.cpp
├── move_tests.cpp
├── modifier_tests.cpp
├── access_tests.cpp
├── iteration_tests.cpp
├── resize_basic_tests.cpp
├── resize_exception_tests.cpp
├── resize_fill_tests.cpp
└── resize_fill_exception_tests.cpp
```

The old 2,800+ line lab driver was intentionally split by behavior. Tests now return `void` and use the repository's [`cpp_mastery::test`](../../../support/cpp/README.md) assertions. Each translation unit exposes a `TestSuite` factory, while `tests/main.cpp` only composes suites and delegates execution to the shared runner.


## Test organization

The Vector executable is divided into named suites:

```text
copy
move
modifiers
access
iteration
resize
resize-exceptions
resize-fill
resize-fill-exceptions
```

A typical test is intentionally declarative:

```cpp
void resize_grows_in_place()
{
    Vector<int> values;
    values.reserve(8);
    values.push_back(10);
    values.push_back(20);

    auto* old_data = values.data();
    values.resize(5);

    REQUIRE_EQ(values.size(), 5U);
    CHECK_EQ(values.data(), old_data);
    CHECK_EQ(values[2], 0);
}
```

The runner supports `--list`, `--suite`, `--test`, and `--verbose`. CMake registers every suite as its own CTest test, so `ctest -L vector` runs the complete Vector test surface while preserving subsystem-level failures.

## Why a `.tpp` file?

Template definitions generally need to be visible at the point where a template is instantiated. Moving the implementation into a normal `.cpp` would therefore break arbitrary `Vector<T>` instantiations unless explicit instantiations were maintained.

`Vector.hpp` includes `Vector.tpp` at the end:

```text
consumer -> Vector.hpp -> Vector.tpp
```

This keeps the interface easy to scan while retaining header-visible template definitions. The `.tpp` is not compiled on its own.

## Refactoring inside `Vector<T>`

Repeated lifetime/reallocation code is centralized around a small set of helpers:

- `destroy_range_reverse` destroys a half-open range in reverse order;
- `destroy_elements` and `deallocate_storage` separate object lifetime from allocation lifetime;
- `commit_reallocation` performs the commit phase only after a replacement allocation is fully ready;
- `grow_to` shares in-place `resize` growth logic;
- `reallocate_and_grow` shares the transactional reallocation path used by both `resize` overloads.

The important alias-sensitive ordering is preserved. For calls such as:

```cpp
v.push_back(v[0]);
v.resize(10, v[0]);
```

the new element/tail is constructed before old elements are relocated. A reference into the old allocation therefore remains valid until it is no longer needed.

## Exception guarantees

The tests inject failures in:

- copy construction;
- default construction;
- fill construction;
- relocation.

When a replacement allocation can be built by copying or non-throwing moves, failed operations clean up partially constructed objects and leave the original container unchanged.

As with `std::vector`-style designs, a move-only type whose move constructor can throw may prevent a full strong guarantee during relocation because an earlier source element may already have been moved from before a later move throws.

## Ranges

The lab statically verifies that `Vector<int>` models:

```text
std::ranges::range
std::ranges::sized_range
std::ranges::common_range
std::ranges::random_access_range
std::ranges::contiguous_range
```

The raw-pointer iterator model is sufficient because the storage is contiguous.

## Run

```bash
cmake --preset dev
cmake --build --preset dev --target containers_vector_tests containers_vector_lab
ctest --preset dev -R vector
```

For memory diagnostics:

```bash
cmake --preset sanitize
cmake --build --preset sanitize --target containers_vector_tests
ctest --preset sanitize -R vector
```
