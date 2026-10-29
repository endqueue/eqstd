// eqstd's C++ half, black-box: eq::array, eq::vector and eq::string by random
// operations against std::vector and std::string, the C macros from C++, the
// layout eq::array shares with EQ_ARRAY, and out of memory at every
// allocation in turn. Included unchanged by eqstd_objcxx_test.mm.

#ifndef EQSTD_TEST_LANGUAGE
#define EQSTD_TEST_LANGUAGE "C++"
#endif

#include "eqstd.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

static int failures = 0;
static const char *current_test = "";

#define CHECK(condition)                                                                                               \
	do {                                                                                                               \
		if (!(condition)) {                                                                                            \
			std::printf("FAIL %s (%s:%d): %s\n", current_test, __FILE__, __LINE__, #condition);                        \
			failures++;                                                                                                \
		}                                                                                                              \
	} while (0)

// The same sequence on every platform and every run.
static uint32_t random_state = 0x2545F491u;
static uint32_t next_random()
{
	random_state ^= random_state << 13;
	random_state ^= random_state >> 17;
	random_state ^= random_state << 5;
	return random_state;
}
static size_t random_below(size_t n)
{
	return n == 0 ? 0 : static_cast<size_t>(next_random()) % n;
}

// ---- A counting allocator ---------------------------------------------------
// Every block with its size and alignment; checks each free and resize against
// them; fails the call numbered fail_at. New memory is filled with 0xCD.

struct counting {
	struct block {
		void *raw;
		size_t size;
		size_t alignment;
	};
	std::unordered_map<void *, block> live;
	size_t calls = 0;
	size_t fail_at = SIZE_MAX;
	size_t failed = 0;
	size_t errors = 0;

	void reset()
	{
		CHECK(live.empty());
		live.clear();
		calls = 0;
		fail_at = SIZE_MAX;
		failed = 0;
		errors = 0;
	}
	bool should_fail()
	{
		if (calls++ == fail_at) {
			failed++;
			return true;
		}
		return false;
	}
	static void *raw_allocate(size_t size, size_t alignment, void **raw)
	{
		unsigned char *bytes = static_cast<unsigned char *>(std::malloc(size + alignment));
		if (bytes == nullptr)
			return nullptr;
		size_t offset = (alignment - reinterpret_cast<uintptr_t>(bytes) % alignment) % alignment;
		*raw = bytes;
		std::memset(bytes + offset, 0xCD, size);
		return bytes + offset;
	}
	static void *allocate(void *user_data, size_t size, size_t alignment)
	{
		counting *c = static_cast<counting *>(user_data);
		if (size == 0 || alignment == 0 || (alignment & (alignment - 1)) != 0) {
			c->errors++;
			return nullptr;
		}
		if (c->should_fail())
			return nullptr;
		void *raw = nullptr;
		void *memory = raw_allocate(size, alignment, &raw);
		if (memory != nullptr)
			c->live[memory] = block{raw, size, alignment};
		return memory;
	}
	static void free(void *user_data, void *memory, size_t size, size_t alignment)
	{
		counting *c = static_cast<counting *>(user_data);
		auto found = c->live.find(memory);
		if (found == c->live.end()) {
			std::printf("counting::free: a block this allocator didn't give\n");
			c->errors++;
			return;
		}
		if (found->second.size != size || found->second.alignment != alignment) {
			std::printf("counting::free: freed with %zu/%zu, block has %zu/%zu\n", size, alignment, found->second.size,
				found->second.alignment);
			c->errors++;
		}
		std::free(found->second.raw);
		c->live.erase(found);
	}
	static void *resize(void *user_data, void *memory, size_t old_size, size_t new_size, size_t alignment)
	{
		counting *c = static_cast<counting *>(user_data);
		auto found = c->live.find(memory);
		if (found == c->live.end() || old_size == 0 || new_size == 0) {
			c->errors++;
			return nullptr;
		}
		if (found->second.size != old_size || found->second.alignment != alignment) {
			std::printf("counting::resize: %zu/%zu, block has %zu/%zu\n", old_size, alignment, found->second.size,
				found->second.alignment);
			c->errors++;
		}
		if (c->should_fail())
			return nullptr;
		void *raw = nullptr;
		void *moved = raw_allocate(new_size, alignment, &raw);
		if (moved == nullptr)
			return nullptr;
		std::memcpy(moved, memory, std::min(old_size, new_size));
		std::free(found->second.raw);
		c->live.erase(found);
		c->live[moved] = block{raw, new_size, alignment};
		return moved;
	}
	EQ_ALLOCATOR allocator(bool with_resize)
	{
		return EQ_ALLOCATOR{this, &counting::allocate, &counting::free, with_resize ? &counting::resize : nullptr};
	}
};

static counting counting_state;

// ---- Element types -----------------------------------------------------------

struct three {
	unsigned char bytes[3];
};
// MSVC warns (C4324) that the alignment pads the struct, which is the point.
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4324)
#endif
struct alignas(64) over_aligned {
	double x;
	double y;
};
#ifdef _MSC_VER
#pragma warning(pop)
#endif

