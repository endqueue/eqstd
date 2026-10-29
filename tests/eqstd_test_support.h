/* What the C tests share: CHECK, a fixed random sequence, and a counting
 * allocator that can be told to fail. Compiled as C, C++ and Objective-C. */
#ifndef EQSTD_TEST_SUPPORT_H
#define EQSTD_TEST_SUPPORT_H

#include "eqstd.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __cplusplus
#define TEST_CAST(T, x) (static_cast<T>(x))
#define TEST_ADDRESS(p) (reinterpret_cast<uintptr_t>(p))
#define TEST_ALIGNAS(n) alignas(n)
#define TEST_ZERO {}
#else
#define TEST_CAST(T, x) ((T)(x))
#define TEST_ADDRESS(p) ((uintptr_t)(p))
#define TEST_ALIGNAS(n) _Alignas(n)
#define TEST_ZERO {0}
#endif

static int failures = 0;
static const char *current_test = "";

#define CHECK(condition)                                                                                               \
	do {                                                                                                               \
		if (!(condition)) {                                                                                            \
			printf("FAIL %s (%s:%d): %s\n", current_test, __FILE__, __LINE__, #condition);                             \
			failures++;                                                                                                \
		}                                                                                                              \
	} while (0)

/* The same sequence on every platform and every run. */
static uint32_t random_state = 0x2545F491u;
static uint32_t next_random(void)
{
	random_state ^= random_state << 13;
	random_state ^= random_state >> 17;
	random_state ^= random_state << 5;
	return random_state;
}
static size_t random_below(size_t n)
{
	return n == 0 ? 0 : TEST_CAST(size_t, next_random()) % n;
}

/* ---- A counting allocator ------------------------------------------------
 * Records every block with its size and alignment, checks each free and
 * resize against them, and fails the call numbered `fail_at` (counting
 * allocate and resize calls from 0). New memory is filled with 0xCD so a
 * test can't pass by relying on zeroed memory. It allocates with malloc and
 * aligns by hand, independently of eqstd's own system heap. */

#define COUNTING_MAX_BLOCKS 4096
#define COUNTING_NEVER SIZE_MAX

typedef struct COUNTING_BLOCK {
	void *memory;
	void *raw;
	size_t size;
	size_t alignment;
} COUNTING_BLOCK;

typedef struct COUNTING {
	COUNTING_BLOCK blocks[COUNTING_MAX_BLOCKS];
	size_t live;
	size_t calls;
	size_t frees;
	size_t fail_at;
	size_t failed;
	size_t errors;
} COUNTING;

static int counting_is_power_of_two(size_t x)
{
	return x != 0 && (x & (x - 1)) == 0;
}

static void *counting_raw_allocate(size_t size, size_t alignment, void **raw)
{
	unsigned char *bytes = TEST_CAST(unsigned char *, malloc(size + alignment));
	size_t offset;
	if (bytes == NULL)
		return NULL;
	offset = (alignment - TEST_ADDRESS(bytes) % alignment) % alignment;
	*raw = bytes;
	memset(bytes + offset, 0xCD, size);
	return bytes + offset;
}

static size_t counting_find(COUNTING *c, void *memory)
{
	size_t i;
	for (i = 0; i < c->live; i++)
		if (c->blocks[i].memory == memory)
			return i;
	return SIZE_MAX;
}

static int counting_should_fail(COUNTING *c)
{
	size_t call = c->calls++;
	if (call == c->fail_at) {
		c->failed++;
		return 1;
	}
	return 0;
}

static void *counting_allocate(void *user_data, size_t size, size_t alignment)
{
	COUNTING *c = TEST_CAST(COUNTING *, user_data);
	void *raw = NULL;
	void *memory;
	if (size == 0 || !counting_is_power_of_two(alignment)) {
		c->errors++;
		return NULL;
	}
	if (counting_should_fail(c))
		return NULL;
	if (c->live == COUNTING_MAX_BLOCKS) {
		c->errors++;
		return NULL;
	}
	memory = counting_raw_allocate(size, alignment, &raw);
	if (memory == NULL)
		return NULL;
	c->blocks[c->live].memory = memory;
	c->blocks[c->live].raw = raw;
	c->blocks[c->live].size = size;
	c->blocks[c->live].alignment = alignment;
	c->live++;
	return memory;
}

static void counting_free(void *user_data, void *memory, size_t size, size_t alignment)
{
	COUNTING *c = TEST_CAST(COUNTING *, user_data);
	size_t at = counting_find(c, memory);
	if (memory == NULL || at == SIZE_MAX) {
		printf("counting_free: a block this allocator didn't give\n");
		c->errors++;
		return;
	}
	if (c->blocks[at].size != size || c->blocks[at].alignment != alignment) {
		printf("counting_free: freed with size %zu alignment %zu, allocated with %zu %zu\n", size, alignment,
			c->blocks[at].size, c->blocks[at].alignment);
		c->errors++;
	}
	free(c->blocks[at].raw);
	c->blocks[at] = c->blocks[c->live - 1];
	c->live--;
	c->frees++;
}

static void *counting_resize(void *user_data, void *memory, size_t old_size, size_t new_size, size_t alignment)
{
	COUNTING *c = TEST_CAST(COUNTING *, user_data);
	size_t at = counting_find(c, memory);
	void *raw = NULL;
	void *moved;
	if (memory == NULL || at == SIZE_MAX || old_size == 0 || new_size == 0) {
		c->errors++;
		return NULL;
	}
	if (c->blocks[at].size != old_size || c->blocks[at].alignment != alignment) {
		printf("counting_resize: resized with size %zu alignment %zu, block has %zu %zu\n", old_size, alignment,
			c->blocks[at].size, c->blocks[at].alignment);
		c->errors++;
	}
	if (counting_should_fail(c))
		return NULL;
	/* Always moves, so a test that keeps a stale pointer fails. */
	moved = counting_raw_allocate(new_size, alignment, &raw);
	if (moved == NULL)
		return NULL;
	memcpy(moved, memory, old_size < new_size ? old_size : new_size);
	free(c->blocks[at].raw);
	c->blocks[at].memory = moved;
	c->blocks[at].raw = raw;
	c->blocks[at].size = new_size;
	return moved;
}

static void counting_reset(COUNTING *c)
{
	memset(c, 0, sizeof(*c));
	c->fail_at = COUNTING_NEVER;
}

static EQ_ALLOCATOR counting_allocator(COUNTING *c, int with_resize)
{
	EQ_ALLOCATOR a;
	a.user_data = c;
	a.allocate = counting_allocate;
	a.free = counting_free;
	a.resize = with_resize ? counting_resize : NULL;
	return a;
}

#endif /* EQSTD_TEST_SUPPORT_H */
