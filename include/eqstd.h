/**
 * @file eqstd.h
 * @brief eqstd's C half: the allocator, typed arrays and a string builder.
 *
 * eqstd is a header-only library of containers and strings. Its scope, in
 * one line: containers, strings and the allocator they take. Nothing else:
 * no algorithms, math, threading or I/O.
 *
 * It belongs to no layer. Any C or C++ code may include it, and it includes
 * only the C standard library. Its types are for code inside a library and
 * never appear in a public API, so no library's ABI depends on their layout.
 *
 * C11, and compiles unchanged as C++ (C++20 and later) and Objective-C.
 * `eqstd.hpp` is the C++ half, built over this one.
 *
 * ## Conventions
 *
 * - **Results.** Every call that can fail returns an EQ_RESULT. Out of
 *   memory is a result, never an abort. A call that fails leaves the object
 *   exactly as it was before the call.
 * - **Arguments are evaluated once.** Every macro here evaluates each of its
 *   arguments exactly once, except eq_array_at() (see there).
 * - **The allocator.** Every array and string remembers the EQ_ALLOCATOR it
 *   was made with and takes and frees all of its memory through it. A null
 *   `EQ_ALLOCATOR *` means the system heap. The allocator struct is not
 *   copied: it must outlive every object made with it.
 * - **Zeroed is empty.** An array or string whose bytes are all zero (`= {0}`
 *   in C, `= {}` in C++) is empty and on the system heap.
 * - **Threading.** Every object (an array, a string) is used by one caller at
 *   a time: no two calls on one object may overlap. Calls on different
 *   objects are independent, as far as the allocator they share allows: the
 *   system heap may be used from any thread at once, and a caller's
 *   allocator is as thread-safe as its functions are. eqstd holds no global
 *   or static state.
 * - **Debug checks.** When EQ_DEBUG is nonzero (by default, when `_DEBUG` is
 *   defined), eq_array_at() checks its index. No struct's layout depends on
 *   EQ_DEBUG.
 */