template <typename T>
static T random_element()
{
	T value;
	unsigned char *bytes = reinterpret_cast<unsigned char *>(&value);
	for (size_t i = 0; i < sizeof(T); i++)
		bytes[i] = static_cast<unsigned char>(next_random());
	return value;
}

template <typename T>
static bool same_bytes(const T &a, const T &b)
{
	return std::memcmp(&a, &b, sizeof(T)) == 0;
}

// ---- eq::array against std::vector ------------------------------------------

template <typename T>
static void check_array(const eq::array<T> &a, const std::vector<T> &ref)
{
	CHECK(a.size() == ref.size());
	CHECK(a.count <= a.capacity);
	CHECK((a.data == nullptr) == (a.capacity == 0));
	CHECK(a.empty() == ref.empty());
	if (a.data != nullptr)
		CHECK(reinterpret_cast<uintptr_t>(a.data) % EQ_ARRAY_ALIGNMENT(sizeof(T)) == 0);
	if (a.size() != ref.size())
		return;
	for (size_t i = 0; i < ref.size(); i++)
		CHECK(same_bytes(a[i], ref[i]));
	size_t walked = 0;
	for (const T &item : a)
		CHECK(same_bytes(item, ref[walked++]));
	CHECK(walked == ref.size());
	std::span<const T> view = a.span();
	CHECK(view.size() == ref.size());
	CHECK(view.data() == a.data);
}

// Random operations on a and ref; with allow_oom, EQ_RESULT_OUT_OF_MEMORY must
// leave a as it was. Returns how many failed so.
template <typename T>
static size_t run_array_ops(eq::array<T> &a, std::vector<T> &ref, size_t ops, bool allow_oom)
{
	size_t out_of_memory = 0;
	for (size_t op = 0; op < ops; op++) {
		const size_t count = a.size();
		const size_t capacity_before = a.capacity;
		const T *data_before = a.data;
		T items[8];
		for (T &item : items)
			item = random_element<T>();
		size_t n = random_below(9);
		size_t kind = random_below(13);
		if (count + n > 400 && kind <= 6)
			kind = 7;
		if (count == 0 && (kind == 1 || kind == 3 || kind == 5 || kind == 8))
			kind = 2;
		EQ_RESULT r = EQ_SUCCESS;
		switch (kind) {
		case 0:
			r = a.push(items[0]);
			if (r == EQ_SUCCESS)
				ref.push_back(items[0]);
			break;
		case 1: {
			size_t from = random_below(count);
			r = a.push(a[from]);
			if (r == EQ_SUCCESS)
				ref.push_back(T(ref[from]));
			break;
		}
		case 2:
			r = a.append(std::span<const T>(items, n));
			if (r == EQ_SUCCESS)
				ref.insert(ref.end(), items, items + n);
			break;
		case 3: {
			size_t from = random_below(count);
			n = std::min(n, count - from);
			r = a.append(a.data + from, n);
			if (r == EQ_SUCCESS) {
				std::vector<T> copy(ref.begin() + static_cast<ptrdiff_t>(from),
					ref.begin() + static_cast<ptrdiff_t>(from + n));
				ref.insert(ref.end(), copy.begin(), copy.end());
			}
			break;
		}
		case 4: {
			size_t index = random_below(count + 1);
			r = a.insert(index, items, n);
			if (r == EQ_SUCCESS)
				ref.insert(ref.begin() + static_cast<ptrdiff_t>(index), items, items + n);
			break;
		}
		case 5: {
			size_t index = random_below(count + 1);
			size_t from = random_below(count);
			n = std::min(n, count - from);
			r = a.insert(index, a.data + from, n);
			if (r == EQ_SUCCESS) {
				std::vector<T> copy(ref.begin() + static_cast<ptrdiff_t>(from),
					ref.begin() + static_cast<ptrdiff_t>(from + n));
				ref.insert(ref.begin() + static_cast<ptrdiff_t>(index), copy.begin(), copy.end());
			}
			break;
		}
		case 6: {
			size_t index = random_below(count + 1);
			r = a.insert(index, items[1]);
			if (r == EQ_SUCCESS)
				ref.insert(ref.begin() + static_cast<ptrdiff_t>(index), items[1]);
			break;
		}
		case 7: {
			size_t index = random_below(count + 1);
			n = random_below(count - index + 1);
			r = a.remove(index, n);
			if (r == EQ_SUCCESS)
				ref.erase(ref.begin() + static_cast<ptrdiff_t>(index),
					ref.begin() + static_cast<ptrdiff_t>(index + n));
			break;
		}
		case 8: {
			size_t index = random_below(count);
			r = a.remove_swap(index);
			if (r == EQ_SUCCESS) {
				ref[index] = ref.back();
				ref.pop_back();
			}
			break;
		}
		case 9: {
			size_t to = random_below(std::min<size_t>(count + 10, 400) + 1);
			r = a.resize(to);
			if (r == EQ_SUCCESS) {
				size_t old = ref.size();
				ref.resize(to);
				for (size_t i = old; i < to; i++)
					std::memset(static_cast<void *>(&ref[i]), 0, sizeof(T));
			}
			break;
		}
		case 10: {
			size_t to = random_below(400);
			r = a.reserve(to);
			if (r == EQ_SUCCESS)
				CHECK(a.capacity >= to);
			break;
		}
		case 11: {
			// copy_from into a fresh array on the same allocator, then moved back.
			eq::array<T> copy(a.allocator);
			r = copy.copy_from(a);
			if (r == EQ_SUCCESS) {
				CHECK(copy.size() == a.size());
				a = std::move(copy);
				CHECK(copy.data == nullptr);
				CHECK(copy.size() == 0);
			}
			if (r == EQ_RESULT_OUT_OF_MEMORY && allow_oom) {
				// a is untouched by a failed copy into another array.
				out_of_memory++;
				r = EQ_SUCCESS;
			}
			check_array(a, ref);
			continue;
		}
		default:
			if (random_below(8) == 0) {
				a.clear();
				CHECK(a.capacity == capacity_before);
				ref.clear();
			}
			break;
		}
		if (r == EQ_RESULT_OUT_OF_MEMORY && allow_oom) {
			out_of_memory++;
			CHECK(a.size() == count);
			CHECK(a.capacity == capacity_before);
			CHECK(a.data == data_before);
		} else {
			CHECK(r == EQ_SUCCESS);
		}
		check_array(a, ref);
	}
	return out_of_memory;
}

