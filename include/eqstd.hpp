/**
 * @file eqstd.hpp
 * @brief eqstd's C++ half: eq::array, eq::vector and eq::string.
 *
 * Built over the C half (`eqstd.h`), not beside it: the same allocator, the
 * same array layout and growth, the same string growth. C++20 and later.
 * Includes only the C and C++ standard libraries.
 *
 * ## Conventions
 *
 * - **Nothing throws.** No member of eqstd throws or calls anything that
 *   does, and growth that fails returns an EQ_RESULT, marked `[[nodiscard]]`.
 *   A call that fails leaves the container as it was. eqstd doesn't catch:
 *   an element type's constructor that throws is the element type's, and
 *   eq::vector requires a non-throwing move constructor and destructor.
 * - **Move-only.** Copying allocates, which can fail, so there are no copy
 *   constructors; `copy_from()` copies and returns its result. A move never
 *   allocates. The moved-from container is empty, on the allocator it had.
 * - **The allocator** is the C half's EQ_ALLOCATOR, null for the system heap,
 *   and must outlive the container. A container is given it when made and
 *   keeps it; a move takes the source's along.
 * - **Threading** is the C half's: one caller at a time per container.
 * - **Debug checks.** When EQ_DEBUG is nonzero, `operator[]` checks its
 *   index and calls EQ_INDEX_ERROR() for one out of range.
 */
#ifndef EQSTD_HPP
#define EQSTD_HPP

#include "eqstd.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>

namespace eq {

/**
 * @brief A growable array of plain data, with `EQ_ARRAY(T)`'s layout.
 *
 * Its fields are `EQ_ARRAY(T)`'s, in order, and public: read `data` and
 * `count` freely, and leave the fields to the members to change. C code is
 * handed `&a` as an `EQ_ARRAY(T) *` (as_c()), and an `EQ_ARRAY(T)` a C struct
 * holds is used here through from_c(); the eq_array_* macros also take a
 * pointer to either. Elements are copied as bytes, so `T` must be trivially
 * copyable and standard layout (a static_assert).
 *
 * The array owns its block and frees it when destroyed. One shared with C
 * code through from_c() is owned by whoever frees it (eq_array_free()), and
 * the reference from_c() returns is never destroyed.
 */
template <typename T>
struct array {
	static_assert(std::is_trivially_copyable_v<T> && std::is_standard_layout_v<T>,
		"eq::array holds plain data (trivially copyable, standard layout); use eq::vector");

	/** The elements; nullptr while `capacity` is 0. */
	T *data = nullptr;
	/** How many elements there are. */
	size_t count = 0;
	/** How many the block has room for. */
	size_t capacity = 0;
	/** Where the block comes from; nullptr for the system heap. */
	const EQ_ALLOCATOR *allocator = nullptr;

	/** @brief An empty array on the system heap. */
	array() noexcept = default;
	/** @brief An empty array on `allocator`. Allocates nothing. */
	explicit array(const EQ_ALLOCATOR *allocator) noexcept;
	array(const array &) = delete;
	array &operator=(const array &) = delete;
	/** @brief Takes `other`'s elements and allocator; `other` is left empty. */
	array(array &&other) noexcept;
	/** @brief Frees this array's block, then takes `other`'s. */
	array &operator=(array &&other) noexcept;
	~array();

	/** @brief Appends a copy of `item`, which may be one of this array's own. */
	[[nodiscard]] EQ_RESULT push(const T &item) noexcept;
	/** @brief Appends `n` elements copied from `items` (may be this array's). */
	[[nodiscard]] EQ_RESULT append(const T *items, size_t n) noexcept;
	/** @brief Appends a span's elements (may be this array's). */
	[[nodiscard]] EQ_RESULT append(std::span<const T> items) noexcept;
	/** @brief Inserts a copy of `item` before element `index`. */
	[[nodiscard]] EQ_RESULT insert(size_t index, const T &item) noexcept;
	/** @brief Inserts `n` elements copied from `items` before element `index`. */
	[[nodiscard]] EQ_RESULT insert(size_t index, const T *items, size_t n) noexcept;
	/** @brief Removes `n` elements from `index` on, keeping the order. */
	[[nodiscard]] EQ_RESULT remove(size_t index, size_t n = 1) noexcept;
	/** @brief Removes element `index`, moving the last into its place. */
	[[nodiscard]] EQ_RESULT remove_swap(size_t index) noexcept;
	/** @brief Sets the count; new elements are zero bytes. */
	[[nodiscard]] EQ_RESULT resize(size_t count) noexcept;
	/** @brief Makes room for `capacity` elements. */
	[[nodiscard]] EQ_RESULT reserve(size_t capacity) noexcept;
	/** @brief Makes this array a copy of `other`'s elements, keeping its own
	 * allocator. */
	[[nodiscard]] EQ_RESULT copy_from(const array &other) noexcept;
	/** @brief Removes every element, keeping the block. */
	void clear() noexcept;
	/** @brief Frees the block, keeping the allocator. */
	void free() noexcept;

