# Type deduction lab

A compact experiment around `auto`, references, `decltype`, and `decltype((expr))`.

## Focus

- top-level `const` removal during `auto` deduction;
- `auto&` and `auto&&` behavior;
- the difference between `decltype(x)` and `decltype((x))`;
- lvalue/xvalue/prvalue effects on deduced reference types.

## Run

```bash
cmake --build --preset dev --target type_deduction_lab
./build/dev/cpp/type_deduction/type_deduction_lab
```