template <typename T>
static void test_array_type(const char *name)
{
	static std::string label;
	for (int mode = 0; mode < 3; mode++) {
		label = std::string("eq::array<") + name + "> random, " +
			(mode == 0 ? "system heap" : mode == 1 ? "with resize" : "without resize");
		current_test = label.c_str();
		counting_state.reset();
		EQ_ALLOCATOR allocator = counting_state.allocator(mode == 1);
		random_state = 0x2545F491u;
		{
			eq::array<T> a(mode == 0 ? nullptr : &allocator);
			std::vector<T> ref;
			CHECK(run_array_ops(a, ref, 4000, false) == 0);
			// Sorted through its span, as any standard algorithm would be.
			if constexpr (std::is_same_v<T, int>) {
				std::sort(a.span().begin(), a.span().end());
				std::sort(ref.begin(), ref.end());
				check_array(a, ref);
			}
		}
		CHECK(counting_state.live.empty());
		CHECK(counting_state.errors == 0);
	}
	for (int with_resize = 0; with_resize < 2; with_resize++) {
		label = std::string("eq::array<") + name + "> out of memory";
		current_test = label.c_str();
		EQ_ALLOCATOR allocator = counting_state.allocator(with_resize != 0);
		size_t fail_at = 0;
		for (;; fail_at++) {
			counting_state.reset();
			counting_state.fail_at = fail_at;
			random_state = 0x9E3779B9u;
			{
				eq::array<T> a(&allocator);
				std::vector<T> ref;
				run_array_ops(a, ref, 300, true);
			}
			CHECK(counting_state.live.empty());
			CHECK(counting_state.errors == 0);
			if (counting_state.failed == 0)
				break;
		}
		CHECK(fail_at > 0);
	}
}

// ---- The layout eq::array shares with EQ_ARRAY, and the C macros from C++ ----

typedef EQ_ARRAY(int) INT_ARRAY;
typedef EQ_ARRAY(over_aligned) OVER_ARRAY;

static_assert(sizeof(eq::array<int>) == sizeof(INT_ARRAY));
static_assert(offsetof(eq::array<int>, data) == offsetof(INT_ARRAY, data));
static_assert(offsetof(eq::array<int>, count) == offsetof(INT_ARRAY, count));
static_assert(offsetof(eq::array<int>, capacity) == offsetof(INT_ARRAY, capacity));
static_assert(offsetof(eq::array<int>, allocator) == offsetof(INT_ARRAY, allocator));
static_assert(std::is_standard_layout_v<eq::array<int>>);
static_assert(sizeof(eq::array<over_aligned>) == sizeof(OVER_ARRAY));
static_assert(!std::is_copy_constructible_v<eq::array<int>>);
static_assert(std::is_nothrow_move_constructible_v<eq::array<int>>);
static_assert(!std::is_copy_constructible_v<eq::vector<int>>);
static_assert(std::is_nothrow_move_constructible_v<eq::vector<int>>);
static_assert(!std::is_copy_constructible_v<eq::string>);
static_assert(std::is_nothrow_move_constructible_v<eq::string>);

struct c_side {
	INT_ARRAY values;
};