	/** @brief Element `index`; checked when EQ_DEBUG is nonzero. */
	T &operator[](size_t index) noexcept;
	/** @copydoc operator[] */
	const T &operator[](size_t index) const noexcept;
	size_t size() const noexcept { return count; }
	bool empty() const noexcept { return count == 0; }
	T *begin() noexcept { return data; }
	T *end() noexcept { return data + count; }
	const T *begin() const noexcept { return data; }
	const T *end() const noexcept { return data + count; }
	/** @brief The elements as a span, valid until the array next grows. */
	std::span<T> span() noexcept { return std::span<T>(data, count); }
	/** @copydoc span */
	std::span<const T> span() const noexcept { return std::span<const T>(data, count); }

	/** @brief An `EQ_ARRAY(T)` a C struct holds, as an eq::array<T>. Never
	 * destroyed through the reference. */
	template <typename C>
	static array &from_c(C &c) noexcept;
	/** @brief This array, as the `EQ_ARRAY(T)` typedef `C` C code uses. */
	template <typename C>
	C &as_c() noexcept;
};

/**
 * @brief A growable array of any type, moved and destroyed as C++ does.
 *
 * For elements with constructors and destructors; plain data goes in
 * eq::array. Its fields are public like eq::array's, to read only. `T`'s move
 * constructor and destructor must not throw (a static_assert). Growing
 * allocates a new block and moves the elements into it.
 */
template <typename T>
struct vector {
	static_assert(std::is_nothrow_move_constructible_v<T> && std::is_nothrow_destructible_v<T>,
		"eq::vector's elements must move and destroy without throwing");

	/** The elements; nullptr while `capacity` is 0. */
	T *data = nullptr;
	/** How many elements there are. */
	size_t count = 0;
	/** How many the block has room for. */
	size_t capacity = 0;
	/** Where the block comes from; nullptr for the system heap. */
	const EQ_ALLOCATOR *allocator = nullptr;

	vector() noexcept = default;
	explicit vector(const EQ_ALLOCATOR *allocator) noexcept;
	vector(const vector &) = delete;
	vector &operator=(const vector &) = delete;
	vector(vector &&other) noexcept;
	vector &operator=(vector &&other) noexcept;
	~vector();

	/** @brief Constructs an element at the end from `arguments`, which may
	 * refer to this vector's own elements. */
	template <typename... Arguments>
	[[nodiscard]] EQ_RESULT emplace(Arguments &&...arguments);
	/** @brief Appends a copy of `item`. */
	[[nodiscard]] EQ_RESULT push(const T &item);
	/** @brief Appends `item`, moved. */
	[[nodiscard]] EQ_RESULT push(T &&item) noexcept;
	/** @brief Inserts `item`, moved, before element `index`. */
	[[nodiscard]] EQ_RESULT insert(size_t index, T &&item) noexcept;
	/** @brief Inserts a copy of `item` before element `index`. */
	[[nodiscard]] EQ_RESULT insert(size_t index, const T &item);
	/** @brief Removes `n` elements from `index` on, keeping the order. */
	[[nodiscard]] EQ_RESULT remove(size_t index, size_t n = 1) noexcept;
	/** @brief Removes element `index`, moving the last into its place. */
	[[nodiscard]] EQ_RESULT remove_swap(size_t index) noexcept;
	/** @brief Sets the count; new elements are value-initialized (`T()`). */
	[[nodiscard]] EQ_RESULT resize(size_t count);
	/** @brief Makes room for `capacity` elements. */
	[[nodiscard]] EQ_RESULT reserve(size_t capacity) noexcept;
	/** @brief Makes this vector a copy of `other`'s elements, keeping its own
	 * allocator; on failure it is as it was. */
	[[nodiscard]] EQ_RESULT copy_from(const vector &other);
	/** @brief Destroys every element, keeping the block. */
	void clear() noexcept;
	/** @brief Destroys every element and frees the block. */
	void free() noexcept;

