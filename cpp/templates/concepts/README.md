# Concepts and constraints lab

A small C++ concepts lab showing both standard concepts and a custom `requires` expression.

## Focus

- `std::integral`;
- custom `Addable` constraints;
- constrained overload diagnostics;
- compile-time validation with `static_assert`.

`examples/unconstrained_failure.cpp` is intentionally excluded from CMake because it is supposed to fail compilation. It exists to compare the diagnostic from an unconstrained template body with the cleaner failure produced by a concept.

## Run

```bash
cmake --build --preset dev --target concepts_lab
./build/dev/cpp/templates/concepts/concepts_lab
```
