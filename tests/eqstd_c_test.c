/* eqstd's C half, black-box: arrays and strings by random operations against
 * a plain reference, a counting allocator that checks every free, and an
 * allocator that fails at each call in turn. Compiled as C11, and included
 * unchanged by eqstd_c_as_cpp_test.cpp (C++) and eqstd_objc_test.m
 * (Objective-C). */

#ifndef EQSTD_TEST_LANGUAGE
#define EQSTD_TEST_LANGUAGE "C"
#endif

#include "eqstd_test_support.h"

static COUNTING counting_state;

/* ---- Element types: sizes 4, 3, 24 and an over-aligned 64 ---------------- */

typedef struct THREE {
	unsigned char bytes[3];
} THREE;

typedef struct TWENTY_FOUR {
	double a;
	double b;
	double c;
} TWENTY_FOUR;

typedef struct OVER_ALIGNED {
	TEST_ALIGNAS(64) double x;
	double y;
} OVER_ALIGNED;

#define TEST_ELEMENT int
#define TEST_NAME int
#include "eqstd_c_array_body.h"
#undef TEST_ELEMENT
#undef TEST_NAME

#define TEST_ELEMENT THREE
#define TEST_NAME three
#include "eqstd_c_array_body.h"
#undef TEST_ELEMENT
#undef TEST_NAME

#define TEST_ELEMENT TWENTY_FOUR
#define TEST_NAME twenty_four
#include "eqstd_c_array_body.h"
#undef TEST_ELEMENT
#undef TEST_NAME

#define TEST_ELEMENT OVER_ALIGNED
#define TEST_NAME over_aligned
#include "eqstd_c_array_body.h"
#undef TEST_ELEMENT
#undef TEST_NAME

/* ---- Results and arguments -------------------------------------------------- */

static void test_result_names(void)
{
	current_test = "result names";
	CHECK(strcmp(eq_result_str(EQ_SUCCESS), "EQ_SUCCESS") == 0);
	CHECK(strcmp(eq_result_str(EQ_RESULT_OUT_OF_MEMORY), "EQ_RESULT_OUT_OF_MEMORY") == 0);
	CHECK(strcmp(eq_result_str(EQ_RESULT_TOO_LARGE), "EQ_RESULT_TOO_LARGE") == 0);
	CHECK(strcmp(eq_result_str(EQ_RESULT_OUT_OF_RANGE), "EQ_RESULT_OUT_OF_RANGE") == 0);
	CHECK(strcmp(eq_result_str(EQ_RESULT_INVALID_ARGUMENT), "EQ_RESULT_INVALID_ARGUMENT") == 0);
	CHECK(strcmp(eq_result_str(EQ_RESULT_INVALID_ALLOCATOR), "EQ_RESULT_INVALID_ALLOCATOR") == 0);
	CHECK(strcmp(eq_result_str(EQ_RESULT_FORMAT_FAILED), "EQ_RESULT_FORMAT_FAILED") == 0);
	CHECK(strcmp(eq_result_str(TEST_CAST(EQ_RESULT, 7)), "EQ_RESULT_UNKNOWN") == 0);
}

typedef EQ_ARRAY(int) INT_ARRAY;

