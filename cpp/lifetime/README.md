# Object lifetime lab

A small tracing program that makes construction, copy/move operations, assignment, destruction, scope exit, and `std::vector` reallocation visible in the console.

## Focus

- deterministic destruction at scope exit;
- copy vs move construction;
- copy vs move assignment;
- moved-from state observation;
- how `std::vector` relocation invokes element constructors/destructors.

## Run

```bash
cmake --build --preset dev --target lifetime_lab
./build/dev/cpp/lifetime/lifetime_lab
```

This is a demonstration executable rather than an assertion-based test suite; its output is the experiment.
