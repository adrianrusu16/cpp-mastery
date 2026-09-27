# RAII / Rule of Zero lab

`IntBuffer` is a deliberately small ownership wrapper used to contrast manual Rule-of-Five management with a final Rule-of-Zero implementation backed by `std::vector<int>`.

## Focus

- resource ownership through a standard container;
- copy independence;
- move construction and move assignment;
- self-copy and self-move behavior;
- reusing a moved-from object.

## Layout

```text
include/cpp_mastery/raii/IntBuffer.hpp
src/IntBuffer.cpp
tests/
```

## Tests

The lab uses the shared `cpp_mastery::test` framework. Its eight cases are grouped in the `int-buffer` suite, with assertion diagnostics and CLI filtering supplied by the common runner.

## Run

```bash
cmake --build --preset dev --target raii_lab
ctest --preset dev -R raii

# or directly
./raii_lab --list
./raii_lab --suite int-buffer --verbose
```