static void test_array_arguments(void)
{
	INT_ARRAY a = TEST_ZERO;
	INT_ARRAY *none = NULL;
	EQ_ALLOCATOR broken;
	int x = 7;
	int values[3] = {1, 2, 3};

	current_test = "array arguments";
	/* Zeroed is empty, on the system heap. */
	CHECK(eq_array_push(&a, &x) == EQ_SUCCESS);
	CHECK(a.count == 1);
	CHECK(a.allocator == NULL);
	CHECK(eq_array_insert(&a, 2, values, 1) == EQ_RESULT_OUT_OF_RANGE);
	CHECK(eq_array_insert(&a, 2, values, 0) == EQ_RESULT_OUT_OF_RANGE);
	CHECK(eq_array_insert(&a, 1, NULL, 0) == EQ_SUCCESS);
	CHECK(eq_array_append(&a, NULL, 0) == EQ_SUCCESS);
	CHECK(eq_array_append(&a, NULL, 2) == EQ_RESULT_INVALID_ARGUMENT);
	CHECK(eq_array_push(&a, NULL) == EQ_RESULT_INVALID_ARGUMENT);
	CHECK(eq_array_remove(&a, 2, 0) == EQ_RESULT_OUT_OF_RANGE);
	CHECK(eq_array_remove(&a, 0, 2) == EQ_RESULT_OUT_OF_RANGE);
	CHECK(eq_array_remove(&a, 1, 0) == EQ_SUCCESS);
	CHECK(eq_array_remove(&a, 0, SIZE_MAX) == EQ_RESULT_OUT_OF_RANGE);
	CHECK(eq_array_remove_swap(&a, 1) == EQ_RESULT_OUT_OF_RANGE);
	CHECK(eq_array_reserve(&a, SIZE_MAX) == EQ_RESULT_TOO_LARGE);
	CHECK(eq_array_resize(&a, TEST_CAST(size_t, PTRDIFF_MAX)) == EQ_RESULT_TOO_LARGE);
	CHECK(eq_array_append(&a, values, SIZE_MAX) == EQ_RESULT_TOO_LARGE);
	CHECK(a.count == 1);
	CHECK(a.data[0] == 7);
	/* An array may hold PTRDIFF_MAX bytes, no more. */
	CHECK(eq_array_reserve(&a, TEST_CAST(size_t, PTRDIFF_MAX) / sizeof(int) + 1) == EQ_RESULT_TOO_LARGE);
	eq_array_free(&a);

	CHECK(eq_array_push(none, &x) == EQ_RESULT_INVALID_ARGUMENT);
	CHECK(eq_array_append(none, values, 3) == EQ_RESULT_INVALID_ARGUMENT);
	CHECK(eq_array_insert(none, 0, values, 3) == EQ_RESULT_INVALID_ARGUMENT);
	CHECK(eq_array_remove(none, 0, 0) == EQ_RESULT_INVALID_ARGUMENT);
	CHECK(eq_array_remove_swap(none, 0) == EQ_RESULT_INVALID_ARGUMENT);
	CHECK(eq_array_reserve(none, 4) == EQ_RESULT_INVALID_ARGUMENT);
	CHECK(eq_array_resize(none, 4) == EQ_RESULT_INVALID_ARGUMENT);
	CHECK(eq_array_clear(none) == EQ_RESULT_INVALID_ARGUMENT);
	eq_array_init(none, NULL);
	eq_array_free(none);

	/* An allocator missing a function is refused before anything is asked of it. */
	memset(&broken, 0, sizeof(broken));
	eq_array_init(&a, &broken);
	CHECK(eq_array_push(&a, &x) == EQ_RESULT_INVALID_ALLOCATOR);
	CHECK(eq_array_reserve(&a, 8) == EQ_RESULT_INVALID_ALLOCATOR);
	CHECK(a.data == NULL);
	CHECK(a.count == 0);
	CHECK(eq_array_clear(&a) == EQ_SUCCESS);
	eq_array_free(&a);

	/* Each argument is evaluated once. */
	{
		INT_ARRAY arrays[2] = {TEST_ZERO, TEST_ZERO};
		INT_ARRAY *next = arrays;
		const int *item = values;
		size_t index = 0;
		CHECK(eq_array_push(next++, item++) == EQ_SUCCESS);
		CHECK(next == arrays + 1);
		CHECK(item == values + 1);
		CHECK(eq_array_insert(next++, index++, item++, 1) == EQ_SUCCESS);
		CHECK(next == arrays + 2);
		CHECK(index == 1);
		CHECK(item == values + 2);
		next = arrays;
		CHECK(eq_array_append(next++, values, 3) == EQ_SUCCESS);
		CHECK(next == arrays + 1);
		CHECK(arrays[0].count == 4);
		CHECK(eq_array_at(&arrays[0], index++) == 1);
		CHECK(index == 2);
		next = arrays;
		CHECK(eq_array_remove(next++, 0, 1) == EQ_SUCCESS);
		CHECK(next == arrays + 1);
		CHECK(arrays[1].count == 1);
		CHECK(arrays[1].data[0] == 2);
		eq_array_free(&arrays[0]);
		eq_array_free(&arrays[1]);
	}
}

