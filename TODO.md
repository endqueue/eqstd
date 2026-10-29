# eqstd TODO

The spec for `eqstd`, Endqueue's small container library, decided
2026-10-28 with the user (in place of card A25's allocator plan), and what
is left. Moved here from the workspace's `EQSTD_TODO.md` when the
repository was made.

**Status (2026-10-28):** built, 0.1.0. Both headers, the tests and the
presets are written, and every test passes on macOS in Debug, Release and
RelWithDebInfo, with Xcode, and under the `-asan` and `-tsan` presets, with
no sanitizer report. Pushed as `endqueue/eqstd` (b302f96). spudlib uses it
since 2026-10-29 ("Who uses it" below). Left: the Linux and Windows runs,
the rest of the workspace wiring ("The repository and the workspace"
below), and the hash map if it is wanted. "Decided while building it" at the end lists the choices the
spec left open.

## What it is

- **A header-only library of containers and strings**, belonging to no
  layer: any C or C++ code in the workspace may include it, and it includes
  nothing of ours (only the C and C++ standard libraries). Like glm.
- **Scope, in one line:** containers, strings and the allocator they take.
  Nothing else: no algorithms (the standard ones work on its spans and
  iterators), math, threading or I/O. A proposal outside that line is a no.
- **Never in a public API.** Its types are for code inside a library; no
  function of SpudLib, ApricotFields, trellislib or Latran takes or returns
  one, so no library's ABI depends on eqstd's layout.
- Apache 2.0, as spudlib and ApricotFields, so both may use it.

## The C half, `eqstd.h`

Prefix `eq_` / `EQ_`. C11, also compiled as C++ and Objective-C.

- **`EQ_ALLOCATOR`:** allocate, resize and free through a caller's
  function pointers and context, each told the size and alignment (as
  `SPUD_ALLOCATOR` is, so spudlib's can be handed over as it is or by a
  trivial adapter). A null `EQ_ALLOCATOR *` means the system heap.
- **`EQ_ARRAY(T)`:** declares a typed struct `{ T *data; size_t count;
  size_t capacity; const EQ_ALLOCATOR *allocator; }`. Elements are read and
  written as `a.data[i]`. Growing, inserting, removing, reserving and
  clearing are inline functions (through macros that pass the element's
  size and alignment) that **return an `EQ_RESULT`, never abort**: out of
  memory is a result. Each macro evaluates its arguments once.
- **`EQ_STRING`:** a string builder: append, append formatted
  (`printf`-style), clear, take the result as a NUL-terminated UTF-8
  pointer; errors as results.
- Debug builds check indices through an accessor macro; Release reads
  `data` directly.

## The C++ half, `eqstd.hpp`

Namespace `eq::`. Over the C half, not beside it.

- **`eq::array<T>`:** a thin wrapper over `EQ_ARRAY(T)` with the same
  layout, so C and C++ code share one array (a C front end and its C++
  backend, say). Plain-data elements only (a static_assert). Add, insert,
  remove, resize, reserve, clear, index, iterate, and a `std::span` view.
  Growth returns `bool` / `EQ_RESULT`; nothing throws.
- **`eq::vector<T>`:** for types with constructors and destructors, over
  the same allocator; growth that fails returns a result (no exception).
- **`eq::string`:** owns UTF-8 text, with a short-string buffer so short
  names don't allocate; `std::string_view` for reading; built on
  `EQ_STRING`'s growth.
- Later, if wanted: the open-addressing hash map (as `apri_hash_index`).

## Tests

- Every container against `std::vector` / `std::string` as the reference,
  by random operations (ApricotFields' brute-force style), in C and C++.
- A counting allocator: every allocation freed, with the size and
  alignment it was made with.
- Out of memory: an allocator that fails at the nth call, every result
  checked, nothing leaked.
- Built and run under the `-asan` and `-tsan` presets, on macOS, Linux
  and Windows. **Done on macOS** (2026-10-28); Linux (GCC) and Windows
  (MSVC, no sanitizers) not run yet.

## The repository and the workspace

- `eqstd/` beside the other repositories: `include/eqstd.h`,
  `include/eqstd.hpp`, a `CMakeLists.txt` with an interface target
  `eqstd`, the tests, a README with the scope line, `LICENSE`. **Done.**
- `projects.conf` names it, so `sync.sh` and `status.sh` cover it. The
  GitHub repository (`endqueue/eqstd`) is created and pushed by the user.
- trellis and Latran pin it in `deps.lock`; spudlib and ApricotFields find
  it at `../eqstd`, with a pinned fetch fallback, as their `.deps` are.
  **spudlib: done** (2026-10-29): `spud_add_workspace_or_fetch` in its
  `CMakeLists.txt` adds `../eqstd` when it is there, else fetches b302f96,
  and links `eqstd` PRIVATE. trellis, latran and ApricotFields build it
  through `../spudlib` today (checked on macOS); ApricotFields' own use and
  the `deps.lock` entries are not done.
- `check.sh`: eqstd may be included by any layer and includes none.
- Its own semantic version (root `CLAUDE.md`, "Versions and releases").

## Who uses it, once it exists

- spudlib: its C backends' hand-grown arrays (`SPUD_RESIZE_ARRAY` with a
  capacity beside each) and the D3D12 backend's leftover `std::vector`s
  and `std::string`s, through `EQ_ARRAY`, `eq::array` and `eq::string`,
  with the instance's `SPUD_ALLOCATOR` handed over. **Started**
  (2026-10-29): SpudNet's response headers and wait set are `EQ_ARRAY`s,
  on `spud_eq_allocator` (`{user_data, allocate, free, NULL}`), tested on
  macOS (its tests plain and under ASan, and SpudNet's fuzz targets, whose
  allocator refuses part way, 5 minutes each). Left: WASAPI, GameInput,
  evdev and D3D12's arrays, and D3D12's `std::` containers, each when it
  can be built on its platform.
- ApricotFields, trellislib and Latran: plain-data arrays as `eq::array`,
  the rest as `eq::vector` / `eq::string`, converted as code is changed,
  with the system heap until a caller allocator is decided (paused card
  X15).

## Decided while building it

Calls the spec left open, made on 2026-10-28 (the workspace's
`claude_made_decisions.md` has each with its reasons and alternatives):

- `EQ_ALLOCATOR` is `{ user_data, allocate, free, resize }`: the first three
  fields in the common C allocator-table order with the same function types,
  so a caller's three-field table is handed over by copying its fields;
  `resize` may be NULL (eqstd then allocates, copies and frees).
- An array's block is allocated at `EQ_ARRAY_ALIGNMENT(sizeof(T))`, the
  largest power of two dividing the element size (always a multiple of
  `T`'s alignment): standard C11 can't take the alignment of an expression,
  and C and C++ code sharing an array must agree.
- The system heap over-aligns by hand above `malloc`'s alignment, with only
  `malloc`, `realloc` and `free`; that alignment is one constant per
  platform, the same in C and C++.
- The array macros take a pointer to the array and copy items from a
  pointer (`eq_array_push(&a, &x)`), since C11 can't make a temporary of an
  unnamed element type; a pointer of the wrong type is caught.
- `eq_array_at` evaluates its array twice when checking (the index once).
- `EQ_RESULT` values: `EQ_SUCCESS`, `_OUT_OF_MEMORY`, `_TOO_LARGE`,
  `_OUT_OF_RANGE`, `_INVALID_ARGUMENT`, `_INVALID_ALLOCATOR`,
  `_FORMAT_FAILED`; a block is at most `PTRDIFF_MAX` bytes.
- A failed index check calls `EQ_INDEX_ERROR` (overridable; by default
  print and `abort()`).
- `EQ_STRING` has a caller's first buffer (`eq_string_init_buffer`), which
  is also `eq::string`'s short-string buffer; "take the result" is
  `eq_string_c_str`, with no handing over of the block. It has insert and
  remove beside append.
- eq::array, eq::vector and eq::string own their memory (destructors),
  are move-only (copying is `copy_from()`, which returns a result), and
  return `EQ_RESULT` from growth. eq::array's fields are public, as
  `EQ_ARRAY`'s are; `from_c()` / `as_c()` convert between the two.
- eq::string is one 64-byte object holding 23 bytes of text inline on a
  64-bit platform; `operator[]` checks `index < size()`.