	T &operator[](size_t index) noexcept;
	const T &operator[](size_t index) const noexcept;
	size_t size() const noexcept { return count; }
	bool empty() const noexcept { return count == 0; }
	T *begin() noexcept { return data; }
	T *end() noexcept { return data + count; }
	const T *begin() const noexcept { return data; }
	const T *end() const noexcept { return data + count; }
	std::span<T> span() noexcept { return std::span<T>(data, count); }
	std::span<const T> span() const noexcept { return std::span<const T>(data, count); }

private:
	EQ_RESULT allocate_block(size_t needed, bool exact, T **block, size_t *block_capacity) noexcept;
	void adopt_block(T *block, size_t block_capacity) noexcept;
	EQ_RESULT grow(size_t needed, bool exact) noexcept;
	size_t own_index(const T *item) const noexcept;
	void place(size_t index, T &&value) noexcept;
};

/**
 * @brief Owned UTF-8 text, NUL-terminated, with a short-string buffer.
 *
 * Text up to `local_capacity` bytes lives inside the object and allocates
 * nothing; longer text is on the allocator. Its growth is EQ_STRING's: the
 * object holds an EQ_STRING whose caller's buffer (eq_string_init_buffer())
 * is the short-string buffer, so builder() hands it to C code as it is.
 * eqstd treats the text as bytes and never checks the encoding.
 */
class string {
public:
	/** @brief The object's size: one 64-byte line on a 64-bit platform. */
	static constexpr size_t object_size = 64;
	/** @brief Bytes of text the object holds without allocating. */
	static constexpr size_t local_capacity = object_size - sizeof(EQ_STRING) - 1;

	string() noexcept;
	explicit string(const EQ_ALLOCATOR *allocator) noexcept;
	string(const string &) = delete;
	string &operator=(const string &) = delete;
	string(string &&other) noexcept;
	string &operator=(string &&other) noexcept;
	~string();

	/** @brief Replaces the text with `text`, which may be part of this one. */
	[[nodiscard]] EQ_RESULT assign(std::string_view text) noexcept;
	/** @brief Appends `text`, which may be part of this one. */
	[[nodiscard]] EQ_RESULT append(std::string_view text) noexcept;
	/** @brief Appends one byte. */
	[[nodiscard]] EQ_RESULT append(char c) noexcept;
	/** @brief Appends text formatted as `printf` does; no argument may point
	 * into this string. */
	[[nodiscard]] EQ_RESULT append_format(const char *format, ...) noexcept EQ_INTERNAL_PRINTF(2, 3);
	/** @brief Inserts `text`, which may be part of this one, before byte
	 * `index`. */
	[[nodiscard]] EQ_RESULT insert(size_t index, std::string_view text) noexcept;
	/** @brief Removes `n` bytes from byte `index` on. */
	[[nodiscard]] EQ_RESULT remove(size_t index, size_t n) noexcept;
	/** @brief Sets the length, filling new bytes with `fill`. */
	[[nodiscard]] EQ_RESULT resize(size_t length, char fill = '\0') noexcept;
	/** @brief Makes room for `capacity` bytes of text. */
	[[nodiscard]] EQ_RESULT reserve(size_t capacity) noexcept;
	/** @brief Makes this string a copy of `other`'s text, keeping its own
	 * allocator. */
	[[nodiscard]] EQ_RESULT copy_from(const string &other) noexcept;
	/** @brief Empties the text, keeping the memory. */
	void clear() noexcept;