/* ---- The allocator functions ------------------------------------------------ */

/* A caller's allocator table of the common three-field shape: an EQ_ALLOCATOR
 * is made from one by copying its fields. */
typedef void *(*TEST_TABLE_ALLOCATE_FUNCTION)(void *user_data, size_t size, size_t alignment);
typedef void (*TEST_TABLE_FREE_FUNCTION)(void *user_data, void *memory, size_t size, size_t alignment);
typedef struct TEST_TABLE_ALLOCATOR {
	void *user_data;
	TEST_TABLE_ALLOCATE_FUNCTION allocate;
	TEST_TABLE_FREE_FUNCTION free;
} TEST_TABLE_ALLOCATOR;

static void test_allocator_functions(void)
{
	size_t alignment;
	current_test = "allocator functions";
	CHECK(eq_allocate(NULL, 0, 8) == NULL);
	CHECK(eq_allocate(NULL, 16, 0) == NULL);
	CHECK(eq_allocate(NULL, 16, 3) == NULL);
	CHECK(eq_resize(NULL, NULL, 8, 16, 8) == NULL);
	eq_free(NULL, NULL, 16, 8);

	/* The system heap at every alignment, grown and shrunk with its bytes kept. */
	for (alignment = 1; alignment <= 4096; alignment *= 2) {
		unsigned char *block = TEST_CAST(unsigned char *, eq_allocate(NULL, 100, alignment));
		size_t i;
		CHECK(block != NULL);
		if (block == NULL)
			continue;
		CHECK(TEST_ADDRESS(block) % alignment == 0);
		for (i = 0; i < 100; i++)
			block[i] = TEST_CAST(unsigned char, i);
		block = TEST_CAST(unsigned char *, eq_resize(NULL, block, 100, 5000, alignment));
		CHECK(block != NULL);
		if (block == NULL)
			continue;
		CHECK(TEST_ADDRESS(block) % alignment == 0);
		for (i = 0; i < 100; i++)
			CHECK(block[i] == i);
		block = TEST_CAST(unsigned char *, eq_resize(NULL, block, 5000, 50, alignment));
		CHECK(block != NULL);
		if (block == NULL)
			continue;
		CHECK(TEST_ADDRESS(block) % alignment == 0);
		for (i = 0; i < 50; i++)
			CHECK(block[i] == i);
		eq_free(NULL, block, 50, alignment);
	}

	/* A caller's allocator, with and without resize; and a three-field table
	 * handed over by its fields. */
	{
		TEST_TABLE_ALLOCATOR table;
		EQ_ALLOCATOR with_resize = counting_allocator(&counting_state, 1);
		EQ_ALLOCATOR handed_over;
		void *block;
		counting_reset(&counting_state);
		table.user_data = &counting_state;
		table.allocate = counting_allocate;
		table.free = counting_free;
		{
			EQ_ALLOCATOR copy = {table.user_data, table.allocate, table.free, NULL};
			handed_over = copy;
		}
		block = eq_allocate(&handed_over, 24, 8);
		CHECK(block != NULL);
		block = eq_resize(&handed_over, block, 24, 96, 8);
		CHECK(block != NULL);
		CHECK(counting_state.live == 1);
		eq_free(&handed_over, block, 96, 8);
		block = eq_allocate(&with_resize, 24, 64);
		CHECK(block != NULL);
		CHECK(TEST_ADDRESS(block) % 64 == 0);
		counting_state.fail_at = counting_state.calls;
		CHECK(eq_resize(&with_resize, block, 24, 96, 64) == NULL);
		CHECK(counting_state.live == 1);
		eq_free(&with_resize, block, 24, 64);
		CHECK(counting_state.live == 0);
		CHECK(counting_state.errors == 0);
	}
}

/* ---- Strings ------------------------------------------------------------------ */