static void test_shared_with_c()
{
	current_test = "eq::array shared with C";
	counting_state.reset();
	EQ_ALLOCATOR allocator = counting_state.allocator(true);

	// A C struct's array, filled by the C macros, used through eq::array.
	c_side c = {};
	eq_array_init(&c.values, &allocator);
	int x = 1;
	CHECK(eq_array_push(&c.values, &x) == EQ_SUCCESS);
	eq::array<int> &view = eq::array<int>::from_c(c.values);
	CHECK(view.size() == 1);
	CHECK(view.push(2) == EQ_SUCCESS);
	CHECK(c.values.count == 2);
	CHECK(c.values.data[1] == 2);
	CHECK(view.data == c.values.data);
	eq_array_free(&c.values);
	CHECK(counting_state.live.empty());

	// An eq::array, handed to C code as its EQ_ARRAY.
	{
		eq::array<int> owned(&allocator);
		CHECK(owned.push(5) == EQ_SUCCESS);
		INT_ARRAY &as_c = owned.as_c<INT_ARRAY>();
		x = 6;
		CHECK(eq_array_push(&as_c, &x) == EQ_SUCCESS);
		CHECK(owned.size() == 2);
		CHECK(owned[1] == 6);
		// The macros take a pointer to an eq::array as well.
		x = 7;
		CHECK(eq_array_push(&owned, &x) == EQ_SUCCESS);
		CHECK(owned.size() == 3);
		CHECK(owned[2] == 7);
	}
	CHECK(counting_state.live.empty());
	CHECK(counting_state.errors == 0);

	// The C macros from C++, against std::vector.
	current_test = "C macros from C++";
	random_state = 0x31415926u;
	INT_ARRAY a = {};
	std::vector<int> ref;
	for (int op = 0; op < 4000; op++) {
		int value = static_cast<int>(next_random());
		size_t kind = random_below(4);
		if (kind == 0 || ref.empty()) {
			CHECK(eq_array_push(&a, &value) == EQ_SUCCESS);
			ref.push_back(value);
		} else if (kind == 1) {
			size_t index = random_below(ref.size() + 1);
			CHECK(eq_array_insert(&a, index, &value, 1) == EQ_SUCCESS);
			ref.insert(ref.begin() + static_cast<ptrdiff_t>(index), value);
		} else if (kind == 2) {
			size_t index = random_below(ref.size());
			CHECK(eq_array_remove(&a, index, 1) == EQ_SUCCESS);
			ref.erase(ref.begin() + static_cast<ptrdiff_t>(index));
		} else {
			size_t index = random_below(ref.size());
			CHECK(eq_array_remove_swap(&a, index) == EQ_SUCCESS);
			ref[index] = ref.back();
			ref.pop_back();
		}
	}
	CHECK(a.count == ref.size());
	CHECK(a.count == 0 || std::memcmp(a.data, ref.data(), a.count * sizeof(int)) == 0);
	eq_array_free(&a);
}

static void test_array_cases()
{
	current_test = "eq::array cases";
	eq::array<int> a;
	CHECK(a.data == nullptr);
	CHECK(a.allocator == nullptr);
	CHECK(a.insert(1, 3) == EQ_RESULT_OUT_OF_RANGE);
	CHECK(a.remove(0) == EQ_RESULT_OUT_OF_RANGE);
	CHECK(a.remove(0, 0) == EQ_SUCCESS);
	CHECK(a.remove_swap(0) == EQ_RESULT_OUT_OF_RANGE);
	CHECK(a.reserve(SIZE_MAX) == EQ_RESULT_TOO_LARGE);
	CHECK(a.append(nullptr, 2) == EQ_RESULT_INVALID_ARGUMENT);
	CHECK(a.append(nullptr, 0) == EQ_SUCCESS);
	CHECK(a.push(1) == EQ_SUCCESS);
	CHECK(a.copy_from(a) == EQ_SUCCESS);
	CHECK(a.size() == 1);
	a.free();
	CHECK(a.data == nullptr);
	CHECK(a.size() == 0);
	CHECK(a.push(2) == EQ_SUCCESS);
	eq::array<int> b(std::move(a));
	CHECK(a.data == nullptr);
	CHECK(b.size() == 1);
	CHECK(a.push(3) == EQ_SUCCESS);
	b = std::move(a);
	CHECK(b.size() == 1);
	CHECK(b[0] == 3);
	eq::array<int> &same = b;
	b = std::move(same);
	CHECK(b.size() == 1);
}

// ---- eq::vector against std::vector --------------------------------------------
// tracked counts live objects and checks that each is only ever touched at the
// address it was constructed at: a vector that moved one by copying bytes
// would be caught.

static long tracked_live = 0;
static long tracked_errors = 0;

struct tracked {
	int value;
	tracked *self;

