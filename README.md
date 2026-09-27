<div align="center">

# 🧱 C++ Mastery

### Modern C++ systems learning lab — lifetime, ownership, allocators and generic programming.

![C++26](https://img.shields.io/badge/C%2B%2B-26-00599C?style=flat-square&logo=cplusplus&logoColor=white)
![C17](https://img.shields.io/badge/C-17-A8B9CC?style=flat-square&logo=c&logoColor=white)
![CMake](https://img.shields.io/badge/CMake-3.31+-064F8C?style=flat-square&logo=cmake&logoColor=white)
![Sanitizers](https://img.shields.io/badge/ASan_%C2%B7_UBSan-selected_labs-555?style=flat-square)

[Case study](https://adrianrusu.dev/projects/cpp-mastery/) ·
[Vector lab](cpp/containers/vector/) ·
[Lifetime lab](cpp/lifetime/) ·
[RAII lab](cpp/raii/)

</div>

---

**C++ Mastery** is a hands-on C and modern C++ repository for building a deeper understanding of how objects, storage and generic code actually behave.

It is intentionally a **learning lab**, not an attempt to replace the C++ standard library.

> Build the abstraction. Trace the lifetime. Break the assumption. Verify the behavior.

---

## 🧭 What is explored

| Area | Examples in the repository |
|---|---|
| 🧬 **Object lifetime** | construction/destruction tracing, copy/move behavior, scope and reallocation |
| 🧰 **RAII** | ownership wrappers and deterministic cleanup |
| ↔️ **Value categories** | lvalues/rvalues, move semantics and `std::forward` |
| 🧠 **Type deduction** | deduction rules and generic call behavior |
| 🧩 **Templates** | class templates, parameter packs, concepts and fold expressions |
| 📦 **Containers** | a custom allocator-backed `Vector<T>` |
| 💥 **Exception safety** | partial-construction cleanup and strong-guarantee experiments |
| 🧱 **Layout/alignment** | `alignas(64)` and address/layout experiments |
| 🔬 **Sanitizers** | ASan/UBSan enabled on selected C/C++ labs |
| 🛠️ **Build system** | CMake with C17 and C++26 targets |

---

## ⭐ The `Vector<T>` lab

The most substantial container exercise is:

```text
cpp/containers/vector/Vector.h
```

It deliberately works below the abstraction level of `std::vector` to practice the mechanics behind a container.

Current implementation includes:

- `std::allocator<T>` / `std::allocator_traits`;
- explicit allocation/deallocation;
- `std::construct_at` / `std::destroy_at`;
- copy construction with partial-construction cleanup;
- copy assignment through copy-and-swap;
- O(1)-style ownership transfer in move construction/assignment;
- `reserve`;
- `push_back`;
- variadic `emplace_back`;
- `std::move_if_noexcept` during relocation;
- bounds-checked `at`;
- overflow-aware geometric capacity growth.

### Alias-sensitive growth

When a new element aliases storage already owned by the vector, growth order matters.

The implementation constructs the **new element first**, before relocating the old allocation:

```text
old vector allocation
       │
       ├── argument may alias v[i]
       │
       ↓
allocate new storage
       ↓
construct appended element
       ↓
relocate old elements
       ↓
destroy old allocation
```

That ordering is exercised by self-copy/self-move reallocation cases in the lab.

---

## 💥 Exception-safety experiments

The vector lab includes a `ThrowOnCopy` probe that can fail during copy construction.

The tests/checks verify properties such as:

- partially constructed objects are destroyed;
- no extra live objects leak from the failed copy;
- the source remains unchanged;
- failed copy assignment leaves the destination unchanged.

This makes exception safety observable rather than only theoretical.

---

## 🧠 Value categories & forwarding

The value-category lab contrasts forwarding code such as:

```cpp
consume(value);
```

with:

```cpp
consume(std::forward<T>(value));
```

and uses overloads / payload construction to make the consequences visible.

The goal is not to memorize reference-collapsing tables in isolation, but to observe which overload and construction path actually occurs.

---

## 🧩 Concepts and folds

The template labs include:

- custom concepts with `requires`;
- standard concepts such as `std::integral`;
- constrained function templates;
- variadic parameter packs;
- left/right fold expressions.

These are small, isolated experiments designed to make language behavior inspectable.

---

## 🧱 Layout & alignment

The container lab includes an explicitly aligned type:

```cpp
struct alignas(64) CacheLineData {
    int values[16]{};
};
```

and prints object addresses / offsets to make alignment and contiguous layout concrete.

This is an experiment in object layout — not a performance benchmark claim.

---

## 🗂️ Repository map

```text
cpp-mastery/
├── c/
│   └── vvector/               C dynamic-vector exercise
├── cpp/
│   ├── containers/
│   │   └── vector/            allocator-backed Vector<T>
│   ├── lifetime/              construction/copy/move/destruction tracing
│   ├── raii/                  RAII exercises
│   ├── templates/
│   │   ├── box/
│   │   ├── concepts/
│   │   ├── deduction/
│   │   └── folds/
│   ├── type_deduction/
│   ├── value_categories/
│   └── exams/                 older/exam-oriented exercises
├── CMakeLists.txt
└── main.cpp
```

---

## 🚀 Build

Requirements are driven by the root CMake configuration:

```text
CMake 3.31+
C17-capable compiler
C++26-capable compiler for the enabled C++ targets
```

Typical configure/build:

```bash
cmake -S . -B build
cmake --build build
```

Because C++26 support differs between compiler versions, use a toolchain that supports the language features exercised by the current labs.

---

## 🧪 Current executable labs

The root `CMakeLists.txt` currently defines targets including:

```text
cpp_mastery
vvector_lab
lifetime_lab
exams
raii_lab
value_categories_lab
template_deduction_lab
class_template_box_lab
concepts_lab
folds_lab
type_deduction_lab
containers_vector_lab
```

Selected GNU/Clang targets enable strict warnings:

```text
-Wall
-Wextra
-Wpedantic
-Wconversion
-Wshadow
```

`vvector_lab` and `raii_lab` currently also enable AddressSanitizer and UndefinedBehaviorSanitizer for GNU/Clang builds.

---

## ⚙️ CI direction

The repository uses **CMake** as its canonical build system, so the relevant GitHub Actions starter is:

> **CMake based, multi-platform projects**

Not MSBuild, and not a separate Make-based workflow.

The workflow should be adapted rather than accepted unchanged:

```text
Linux / GCC       required
Linux / Clang     required
Windows / MSVC    add once the current C++26 surface is clean
macOS             informational/canary if AppleClang support lags
```

A useful next step is to register deterministic labs with CTest so CI can move from "build every target" to "build + execute the appropriate verification targets".

---

## 🎯 Why this repository exists

My day-to-day work is Android/AAOS. This repository is deliberately about moving closer to the language/runtime mechanics underneath higher-level application code:

- ownership;
- memory;
- object lifetime;
- exception guarantees;
- generic programming;
- data layout;
- toolchain behavior.

It is a place to make those concepts executable.

---

<div align="center">

[Explore the C++ Mastery case study →](https://adrianrusu.dev/projects/cpp-mastery/)

</div>
