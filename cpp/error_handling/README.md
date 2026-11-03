# Phase 2 / Lesson 1 — Strict integer parsing with `std::expected`

## Contract

`parse_positive_int(std::string_view)` returns `std::expected<int, ParseError>`.
It accepts **ASCII decimal digits only**, with no whitespace or sign. Leading
zeros are allowed. The numeric value must be between 1 and `INT_MAX` inclusive.

| Input | Result |
|---|---|
| `"42"` | `42` |
| `"0042"` | `42` |
| `""` | `empty_input` |
| `" 42 "` | `invalid_character` |
| `"-1"` / `"+1"` | `invalid_character` |
| `"12x"` | `invalid_character` |
| `"0"` | `out_of_range` |
| Integer overflow | `out_of_range` |

If the input contains **any** non-digit, `invalid_character` takes precedence
over numeric overflow. This makes error classification deterministic regardless
of how many digits precede the invalid character.

## Rationale

- The parser performs no trimming or allocations, and does not throw.
- `std::from_chars` converts the digits without locale dependence.
- An explicit digit-validation pass gives precise `invalid_character` precedence.
  This second pass has a cost; do not infer performance without benchmarking.
- Failure is returned as a value. Accessing `.value()` on a failed
  `std::expected` would throw `std::bad_expected_access<ParseError>`.

## Run

```sh
cmake --preset dev
cmake --build --preset dev
ctest --preset dev -R '^parsing\.positive-int$' --output-on-failure
./build/dev/cpp/error_handling/cpp_mastery_parsing_tests --list
./build/dev/cpp/error_handling/cpp_mastery_parsing_tests
```

RED intentionally returns `empty_input` for every input. Observe the failures,
then apply GREEN and verify all 13 cases pass. The repository should register
13 CTest entries after this lesson, up from 12.