	tracked() noexcept : value(0), self(this) { tracked_live++; }
	explicit tracked(int v) noexcept : value(v), self(this) { tracked_live++; }
	tracked(const tracked &other) noexcept : value(other.value), self(this)
	{
		other.check();
		tracked_live++;
	}
	tracked(tracked &&other) noexcept : value(other.value), self(this)
	{
		other.check();
		other.value = -1;
		tracked_live++;
	}
	tracked &operator=(const tracked &other) noexcept
	{
		check();
		other.check();
		value = other.value;
		return *this;
	}
	tracked &operator=(tracked &&other) noexcept
	{
		check();
		other.check();
		value = other.value;
		if (&other != this)
			other.value = -1;
		return *this;
	}
	~tracked()
	{
		check();
		self = nullptr;
		tracked_live--;
	}
	void check() const
	{
		if (self != this)
			tracked_errors++;
	}
};

static void check_vector(const eq::vector<tracked> &v, const std::vector<int> &ref)
{
	CHECK(v.size() == ref.size());
	CHECK(v.count <= v.capacity);
	CHECK((v.data == nullptr) == (v.capacity == 0));
	if (v.data != nullptr)
		CHECK(reinterpret_cast<uintptr_t>(v.data) % alignof(tracked) == 0);
	if (v.size() != ref.size())
		return;
	for (size_t i = 0; i < ref.size(); i++)
		CHECK(v[i].value == ref[i]);
	CHECK(v.span().size() == ref.size());
}

static size_t run_vector_ops(eq::vector<tracked> &v, std::vector<int> &ref, size_t ops, bool allow_oom)
{
	size_t out_of_memory = 0;
	for (size_t op = 0; op < ops; op++) {
		const size_t count = v.size();
		const size_t capacity_before = v.capacity;
		const tracked *data_before = v.data;
		const long live_before = tracked_live;
		int value = static_cast<int>(next_random() % 100000);
		size_t kind = random_below(14);
		if (count > 400 && kind <= 6)
			kind = 7;
		if (count == 0 && (kind == 1 || kind == 3 || kind == 5 || kind == 8))
			kind = 0;
		EQ_RESULT r = EQ_SUCCESS;
		switch (kind) {
		case 0: {
			tracked item(value);
			r = v.push(item);
			if (r == EQ_SUCCESS)
				ref.push_back(value);
			break;
		}
		case 1: {
			// From the vector's own element, which growth would move.
			size_t from = random_below(count);
			r = v.push(v[from]);
			if (r == EQ_SUCCESS)
				ref.push_back(ref[from]);
			break;
		}
		case 2:
			r = v.push(tracked(value));
			if (r == EQ_SUCCESS)
				ref.push_back(value);
			break;
		case 3: {
			size_t from = random_below(count);
			r = v.emplace(v[from]);
			if (r == EQ_SUCCESS)
				ref.push_back(ref[from]);
			break;
		}
		case 4:
			r = v.emplace(value);
			if (r == EQ_SUCCESS)
				ref.push_back(value);
			break;
		case 5: {
			size_t index = random_below(count + 1);
			size_t from = random_below(count);
			r = v.insert(index, v[from]);
			if (r == EQ_SUCCESS)
				ref.insert(ref.begin() + static_cast<ptrdiff_t>(index), ref[from]);
			break;
		}
		case 6: {
			size_t index = random_below(count + 1);
			r = v.insert(index, tracked(value));
			if (r == EQ_SUCCESS)
				ref.insert(ref.begin() + static_cast<ptrdiff_t>(index), value);
			break;
		}
		case 7: {
			size_t index = random_below(count + 1);
			size_t n = random_below(std::min<size_t>(count - index, 8) + 1);
			r = v.remove(index, n);
			if (r == EQ_SUCCESS)
				ref.erase(ref.begin() + static_cast<ptrdiff_t>(index),
					ref.begin() + static_cast<ptrdiff_t>(index + n));
			break;
		}
		case 8: {
			size_t index = random_below(count);
			r = v.remove_swap(index);
			if (r == EQ_SUCCESS) {
				ref[index] = ref.back();
				ref.pop_back();
			}
			break;
		}
		case 9: {
			size_t to = random_below(std::min<size_t>(count + 10, 400) + 1);
			r = v.resize(to);
			if (r == EQ_SUCCESS)
				ref.resize(to, 0);
			break;
		}
		case 10: {
			size_t to = random_below(400);
			r = v.reserve(to);
			if (r == EQ_SUCCESS)
				CHECK(v.capacity >= to);
			break;
		}
		case 11: {
			eq::vector<tracked> copy(v.allocator);
			r = copy.copy_from(v);
			if (r == EQ_SUCCESS) {
				v = std::move(copy);
				CHECK(copy.size() == 0);
			} else if (r == EQ_RESULT_OUT_OF_MEMORY && allow_oom) {
				out_of_memory++;
				r = EQ_SUCCESS;
			}
			CHECK(r == EQ_SUCCESS);
			check_vector(v, ref);
			continue;
		}
		case 12: {
			// copy_from over a vector that already holds elements.
			eq::vector<tracked> other(v.allocator);
			for (int i = 0; i < 3; i++) {
				EQ_RESULT pushed = other.emplace(i);
				if (pushed != EQ_SUCCESS)
					break;
			}
			r = v.copy_from(other);
			if (r == EQ_SUCCESS) {
				ref.clear();
				for (const tracked &t : other)
					ref.push_back(t.value);
			}
			break;
		}
		default:
			if (random_below(8) == 0) {
				v.clear();
				ref.clear();
			}
			break;
		}
		if (r == EQ_RESULT_OUT_OF_MEMORY && allow_oom) {
			out_of_memory++;
			CHECK(v.size() == count);
			CHECK(v.capacity == capacity_before);
			CHECK(v.data == data_before);
			if (kind != 12)
				CHECK(tracked_live == live_before);
		} else {
			CHECK(r == EQ_SUCCESS);
		}
		check_vector(v, ref);
	}
	return out_of_memory;
}

