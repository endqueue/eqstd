/* The Debug index checks, turned on here in every configuration, with an
 * EQ_INDEX_ERROR that records the failure and jumps back to the test. As C,
 * and as C++ through eqstd_index_cpp_test.cpp, which adds eq::array's,
 * eq::vector's and eq::string's operator[]. */

#include <setjmp.h>
#include <stddef.h>
#include <stdio.h>

/* MSVC warns (C4611) at every setjmp in C++. The jumps here cross only
 * frames with no destructors to run (the accessors and on_index_error),
 * which C++ allows. */
#if defined(_MSC_VER) && defined(__cplusplus)
#pragma warning(disable : 4611)
#endif

static jmp_buf index_error_jump;
static size_t index_error_index;
static size_t index_error_count;
static int index_error_calls;
static const char *index_error_file;

static void on_index_error(size_t index, size_t count, const char *file, int line)
{
	(void)line;
	index_error_index = index;
	index_error_count = count;
	index_error_file = file;
	index_error_calls++;
	longjmp(index_error_jump, 1);
}

#define EQ_DEBUG 1
#define EQ_INDEX_ERROR(index, count, file, line) on_index_error((index), (count), (file), (line))
#ifdef __cplusplus
#include "eqstd.hpp"
#else
#include "eqstd.h"
#endif

#include <string.h>

static int failures = 0;

#define CHECK(condition)                                                                                               \
	do {                                                                                                               \
		if (!(condition)) {                                                                                            \
			printf("FAIL (%s:%d): %s\n", __FILE__, __LINE__, #condition);                                              \
			failures++;                                                                                                \
		}                                                                                                              \
	} while (0)

typedef EQ_ARRAY(int) INT_ARRAY;

/* Reads element `index` through the checked accessor; returns 1 if the check
 * caught it. volatile: set between setjmp and longjmp. */
static int read_caught(INT_ARRAY *a, long index, int *value)
{
	volatile int caught = 0;
	if (setjmp(index_error_jump) == 0)
		*value = eq_array_at(a, index);
	else
		caught = 1;
	return caught;
}

int main(void)
{
#ifdef __cplusplus
	INT_ARRAY a = {};
#else
	INT_ARRAY a = {0};
#endif
	int values[3] = {10, 20, 30};
	int value = 0;

	CHECK(eq_array_append(&a, values, 3) == EQ_SUCCESS);
	CHECK(read_caught(&a, 2, &value) == 0);
	CHECK(value == 30);
	CHECK(index_error_calls == 0);
	eq_array_at(&a, 1) = 21;
	CHECK(a.data[1] == 21);

	CHECK(read_caught(&a, 3, &value) == 1);
	CHECK(index_error_calls == 1);
	CHECK(index_error_index == 3);
	CHECK(index_error_count == 3);
	CHECK(index_error_file != NULL && strstr(index_error_file, "eqstd_index_test") != NULL);

	CHECK(read_caught(&a, -1, &value) == 1);
	CHECK(index_error_index == SIZE_MAX);

	eq_array_free(&a);
	CHECK(read_caught(&a, 0, &value) == 1);
	CHECK(index_error_count == 0);

#ifdef __cplusplus
	{
		eq::array<int> b;
		eq::vector<int> v;
		eq::string s;
		volatile int caught = 0;
		CHECK(b.push(5) == EQ_SUCCESS);
		CHECK(v.push(6) == EQ_SUCCESS);
		CHECK(s.append("x") == EQ_SUCCESS);
		CHECK(b[0] == 5);
		CHECK(v[0] == 6);
		CHECK(s[0] == 'x');
		index_error_calls = 0;
		if (setjmp(index_error_jump) == 0)
			value = b[1];
		else
			caught = caught + 1;
		if (setjmp(index_error_jump) == 0)
			value = v[1];
		else
			caught = caught + 1;
		if (setjmp(index_error_jump) == 0)
			value = s[1];
		else
			caught = caught + 1;
		CHECK(caught == 3);
		CHECK(index_error_calls == 3);
		CHECK(index_error_index == 1);
		CHECK(index_error_count == 1);
	}
#endif

	if (failures != 0) {
		printf("eqstd index test: %d failure(s)\n", failures);
		return 1;
	}
	printf("eqstd index test: all passed\n");
	return 0;
}
