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

## Phase 2 / Lesson 2 — Error propagation without reclassification

`parse_zero_based_index(std::string_view)` composes the strict positive parser.
It returns `std::expected<int, ParseError>` and converts a one-based decimal index
from `[1, INT_MAX]` into a zero-based index from `[0, INT_MAX - 1]`.

- `"1"` yields `0`; `"0042"` yields `41`; `INT_MAX` yields `INT_MAX - 1`.
- Whitespace, signs, invalid characters, zero, and overflow are rejected by
  the existing parser **without changing the error category**.
- When parsing fails, the function returns `std::unexpected(parsed.error())`.
  Using `return parsed.error();` would not construct this result type, because
  `ParseError` is not implicitly convertible to `int`.
- On success, dereferencing `parsed` is safe because we checked `has_value()`.
  Subtracting one cannot underflow: the positive parser never returns zero.
- This wrapper performs no heap allocation and declares `noexcept`.

Run the focused CTest target with:

```sh
ctest --preset dev -R '^parsing\.zero-based-index$' --output-on-failure
```

The RED stub deliberately always reports `empty_input`, so most new tests
fail. GREEN propagates errors unchanged and transforms only successful values.
There are now 14 project CTest entries, including two parsing suites.

## Phase 2 / Lesson 3 — Error translation with preserved causes

`cpp_mastery::config::read_worker_count(std::string_view)` consumes the strict
positive integer parser from Lesson 1. The configuration domain requires a
worker count in **[1, 64]**, with exactly the same ASCII-digit-only grammar.

| Input | Result | Error origin |
|---|---|---|
| `"12"`, `"00064"` | `12`, `64` | success |
| `""` | `ConfigError{field, ParseError::empty_input}` | parsing |
| `"0"` | `ConfigError{field, ParseError::out_of_range}` | parsing |
| `"12x"` | `ConfigError{field, ParseError::invalid_character}` | parsing |
| `"65"` | `ConfigError{field, ConfigConstraintError::worker_count_above_maximum}` | domain |
| `"99999999999999999999"` | `ConfigError{field, ParseError::out_of_range}` | parsing |

The `ConfigError::cause` field is a `std::variant<ParseError, ConfigConstraintError>`.
It holds **exactly one** of these alternatives; callers can use
`std::get_if<ParseError>(&error.cause)` to inspect the original parsing cause,
or `std::get_if<ConfigConstraintError>(&error.cause)` for a domain limit error.
A variant avoids an inconsistent state such as a domain-limit error with a
spurious parse cause.

`ConfigError::field` is a non-owning `std::string_view` with a deliberate lifetime
contract: this API always returns the view into the **string literal**
`"worker_threads"`. The error never borrows the caller's potentially temporary
input string; a caller can retain the error after discarding the input.

The wrapper performs no dynamic allocation and is `noexcept`. Its first layer
parses and classifies syntax/range errors; its second layer enforces the domain
maximum. It never maps an otherwise valid but excessive worker count to
`ParseError::out_of_range` because that would lose the cause's provenance.

Run the additional CTest entry with:

```sh
ctest --preset dev -R '^config\.worker-count$' --output-on-failure
```

RED deliberately returns `empty_input` for all inputs; GREEN implements the
composition. After Lesson 3, 15 project CTest entries should exist.

## Phase 2 / Lesson 4 — Atomic configuration construction

`read_render_distance(std::string_view)` uses the strict parser and enforces the
independent **[1, 32]** domain limit. Its `ConfigError::field` is always the
static literal `"render_distance"`. `ConfigConstraintError` and `ConfigError`
now live in a shared `ConfigError.hpp` header, so the two validators do not
depend on one another's declarations.

`read_engine_config(workers, distance)` validates **both** fields and returns
`std::expected<EngineConfig, ConfigErrors>`, where `ConfigErrors` is
`std::vector<ConfigError>`. Its contract is:

- On success, return a complete `EngineConfig{worker_threads, render_distance}`.
  The object is constructed only after both validators succeed.
- On failure, return **all** independently detected errors, ordered first by
  `worker_threads`, then by `render_distance`.
- Preserve the original parsing or domain cause, without replacing one with the other.
- Never borrow the input storage in diagnostics. Field views refer to static literals.
- Unlike the individual no-allocation validators, the vector-based builder is
  **not `noexcept`**: collecting errors may throw (for example, `std::bad_alloc`).

| Workers | Distance | Result |
|---|---|---|
| `"8"` | `"24"` | `EngineConfig{8, 24}` |
| `"64"` | `"32"` | `EngineConfig{64, 32}` |
| `"65"` | `"12x"` | worker limit, then invalid character |
| `"0"` | `"33"` | zero out of range, then render limit |
| `"8"` | `"12x"` | only the render-distance parsing error |

