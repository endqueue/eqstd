/* The C array tests for one element type, included once per type by
 * eqstd_c_test.c with TEST_ELEMENT and TEST_NAME defined. Every operation is
 * checked against a plain reference array kept beside it. */

#define TEST_CONCAT2(a, b) a##b
#define TEST_CONCAT(a, b) TEST_CONCAT2(a, b)
#define TEST_FN(x) TEST_CONCAT(x, TEST_NAME)
#define TEST_ARRAY TEST_FN(ARRAY_)
#define TEST_MAX 400

typedef EQ_ARRAY(TEST_ELEMENT) TEST_ARRAY;

static TEST_ELEMENT TEST_FN(ref_)[TEST_MAX + 16];
static size_t TEST_FN(ref_count_);

static void TEST_FN(random_element_)(TEST_ELEMENT *out)
{
	unsigned char *bytes = TEST_CAST(unsigned char *, TEST_CAST(void *, out));
	size_t i;
	for (i = 0; i < sizeof(*out); i++)
		bytes[i] = TEST_CAST(unsigned char, next_random());
}

static void TEST_FN(check_same_)(const TEST_ARRAY *a)
{
	size_t i;
	CHECK(a->count == TEST_FN(ref_count_));
	CHECK(a->count <= a->capacity);
	CHECK((a->data == NULL) == (a->capacity == 0));
	if (a->data != NULL)
		CHECK(TEST_ADDRESS(a->data) % EQ_ARRAY_ALIGNMENT(sizeof(TEST_ELEMENT)) == 0);
	if (a->count != TEST_FN(ref_count_))
		return;
	for (i = 0; i < a->count; i++)
		CHECK(memcmp(&eq_array_at(a, i), &TEST_FN(ref_)[i], sizeof(TEST_ELEMENT)) == 0);
}

/* Inserts n elements copied from `from` at `index` in the reference. `from`
 * may be the reference's own elements. */
static void TEST_FN(ref_insert_)(size_t index, const TEST_ELEMENT *from, size_t n)
{
	TEST_ELEMENT copy[16];
	TEST_ELEMENT *ref = TEST_FN(ref_);
	if (n == 0)
		return;
	memcpy(copy, from, n * sizeof(TEST_ELEMENT));
	memmove(ref + index + n, ref + index, (TEST_FN(ref_count_) - index) * sizeof(TEST_ELEMENT));
	memcpy(ref + index, copy, n * sizeof(TEST_ELEMENT));
	TEST_FN(ref_count_) += n;
}

/* Runs `ops` random operations on `a` and the reference. With `allow_oom`,
 * EQ_RESULT_OUT_OF_MEMORY is accepted and must leave `a` as it was. Returns
 * how many operations failed that way. */
static size_t TEST_FN(run_ops_)(TEST_ARRAY *a, size_t ops, int allow_oom)
{
	TEST_ELEMENT *ref = TEST_FN(ref_);
	size_t out_of_memory = 0;
	size_t op;
	for (op = 0; op < ops; op++) {
		TEST_ELEMENT items[8];
		size_t count = a->count;
		size_t capacity_before = a->capacity;
		void *data_before = a->data;
		size_t room = TEST_MAX - TEST_FN(ref_count_);
		size_t n = random_below(9);
		size_t index = 0;
		size_t from = 0;
		size_t kind = random_below(11);
		size_t i;
		EQ_RESULT r = EQ_SUCCESS;
		if (n > room)
			n = room;
		for (i = 0; i < 8; i++)
			TEST_FN(random_element_)(&items[i]);
		/* The operations that copy from the array itself need elements. */
		if (count == 0 && (kind == 1 || kind == 3 || kind == 5 || kind == 7))
			kind = 2;
		if (room == 0 && kind <= 5)
			kind = 6;
		switch (kind) {
		case 0:
			r = eq_array_push(a, &items[0]);
			if (r == EQ_SUCCESS)
				TEST_FN(ref_insert_)(count, &items[0], 1);
			break;
		case 1:
			from = random_below(count);
			r = eq_array_push(a, &a->data[from]);
			if (r == EQ_SUCCESS)
				TEST_FN(ref_insert_)(count, &ref[from], 1);
			break;
		case 2:
			r = eq_array_append(a, items, n);
			if (r == EQ_SUCCESS)
				TEST_FN(ref_insert_)(count, items, n);
			break;
		case 3:
			from = random_below(count);
			if (n > count - from)
				n = count - from;
			r = eq_array_append(a, a->data + from, n);
			if (r == EQ_SUCCESS)
				TEST_FN(ref_insert_)(count, ref + from, n);
			break;
		case 4:
			index = random_below(count + 1);
			r = eq_array_insert(a, index, items, n);
			if (r == EQ_SUCCESS)
				TEST_FN(ref_insert_)(index, items, n);
			break;
		case 5:
			/* From the array's own elements, a range that may straddle the
			 * insertion point. */
			index = random_below(count + 1);
			from = random_below(count);
			if (n > count - from)
				n = count - from;
			r = eq_array_insert(a, index, a->data + from, n);
			if (r == EQ_SUCCESS)
				TEST_FN(ref_insert_)(index, ref + from, n);
			break;
		case 6:
			index = random_below(count + 1);
			n = random_below(count - index + 1);
			r = eq_array_remove(a, index, n);
			if (r == EQ_SUCCESS) {
				memmove(ref + index, ref + index + n, (count - index - n) * sizeof(TEST_ELEMENT));
				TEST_FN(ref_count_) -= n;
			}
			break;
		case 7:
			index = random_below(count);
			r = eq_array_remove_swap(a, index);
			if (r == EQ_SUCCESS) {
				ref[index] = ref[count - 1];
				TEST_FN(ref_count_)--;
			}
			break;
		case 8:
			n = random_below((count + 10 < TEST_MAX ? count + 10 : TEST_MAX) + 1);
			r = eq_array_resize(a, n);
			if (r == EQ_SUCCESS) {
				if (n > count)
					memset(TEST_CAST(void *, ref + count), 0, (n - count) * sizeof(TEST_ELEMENT));
				TEST_FN(ref_count_) = n;
			}
			break;
		case 9:
			n = random_below(TEST_MAX);
			r = eq_array_reserve(a, n);
			if (r == EQ_SUCCESS)
				CHECK(a->capacity >= n);
			break;
		default:
			if (random_below(8) == 0) {
				r = eq_array_clear(a);
				if (r == EQ_SUCCESS) {
					CHECK(a->capacity == capacity_before);
					TEST_FN(ref_count_) = 0;
				}
			}
			break;
		}
		if (r == EQ_RESULT_OUT_OF_MEMORY && allow_oom) {
			out_of_memory++;
			CHECK(a->count == count);
			CHECK(a->capacity == capacity_before);
			CHECK(a->data == data_before);
		} else {
			CHECK(r == EQ_SUCCESS);
		}
		TEST_FN(check_same_)(a);
	}
	return out_of_memory;
}