#define STRING_MAX 2000

static char string_ref[STRING_MAX + 64];
static size_t string_ref_length;

static void ref_string_insert(size_t index, const char *text, size_t n)
{
	char copy[64];
	memcpy(copy, text, n);
	memmove(string_ref + index + n, string_ref + index, string_ref_length - index);
	memcpy(string_ref + index, copy, n);
	string_ref_length += n;
	string_ref[string_ref_length] = '\0';
}

static void check_string(const EQ_STRING *s)
{
	const char *text = eq_string_c_str(s);
	CHECK(s->length == string_ref_length);
	CHECK(text != NULL);
	if (s->data != NULL) {
		CHECK(s->length <= s->capacity);
		CHECK(text == s->data);
	}
	if (s->length == string_ref_length && text != NULL) {
		CHECK(memcmp(text, string_ref, s->length) == 0);
		CHECK(text[s->length] == '\0');
	}
}

static void random_text(char *out, size_t n)
{
	size_t i;
	for (i = 0; i < n; i++)
		out[i] = TEST_CAST(char, 'a' + random_below(26));
	out[n] = '\0';
}

static size_t run_string_ops(EQ_STRING *s, size_t ops, int allow_oom)
{
	size_t out_of_memory = 0;
	size_t op;
	for (op = 0; op < ops; op++) {
		char text[40];
		char formatted[64];
		size_t length = s->length;
		size_t capacity_before = s->capacity;
		const char *data_before = s->data;
		size_t n = random_below(21);
		size_t index = 0;
		size_t from = 0;
		size_t kind = random_below(10);
		int number = TEST_CAST(int, next_random() % 100000);
		int written = 0;
		EQ_RESULT r = EQ_SUCCESS;
		random_text(text, n);
		if (length == 0 && (kind == 2 || kind == 5))
			kind = 1;
		if (length + 40 > STRING_MAX && kind <= 5)
			kind = 6;
		switch (kind) {
		case 0:
			r = eq_string_append(s, text);
			if (r == EQ_SUCCESS)
				ref_string_insert(length, text, n);
			break;
		case 1:
			r = eq_string_append_n(s, text, n);
			if (r == EQ_SUCCESS)
				ref_string_insert(length, text, n);
			break;
		case 2:
			from = random_below(length);
			if (n > length - from)
				n = length - from;
			r = eq_string_append_n(s, s->data + from, n);
			if (r == EQ_SUCCESS)
				ref_string_insert(length, string_ref + from, n);
			break;
		case 3:
			written = snprintf(formatted, sizeof(formatted), "%d:%s|", number, text);
			r = eq_string_append_format(s, "%d:%s|", number, text);
			if (r == EQ_SUCCESS)
				ref_string_insert(length, formatted, TEST_CAST(size_t, written));
			break;
		case 4:
			index = random_below(length + 1);
			r = eq_string_insert(s, index, text, n);
			if (r == EQ_SUCCESS)
				ref_string_insert(index, text, n);
			break;
		case 5:
			index = random_below(length + 1);
			from = random_below(length);
			if (n > length - from)
				n = length - from;
			r = eq_string_insert(s, index, s->data + from, n);
			if (r == EQ_SUCCESS)
				ref_string_insert(index, string_ref + from, n);
			break;
		case 6:
			index = random_below(length + 1);
			n = random_below(length - index + 1);
			r = eq_string_remove(s, index, n);
			if (r == EQ_SUCCESS) {
				memmove(string_ref + index, string_ref + index + n, length - index - n + 1);
				string_ref_length -= n;
			}
			break;
		case 7:
			n = random_below(STRING_MAX);
			r = eq_string_reserve(s, n);
			if (r == EQ_SUCCESS)
				CHECK(s->capacity >= n);
			break;
		case 8:
			if (random_below(6) == 0) {
				r = eq_string_clear(s);
				if (r == EQ_SUCCESS) {
					CHECK(s->capacity == capacity_before);
					string_ref_length = 0;
					string_ref[0] = '\0';
				}
			}
			break;
		default:
			r = eq_string_append(s, "");
			break;
		}
		if (r == EQ_RESULT_OUT_OF_MEMORY && allow_oom) {
			out_of_memory++;
			CHECK(s->length == length);
			CHECK(s->capacity == capacity_before);
			CHECK(s->data == data_before);
		} else {
			CHECK(r == EQ_SUCCESS);
		}
		check_string(s);
	}
	return out_of_memory;
}