static void test_vector()
{
	for (int mode = 0; mode < 2; mode++) {
		current_test = mode == 0 ? "eq::vector random, system heap" : "eq::vector random, allocator";
		counting_state.reset();
		EQ_ALLOCATOR allocator = counting_state.allocator(true);
		random_state = 0x2545F491u;
		{
			eq::vector<tracked> v(mode == 0 ? nullptr : &allocator);
			std::vector<int> ref;
			CHECK(run_vector_ops(v, ref, 4000, false) == 0);
			CHECK(tracked_live == static_cast<long>(v.size()));
		}
		CHECK(tracked_live == 0);
		CHECK(tracked_errors == 0);
		CHECK(counting_state.live.empty());
		CHECK(counting_state.errors == 0);
	}

	current_test = "eq::vector out of memory";
	EQ_ALLOCATOR allocator = counting_state.allocator(true);
	size_t fail_at = 0;
	for (;; fail_at++) {
		counting_state.reset();
		counting_state.fail_at = fail_at;
		random_state = 0x9E3779B9u;
		{
			eq::vector<tracked> v(&allocator);
			std::vector<int> ref;
			run_vector_ops(v, ref, 300, true);
		}
		CHECK(tracked_live == 0);
		CHECK(tracked_errors == 0);
		CHECK(counting_state.live.empty());
		CHECK(counting_state.errors == 0);
		if (counting_state.failed == 0)
			break;
	}
	CHECK(fail_at > 0);

	current_test = "eq::vector cases";
	{
		eq::vector<tracked> v;
		CHECK(v.insert(1, tracked(1)) == EQ_RESULT_OUT_OF_RANGE);
		CHECK(v.remove(0) == EQ_RESULT_OUT_OF_RANGE);
		CHECK(v.remove_swap(0) == EQ_RESULT_OUT_OF_RANGE);
		CHECK(v.reserve(SIZE_MAX) == EQ_RESULT_TOO_LARGE);
		CHECK(v.resize(SIZE_MAX) == EQ_RESULT_TOO_LARGE);
		CHECK(v.emplace(4) == EQ_SUCCESS);
		eq::vector<tracked> w(std::move(v));
		CHECK(v.size() == 0);
		CHECK(w.size() == 1);
		CHECK(w[0].value == 4);
		eq::vector<tracked> &same = w;
		w = std::move(same);
		CHECK(w.size() == 1);
		CHECK(w.copy_from(w) == EQ_SUCCESS);
		CHECK(w.size() == 1);
		w.free();
		CHECK(w.data == nullptr);
		CHECK(tracked_live == 0);
	}
	CHECK(tracked_errors == 0);
}

// eq::vector<eq::string> against std::vector<std::string>: the string's own
// buffer means it can't be moved as bytes.
static void test_vector_of_strings()
{
	current_test = "eq::vector<eq::string>";
	counting_state.reset();
	EQ_ALLOCATOR allocator = counting_state.allocator(true);
	random_state = 0x5EED5EEDu;
	{
		eq::vector<eq::string> v(&allocator);
		std::vector<std::string> ref;
		for (int op = 0; op < 3000; op++) {
			size_t kind = random_below(4);
			if (kind <= 1 || ref.empty()) {
				std::string text(random_below(40), 'a');
				for (char &c : text)
					c = static_cast<char>('a' + random_below(26));
				eq::string s(&allocator);
				CHECK(s.assign(text) == EQ_SUCCESS);
				size_t index = random_below(ref.size() + 1);
				CHECK(v.insert(index, std::move(s)) == EQ_SUCCESS);
				ref.insert(ref.begin() + static_cast<ptrdiff_t>(index), text);
			} else if (kind == 2) {
				size_t index = random_below(ref.size());
				CHECK(v.remove(index) == EQ_SUCCESS);
				ref.erase(ref.begin() + static_cast<ptrdiff_t>(index));
			} else {
				size_t index = random_below(ref.size());
				CHECK(v[index].append("!") == EQ_SUCCESS);
				ref[index] += "!";
			}
		}
		CHECK(v.size() == ref.size());
		for (size_t i = 0; i < ref.size() && i < v.size(); i++) {
			CHECK(v[i].view() == ref[i]);
			CHECK(std::strlen(v[i].c_str()) == ref[i].size());
		}
	}
	CHECK(counting_state.live.empty());
	CHECK(counting_state.errors == 0);
}

