# Value categories and forwarding lab

This lab makes lvalue/rvalue behavior observable through overload selection and payload copy/move construction.

## Focus

- named rvalue references are lvalue expressions;
- `std::move` produces an xvalue but performs no move by itself;
- forwarding references and reference collapsing;
- `std::forward<T>` preserving the caller's value category;
- why forwarding a `const` rvalue often still leads to copying.

## Run

```bash
cmake --build --preset dev --target value_categories_lab
./build/dev/cpp/value_categories/value_categories_lab
```