static void TEST_FN(test_random_)(const EQ_ALLOCATOR *allocator)
{
	TEST_ARRAY a;
	random_state = 0x2545F491u;
	eq_array_init(&a, allocator);
	CHECK(a.data == NULL);
	CHECK(a.count == 0);
	CHECK(a.capacity == 0);
	CHECK(a.allocator == allocator);
	TEST_FN(ref_count_) = 0;
	CHECK(TEST_FN(run_ops_)(&a, 4000, 0) == 0);
	eq_array_free(&a);
	CHECK(a.data == NULL);
	CHECK(a.count == 0);
	CHECK(a.capacity == 0);
	CHECK(a.allocator == allocator);
	/* Freed twice is still empty. */
	eq_array_free(&a);
	CHECK(a.data == NULL);
}

/* Out of memory at every allocation in turn, until a run makes no call that
 * could fail. */
static void TEST_FN(test_out_of_memory_)(int with_resize)
{
	EQ_ALLOCATOR allocator = counting_allocator(&counting_state, with_resize);
	size_t fail_at;
	size_t failures_seen = 0;
	for (fail_at = 0;; fail_at++) {
		TEST_ARRAY a;
		counting_reset(&counting_state);
		counting_state.fail_at = fail_at;
		random_state = 0x9E3779B9u;
		TEST_FN(ref_count_) = 0;
		eq_array_init(&a, &allocator);
		failures_seen += TEST_FN(run_ops_)(&a, 300, 1);
		eq_array_free(&a);
		CHECK(counting_state.live == 0);
		CHECK(counting_state.errors == 0);
		if (counting_state.failed == 0)
			break;
	}
	CHECK(fail_at > 0);
	CHECK(failures_seen >= fail_at);
}

static void TEST_FN(test_all_)(void)
{
	EQ_ALLOCATOR with_resize = counting_allocator(&counting_state, 1);
	EQ_ALLOCATOR without_resize = counting_allocator(&counting_state, 0);

	current_test = "array random, system heap";
	TEST_FN(test_random_)(NULL);

	current_test = "array random, allocator with resize";
	counting_reset(&counting_state);
	TEST_FN(test_random_)(&with_resize);
	CHECK(counting_state.live == 0);
	CHECK(counting_state.errors == 0);
	CHECK(counting_state.calls > 0);

	current_test = "array random, allocator without resize";
	counting_reset(&counting_state);
	TEST_FN(test_random_)(&without_resize);
	CHECK(counting_state.live == 0);
	CHECK(counting_state.errors == 0);

	current_test = "array out of memory, with resize";
	TEST_FN(test_out_of_memory_)(1);
	current_test = "array out of memory, without resize";
	TEST_FN(test_out_of_memory_)(0);
}

#undef TEST_ARRAY
#undef TEST_MAX
#undef TEST_FN
#undef TEST_CONCAT
#undef TEST_CONCAT2