// ---- eq::string against std::string -----------------------------------------------

static void check_string(const eq::string &s, const std::string &ref)
{
	CHECK(s.size() == ref.size());
	CHECK(s.view() == ref);
	CHECK(s.c_str() != nullptr);
	CHECK(s.c_str()[s.size()] == '\0');
	CHECK(s.size() <= s.capacity());
	CHECK(s.capacity() >= eq::string::local_capacity);
	CHECK(s == std::string_view(ref));
	CHECK(s.empty() == ref.empty());
}

static std::string random_string(size_t max)
{
	std::string text(random_below(max + 1), 'a');
	for (char &c : text)
		c = static_cast<char>('a' + random_below(26));
	return text;
}

static size_t run_string_ops(eq::string &s, std::string &ref, size_t ops, bool allow_oom)
{
	size_t out_of_memory = 0;
	for (size_t op = 0; op < ops; op++) {
		const size_t length = s.size();
		const size_t capacity_before = s.capacity();
		const char *data_before = s.c_str();
		std::string text = random_string(30);
		size_t kind = random_below(15);
		if (length > 2000 && kind <= 9)
			kind = 10;
		if (length == 0 && (kind == 2 || kind == 6 || kind == 9))
			kind = 1;
		EQ_RESULT r = EQ_SUCCESS;
		switch (kind) {
		case 0: {
			char c = static_cast<char>('a' + random_below(26));
			r = s.append(c);
			if (r == EQ_SUCCESS)
				ref += c;
			break;
		}
		case 1:
			r = s.append(text);
			if (r == EQ_SUCCESS)
				ref += text;
			break;
		case 2: {
			size_t from = random_below(length);
			size_t n = std::min(random_below(30), length - from);
			r = s.append(s.view().substr(from, n));
			if (r == EQ_SUCCESS)
				ref += ref.substr(from, n);
			break;
		}
		case 3: {
			int number = static_cast<int>(next_random() % 100000);
			r = s.append_format("<%d %s>", number, text.c_str());
			if (r == EQ_SUCCESS) {
				char formatted[64];
				std::snprintf(formatted, sizeof(formatted), "<%d %s>", number, text.c_str());
				ref += formatted;
			}
			break;
		}
		case 4: {
			size_t index = random_below(length + 1);
			r = s.insert(index, text);
			if (r == EQ_SUCCESS)
				ref.insert(index, text);
			break;
		}
		case 5: {
			size_t index = random_below(length + 1);
			size_t n = random_below(length - index + 1);
			r = s.remove(index, n);
			if (r == EQ_SUCCESS)
				ref.erase(index, n);
			break;
		}
		case 6: {
			size_t index = random_below(length + 1);
			size_t from = random_below(length);
			size_t n = std::min(random_below(30), length - from);
			r = s.insert(index, s.view().substr(from, n));
			if (r == EQ_SUCCESS) {
				std::string copy = ref.substr(from, n);
				ref.insert(index, copy);
			}
			break;
		}
		case 7: {
			size_t to = random_below(std::min<size_t>(length + 40, 2000) + 1);
			char fill = static_cast<char>('A' + random_below(26));
			r = s.resize(to, fill);
			if (r == EQ_SUCCESS)
				ref.resize(to, fill);
			break;
		}
		case 8:
			r = s.assign(text);
			if (r == EQ_SUCCESS)
				ref = text;
			break;
		case 9: {
			size_t from = random_below(length);
			size_t n = std::min(random_below(60), length - from);
			r = s.assign(s.view().substr(from, n));
			if (r == EQ_SUCCESS)
				ref = ref.substr(from, n);
			break;
		}
		case 10: {
			size_t to = random_below(2000);
			r = s.reserve(to);
			if (r == EQ_SUCCESS)
				CHECK(s.capacity() >= to);
			break;
		}
		case 11: {
			eq::string copy(s.allocator());
			r = copy.copy_from(s);
			if (r == EQ_SUCCESS) {
				CHECK(copy.view() == s.view());
				s = std::move(copy);
				CHECK(copy.size() == 0);
				CHECK(copy.c_str()[0] == '\0');
			} else if (r == EQ_RESULT_OUT_OF_MEMORY && allow_oom) {
				out_of_memory++;
				r = EQ_SUCCESS;
			}
			CHECK(r == EQ_SUCCESS);
			check_string(s, ref);
			continue;
		}
		case 12: {
			eq::string moved(std::move(s));
			CHECK(s.size() == 0);
			CHECK(moved.view() == ref);
			s = std::move(moved);
			check_string(s, ref);
			continue;
		}
		default:
			if (random_below(6) == 0) {
				s.clear();
				CHECK(s.capacity() == capacity_before);
				ref.clear();
			}
			break;
		}
		if (r == EQ_RESULT_OUT_OF_MEMORY && allow_oom) {
			out_of_memory++;
			CHECK(s.size() == length);
			CHECK(s.capacity() == capacity_before);
			CHECK(s.c_str() == data_before);
		} else {
			CHECK(r == EQ_SUCCESS);
		}
		check_string(s, ref);
	}
	return out_of_memory;
}

