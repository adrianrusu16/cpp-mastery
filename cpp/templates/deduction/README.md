# Template deduction lab

A focused comparison of template argument deduction through by-value, lvalue-reference, const-reference, and forwarding-reference parameters.

## Focus

- when top-level `const` is preserved or removed;
- how `T&` deduces for const and non-const lvalues;
- how forwarding references encode the caller's value category.

## Run

```bash
cmake --build --preset dev --target template_deduction_lab
./build/dev/cpp/templates/deduction/template_deduction_lab
```
