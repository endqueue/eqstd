# eqstd

Endqueue's header-only library of containers and strings, in the eqdev
workspace. Follows every rule in the workspace `CLAUDE.md` that applies to a
library (no global state, no automatic commits, `.git/COMMIT_DRAFT`).
`TODO.md` is its spec, decided with the user on 2026-10-28, and what is left.

## The one rule everything else follows

**Scope: containers, strings and the allocator they take. Nothing else.**
No algorithms (the standard ones work on its spans and iterators), math,
threading or I/O. A proposal outside that line is a no.

- **It belongs to no layer.** Any C or C++ code in the workspace may include
  it, and it includes nothing of ours: only the C and C++ standard
  libraries. Like glm.
- **Never in a public API.** No function of SpudLib, ApricotFields,
  trellislib or Latran takes or returns one of its types, so no library's
  ABI depends on its layout.
- **eqstd doesn't know who calls it.** Nothing here - code, comments, headers,
  docs - names a caller. Write "the caller".

## Structure

- `include/eqstd.h`: the C half. C11, and compiles unchanged as C++ and
  Objective-C: casts go through `EQ_INTERNAL_CAST`, null through
  `EQ_INTERNAL_NULL`. The interface comes first, declared and documented;
  the implementation follows `/* ==== Implementation ==== */`, all `static
  inline`.
- `include/eqstd.hpp`: the C++ half, C++20, **over the C half, not beside
  it**: `eq::array` calls the C half's `eq_internal_array_*_at` functions on
  its own fields, and `eq::string` holds an `EQ_STRING` whose caller's
  buffer is its short-string buffer. `eq::vector` alone has its own growth,
  because its elements are moved by their constructors, not by bytes; it
  still allocates through the same `eq_allocate` / `eq_free`.
- `tests/`: black-box tests only (see "Tests").

## Rules the code keeps

- **Out of memory is a result.** Every call that can fail returns an
  `EQ_RESULT`; nothing aborts on a failed allocation and nothing throws. A
  call that fails leaves its container exactly as it was: check every
  argument, then grow, then change anything.
- **Results are append-only.** Never renumber an `EQ_RESULT`; add new ones
  at the end, and give `eq_result_str()` their names.
- **Every macro evaluates each argument once**, `eq_array_at` excepted (its
  array twice, when checking; documented). Element sizes come from `sizeof`,
  which evaluates nothing; a pointer of the wrong element type is caught by
  `EQ_INTERNAL_ITEMS`.
- **An array is never read through a pointer to another struct type.** The C
  functions copy `EQ_ARRAY(T)` in and out of an `EQ_INTERNAL_ARRAY` with
  `memcpy`; the C++ members build one from their fields.
- **Free with the size and alignment of the allocation**, always: blocks are
  `capacity * sizeof(T)` at `EQ_ARRAY_ALIGNMENT(sizeof(T))` for arrays,
  `capacity + 1` at 1 for strings, `capacity * sizeof(T)` at `alignof(T)` for
  `eq::vector`. The array alignment is a function of the size alone, so C and
  C++ code sharing an array agree on it.
- **A source may be the container's own elements.** Growing can move them, so
  push, append and insert find them again by offset after growing (the
  random tests exercise this); `eq::vector` makes the new element before it
  moves the old ones.
- **No struct's layout depends on a build setting.** `EQ_DEBUG` (from
  `_DEBUG`) switches the index checks only.
- **No writable static data, no globals, no threads.** Each object is used by
  one caller at a time.
- Argument checks one `if` per condition, in parameter order, as SpudLib's
  and ApricotFields' are.
- The headers build clean under `-Wall -Wextra -Wpedantic -Wshadow
  -Wconversion -Wsign-conversion -Wcast-qual -Wold-style-cast
  -Wzero-as-null-pointer-constant` and the rest of the tests' list, as
  errors; a consumer's strict flags must never see a warning from them.

## Tests

Contract first: a new call gets its declaration and doc, then a black-box
test, then the code, and a test is never weakened to pass. Each container is
checked by random operations against `std::vector` / `std::string` (a plain
reference array in C), through `counting` allocators that check each free
against its allocation and fail at each call in turn (every result checked,
the container unchanged by a failure, nothing leaked). The C test is also
compiled as C++ and Objective-C, the C++ test as C++20, C++23 and
Objective-C++. Run Debug, Release, `-asan` and `-tsan` after every change.

What has run: macOS (Apple clang 21, arm64), 2026-10-28: Debug, Release and
RelWithDebInfo with Ninja Multi-Config, Debug with Xcode, and the `-asan` and
`-tsan` presets, all nine tests, no sanitizer report. Not yet run on Linux
(GCC) or Windows (MSVC).

## Version

Its own semantic version, written once in `eqstd.h` (`EQSTD_VERSION_*`) and
read by `CMakeLists.txt`. 0.1.0; before 1.0 a minor version may break
callers.