	/** @brief The text, NUL-terminated; never null. */
	const char *c_str() const noexcept;
	const char *data() const noexcept { return c_str(); }
	/** @brief The text, writable; never null. Valid until the string grows. */
	char *data() noexcept;
	size_t size() const noexcept { return s.length; }
	bool empty() const noexcept { return s.length == 0; }
	size_t capacity() const noexcept { return s.capacity; }
	const EQ_ALLOCATOR *allocator() const noexcept { return s.allocator; }
	/** @brief The text as a view, valid until the string next changes. */
	std::string_view view() const noexcept { return std::string_view(c_str(), s.length); }
	operator std::string_view() const noexcept { return view(); }
	char &operator[](size_t index) noexcept;
	const char &operator[](size_t index) const noexcept;
	char *begin() noexcept { return data(); }
	char *end() noexcept { return data() + s.length; }
	const char *begin() const noexcept { return c_str(); }
	const char *end() const noexcept { return c_str() + s.length; }

	/** @brief The EQ_STRING underneath, for C code; it keeps working on the
	 * short-string buffer. */
	EQ_STRING &builder() noexcept { return s; }

	friend bool operator==(const string &a, std::string_view b) noexcept { return a.view() == b; }

private:
	EQ_STRING s;
	char local[local_capacity + 1];

