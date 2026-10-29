// Containers on different threads at once, each used by one thread, some
// sharing an allocator: eqstd keeps no state of its own, so nothing here may
// race. Meaningful under the -tsan presets; a plain check elsewhere.

#include "eqstd.hpp"

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// A thread-safe allocator over the system heap that counts live blocks.
struct shared_heap {
	std::mutex lock;
	long live = 0;
	long errors = 0;

	static void *allocate(void *user_data, size_t size, size_t alignment)
	{
		shared_heap *h = static_cast<shared_heap *>(user_data);
		void *memory = eq_allocate(nullptr, size, alignment);
		std::lock_guard<std::mutex> guard(h->lock);
		if (memory != nullptr)
			h->live++;
		return memory;
	}
	static void free(void *user_data, void *memory, size_t size, size_t alignment)
	{
		shared_heap *h = static_cast<shared_heap *>(user_data);
		eq_free(nullptr, memory, size, alignment);
		std::lock_guard<std::mutex> guard(h->lock);
		h->live--;
	}
	static void *resize(void *user_data, void *memory, size_t old_size, size_t new_size, size_t alignment)
	{
		(void)user_data;
		return eq_resize(nullptr, memory, old_size, new_size, alignment);
	}
};

static std::atomic<int> failures{0};

static void work(const EQ_ALLOCATOR *allocator, uint32_t seed)
{
	uint32_t state = seed;
	auto next = [&state]() {
		state ^= state << 13;
		state ^= state >> 17;
		state ^= state << 5;
		return state;
	};
	eq::array<uint32_t> a(allocator);
	eq::vector<eq::string> v(allocator);
	eq::string s(allocator);
	EQ_STRING builder;
	eq_string_init(&builder, allocator);
	std::vector<uint32_t> a_ref;
	std::vector<std::string> v_ref;
	std::string s_ref;
	std::string builder_ref;
	for (int op = 0; op < 20000; op++) {
		uint32_t value = next();
		bool ok = true;
		switch (value % 6) {
		case 0:
			ok = a.push(value) == EQ_SUCCESS;
			a_ref.push_back(value);
			break;
		case 1:
			if (!a_ref.empty()) {
				ok = a.remove_swap(value % a_ref.size()) == EQ_SUCCESS;
				a_ref[value % a_ref.size()] = a_ref.back();
				a_ref.pop_back();
			}
			break;
		case 2: {
			eq::string item(allocator);
			std::string text(value % 50, static_cast<char>('a' + value % 26));
			ok = item.assign(text) == EQ_SUCCESS && v.push(std::move(item)) == EQ_SUCCESS;
			v_ref.push_back(text);
			break;
		}
		case 3:
			if (!v_ref.empty() && value % 3 == 0) {
				ok = v.remove(0) == EQ_SUCCESS;
				v_ref.erase(v_ref.begin());
			}
			break;
		case 4:
			ok = s.append(static_cast<char>('a' + value % 26)) == EQ_SUCCESS;
			s_ref += static_cast<char>('a' + value % 26);
			if (s_ref.size() > 3000) {
				s.clear();
				s_ref.clear();
			}
			break;
		default:
			ok = eq_string_append_format(&builder, "%u,", value % 1000) == EQ_SUCCESS;
			builder_ref += std::to_string(value % 1000) + ",";
			if (builder_ref.size() > 3000) {
				ok = ok && eq_string_clear(&builder) == EQ_SUCCESS;
				builder_ref.clear();
			}
			break;
		}
		if (!ok)
			failures++;
	}
	bool same = a.size() == a_ref.size() && v.size() == v_ref.size() && s.view() == s_ref &&
		std::string_view(eq_string_c_str(&builder)) == builder_ref;
	for (size_t i = 0; same && i < a_ref.size(); i++)
		same = a[i] == a_ref[i];
	for (size_t i = 0; same && i < v_ref.size(); i++)
		same = v[i].view() == v_ref[i];
	if (!same)
		failures++;
	eq_string_free(&builder);
}

int main()
{
	shared_heap heap;
	EQ_ALLOCATOR shared = {&heap, &shared_heap::allocate, &shared_heap::free, &shared_heap::resize};
	{
		std::vector<std::thread> threads;
		for (uint32_t i = 0; i < 8; i++)
			threads.emplace_back(work, i % 2 == 0 ? &shared : nullptr, 0x1000193u * (i + 1));
		for (std::thread &t : threads)
			t.join();
	}
	if (heap.live != 0) {
		std::printf("eqstd threads test: %ld block(s) not freed\n", heap.live);
		failures++;
	}
	if (failures != 0) {
		std::printf("eqstd threads test: %d failure(s)\n", failures.load());
		return 1;
	}
	std::printf("eqstd threads test: all passed\n");
	return 0;
}
