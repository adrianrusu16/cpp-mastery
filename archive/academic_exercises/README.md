# Archived academic exercises

This directory preserves earlier university/exam-oriented exercises that predate the structured learning labs in the rest of the repository.

They are intentionally excluded from the default build and CI because some exercises contain partial or assignment-specific implementations. To build them explicitly:

```bash
cmake -S . -B build/archive -DCPP_MASTERY_BUILD_ARCHIVE=ON
cmake --build build/archive --target academic_exercises
```

The active portfolio-facing labs live under `c/` and `cpp/` and are the code paths covered by CI.