	void take(string &other) noexcept;
};

static_assert(sizeof(string) == string::object_size, "eq::string is one 64-byte line");

/* ==== Implementation ====================================================== */

// ---- eq::array ----
// Each member hands the C half's *_at functions an EQ_INTERNAL_ARRAY made from
// the fields, and takes the fields back: the same code as the C macros.

template <typename T>
array<T>::array(const EQ_ALLOCATOR *allocator_) noexcept : allocator(allocator_)
{
}

template <typename T>
array<T>::array(array &&other) noexcept
	: data(other.data), count(other.count), capacity(other.capacity), allocator(other.allocator)
{
	other.data = nullptr;
	other.count = 0;
	other.capacity = 0;
}

template <typename T>
array<T> &array<T>::operator=(array &&other) noexcept
{
	if (this != &other) {
		free();
		data = other.data;
		count = other.count;
		capacity = other.capacity;
		allocator = other.allocator;
		other.data = nullptr;
		other.count = 0;
		other.capacity = 0;
	}
	return *this;
}

template <typename T>
array<T>::~array()
{
	static_assert(sizeof(array) == sizeof(EQ_INTERNAL_ARRAY), "eq::array has EQ_ARRAY's layout");
	free();
}

namespace detail {
template <typename T>
EQ_INTERNAL_ARRAY raw(const array<T> &a) noexcept
{
	EQ_INTERNAL_ARRAY r;
	r.data = static_cast<void *>(a.data);
	r.count = a.count;
	r.capacity = a.capacity;
	r.allocator = a.allocator;
	return r;
}
template <typename T>
EQ_RESULT store(array<T> &a, const EQ_INTERNAL_ARRAY &r, EQ_RESULT result) noexcept
{
	a.data = static_cast<T *>(r.data);
	a.count = r.count;
	a.capacity = r.capacity;
	return result;
}
} // namespace detail

template <typename T>
EQ_RESULT array<T>::push(const T &item) noexcept
{
	return insert(count, &item, 1);
}

template <typename T>
EQ_RESULT array<T>::append(const T *items, size_t n) noexcept
{
	return insert(count, items, n);
}

template <typename T>
EQ_RESULT array<T>::append(std::span<const T> items) noexcept
{
	return insert(count, items.data(), items.size());
}

template <typename T>
EQ_RESULT array<T>::insert(size_t index, const T &item) noexcept
{
	return insert(index, &item, 1);
}

template <typename T>
EQ_RESULT array<T>::insert(size_t index, const T *items, size_t n) noexcept
{
	EQ_INTERNAL_ARRAY r = detail::raw(*this);
	return detail::store(*this, r, eq_internal_array_insert_at(&r, sizeof(T), index, items, n));
}

template <typename T>
EQ_RESULT array<T>::remove(size_t index, size_t n) noexcept
{
	EQ_INTERNAL_ARRAY r = detail::raw(*this);
	return detail::store(*this, r, eq_internal_array_remove_at(&r, sizeof(T), index, n));
}

template <typename T>
EQ_RESULT array<T>::remove_swap(size_t index) noexcept
{
	EQ_INTERNAL_ARRAY r = detail::raw(*this);
	return detail::store(*this, r, eq_internal_array_remove_swap_at(&r, sizeof(T), index));
}

template <typename T>
EQ_RESULT array<T>::resize(size_t new_count) noexcept
{
	EQ_INTERNAL_ARRAY r = detail::raw(*this);
	return detail::store(*this, r, eq_internal_array_resize_at(&r, sizeof(T), new_count));
}

template <typename T>
EQ_RESULT array<T>::reserve(size_t new_capacity) noexcept
{
	EQ_INTERNAL_ARRAY r = detail::raw(*this);
	return detail::store(*this, r, eq_internal_array_grow_at(&r, sizeof(T), new_capacity, 1));
}

template <typename T>
EQ_RESULT array<T>::copy_from(const array &other) noexcept
{
	if (&other == this)
		return EQ_SUCCESS;
	EQ_RESULT result = reserve(other.count);
	if (result != EQ_SUCCESS)
		return result;
	if (other.count != 0)
		std::memcpy(static_cast<void *>(data), static_cast<const void *>(other.data), other.count * sizeof(T));
	count = other.count;
	return EQ_SUCCESS;
}

template <typename T>
void array<T>::clear() noexcept
{
	count = 0;
}

template <typename T>
void array<T>::free() noexcept
{
	EQ_INTERNAL_ARRAY r = detail::raw(*this);
	eq_internal_array_free_at(&r, sizeof(T));
	detail::store(*this, r, EQ_SUCCESS);
}

template <typename T>
T &array<T>::operator[](size_t index) noexcept
{
#if EQ_DEBUG
	return data[eq_internal_check_index(index, count, __FILE__, __LINE__)];
#else
	return data[index];
#endif
}

template <typename T>
const T &array<T>::operator[](size_t index) const noexcept
{
#if EQ_DEBUG
	return data[eq_internal_check_index(index, count, __FILE__, __LINE__)];
#else
	return data[index];
#endif
}

template <typename T>
template <typename C>
array<T> &array<T>::from_c(C &c) noexcept
{
	static_assert(std::is_same_v<decltype(c.data), T *>, "from_c takes an EQ_ARRAY(T) of this array's T");
	static_assert(sizeof(C) == sizeof(array), "from_c takes an EQ_ARRAY(T)");
	static_assert(offsetof(C, count) == offsetof(array, count), "from_c takes an EQ_ARRAY(T)");
	static_assert(offsetof(C, capacity) == offsetof(array, capacity), "from_c takes an EQ_ARRAY(T)");
	static_assert(offsetof(C, allocator) == offsetof(array, allocator), "from_c takes an EQ_ARRAY(T)");
	return *reinterpret_cast<array *>(&c);
}

template <typename T>
template <typename C>
C &array<T>::as_c() noexcept
{
	static_assert(std::is_same_v<decltype(std::declval<C &>().data), T *>, "as_c gives an EQ_ARRAY(T) of this T");
	static_assert(sizeof(C) == sizeof(array), "as_c gives an EQ_ARRAY(T)");
	static_assert(offsetof(C, count) == offsetof(array, count), "as_c gives an EQ_ARRAY(T)");
	static_assert(offsetof(C, capacity) == offsetof(array, capacity), "as_c gives an EQ_ARRAY(T)");
	static_assert(offsetof(C, allocator) == offsetof(array, allocator), "as_c gives an EQ_ARRAY(T)");
	return *reinterpret_cast<C *>(this);
}

// ---- eq::vector ----

template <typename T>
vector<T>::vector(const EQ_ALLOCATOR *allocator_) noexcept : allocator(allocator_)
{
}

template <typename T>
vector<T>::vector(vector &&other) noexcept
	: data(other.data), count(other.count), capacity(other.capacity), allocator(other.allocator)
{
	other.data = nullptr;
	other.count = 0;
	other.capacity = 0;
}

template <typename T>
vector<T> &vector<T>::operator=(vector &&other) noexcept
{
	if (this != &other) {
		free();
		data = other.data;
		count = other.count;
		capacity = other.capacity;
		allocator = other.allocator;
		other.data = nullptr;
		other.count = 0;
		other.capacity = 0;
	}
	return *this;
}

template <typename T>
vector<T>::~vector()
{
	free();
}

template <typename T>
EQ_RESULT vector<T>::allocate_block(size_t needed, bool exact, T **block, size_t *block_capacity) noexcept
{
	const size_t maximum = static_cast<size_t>(PTRDIFF_MAX) / sizeof(T);
	if (needed > maximum)
		return EQ_RESULT_TOO_LARGE;
	EQ_RESULT result = eq_internal_check_allocator(allocator);
	if (result != EQ_SUCCESS)
		return result;
	size_t grown = exact ? needed : eq_internal_grown_capacity(capacity, needed, maximum, 4);
	void *memory = eq_allocate(allocator, grown * sizeof(T), alignof(T));
	if (memory == nullptr)
		return EQ_RESULT_OUT_OF_MEMORY;
	*block = static_cast<T *>(memory);
	*block_capacity = grown;
	return EQ_SUCCESS;
}

template <typename T>
void vector<T>::adopt_block(T *block, size_t block_capacity) noexcept
{
	for (size_t i = 0; i < count; i++) {
		::new (static_cast<void *>(block + i)) T(std::move(data[i]));
		data[i].~T();
	}
	if (data != nullptr)
		eq_free(allocator, data, capacity * sizeof(T), alignof(T));
	data = block;
	capacity = block_capacity;
}

template <typename T>
EQ_RESULT vector<T>::grow(size_t needed, bool exact) noexcept
{
	if (needed <= capacity)
		return EQ_SUCCESS;
	T *block = nullptr;
	size_t block_capacity = 0;
	EQ_RESULT result = allocate_block(needed, exact, &block, &block_capacity);
	if (result != EQ_SUCCESS)
		return result;
	adopt_block(block, block_capacity);
	return EQ_SUCCESS;
}

template <typename T>
size_t vector<T>::own_index(const T *item) const noexcept
{
	size_t offset = eq_internal_offset_in(data, count * sizeof(T), item);
	return offset == SIZE_MAX ? SIZE_MAX : offset / sizeof(T);
}

template <typename T>
template <typename... Arguments>
EQ_RESULT vector<T>::emplace(Arguments &&...arguments)
{
	if (count < capacity) {
		::new (static_cast<void *>(data + count)) T(std::forward<Arguments>(arguments)...);
		count++;
		return EQ_SUCCESS;
	}
	if (count == SIZE_MAX)
		return EQ_RESULT_TOO_LARGE;
	// The new element is made in the new block first, while the arguments may
	// still refer to elements of the old one.
	T *block = nullptr;
	size_t block_capacity = 0;
	EQ_RESULT result = allocate_block(count + 1, false, &block, &block_capacity);
	if (result != EQ_SUCCESS)
		return result;
	::new (static_cast<void *>(block + count)) T(std::forward<Arguments>(arguments)...);
	adopt_block(block, block_capacity);
	count++;
	return EQ_SUCCESS;
}

template <typename T>
EQ_RESULT vector<T>::push(const T &item)
{
	return emplace(item);
}

template <typename T>
EQ_RESULT vector<T>::push(T &&item) noexcept
{
	return emplace(std::move(item));
}

template <typename T>
void vector<T>::place(size_t index, T &&value) noexcept
{
	if (index == count) {
		::new (static_cast<void *>(data + count)) T(std::move(value));
	} else {
		::new (static_cast<void *>(data + count)) T(std::move(data[count - 1]));
		for (size_t i = count - 1; i > index; i--)
			data[i] = std::move(data[i - 1]);
		data[index] = std::move(value);
	}
	count++;
}

template <typename T>
EQ_RESULT vector<T>::insert(size_t index, T &&item) noexcept
{
	if (index > count)
		return EQ_RESULT_OUT_OF_RANGE;
	if (count == SIZE_MAX)
		return EQ_RESULT_TOO_LARGE;
	// Growing moves the elements; an item that is one of them is found again
	// by its index. Nothing is taken from it until growing has succeeded.
	size_t own = own_index(&item);
	EQ_RESULT result = grow(count + 1, false);
	if (result != EQ_SUCCESS)
		return result;
	T value(std::move(own == SIZE_MAX ? item : data[own]));
	place(index, std::move(value));
	return EQ_SUCCESS;
}

template <typename T>
EQ_RESULT vector<T>::insert(size_t index, const T &item)
{
	if (index > count)
		return EQ_RESULT_OUT_OF_RANGE;
	if (count == SIZE_MAX)
		return EQ_RESULT_TOO_LARGE;
	size_t own = own_index(&item);
	EQ_RESULT result = grow(count + 1, false);
	if (result != EQ_SUCCESS)
		return result;
	T value(own == SIZE_MAX ? item : static_cast<const T &>(data[own]));
	place(index, std::move(value));
	return EQ_SUCCESS;
}

template <typename T>
EQ_RESULT vector<T>::remove(size_t index, size_t n) noexcept
{
	if (index > count)
		return EQ_RESULT_OUT_OF_RANGE;
	if (n > count - index)
		return EQ_RESULT_OUT_OF_RANGE;
	for (size_t i = index; i + n < count; i++)
		data[i] = std::move(data[i + n]);
	for (size_t i = count - n; i < count; i++)
		data[i].~T();
	count -= n;
	return EQ_SUCCESS;
}

template <typename T>
EQ_RESULT vector<T>::remove_swap(size_t index) noexcept
{
	if (index >= count)
		return EQ_RESULT_OUT_OF_RANGE;
	if (index != count - 1)
		data[index] = std::move(data[count - 1]);
	data[count - 1].~T();
	count--;
	return EQ_SUCCESS;
}

template <typename T>
EQ_RESULT vector<T>::resize(size_t new_count)
{
	if (new_count <= count) {
		for (size_t i = new_count; i < count; i++)
			data[i].~T();
		count = new_count;
		return EQ_SUCCESS;
	}
	EQ_RESULT result = grow(new_count, true);
	if (result != EQ_SUCCESS)
		return result;
	for (size_t i = count; i < new_count; i++)
		::new (static_cast<void *>(data + i)) T();
	count = new_count;
	return EQ_SUCCESS;
}

template <typename T>
EQ_RESULT vector<T>::reserve(size_t new_capacity) noexcept
{
	return grow(new_capacity, true);
}

template <typename T>
EQ_RESULT vector<T>::copy_from(const vector &other)
{
	if (&other == this)
		return EQ_SUCCESS;
	if (other.count > capacity) {
		// Copied into a new block first, so a failure leaves this vector as it was.
		T *block = nullptr;
		size_t block_capacity = 0;
		EQ_RESULT result = allocate_block(other.count, true, &block, &block_capacity);
		if (result != EQ_SUCCESS)
			return result;
		for (size_t i = 0; i < other.count; i++)
			::new (static_cast<void *>(block + i)) T(other.data[i]);
		free();
		data = block;
		capacity = block_capacity;
		count = other.count;
		return EQ_SUCCESS;
	}
	clear();
	for (size_t i = 0; i < other.count; i++)
		::new (static_cast<void *>(data + i)) T(other.data[i]);
	count = other.count;
	return EQ_SUCCESS;
}

template <typename T>
void vector<T>::clear() noexcept
{
	for (size_t i = 0; i < count; i++)
		data[i].~T();
	count = 0;
}

template <typename T>
void vector<T>::free() noexcept
{
	clear();
	if (data != nullptr)
		eq_free(allocator, data, capacity * sizeof(T), alignof(T));
	data = nullptr;
	capacity = 0;
}

template <typename T>
T &vector<T>::operator[](size_t index) noexcept
{
#if EQ_DEBUG
	return data[eq_internal_check_index(index, count, __FILE__, __LINE__)];
#else
	return data[index];
#endif
}

template <typename T>
const T &vector<T>::operator[](size_t index) const noexcept
{
#if EQ_DEBUG
	return data[eq_internal_check_index(index, count, __FILE__, __LINE__)];
#else
	return data[index];
#endif
}

// ---- eq::string ----

inline string::string() noexcept : string(nullptr)
{
}

inline string::string(const EQ_ALLOCATOR *allocator_) noexcept : s(), local()
{
	eq_string_init_buffer(&s, allocator_, local, sizeof(local));
}

inline string::~string()
{
	eq_string_free(&s);
}

// Takes other's text into this string, which has just been made empty on its
// own short-string buffer, and leaves other empty on its own.
inline void string::take(string &other) noexcept
{
	s.allocator = other.s.allocator;
	if (other.s.data == other.local) {
		std::memcpy(local, other.local, other.s.length + 1);
		s.length = other.s.length;
	} else if (other.s.data != nullptr) {
		// The allocator's block, or a buffer C code gave the builder: the
		// pointer moves, and so does whose buffer it is.
		s.data = other.s.data;
		s.length = other.s.length;
		s.capacity = other.s.capacity;
		s.buffer = other.s.buffer == other.local ? local : other.s.buffer;
	}
	eq_string_init_buffer(&other.s, other.s.allocator, other.local, sizeof(other.local));
}

inline string::string(string &&other) noexcept : string(other.s.allocator)
{
	take(other);
}

inline string &string::operator=(string &&other) noexcept
{
	if (this != &other) {
		eq_string_free(&s);
		eq_string_init_buffer(&s, other.s.allocator, local, sizeof(local));
		take(other);
	}
	return *this;
}

inline EQ_RESULT string::assign(std::string_view text) noexcept
{
	size_t offset = eq_internal_offset_in(s.data, s.length, text.data());
	if (offset != SIZE_MAX) {
		// Part of this string's own text, which is never longer than it.
		std::memmove(s.data, s.data + offset, text.size());
		s.length = text.size();
		s.data[s.length] = '\0';
		return EQ_SUCCESS;
	}
	EQ_RESULT result = eq_string_reserve(&s, text.size());
	if (result != EQ_SUCCESS)
		return result;
	if (s.data == nullptr)
		return EQ_SUCCESS; // nothing ever stored, and text is empty
	if (!text.empty())
		std::memcpy(s.data, text.data(), text.size());
	s.length = text.size();
	s.data[s.length] = '\0';
	return EQ_SUCCESS;
}

inline EQ_RESULT string::append(std::string_view text) noexcept
{
	return eq_string_append_n(&s, text.data(), text.size());
}

inline EQ_RESULT string::append(char c) noexcept
{
	return eq_string_append_n(&s, &c, 1);
}

inline EQ_RESULT string::append_format(const char *format, ...) noexcept
{
	va_list arguments;
	va_start(arguments, format);
	EQ_RESULT result = eq_string_append_format_v(&s, format, arguments);
	va_end(arguments);
	return result;
}

inline EQ_RESULT string::insert(size_t index, std::string_view text) noexcept
{
	return eq_string_insert(&s, index, text.data(), text.size());
}

inline EQ_RESULT string::remove(size_t index, size_t n) noexcept
{
	return eq_string_remove(&s, index, n);
}

inline EQ_RESULT string::resize(size_t length, char fill) noexcept
{
	if (length <= s.length) {
		if (s.data != nullptr) {
			s.length = length;
			s.data[length] = '\0';
		}
		return EQ_SUCCESS;
	}
	EQ_RESULT result = eq_internal_string_grow(&s, length, 0);
	if (result != EQ_SUCCESS)
		return result;
	std::memset(s.data + s.length, fill, length - s.length);
	s.length = length;
	s.data[length] = '\0';
	return EQ_SUCCESS;
}

inline EQ_RESULT string::reserve(size_t new_capacity) noexcept
{
	return eq_string_reserve(&s, new_capacity);
}

inline EQ_RESULT string::copy_from(const string &other) noexcept
{
	if (&other == this)
		return EQ_SUCCESS;
	return assign(other.view());
}

inline void string::clear() noexcept
{
	(void)eq_string_clear(&s);
}

inline const char *string::c_str() const noexcept
{
	return eq_string_c_str(&s);
}

inline char *string::data() noexcept
{
	// Only C code freeing the builder leaves it without text; it is given
	// its short-string buffer back.
	if (s.data == nullptr)
		eq_string_init_buffer(&s, s.allocator, local, sizeof(local));
	return s.data;
}

inline char &string::operator[](size_t index) noexcept
{
#if EQ_DEBUG
	return s.data[eq_internal_check_index(index, s.length, __FILE__, __LINE__)];
#else
	return s.data[index];
#endif
}

inline const char &string::operator[](size_t index) const noexcept
{
#if EQ_DEBUG
	return s.data[eq_internal_check_index(index, s.length, __FILE__, __LINE__)];
#else
	return s.data[index];
#endif
}

} // namespace eq

#endif // EQSTD_HPP