static void test_string_random(const EQ_ALLOCATOR *allocator, int with_buffer)
{
	EQ_STRING s;
	char buffer[16];
	random_state = 0x1234567u;
	string_ref_length = 0;
	string_ref[0] = '\0';
	if (with_buffer) {
		eq_string_init_buffer(&s, allocator, buffer, sizeof(buffer));
		CHECK(s.data == buffer);
		CHECK(s.capacity == sizeof(buffer) - 1);
		CHECK(s.buffer == buffer);
	} else {
		eq_string_init(&s, allocator);
		CHECK(s.data == NULL);
		CHECK(s.capacity == 0);
	}
	CHECK(s.allocator == allocator);
	CHECK(strcmp(eq_string_c_str(&s), "") == 0);
	CHECK(run_string_ops(&s, 4000, 0) == 0);
	eq_string_free(&s);
	CHECK(s.data == NULL);
	CHECK(s.length == 0);
	CHECK(s.capacity == 0);
	CHECK(s.buffer == NULL);
	CHECK(s.allocator == allocator);
	CHECK(strcmp(eq_string_c_str(&s), "") == 0);
}

static void test_string_out_of_memory(int with_resize, int with_buffer)
{
	EQ_ALLOCATOR allocator = counting_allocator(&counting_state, with_resize);
	size_t fail_at;
	for (fail_at = 0;; fail_at++) {
		EQ_STRING s;
		char buffer[16];
		counting_reset(&counting_state);
		counting_state.fail_at = fail_at;
		random_state = 0x7654321u;
		string_ref_length = 0;
		string_ref[0] = '\0';
		if (with_buffer)
			eq_string_init_buffer(&s, &allocator, buffer, sizeof(buffer));
		else
			eq_string_init(&s, &allocator);
		run_string_ops(&s, 300, 1);
		eq_string_free(&s);
		CHECK(counting_state.live == 0);
		CHECK(counting_state.errors == 0);
		if (counting_state.failed == 0)
			break;
	}
	CHECK(fail_at > 0);
}