static void test_string()
{
	for (int mode = 0; mode < 3; mode++) {
		current_test = mode == 0 ? "eq::string random, system heap"
			: mode == 1          ? "eq::string random, with resize"
								 : "eq::string random, without resize";
		counting_state.reset();
		EQ_ALLOCATOR allocator = counting_state.allocator(mode == 1);
		random_state = 0x1234567u;
		{
			eq::string s(mode == 0 ? nullptr : &allocator);
			std::string ref;
			check_string(s, ref);
			CHECK(run_string_ops(s, ref, 4000, false) == 0);
		}
		CHECK(counting_state.live.empty());
		CHECK(counting_state.errors == 0);
	}

	current_test = "eq::string out of memory";
	for (int with_resize = 0; with_resize < 2; with_resize++) {
		EQ_ALLOCATOR allocator = counting_state.allocator(with_resize != 0);
		size_t fail_at = 0;
		for (;; fail_at++) {
			counting_state.reset();
			counting_state.fail_at = fail_at;
			random_state = 0x7654321u;
			{
				eq::string s(&allocator);
				std::string ref;
				run_string_ops(s, ref, 300, true);
			}
			CHECK(counting_state.live.empty());
			CHECK(counting_state.errors == 0);
			if (counting_state.failed == 0)
				break;
		}
		CHECK(fail_at > 0);
	}

	current_test = "eq::string short names";
	counting_state.reset();
	EQ_ALLOCATOR allocator = counting_state.allocator(true);
	{
		eq::string s(&allocator);
		std::string text(eq::string::local_capacity, 'n');
		CHECK(s.assign(text) == EQ_SUCCESS);
		CHECK(s.view() == text);
		CHECK(counting_state.calls == 0);
		eq::string moved(std::move(s));
		CHECK(moved.view() == text);
		CHECK(counting_state.calls == 0);
		CHECK(moved.append('!') == EQ_SUCCESS);
		CHECK(counting_state.calls == 1);
		CHECK(counting_state.live.size() == 1);
		// A heap string moves without allocating.
		eq::string again(std::move(moved));
		CHECK(counting_state.calls == 1);
		CHECK(again.size() == eq::string::local_capacity + 1);
		CHECK(moved.empty());
		// C code works on the same text through its EQ_STRING.
		CHECK(eq_string_append(&again.builder(), "?") == EQ_SUCCESS);
		CHECK(again.size() == eq::string::local_capacity + 2);
		CHECK(again.view().back() == '?');
	}
	CHECK(counting_state.live.empty());
	CHECK(counting_state.errors == 0);

	current_test = "eq::string cases";
	{
		eq::string s;
		CHECK(s.c_str()[0] == '\0');
		CHECK(s.allocator() == nullptr);
		CHECK(s.insert(1, "x") == EQ_RESULT_OUT_OF_RANGE);
		CHECK(s.remove(0, 1) == EQ_RESULT_OUT_OF_RANGE);
		CHECK(s.reserve(SIZE_MAX) == EQ_RESULT_TOO_LARGE);
		CHECK(s.append_format("%s-%d", "level", 3) == EQ_SUCCESS);
		CHECK(s == "level-3");
		CHECK(s.copy_from(s) == EQ_SUCCESS);
		CHECK(s == "level-3");
		std::string_view view = s;
		CHECK(view == "level-3");
		s[0] = 'L';
		CHECK(s == "Level-3");
		CHECK(std::count(s.begin(), s.end(), 'e') == 2);
		eq::string &same = s;
		s = std::move(same);
		CHECK(s == "Level-3");
		s.clear();
		CHECK(s.empty());
	}
}

int main()
{
	test_array_type<int>("int");
	test_array_type<three>("three");
	test_array_type<over_aligned>("over_aligned");
	test_array_cases();
	test_shared_with_c();
	test_vector();
	test_vector_of_strings();
	test_string();

	if (failures != 0) {
		std::printf("eqstd C++ test (%s): %d failure(s)\n", EQSTD_TEST_LANGUAGE, failures);
		return 1;
	}
	std::printf("eqstd C++ test (%s): all passed\n", EQSTD_TEST_LANGUAGE);
	return 0;
}
