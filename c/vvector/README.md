# C `vvector` lab

A generic dynamic array written in C17 using `void*`, byte-wise copies, explicit error codes, and manual capacity management.

## Focus

- generic storage through `elem_size`;
- `malloc` / `realloc` / `free` ownership;
- overflow-aware capacity growth;
- `memcpy` / `memmove` and byte-address calculations;
- alias-safe insertion when the source element already lives inside the vector;
- small-buffer temporary copies with a heap fallback for large elements.

## Layout

```text
include/vvector.h       public C API
src/vvector.c           implementation
tests/basic_tests.c     core operations
tests/aliasing_tests.c  self-reference and large-element cases
```

`vvector_push_back` and `vvector_insert_at` snapshot the incoming element before a possible `realloc`. That prevents a pointer into the old allocation from becoming dangling halfway through the operation.

## Run

```bash
cmake --preset dev
cmake --build --preset dev --target vvector_tests
ctest --preset dev -R vvector
```

The suite is also run under ASan/UBSan by the sanitizer preset and CI job.