#ifndef EQSTD_H
#define EQSTD_H

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** @name Version
 * eqstd's semantic version. Before 1.0, a minor version may break callers.
 * @{ */
#define EQSTD_VERSION_MAJOR 0
#define EQSTD_VERSION_MINOR 1
#define EQSTD_VERSION_PATCH 0
/** @} */

#ifdef __cplusplus
extern "C" {
#endif

/* Helpers for the declarations below; not the interface. */
#ifdef __cplusplus
#define EQ_INTERNAL_CAST(T, x) (static_cast<T>(x))
#define EQ_INTERNAL_ADDRESS(p) (reinterpret_cast<uintptr_t>(p))
#define EQ_INTERNAL_NULL nullptr
#else
#define EQ_INTERNAL_CAST(T, x) ((T)(x))
#define EQ_INTERNAL_ADDRESS(p) ((uintptr_t)(p))
#define EQ_INTERNAL_NULL NULL
#endif

#if defined(__GNUC__) || defined(__clang__)
#define EQ_INTERNAL_NODISCARD __attribute__((warn_unused_result))
#define EQ_INTERNAL_NORETURN __attribute__((noreturn))
#define EQ_INTERNAL_PRINTF(format_index, first_argument) __attribute__((format(printf, format_index, first_argument)))
#else
#define EQ_INTERNAL_NODISCARD
#if defined(_MSC_VER)
#define EQ_INTERNAL_NORETURN __declspec(noreturn)
#else
#define EQ_INTERNAL_NORETURN
#endif
#define EQ_INTERNAL_PRINTF(format_index, first_argument)
#endif

/* The alignment `malloc` promises, the same in C and C++ on one platform, so
 * a block allocated by one language's code and freed by the other's takes
 * the same path. */
#if defined(_MSC_VER)
#define EQ_INTERNAL_MALLOC_ALIGNMENT EQ_INTERNAL_CAST(size_t, 8) /* alignof(max_align_t) in MSVC's C++ */
#elif defined(__APPLE__)
#define EQ_INTERNAL_MALLOC_ALIGNMENT EQ_INTERNAL_CAST(size_t, 16) /* Apple's malloc on every platform */
#elif defined(__cplusplus)
#define EQ_INTERNAL_MALLOC_ALIGNMENT alignof(max_align_t)
#else
#define EQ_INTERNAL_MALLOC_ALIGNMENT _Alignof(max_align_t)
#endif

/* ---- Results ------------------------------------------------------------ */

/**
 * @brief What a call did. EQ_SUCCESS is 0 and the only success value.
 *
 * Values are never renumbered; new ones are appended.
 */
typedef enum EQ_RESULT {
	/** The call did what it was asked. */
	EQ_SUCCESS = 0,
	/** The allocator returned NULL. The object is as it was. */
	EQ_RESULT_OUT_OF_MEMORY = 1,
	/** The size asked for can't be represented: more than PTRDIFF_MAX bytes,
	 * or a count that overflows `size_t`. */
	EQ_RESULT_TOO_LARGE = 2,
	/** An index, or an index and a count, reach past the elements there are. */
	EQ_RESULT_OUT_OF_RANGE = 3,
	/** An argument is NULL where it can't be: the object, or the items of a
	 * nonzero count. */
	EQ_RESULT_INVALID_ARGUMENT = 4,
	/** The object's allocator has a NULL `allocate` or `free`. */
	EQ_RESULT_INVALID_ALLOCATOR = 5,
	/** `vsnprintf` reported an error for a format and its arguments. */
	EQ_RESULT_FORMAT_FAILED = 6
} EQ_RESULT;

/**
 * @brief Returns the name of an EQ_RESULT value, for logs.
 *
 * @param[in] result The result to name.
 *
 * @return The enumerator's identifier as spelled here, such as
 *         "EQ_RESULT_OUT_OF_MEMORY": a string literal, never NULL.
 * @retval "EQ_RESULT_UNKNOWN" `result` is not one of EQ_RESULT's values.
 */
static inline const char *eq_result_str(EQ_RESULT result);

/* ---- The allocator ------------------------------------------------------ */

/**
 * @defgroup eq_allocator The allocator
 *
 * A caller's allocator: three functions and the state they share. Every
 * function is told the size and alignment, so the allocator need not record
 * either. The first three fields are the common shape of a C allocator table
 * (`user_data`, then `allocate` and `free` with these function types), so a
 * caller's table of that shape is handed over by copying its fields and
 * leaving `resize` NULL:
 *
 * @code
 * EQ_ALLOCATOR eq = { theirs->user_data, theirs->allocate, theirs->free, NULL };
 * @endcode
 *
 * The rules an allocator keeps, and eqstd keeps with it:
 *
 * - `allocate` returns a block of at least `size` bytes whose address is a
 *   multiple of `alignment`, or NULL if it can't. eqstd never asks for 0
 *   bytes, and `alignment` is always a power of two, 1 or more.
 * - `free` is called once for each block, never with NULL, with the `size`
 *   and `alignment` the block has at that moment (after any resize).
 * - `resize` may be NULL. When it isn't, eqstd calls it to grow a block: it
 *   returns a block of `new_size` bytes holding the first
 *   `min(old_size, new_size)` bytes of the old one (moved or in place), or
 *   NULL, leaving the old block as it was. When it is NULL, eqstd allocates
 *   a new block, copies, and frees the old one.
 * - None of the functions may call eqstd on the object being changed.
 *
 * The system heap (a null `EQ_ALLOCATOR *`) is `malloc`, `realloc` and
 * `free`, with blocks over-aligned by hand for an alignment above what
 * `malloc` promises. It is safe from any thread at once.
 * @{
 */

/**
 * @brief Allocates a block.
 *
 * @param[in] user_data The allocator's `user_data`, as it was given.
 * @param[in] size      The bytes wanted. Never 0.
 * @param[in] alignment What the address must be a multiple of: a power of
 *                      two, 1 or more.
 *
 * @return The block, or NULL if it can't be given.
 */
typedef void *(*EQ_ALLOCATE_FUNCTION)(void *user_data, size_t size, size_t alignment);

/**
 * @brief Frees a block the allocator gave.
 *
 * @param[in] user_data The allocator's `user_data`, as it was given.
 * @param[in] memory    The block. Never NULL.
 * @param[in] size      The block's size.
 * @param[in] alignment The alignment it was allocated with.
 */
typedef void (*EQ_FREE_FUNCTION)(void *user_data, void *memory, size_t size, size_t alignment);

/**
 * @brief Resizes a block the allocator gave, moving it if it must.
 *
 * @param[in] user_data The allocator's `user_data`, as it was given.
 * @param[in] memory    The block. Never NULL.
 * @param[in] old_size  The block's size now. Never 0.
 * @param[in] new_size  The size wanted. Never 0.
 * @param[in] alignment The alignment it was allocated with, kept.
 *
 * @return The block, holding the old one's first `min(old_size, new_size)`
 *         bytes, or NULL if it can't be given; the old block is then
 *         untouched and still the caller's.
 */
typedef void *(*EQ_RESIZE_FUNCTION)(void *user_data, void *memory, size_t old_size, size_t new_size, size_t alignment);

/** @brief The caller's allocator. */
/* Frozen: a table of functions. A new function is a new struct. */
typedef struct EQ_ALLOCATOR {
	/** Passed to every function as it is. eqstd never reads it. May be NULL. */
	void *user_data;
	/** Gives a block. Not NULL. */
	EQ_ALLOCATE_FUNCTION allocate;
	/** Frees a block. Not NULL. */
	EQ_FREE_FUNCTION free;
	/** Grows a block. May be NULL: eqstd then allocates, copies and frees. */
	EQ_RESIZE_FUNCTION resize;
} EQ_ALLOCATOR;

/**
 * @brief Allocates a block through an allocator, or the system heap.
 *
 * @param[in] allocator The allocator, or NULL for the system heap.
 * @param[in] size      The bytes wanted.
 * @param[in] alignment A power of two, 1 or more.
 *
 * @return The block, or NULL: when the allocator gives none, when `size` is
 *         0, when `alignment` is not a power of two, or when `allocator`
 *         has a NULL `allocate`.
 */
static inline void *eq_allocate(const EQ_ALLOCATOR *allocator, size_t size, size_t alignment);

/**
 * @brief Resizes a block from eq_allocate() through the same allocator.
 *
 * Uses the allocator's `resize` when it has one; otherwise allocates a new
 * block, copies `min(old_size, new_size)` bytes and frees the old block.
 *
 * @param[in] allocator The block's allocator, or NULL for the system heap.
 * @param[in] memory    The block. Not NULL.
 * @param[in] old_size  Its size now. Not 0.
 * @param[in] new_size  The size wanted. Not 0.
 * @param[in] alignment The alignment it was allocated with.
 *
 * @return The block, or NULL, leaving `memory` untouched and still the
 *         caller's. NULL as well, doing nothing, for a NULL `memory`, a size
 *         of 0 or an alignment that is not a power of two.
 */
static inline void *eq_resize(const EQ_ALLOCATOR *allocator, void *memory, size_t old_size, size_t new_size,
	size_t alignment);

/**
 * @brief Frees a block from eq_allocate() or eq_resize().
 *
 * @param[in] allocator The block's allocator, or NULL for the system heap.
 * @param[in] memory    The block. NULL does nothing.
 * @param[in] size      Its size.
 * @param[in] alignment The alignment it was allocated with.
 */
static inline void eq_free(const EQ_ALLOCATOR *allocator, void *memory, size_t size, size_t alignment);

/** @} */

/* ---- Arrays ------------------------------------------------------------- */

/**
 * @defgroup eq_array Arrays
 *
 * `EQ_ARRAY(T)` is a struct type holding a growable array of `T`:
 *
 * @code
 * typedef EQ_ARRAY(float) FLOAT_ARRAY;
 * FLOAT_ARRAY a = {0};                     // empty, system heap
 * eq_array_init(&a, &allocator);           // or a caller's allocator
 * float x = 1.0f;
 * if (eq_array_push(&a, &x) != EQ_SUCCESS)
 *     return ...;
 * a.data[0] = 2.0f;                        // read and write elements directly
 * eq_array_free(&a);
 * @endcode
 *
 * Each `EQ_ARRAY(T)` written out is a distinct struct type in C, so name one
 * with a typedef and use the name. `T` is copied with `memcpy`: plain data
 * only. `eq::array<T>` (`eqstd.hpp`) has the same layout, and the macros
 * here take a pointer to either.
 *
 * Invariants: `count <= capacity`; `data` is NULL exactly when `capacity` is
 * 0; `data[0]` to `data[count - 1]` are the elements; the block holds
 * `capacity * sizeof(T)` bytes.
 *
 * Every macro takes a pointer to the array, `a`, and evaluates each argument
 * once. Growth is geometric (doubling, from 4 elements), except
 * eq_array_reserve() and eq_array_resize(), which allocate what they are
 * asked for. Memory is never given back until eq_array_free().
 *
 * The alignment eqstd allocates elements with is EQ_ARRAY_ALIGNMENT() of
 * `sizeof(T)`: the largest power of two dividing the size, which is always
 * a multiple of `T`'s alignment. Standard C11 can't take the alignment of an
 * expression, and C and C++ code sharing an array must agree on it.
 * @{
 */

/** @brief Declares the struct type of an array of `T`. */
#define EQ_ARRAY(T)                                                                                                    \
	struct {                                                                                                           \
		T *data;                                                                                                       \
		size_t count;                                                                                                  \
		size_t capacity;                                                                                               \
		const EQ_ALLOCATOR *allocator;                                                                                 \
	}

/** @brief The alignment an array's block is allocated with, for elements of
 * `element_size` bytes: the largest power of two that divides it. */
#define EQ_ARRAY_ALIGNMENT(element_size) ((element_size) & (~(element_size) + 1u))

/**
 * @brief Makes `*a` an empty array on `allocator`.
 *
 * Doesn't free what `*a` held: use it on a new array, or after
 * eq_array_free(). Allocates nothing.
 *
 * @param a         `EQ_ARRAY(T) *`. NULL does nothing.
 * @param allocator `const EQ_ALLOCATOR *`, or NULL for the system heap. Must
 *                  outlive the array.
 */
#define eq_array_init(a, allocator) eq_internal_array_init((a), (allocator))

/**
 * @brief Frees the array's block and empties it, keeping its allocator.
 *
 * @param a `EQ_ARRAY(T) *`. NULL does nothing.
 */
#define eq_array_free(a) eq_internal_array_free((a), EQ_INTERNAL_ELEMENT_SIZE(a))

/**
 * @brief Makes room for at least `capacity` elements without growing again.
 *
 * @param a        `EQ_ARRAY(T) *`.
 * @param capacity `size_t`. At or below the capacity there is, nothing
 *                 changes.
 *
 * @return EQ_RESULT: EQ_SUCCESS; EQ_RESULT_INVALID_ARGUMENT for a NULL `a`;
 *         EQ_RESULT_TOO_LARGE; EQ_RESULT_INVALID_ALLOCATOR;
 *         EQ_RESULT_OUT_OF_MEMORY.
 */
#define eq_array_reserve(a, capacity) eq_internal_array_reserve((a), EQ_INTERNAL_ELEMENT_SIZE(a), (capacity))

/**
 * @brief Sets the number of elements. New elements are zero bytes.
 *
 * @param a     `EQ_ARRAY(T) *`.
 * @param count `size_t`. Growing past the capacity allocates exactly `count`.
 *
 * @return EQ_RESULT: as eq_array_reserve().
 */
#define eq_array_resize(a, count) eq_internal_array_resize((a), EQ_INTERNAL_ELEMENT_SIZE(a), (count))

/**
 * @brief Appends one element, copied from `*item`.
 *
 * @param a    `EQ_ARRAY(T) *`.
 * @param item `const T *`. May point at one of the array's own elements.
 *
 * @return EQ_RESULT: EQ_SUCCESS; EQ_RESULT_INVALID_ARGUMENT for a NULL `a`
 *         or `item`; EQ_RESULT_TOO_LARGE; EQ_RESULT_INVALID_ALLOCATOR;
 *         EQ_RESULT_OUT_OF_MEMORY.
 */
#define eq_array_push(a, item) eq_internal_array_append((a), EQ_INTERNAL_ELEMENT_SIZE(a), EQ_INTERNAL_ITEMS(a, item), 1u)

/**
 * @brief Appends `n` elements, copied from `items`.
 *
 * @param a     `EQ_ARRAY(T) *`.
 * @param items `const T *`: `n` elements. May be the array's own elements.
 *              May be NULL when `n` is 0.
 * @param n     `size_t`. 0 does nothing.
 *
 * @return EQ_RESULT: as eq_array_push().
 */
#define eq_array_append(a, items, n) eq_internal_array_append((a), EQ_INTERNAL_ELEMENT_SIZE(a), EQ_INTERNAL_ITEMS(a, items), (n))

/**
 * @brief Inserts `n` elements, copied from `items`, before element `index`.
 *
 * The elements from `index` on move up by `n`, in order.
 *
 * @param a     `EQ_ARRAY(T) *`.
 * @param index `size_t`. `count` appends.
 * @param items `const T *`: `n` elements. May be the array's own elements.
 *              May be NULL when `n` is 0.
 * @param n     `size_t`. 0 does nothing but check `index`.
 *
 * @return EQ_RESULT: as eq_array_push(), and EQ_RESULT_OUT_OF_RANGE for an
 *         `index` past `count`.
 */
#define eq_array_insert(a, index, items, n)                                                                            \
	eq_internal_array_insert((a), EQ_INTERNAL_ELEMENT_SIZE(a), (index), EQ_INTERNAL_ITEMS(a, items), (n))

/**
 * @brief Removes `n` elements from `index` on, keeping the order of the rest.
 *
 * @param a     `EQ_ARRAY(T) *`.
 * @param index `size_t`.
 * @param n     `size_t`. 0 does nothing but check `index`.
 *
 * @return EQ_RESULT: EQ_SUCCESS; EQ_RESULT_INVALID_ARGUMENT for a NULL `a`;
 *         EQ_RESULT_OUT_OF_RANGE when `index` is past `count` or `index + n`
 *         is.
 */
#define eq_array_remove(a, index, n) eq_internal_array_remove((a), EQ_INTERNAL_ELEMENT_SIZE(a), (index), (n))

/**
 * @brief Removes element `index` by moving the last element into its place.
 *
 * @param a     `EQ_ARRAY(T) *`.
 * @param index `size_t`.
 *
 * @return EQ_RESULT: EQ_SUCCESS; EQ_RESULT_INVALID_ARGUMENT for a NULL `a`;
 *         EQ_RESULT_OUT_OF_RANGE for an `index` at or past `count`.
 */
#define eq_array_remove_swap(a, index) eq_internal_array_remove_swap((a), EQ_INTERNAL_ELEMENT_SIZE(a), (index))

/**
 * @brief Removes every element, keeping the block.
 *
 * @param a `EQ_ARRAY(T) *`.
 *
 * @return EQ_RESULT: EQ_SUCCESS, or EQ_RESULT_INVALID_ARGUMENT for a NULL
 *         `a`.
 */
#define eq_array_clear(a) eq_internal_array_clear((a), EQ_INTERNAL_ELEMENT_SIZE(a))


/** @} */

/* ---- Strings ------------------------------------------------------------ */

/**
 * @defgroup eq_string Strings
 *
 * `EQ_STRING` builds UTF-8 text (eqstd treats it as bytes and never checks
 * the encoding):
 *
 * @code
 * EQ_STRING s = {0};
 * if (eq_string_append_format(&s, "%s has %d walls", name, count) != EQ_SUCCESS)
 *     ...
 * puts(eq_string_c_str(&s));
 * eq_string_free(&s);
 * @endcode
 *
 * Invariants: `data` is NULL or holds `length` bytes and a NUL after them
 * (bytes before it may be NUL too, if appended); the block or buffer holds
 * `capacity + 1` bytes. `buffer` is a caller's first buffer
 * (eq_string_init_buffer()), never freed by eqstd: while `data == buffer`
 * the string allocates nothing, and growing past it moves the text to the
 * allocator.
 *
 * A text argument may point into the string itself, except a `%s` argument
 * of eq_string_append_format().
 * @{
 */

/** @brief A string builder. Zeroed is empty, on the system heap. */
/* Frozen: eqstd's own struct, never in a public API. */
typedef struct EQ_STRING {
	/** The text, NUL-terminated; NULL while nothing was ever stored. */
	char *data;
	/** Bytes of text, not counting the NUL. */
	size_t length;
	/** Bytes of text `data` has room for, not counting the NUL. */
	size_t capacity;
	/** Where the text's memory comes from; NULL for the system heap. */
	const EQ_ALLOCATOR *allocator;
	/** The caller's first buffer, or NULL. Never freed by eqstd. */
	char *buffer;
} EQ_STRING;

/**
 * @brief Makes `*s` an empty string on `allocator`. Allocates nothing.
 *
 * Doesn't free what `*s` held: use it on a new string, or after
 * eq_string_free().
 *
 * @param[out] s         The string. NULL does nothing.
 * @param[in]  allocator The allocator, or NULL for the system heap. Must
 *                       outlive the string.
 */
static inline void eq_string_init(EQ_STRING *s, const EQ_ALLOCATOR *allocator);

/**
 * @brief Makes `*s` an empty string that writes into the caller's buffer
 *        until it outgrows it.
 *
 * Text up to `buffer_size - 1` bytes stays in `buffer` and allocates
 * nothing; past that it moves to `allocator`. eqstd never frees `buffer`,
 * which must outlive the string or its next eq_string_free().
 *
 * @param[out] s           The string. NULL does nothing.
 * @param[in]  allocator   The allocator, or NULL for the system heap.
 * @param[in]  buffer      The buffer, or NULL for none.
 * @param[in]  buffer_size Its size in bytes, the NUL's included. 0 is none.
 */
static inline void eq_string_init_buffer(EQ_STRING *s, const EQ_ALLOCATOR *allocator, char *buffer,
	size_t buffer_size);

/**
 * @brief Frees the string's memory and empties it, keeping its allocator and
 *        forgetting its buffer.
 *
 * @param[in,out] s The string. NULL does nothing.
 */
static inline void eq_string_free(EQ_STRING *s);

/**
 * @brief Makes room for `capacity` bytes of text without growing again.
 *
 * @param[in,out] s        The string.
 * @param[in]     capacity Bytes of text, the NUL not counted.
 *
 * @return EQ_SUCCESS; EQ_RESULT_INVALID_ARGUMENT for a NULL `s`;
 *         EQ_RESULT_TOO_LARGE; EQ_RESULT_INVALID_ALLOCATOR;
 *         EQ_RESULT_OUT_OF_MEMORY.
 */
EQ_INTERNAL_NODISCARD static inline EQ_RESULT eq_string_reserve(EQ_STRING *s, size_t capacity);

/**
 * @brief Appends a NUL-terminated text.
 *
 * @param[in,out] s    The string.
 * @param[in]     text The text. May point into `s`.
 *
 * @return As eq_string_reserve(), and EQ_RESULT_INVALID_ARGUMENT for a NULL
 *         `text`.
 */
EQ_INTERNAL_NODISCARD static inline EQ_RESULT eq_string_append(EQ_STRING *s, const char *text);

/**
 * @brief Appends `length` bytes of text.
 *
 * @param[in,out] s      The string.
 * @param[in]     text   The bytes. May point into `s`. May be NULL when
 *                       `length` is 0.
 * @param[in]     length How many. 0 does nothing.
 *
 * @return As eq_string_append().
 */
EQ_INTERNAL_NODISCARD static inline EQ_RESULT eq_string_append_n(EQ_STRING *s, const char *text, size_t length);

/**
 * @brief Inserts `length` bytes of text before byte `index`.
 *
 * @param[in,out] s      The string.
 * @param[in]     index  Where. `s->length` appends.
 * @param[in]     text   The bytes. May point into `s`. May be NULL when
 *                       `length` is 0.
 * @param[in]     length How many. 0 does nothing but check `index`.
 *
 * @return As eq_string_append(), and EQ_RESULT_OUT_OF_RANGE for an `index`
 *         past `s->length`.
 */
EQ_INTERNAL_NODISCARD static inline EQ_RESULT eq_string_insert(EQ_STRING *s, size_t index, const char *text, size_t length);

/**
 * @brief Removes `length` bytes from byte `index` on.
 *
 * @param[in,out] s      The string.
 * @param[in]     index  Where.
 * @param[in]     length How many.
 *
 * @return EQ_SUCCESS; EQ_RESULT_INVALID_ARGUMENT for a NULL `s`;
 *         EQ_RESULT_OUT_OF_RANGE when `index` or `index + length` is past
 *         `s->length`.
 */
EQ_INTERNAL_NODISCARD static inline EQ_RESULT eq_string_remove(EQ_STRING *s, size_t index, size_t length);

/**
 * @brief Appends text formatted as `printf` formats it.
 *
 * @param[in,out] s      The string.
 * @param[in]     format The format. Not NULL. No argument may point into `s`.
 *
 * @return As eq_string_append(), and EQ_RESULT_FORMAT_FAILED when
 *         `vsnprintf` reports an error; the text is then as it was (if the
 *         error came only on the second of its two passes, the capacity may
 *         have grown).
 */
EQ_INTERNAL_NODISCARD EQ_INTERNAL_PRINTF(2, 3) static inline EQ_RESULT eq_string_append_format(EQ_STRING *s,
	const char *format, ...);

/** @brief eq_string_append_format() with a `va_list`, which it doesn't
 * consume (it works on copies). */
EQ_INTERNAL_NODISCARD EQ_INTERNAL_PRINTF(2, 0) static inline EQ_RESULT eq_string_append_format_v(EQ_STRING *s,
	const char *format, va_list arguments);

/**
 * @brief Empties the string, keeping its memory.
 *
 * @param[in,out] s The string.
 *
 * @return EQ_SUCCESS, or EQ_RESULT_INVALID_ARGUMENT for a NULL `s`.
 */
static inline EQ_RESULT eq_string_clear(EQ_STRING *s);

/**
 * @brief The text, NUL-terminated.
 *
 * @param[in] s The string, or NULL.
 *
 * @return `s->data`, or "" (a literal, not to be written) when `s` or its
 *         data is NULL. Valid until the string next changes.
 */
static inline const char *eq_string_c_str(const EQ_STRING *s);

/** @} */

/* ---- Debug checks --------------------------------------------------------- */

/**
 * @brief Nonzero turns on eq_array_at()'s index check (and eqstd.hpp's).
 *
 * Defaults to 1 when `_DEBUG` is defined and 0 otherwise. Define it before
 * including eqstd to choose. It changes no struct's layout.
 */
#ifndef EQ_DEBUG
#if defined(_DEBUG)
#define EQ_DEBUG 1
#else
#define EQ_DEBUG 0
#endif
#endif

/**
 * @brief Called by a failed index check with the index, the count, and the
 *        file and line of the check.
 *
 * By default it prints them to `stderr` and calls `abort()`. Define it before
 * including eqstd to report another way. It should not return: if it does,
 * the out-of-range access goes ahead.
 */
#ifndef EQ_INDEX_ERROR
#define EQ_INDEX_ERROR(index, count, file, line) eq_internal_index_error((index), (count), (file), (line))
#endif

/**
 * @brief Element `index` of `*a`, as an lvalue.
 *
 * When EQ_DEBUG is nonzero, an `index` at or past `count` calls
 * EQ_INDEX_ERROR(); otherwise this is `a->data[index]`. `index` is
 * evaluated once; `a` twice when checking, so pass it without side effects.
 * A negative `index` converts to a large `size_t` and fails the check.
 *
 * @param a     `EQ_ARRAY(T) *`.
 * @param index An integer.
 */
#if EQ_DEBUG
#define eq_array_at(a, index)                                                                                          \
	((a)->data[eq_internal_check_index(EQ_INTERNAL_CAST(size_t, (index)), (a)->count, __FILE__, __LINE__)])
#else
#define eq_array_at(a, index) ((a)->data[(index)])
#endif

/* ---- Internal: nothing from here on is the interface ---------------------- */

/* Any array as the macros' functions see it: EQ_ARRAY(T) and eq::array<T>
 * have this layout. The functions copy it in and out with memcpy, so no
 * array is ever read through a pointer to another struct type. */
typedef struct EQ_INTERNAL_ARRAY {
	void *data;
	size_t count;
	size_t capacity;
	const EQ_ALLOCATOR *allocator;
} EQ_INTERNAL_ARRAY;

/* sizeof(T), and a compile error unless `a` points at an array struct. Not
 * evaluated. */
#define EQ_INTERNAL_ELEMENT_SIZE(a)                                                                                    \
	(sizeof(*(a)->data) + 0u * sizeof(char[sizeof(*(a)) == sizeof(EQ_INTERNAL_ARRAY) ? 1 : -1]))

/* `items`, evaluated once and `a` not at all, as a pointer of the array's
 * element type: a pointer of another type doesn't compile (C++) or warns (C). */
#define EQ_INTERNAL_ITEMS(a, items) (1 ? (items) : (a)->data)

EQ_INTERNAL_NORETURN static inline void eq_internal_index_error(size_t index, size_t count, const char *file, int line);
static inline size_t eq_internal_check_index(size_t index, size_t count, const char *file, int line);
static inline void eq_internal_array_init(void *array, const EQ_ALLOCATOR *allocator);
static inline void eq_internal_array_free(void *array, size_t element_size);
EQ_INTERNAL_NODISCARD static inline EQ_RESULT eq_internal_array_reserve(void *array, size_t element_size,
	size_t capacity);
EQ_INTERNAL_NODISCARD static inline EQ_RESULT eq_internal_array_resize(void *array, size_t element_size, size_t count);
EQ_INTERNAL_NODISCARD static inline EQ_RESULT eq_internal_array_append(void *array, size_t element_size,
	const void *items, size_t n);
EQ_INTERNAL_NODISCARD static inline EQ_RESULT eq_internal_array_insert(void *array, size_t element_size, size_t index,
	const void *items, size_t n);
EQ_INTERNAL_NODISCARD static inline EQ_RESULT eq_internal_array_remove(void *array, size_t element_size, size_t index,
	size_t n);
EQ_INTERNAL_NODISCARD static inline EQ_RESULT eq_internal_array_remove_swap(void *array, size_t element_size,
	size_t index);
static inline EQ_RESULT eq_internal_array_clear(void *array, size_t element_size);

/* ==== Implementation ====================================================== */

/* ---- Results ---- */

static inline const char *eq_result_str(EQ_RESULT result)
{
	if (result == EQ_SUCCESS)
		return "EQ_SUCCESS";
	if (result == EQ_RESULT_OUT_OF_MEMORY)
		return "EQ_RESULT_OUT_OF_MEMORY";
	if (result == EQ_RESULT_TOO_LARGE)
		return "EQ_RESULT_TOO_LARGE";
	if (result == EQ_RESULT_OUT_OF_RANGE)
		return "EQ_RESULT_OUT_OF_RANGE";
	if (result == EQ_RESULT_INVALID_ARGUMENT)
		return "EQ_RESULT_INVALID_ARGUMENT";
	if (result == EQ_RESULT_INVALID_ALLOCATOR)
		return "EQ_RESULT_INVALID_ALLOCATOR";
	if (result == EQ_RESULT_FORMAT_FAILED)
		return "EQ_RESULT_FORMAT_FAILED";
	return "EQ_RESULT_UNKNOWN";
}

/* ---- The allocator ---- */

static inline int eq_internal_is_power_of_two(size_t x)
{
	return x != 0 && (x & (x - 1)) == 0;
}

/* The system heap. An alignment `malloc` promises goes to malloc, realloc and
 * free. A larger one over-allocates and moves the block up to the alignment,
 * with the address malloc gave stored just below it. */
static inline void *eq_internal_system_allocate(size_t size, size_t alignment)
{
	unsigned char *raw;
	unsigned char *block;
	size_t extra;
	if (alignment <= EQ_INTERNAL_MALLOC_ALIGNMENT)
		return malloc(size);
	extra = alignment - 1 + sizeof(void *);
	if (size > SIZE_MAX - extra)
		return EQ_INTERNAL_NULL;
	raw = EQ_INTERNAL_CAST(unsigned char *, malloc(size + extra));
	if (raw == EQ_INTERNAL_NULL)
		return EQ_INTERNAL_NULL;
	block = raw + sizeof(void *);
	block += EQ_INTERNAL_CAST(size_t, (alignment - EQ_INTERNAL_ADDRESS(block) % alignment) % alignment);
	memcpy(block - sizeof(void *), &raw, sizeof(raw));
	return block;
}

static inline void eq_internal_system_free(void *memory, size_t alignment)
{
	unsigned char *raw;
	if (alignment <= EQ_INTERNAL_MALLOC_ALIGNMENT) {
		free(memory);
		return;
	}
	memcpy(&raw, EQ_INTERNAL_CAST(unsigned char *, memory) - sizeof(void *), sizeof(raw));
	free(raw);
}

static inline void *eq_internal_system_resize(void *memory, size_t old_size, size_t new_size, size_t alignment)
{
	void *moved;
	if (alignment <= EQ_INTERNAL_MALLOC_ALIGNMENT)
		return realloc(memory, new_size);
	moved = eq_internal_system_allocate(new_size, alignment);
	if (moved == EQ_INTERNAL_NULL)
		return EQ_INTERNAL_NULL;
	memcpy(moved, memory, old_size < new_size ? old_size : new_size);
	eq_internal_system_free(memory, alignment);
	return moved;
}

static inline void *eq_allocate(const EQ_ALLOCATOR *allocator, size_t size, size_t alignment)
{
	if (size == 0)
		return EQ_INTERNAL_NULL;
	if (!eq_internal_is_power_of_two(alignment))
		return EQ_INTERNAL_NULL;
	if (allocator == EQ_INTERNAL_NULL)
		return eq_internal_system_allocate(size, alignment);
	if (allocator->allocate == EQ_INTERNAL_NULL)
		return EQ_INTERNAL_NULL;
	return allocator->allocate(allocator->user_data, size, alignment);
}

static inline void *eq_resize(const EQ_ALLOCATOR *allocator, void *memory, size_t old_size, size_t new_size,
	size_t alignment)
{
	void *moved;
	if (memory == EQ_INTERNAL_NULL)
		return EQ_INTERNAL_NULL;
	if (old_size == 0)
		return EQ_INTERNAL_NULL;
	if (new_size == 0)
		return EQ_INTERNAL_NULL;
	if (!eq_internal_is_power_of_two(alignment))
		return EQ_INTERNAL_NULL;
	if (allocator == EQ_INTERNAL_NULL)
		return eq_internal_system_resize(memory, old_size, new_size, alignment);
	if (allocator->resize != EQ_INTERNAL_NULL)
		return allocator->resize(allocator->user_data, memory, old_size, new_size, alignment);
	if (allocator->allocate == EQ_INTERNAL_NULL)
		return EQ_INTERNAL_NULL;
	if (allocator->free == EQ_INTERNAL_NULL)
		return EQ_INTERNAL_NULL;
	moved = allocator->allocate(allocator->user_data, new_size, alignment);
	if (moved == EQ_INTERNAL_NULL)
		return EQ_INTERNAL_NULL;
	memcpy(moved, memory, old_size < new_size ? old_size : new_size);
	allocator->free(allocator->user_data, memory, old_size, alignment);
	return moved;
}

static inline void eq_free(const EQ_ALLOCATOR *allocator, void *memory, size_t size, size_t alignment)
{
	if (memory == EQ_INTERNAL_NULL)
		return;
	if (allocator == EQ_INTERNAL_NULL) {
		eq_internal_system_free(memory, alignment);
		return;
	}
	if (allocator->free != EQ_INTERNAL_NULL)
		allocator->free(allocator->user_data, memory, size, alignment);
}

/* EQ_RESULT_INVALID_ALLOCATOR for an allocator missing a function, before
 * anything is asked of it. */
static inline EQ_RESULT eq_internal_check_allocator(const EQ_ALLOCATOR *allocator)
{
	if (allocator == EQ_INTERNAL_NULL)
		return EQ_SUCCESS;
	if (allocator->allocate == EQ_INTERNAL_NULL)
		return EQ_RESULT_INVALID_ALLOCATOR;
	if (allocator->free == EQ_INTERNAL_NULL)
		return EQ_RESULT_INVALID_ALLOCATOR;
	return EQ_SUCCESS;
}

/* The capacity to grow to: double, from `minimum`, at least `needed`, at most
 * `maximum`. `needed <= maximum`. */
static inline size_t eq_internal_grown_capacity(size_t capacity, size_t needed, size_t maximum, size_t minimum)
{
	size_t grown = capacity > maximum / 2 ? maximum : capacity * 2;
	if (grown < minimum)
		grown = minimum;
	if (grown > maximum)
		grown = maximum;
	if (grown < needed)
		grown = needed;
	return grown;
}

/* Where `p` is inside the first `bytes` bytes of `block`, as an offset, or
 * SIZE_MAX when it isn't. Compares addresses as integers, which is defined
 * for pointers into different objects. */
static inline size_t eq_internal_offset_in(const void *block, size_t bytes, const void *p)
{
	uintptr_t begin;
	uintptr_t at;
	if (block == EQ_INTERNAL_NULL)
		return SIZE_MAX;
	begin = EQ_INTERNAL_ADDRESS(block);
	at = EQ_INTERNAL_ADDRESS(p);
	if (at < begin)
		return SIZE_MAX;
	if (at - begin >= bytes)
		return SIZE_MAX;
	return EQ_INTERNAL_CAST(size_t, at - begin);
}

/* Inserts `bytes` bytes at byte `at` of `data`, which holds `used` bytes and
 * has room for `bytes` more. The bytes come from `source`, or, when `offset`
 * isn't SIZE_MAX, from `data` itself at that offset before the move. */
static inline void eq_internal_insert_bytes(unsigned char *data, size_t used, size_t at, const void *source,
	size_t offset, size_t bytes)
{
	size_t below;
	memmove(data + at + bytes, data + at, used - at);
	if (offset == SIZE_MAX) {
		memcpy(data + at, source, bytes);
		return;
	}
	/* The source was the block's own bytes: those below `at` stayed where
	 * they were, and those from `at` on moved up by `bytes`. */
	below = offset < at ? at - offset : 0;
	if (below > bytes)
		below = bytes;
	memcpy(data + at, data + offset, below);
	memcpy(data + at + below, data + offset + below + bytes, bytes - below);
}

/* ---- Debug checks ---- */

static inline void eq_internal_index_error(size_t index, size_t count, const char *file, int line)
{
	fprintf(stderr, "eqstd: index %zu is out of range (count %zu) at %s:%d\n", index, count, file, line);
	abort();
}

static inline size_t eq_internal_check_index(size_t index, size_t count, const char *file, int line)
{
	if (index >= count)
		EQ_INDEX_ERROR(index, count, file, line);
	return index;
}

/* ---- Arrays ----
 * The *_at functions work on an EQ_INTERNAL_ARRAY and change it only on
 * success; eqstd.hpp calls them on its own fields. The functions the macros
 * call copy an array in, call one, and copy it back. */

static inline void eq_internal_array_load(const void *array, EQ_INTERNAL_ARRAY *a)
{
	memcpy(a, array, sizeof(*a));
}

static inline void eq_internal_array_store(void *array, const EQ_INTERNAL_ARRAY *a)
{
	memcpy(array, a, sizeof(*a));
}

static inline void eq_internal_array_free_at(EQ_INTERNAL_ARRAY *a, size_t element_size)
{
	if (a->data != EQ_INTERNAL_NULL)
		eq_free(a->allocator, a->data, a->capacity * element_size, EQ_ARRAY_ALIGNMENT(element_size));
	a->data = EQ_INTERNAL_NULL;
	a->count = 0;
	a->capacity = 0;
}

/* Grows the block to hold `needed` elements: exactly, or geometrically. */
static inline EQ_RESULT eq_internal_array_grow_at(EQ_INTERNAL_ARRAY *a, size_t element_size, size_t needed, int exact)
{
	size_t maximum = EQ_INTERNAL_CAST(size_t, PTRDIFF_MAX) / element_size;
	size_t alignment = EQ_ARRAY_ALIGNMENT(element_size);
	size_t capacity;
	void *data;
	EQ_RESULT result;
	if (needed <= a->capacity)
		return EQ_SUCCESS;
	if (needed > maximum)
		return EQ_RESULT_TOO_LARGE;
	result = eq_internal_check_allocator(a->allocator);
	if (result != EQ_SUCCESS)
		return result;
	capacity = exact ? needed : eq_internal_grown_capacity(a->capacity, needed, maximum, 4);
	if (a->data == EQ_INTERNAL_NULL)
		data = eq_allocate(a->allocator, capacity * element_size, alignment);
	else
		data = eq_resize(a->allocator, a->data, a->capacity * element_size, capacity * element_size, alignment);
	if (data == EQ_INTERNAL_NULL)
		return EQ_RESULT_OUT_OF_MEMORY;
	a->data = data;
	a->capacity = capacity;
	return EQ_SUCCESS;
}

static inline EQ_RESULT eq_internal_array_insert_at(EQ_INTERNAL_ARRAY *a, size_t element_size, size_t index,
	const void *items, size_t n)
{
	size_t offset;
	EQ_RESULT result;
	if (index > a->count)
		return EQ_RESULT_OUT_OF_RANGE;
	if (n == 0)
		return EQ_SUCCESS;
	if (items == EQ_INTERNAL_NULL)
		return EQ_RESULT_INVALID_ARGUMENT;
	if (n > SIZE_MAX - a->count)
		return EQ_RESULT_TOO_LARGE;
	/* Appended into room the block has: nothing moves, and the new elements
	 * land past every existing one, so items that are the array's own are
	 * copied from where they are. */
	if (index == a->count && n <= a->capacity - a->count) {
		memcpy(EQ_INTERNAL_CAST(unsigned char *, a->data) + a->count * element_size, items, n * element_size);
		a->count += n;
		return EQ_SUCCESS;
	}
	/* Growing may move the array's own elements: remember where they were. */
	offset = eq_internal_offset_in(a->data, a->count * element_size, items);
	result = eq_internal_array_grow_at(a, element_size, a->count + n, 0);
	if (result != EQ_SUCCESS)
		return result;
	eq_internal_insert_bytes(EQ_INTERNAL_CAST(unsigned char *, a->data), a->count * element_size,
		index * element_size, items, offset, n * element_size);
	a->count += n;
	return EQ_SUCCESS;
}

static inline EQ_RESULT eq_internal_array_resize_at(EQ_INTERNAL_ARRAY *a, size_t element_size, size_t count)
{
	EQ_RESULT result;
	if (count > a->count) {
		result = eq_internal_array_grow_at(a, element_size, count, 1);
		if (result != EQ_SUCCESS)
			return result;
		memset(EQ_INTERNAL_CAST(unsigned char *, a->data) + a->count * element_size, 0,
			(count - a->count) * element_size);
	}
	a->count = count;
	return EQ_SUCCESS;
}

static inline EQ_RESULT eq_internal_array_remove_at(EQ_INTERNAL_ARRAY *a, size_t element_size, size_t index,
	size_t n)
{
	unsigned char *data;
	if (index > a->count)
		return EQ_RESULT_OUT_OF_RANGE;
	if (n > a->count - index)
		return EQ_RESULT_OUT_OF_RANGE;
	if (n == 0)
		return EQ_SUCCESS;
	data = EQ_INTERNAL_CAST(unsigned char *, a->data);
	memmove(data + index * element_size, data + (index + n) * element_size, (a->count - index - n) * element_size);
	a->count -= n;
	return EQ_SUCCESS;
}

static inline EQ_RESULT eq_internal_array_remove_swap_at(EQ_INTERNAL_ARRAY *a, size_t element_size, size_t index)
{
	unsigned char *data;
	if (index >= a->count)
		return EQ_RESULT_OUT_OF_RANGE;
	data = EQ_INTERNAL_CAST(unsigned char *, a->data);
	if (index != a->count - 1)
		memcpy(data + index * element_size, data + (a->count - 1) * element_size, element_size);
	a->count--;
	return EQ_SUCCESS;
}

static inline void eq_internal_array_init(void *array, const EQ_ALLOCATOR *allocator)
{
	EQ_INTERNAL_ARRAY a;
	if (array == EQ_INTERNAL_NULL)
		return;
	a.data = EQ_INTERNAL_NULL;
	a.count = 0;
	a.capacity = 0;
	a.allocator = allocator;
	eq_internal_array_store(array, &a);
}

static inline void eq_internal_array_free(void *array, size_t element_size)
{
	EQ_INTERNAL_ARRAY a;
	if (array == EQ_INTERNAL_NULL)
		return;
	eq_internal_array_load(array, &a);
	eq_internal_array_free_at(&a, element_size);
	eq_internal_array_store(array, &a);
}

static inline EQ_RESULT eq_internal_array_reserve(void *array, size_t element_size, size_t capacity)
{
	EQ_INTERNAL_ARRAY a;
	EQ_RESULT result;
	if (array == EQ_INTERNAL_NULL)
		return EQ_RESULT_INVALID_ARGUMENT;
	eq_internal_array_load(array, &a);
	result = eq_internal_array_grow_at(&a, element_size, capacity, 1);
	if (result == EQ_SUCCESS)
		eq_internal_array_store(array, &a);
	return result;
}

static inline EQ_RESULT eq_internal_array_resize(void *array, size_t element_size, size_t count)
{
	EQ_INTERNAL_ARRAY a;
	EQ_RESULT result;
	if (array == EQ_INTERNAL_NULL)
		return EQ_RESULT_INVALID_ARGUMENT;
	eq_internal_array_load(array, &a);
	result = eq_internal_array_resize_at(&a, element_size, count);
	if (result == EQ_SUCCESS)
		eq_internal_array_store(array, &a);
	return result;
}

static inline EQ_RESULT eq_internal_array_append(void *array, size_t element_size, const void *items, size_t n)
{
	EQ_INTERNAL_ARRAY a;
	EQ_RESULT result;
	if (array == EQ_INTERNAL_NULL)
		return EQ_RESULT_INVALID_ARGUMENT;
	eq_internal_array_load(array, &a);
	result = eq_internal_array_insert_at(&a, element_size, a.count, items, n);
	if (result == EQ_SUCCESS)
		eq_internal_array_store(array, &a);
	return result;
}

static inline EQ_RESULT eq_internal_array_insert(void *array, size_t element_size, size_t index, const void *items,
	size_t n)
{
	EQ_INTERNAL_ARRAY a;
	EQ_RESULT result;
	if (array == EQ_INTERNAL_NULL)
		return EQ_RESULT_INVALID_ARGUMENT;
	eq_internal_array_load(array, &a);
	result = eq_internal_array_insert_at(&a, element_size, index, items, n);
	if (result == EQ_SUCCESS)
		eq_internal_array_store(array, &a);
	return result;
}

static inline EQ_RESULT eq_internal_array_remove(void *array, size_t element_size, size_t index, size_t n)
{
	EQ_INTERNAL_ARRAY a;
	EQ_RESULT result;
	if (array == EQ_INTERNAL_NULL)
		return EQ_RESULT_INVALID_ARGUMENT;
	eq_internal_array_load(array, &a);
	result = eq_internal_array_remove_at(&a, element_size, index, n);
	if (result == EQ_SUCCESS)
		eq_internal_array_store(array, &a);
	return result;
}

static inline EQ_RESULT eq_internal_array_remove_swap(void *array, size_t element_size, size_t index)
{
	EQ_INTERNAL_ARRAY a;
	EQ_RESULT result;
	if (array == EQ_INTERNAL_NULL)
		return EQ_RESULT_INVALID_ARGUMENT;
	eq_internal_array_load(array, &a);
	result = eq_internal_array_remove_swap_at(&a, element_size, index);
	if (result == EQ_SUCCESS)
		eq_internal_array_store(array, &a);
	return result;
}

static inline EQ_RESULT eq_internal_array_clear(void *array, size_t element_size)
{
	EQ_INTERNAL_ARRAY a;
	(void)element_size;
	if (array == EQ_INTERNAL_NULL)
		return EQ_RESULT_INVALID_ARGUMENT;
	eq_internal_array_load(array, &a);
	a.count = 0;
	eq_internal_array_store(array, &a);
	return EQ_SUCCESS;
}

/* ---- Strings ---- */

static inline void eq_string_init(EQ_STRING *s, const EQ_ALLOCATOR *allocator)
{
	if (s == EQ_INTERNAL_NULL)
		return;
	s->data = EQ_INTERNAL_NULL;
	s->length = 0;
	s->capacity = 0;
	s->allocator = allocator;
	s->buffer = EQ_INTERNAL_NULL;
}

static inline void eq_string_init_buffer(EQ_STRING *s, const EQ_ALLOCATOR *allocator, char *buffer,
	size_t buffer_size)
{
	if (s == EQ_INTERNAL_NULL)
		return;
	eq_string_init(s, allocator);
	if (buffer == EQ_INTERNAL_NULL)
		return;
	if (buffer_size == 0)
		return;
	buffer[0] = '\0';
	s->data = buffer;
	s->capacity = buffer_size - 1;
	s->buffer = buffer;
}

/* Whether `data` is a block of the allocator's, rather than nothing or the
 * caller's buffer. */
static inline int eq_internal_string_owns(const EQ_STRING *s)
{
	return s->data != EQ_INTERNAL_NULL && s->data != s->buffer;
}

static inline void eq_string_free(EQ_STRING *s)
{
	if (s == EQ_INTERNAL_NULL)
		return;
	if (eq_internal_string_owns(s))
		eq_free(s->allocator, s->data, s->capacity + 1, 1);
	s->data = EQ_INTERNAL_NULL;
	s->length = 0;
	s->capacity = 0;
	s->buffer = EQ_INTERNAL_NULL;
}

/* Grows the string to hold `needed` bytes of text and the NUL: exactly, or
 * geometrically. The text moves from the caller's buffer to the allocator
 * when it outgrows it. */
static inline EQ_RESULT eq_internal_string_grow(EQ_STRING *s, size_t needed, int exact)
{
	size_t maximum = EQ_INTERNAL_CAST(size_t, PTRDIFF_MAX) - 1;
	size_t capacity;
	char *data;
	EQ_RESULT result;
	if (needed <= s->capacity)
		return EQ_SUCCESS;
	if (needed > maximum)
		return EQ_RESULT_TOO_LARGE;
	result = eq_internal_check_allocator(s->allocator);
	if (result != EQ_SUCCESS)
		return result;
	capacity = exact ? needed : eq_internal_grown_capacity(s->capacity, needed, maximum, 15);
	if (eq_internal_string_owns(s)) {
		data = EQ_INTERNAL_CAST(char *, eq_resize(s->allocator, s->data, s->capacity + 1, capacity + 1, 1));
		if (data == EQ_INTERNAL_NULL)
			return EQ_RESULT_OUT_OF_MEMORY;
	} else {
		data = EQ_INTERNAL_CAST(char *, eq_allocate(s->allocator, capacity + 1, 1));
		if (data == EQ_INTERNAL_NULL)
			return EQ_RESULT_OUT_OF_MEMORY;
		if (s->data != EQ_INTERNAL_NULL)
			memcpy(data, s->data, s->length + 1);
		else
			data[0] = '\0';
	}
	s->data = data;
	s->capacity = capacity;
	return EQ_SUCCESS;
}

static inline EQ_RESULT eq_string_reserve(EQ_STRING *s, size_t capacity)
{
	if (s == EQ_INTERNAL_NULL)
		return EQ_RESULT_INVALID_ARGUMENT;
	return eq_internal_string_grow(s, capacity, 1);
}

static inline EQ_RESULT eq_string_insert(EQ_STRING *s, size_t index, const char *text, size_t length)
{
	size_t offset;
	EQ_RESULT result;
	if (s == EQ_INTERNAL_NULL)
		return EQ_RESULT_INVALID_ARGUMENT;
	if (index > s->length)
		return EQ_RESULT_OUT_OF_RANGE;
	if (length == 0)
		return EQ_SUCCESS;
	if (text == EQ_INTERNAL_NULL)
		return EQ_RESULT_INVALID_ARGUMENT;
	if (length > SIZE_MAX - s->length)
		return EQ_RESULT_TOO_LARGE;
	/* Growing may move the string's own text: remember where it was. */
	offset = eq_internal_offset_in(s->data, s->length, text);
	result = eq_internal_string_grow(s, s->length + length, 0);
	if (result != EQ_SUCCESS)
		return result;
	/* The NUL moves with the text after `index`. */
	eq_internal_insert_bytes(EQ_INTERNAL_CAST(unsigned char *, EQ_INTERNAL_CAST(void *, s->data)), s->length + 1,
		index, text, offset, length);
	s->length += length;
	return EQ_SUCCESS;
}

static inline EQ_RESULT eq_string_append_n(EQ_STRING *s, const char *text, size_t length)
{
	if (s == EQ_INTERNAL_NULL)
		return EQ_RESULT_INVALID_ARGUMENT;
	return eq_string_insert(s, s->length, text, length);
}

static inline EQ_RESULT eq_string_append(EQ_STRING *s, const char *text)
{
	if (s == EQ_INTERNAL_NULL)
		return EQ_RESULT_INVALID_ARGUMENT;
	if (text == EQ_INTERNAL_NULL)
		return EQ_RESULT_INVALID_ARGUMENT;
	return eq_string_insert(s, s->length, text, strlen(text));
}

static inline EQ_RESULT eq_string_remove(EQ_STRING *s, size_t index, size_t length)
{
	if (s == EQ_INTERNAL_NULL)
		return EQ_RESULT_INVALID_ARGUMENT;
	if (index > s->length)
		return EQ_RESULT_OUT_OF_RANGE;
	if (length > s->length - index)
		return EQ_RESULT_OUT_OF_RANGE;
	if (length == 0)
		return EQ_SUCCESS;
	memmove(s->data + index, s->data + index + length, s->length - index - length + 1);
	s->length -= length;
	return EQ_SUCCESS;
}

static inline EQ_RESULT eq_string_append_format_v(EQ_STRING *s, const char *format, va_list arguments)
{
	va_list copy;
	int measured;
	int written;
	size_t length;
	EQ_RESULT result;
	if (s == EQ_INTERNAL_NULL)
		return EQ_RESULT_INVALID_ARGUMENT;
	if (format == EQ_INTERNAL_NULL)
		return EQ_RESULT_INVALID_ARGUMENT;
	va_copy(copy, arguments);
	measured = vsnprintf(EQ_INTERNAL_NULL, 0, format, copy);
	va_end(copy);
	if (measured < 0)
		return EQ_RESULT_FORMAT_FAILED;
	length = EQ_INTERNAL_CAST(size_t, measured);
	if (length == 0)
		return EQ_SUCCESS;
	if (length > SIZE_MAX - s->length)
		return EQ_RESULT_TOO_LARGE;
	result = eq_internal_string_grow(s, s->length + length, 0);
	if (result != EQ_SUCCESS)
		return result;
	va_copy(copy, arguments);
	written = vsnprintf(s->data + s->length, length + 1, format, copy);
	va_end(copy);
	if (written != measured) {
		s->data[s->length] = '\0';
		return EQ_RESULT_FORMAT_FAILED;
	}
	s->length += length;
	return EQ_SUCCESS;
}

static inline EQ_RESULT eq_string_append_format(EQ_STRING *s, const char *format, ...)
{
	va_list arguments;
	EQ_RESULT result;
	if (s == EQ_INTERNAL_NULL)
		return EQ_RESULT_INVALID_ARGUMENT;
	if (format == EQ_INTERNAL_NULL)
		return EQ_RESULT_INVALID_ARGUMENT;
	va_start(arguments, format);
	result = eq_string_append_format_v(s, format, arguments);
	va_end(arguments);
	return result;
}

static inline EQ_RESULT eq_string_clear(EQ_STRING *s)
{
	if (s == EQ_INTERNAL_NULL)
		return EQ_RESULT_INVALID_ARGUMENT;
	s->length = 0;
	if (s->data != EQ_INTERNAL_NULL)
		s->data[0] = '\0';
	return EQ_SUCCESS;
}

static inline const char *eq_string_c_str(const EQ_STRING *s)
{
	if (s == EQ_INTERNAL_NULL)
		return "";
	if (s->data == EQ_INTERNAL_NULL)
		return "";
	return s->data;
}

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* EQSTD_H */
