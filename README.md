# eqstd

Endqueue's small container library: growable arrays and strings for C and
C++, over an allocator the caller chooses. Header-only.

**Scope, in one line: containers, strings and the allocator they take.**
Nothing else: no algorithms (the standard ones work on its spans and
iterators), math, threading or I/O.

- `include/eqstd.h`, the C half (C11; also compiles as C++ and Objective-C):
  `EQ_ALLOCATOR`, `EQ_ARRAY(T)`, `EQ_STRING`, `EQ_RESULT`.
- `include/eqstd.hpp`, the C++ half (C++20), built over the C half:
  `eq::array<T>` (plain data, `EQ_ARRAY(T)`'s layout), `eq::vector<T>` (any
  type), `eq::string` (UTF-8, short strings without allocating).

It includes only the C and C++ standard libraries. Its types are for code
inside a library, never in a public API.

## Rules it keeps

- **Out of memory is a result, never an abort or an exception.** Every call
  that can fail returns an `EQ_RESULT`, and a call that fails leaves its
  container as it was. Nothing throws.
- **The caller's allocator, or the system heap.** An `EQ_ALLOCATOR` is three
  functions (allocate, free, and an optional resize), each told the size and
  alignment, and `user_data`. A null `EQ_ALLOCATOR *` is the system heap.
- **Every macro evaluates its arguments once** (except the Debug-checked
  accessor `eq_array_at`, which evaluates its array twice).
- **No global or static state.** Each container is used by one caller at a
  time; different containers are independent.

## Example

```c
#include <eqstd.h>

typedef EQ_ARRAY(float) FLOAT_ARRAY;

EQ_RESULT collect(FLOAT_ARRAY *out, const EQ_ALLOCATOR *allocator)
{
	EQ_STRING message = {0};
	eq_array_init(out, allocator);
	for (int i = 0; i < 10; i++) {
		float x = (float)i * 0.5f;
		EQ_RESULT r = eq_array_push(out, &x);
		if (r != EQ_SUCCESS)
			return r;                  /* out is as it was before the push */
	}
	out->data[0] = 1.0f;               /* elements are read and written directly */
	if (eq_string_append_format(&message, "%zu values", out->count) == EQ_SUCCESS)
		puts(eq_string_c_str(&message));
	eq_string_free(&message);
	return EQ_SUCCESS;
}
```

```cpp
#include <eqstd.hpp>

eq::vector<eq::string> names(&allocator);   // or nullptr for the system heap
eq::string name;
if (name.assign("Wall 1") != EQ_SUCCESS || names.push(std::move(name)) != EQ_SUCCESS)
	return false;
for (const eq::string &n : names)
	std::puts(n.c_str());
```

## Build and test

Header-only: add `include/` to the include path, or `add_subdirectory(eqstd)`
and link the `eqstd` interface target. The tests:

```sh
cmake --preset eqstd-macos           # or eqstd-linux, eqstd-windows, eqstd-macos-xcode
cmake --build --preset eqstd-macos-debug
ctest --test-dir build-macos -C Debug
```

The `-asan` (AddressSanitizer + UBSan) and `-tsan` presets, on macOS and
Linux, build and test under the sanitizers:
`cmake --preset eqstd-macos-asan && cmake --build --preset eqstd-macos-asan-debug && ctest --preset eqstd-macos-asan`.

## Tests

Nine CTest targets. Every container is checked against `std::vector` /
`std::string` (or a plain C reference, in C) by thousands of random
operations, through a counting allocator that checks every block is freed
with the size and alignment it was made with, and through an allocator that
fails at each call in turn, with every result checked and nothing leaked.
The C half's test also runs compiled as C++ and as Objective-C, the C++
half's as C++20, C++23 and Objective-C++; one test runs containers on eight
threads at once.

## License

[Apache License 2.0](LICENSE).