static void test_string_cases(void)
{
	EQ_STRING s = TEST_ZERO;
	EQ_STRING *none = NULL;
	EQ_ALLOCATOR broken;
	EQ_ALLOCATOR allocator = counting_allocator(&counting_state, 1);
	char buffer[8];

	current_test = "string cases";
	CHECK(strcmp(eq_string_c_str(&s), "") == 0);
	CHECK(strcmp(eq_string_c_str(NULL), "") == 0);
	CHECK(eq_string_append(&s, NULL) == EQ_RESULT_INVALID_ARGUMENT);
	CHECK(eq_string_append_n(&s, NULL, 0) == EQ_SUCCESS);
	CHECK(eq_string_append_n(&s, NULL, 3) == EQ_RESULT_INVALID_ARGUMENT);
	CHECK(eq_string_insert(&s, 1, "x", 1) == EQ_RESULT_OUT_OF_RANGE);
	CHECK(eq_string_remove(&s, 0, 1) == EQ_RESULT_OUT_OF_RANGE);
	CHECK(eq_string_reserve(&s, SIZE_MAX) == EQ_RESULT_TOO_LARGE);
	CHECK(eq_string_append_format(&s, "%s=%d", "walls", 4) == EQ_SUCCESS);
	CHECK(strcmp(eq_string_c_str(&s), "walls=4") == 0);
	CHECK(eq_string_append_format(&s, "%s", "") == EQ_SUCCESS);
	CHECK(s.length == 7);
	CHECK(eq_string_remove(&s, 5, SIZE_MAX) == EQ_RESULT_OUT_OF_RANGE);
	CHECK(eq_string_append_n(&s, "ab\0cd", 5) == EQ_SUCCESS);
	CHECK(s.length == 12);
	CHECK(memcmp(s.data + 7, "ab\0cd", 6) == 0);
	CHECK(eq_string_clear(&s) == EQ_SUCCESS);
	CHECK(strcmp(eq_string_c_str(&s), "") == 0);
	eq_string_free(&s);

	CHECK(eq_string_append(none, "x") == EQ_RESULT_INVALID_ARGUMENT);
	CHECK(eq_string_append_n(none, "x", 1) == EQ_RESULT_INVALID_ARGUMENT);
	CHECK(eq_string_insert(none, 0, "x", 1) == EQ_RESULT_INVALID_ARGUMENT);
	CHECK(eq_string_remove(none, 0, 0) == EQ_RESULT_INVALID_ARGUMENT);
	CHECK(eq_string_reserve(none, 1) == EQ_RESULT_INVALID_ARGUMENT);
	CHECK(eq_string_append_format(none, "%d", 1) == EQ_RESULT_INVALID_ARGUMENT);
	CHECK(eq_string_clear(none) == EQ_RESULT_INVALID_ARGUMENT);
	eq_string_init(none, NULL);
	eq_string_init_buffer(none, NULL, buffer, sizeof(buffer));
	eq_string_free(none);

	memset(&broken, 0, sizeof(broken));
	eq_string_init(&s, &broken);
	CHECK(eq_string_append(&s, "x") == EQ_RESULT_INVALID_ALLOCATOR);
	CHECK(s.data == NULL);
	eq_string_free(&s);

	/* A caller's buffer holds text up to its size less one, allocating
	 * nothing, and is never handed to the allocator. */
	counting_reset(&counting_state);
	eq_string_init_buffer(&s, &allocator, buffer, sizeof(buffer));
	CHECK(eq_string_append(&s, "1234567") == EQ_SUCCESS);
	CHECK(s.data == buffer);
	CHECK(counting_state.calls == 0);
	CHECK(eq_string_append(&s, "8") == EQ_SUCCESS);
	CHECK(s.data != buffer);
	CHECK(counting_state.live == 1);
	CHECK(strcmp(eq_string_c_str(&s), "12345678") == 0);
	eq_string_free(&s);
	CHECK(counting_state.live == 0);
	CHECK(counting_state.errors == 0);
	/* A buffer of 0 bytes is none. */
	eq_string_init_buffer(&s, NULL, buffer, 0);
	CHECK(s.data == NULL);
	CHECK(s.buffer == NULL);
	eq_string_free(&s);
}

int main(void)
{
	EQ_ALLOCATOR with_resize = counting_allocator(&counting_state, 1);
	EQ_ALLOCATOR without_resize = counting_allocator(&counting_state, 0);

	test_result_names();
	test_allocator_functions();
	test_array_arguments();
	test_all_int();
	test_all_three();
	test_all_twenty_four();
	test_all_over_aligned();

	test_string_cases();
	current_test = "string random, system heap";
	test_string_random(NULL, 0);
	current_test = "string random, system heap and a buffer";
	test_string_random(NULL, 1);
	current_test = "string random, allocator with resize";
	counting_reset(&counting_state);
	test_string_random(&with_resize, 0);
	CHECK(counting_state.live == 0);
	CHECK(counting_state.errors == 0);
	current_test = "string random, allocator without resize and a buffer";
	counting_reset(&counting_state);
	test_string_random(&without_resize, 1);
	CHECK(counting_state.live == 0);
	CHECK(counting_state.errors == 0);
	current_test = "string out of memory";
	test_string_out_of_memory(1, 0);
	test_string_out_of_memory(0, 0);
	test_string_out_of_memory(1, 1);
	test_string_out_of_memory(0, 1);

	if (failures != 0) {
		printf("eqstd C test (%s): %d failure(s)\n", EQSTD_TEST_LANGUAGE, failures);
		return 1;
	}
	printf("eqstd C test (%s): all passed\n", EQSTD_TEST_LANGUAGE);
	return 0;
}