RED deliberately uses incorrect placeholders so both new suites fail.
GREEN composes `read_worker_count` and `read_render_distance`, collects
errors in a deterministic order, and only then creates the valid object.
No default or partial `EngineConfig` escapes.

Run the two new suites with:

```sh
ctest --preset gcc16 -R '^config\.(render-distance|engine-config)$' --output-on-failure
```

After this lesson, the complete repository should register **17 CTest suites**.

## Phase 2 / Lesson 5 — Strong exception guarantee and RAII

`runtime::RuntimeState` owns a pair of render buffers and committed runtime settings.
The `ResourceFactory` seam supports deterministic failure injection. The included
`CpuResourceFactory` allocates real CPU-backed byte storage as a lightweight
substitute for GPU resources; it does not call any GPU API.

- Prepare two resources into a local `ResourceSet` owned by `std::unique_ptr`.
- If the second allocation fails, RAII automatically frees the first.
- The original settings and both original resource handles remain unchanged.
- Commit only after all allocations succeed: non-throwing `unique_ptr::swap`
  followed by non-throwing assignment of two integers.
- After swapping, the staging object's destructor releases the old resources.
- The factory is assumed not to mutate the active runtime as an external side
  effect. That limitation matters for real GPU queues and worker pools.

The RED implementation intentionally updates `worker_threads` before preparation.
GREEN moves that assignment into the non-throwing commit.

Run `ctest --preset gcc16 -R '^runtime\.transaction$' --output-on-failure`.
The completed project registers **18** CTest entries.

## Phase 2 / Lesson 6 — Parked worker pools and external side effects

`runtime::WorkerRuntime` stages CPU-backed render resources together with a new
`ParkedWorkerPool`. Each staged `std::jthread` waits at an atomic gate, with a stop callback
that cancels the whole staged pool if construction fails.
The pool constructor waits for each launched thread to park, so the test hook can
reliably simulate failure at the **fifth thread** after four have parked.

- On failed staging, `std::jthread` destruction requests stop, changes the atomic gate to cancelled, wakes the
  atomic wait, and joins every previously started thread.
  The staged render resources are also destroyed through RAII.
- RED publishes the new render state **before** starting the new worker pool;
  a launch failure leaves new settings with the old workers.
- GREEN prepares a complete bundle first and publishes it via a non-throwing
  `std::unique_ptr::swap`. Only **after** publication does `activate()` release
  the parked workers (atomic store and `notify_all`).
- The worker task is a **simulated** atomic counter increment. Real tasks may
  have irreversible external effects and cannot simply be rolled back.
- This is a **single-controller**, non-concurrent-access model: the bundle swap
  does not make concurrent readers safe. A real engine requires synchronized
  publication and explicit worker lifecycle and task-draining policies.
- The launch hook throws before constructing the selected `std::jthread` to
  deterministically emulate an operating-system thread-creation failure.

Run `ctest --preset gcc16 -R '^runtime\.worker-pool$' --output-on-failure`.
After this lesson the project registers **19** CTest entries.

## Phase 2 / Lesson 7 — Immutable snapshots for concurrent readers

`SnapshotRuntime` is a deliberately **separate** single-writer publisher. It
uses `std::atomic<std::shared_ptr<const RuntimeSnapshot>>` for publication and
reader ownership. Readers call `acquire()` and retain the returned handle while
reading settings and render-buffer metadata. The snapshot is immutable after
construction; its CPU-backed resources remain alive until the last reader lets go.

- Resource preparation may throw and is never published partially.
- Publish a complete snapshot with release semantics; acquire with acquire
  semantics. A reader can see the old or new snapshot, never a half-built one.
- If a factory throws or returns a null buffer, the published snapshot remains
  unchanged, and staging is destroyed through RAII.
- The tests use an intentionally blocked second allocation to expose the RED
  publication gap deterministically, without relying on scheduling luck.
- An immutable snapshot prevents data races on the settings and buffer metadata,
  but the atomic `shared_ptr` is **not necessarily lock-free**.
- This component does not activate, stop, or retire worker pools; GPU command
  completion and deferred GPU destruction also remain out of scope.
- Updates are single-writer; multiple concurrent writers would need additional
  serialization or compare-and-exchange logic to prevent lost updates.

RED clears the current snapshot before preparing the replacement; GREEN stages
the replacement first, then publishes it as one atomic operation.

Run `ctest --preset gcc16 -R '^runtime\.snapshot-publication$' --output-on-failure`.
After Lesson 7 the full repository registers **20** CTest suites.
