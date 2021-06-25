#pragma once

#include <coroutine>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <stdlib.h>
#include <xmmintrin.h>
#include <vector>

namespace edce {

template<typename Object>
struct LargerRecyclingFixedSizeAllocator {
	constexpr static size_t MAX_CONCURRENT_OBJS = 8192;
	constexpr static uint8_t NUM_FLAG_WORDS = MAX_CONCURRENT_OBJS / 64;

	constexpr static size_t OBJ_SIZE = 512;

	void* ptr;
	void* aligned_ptr;

	uint64_t bv[NUM_FLAG_WORDS];

	LargerRecyclingFixedSizeAllocator()
	: ptr(nullptr) {
		size_t bytes_len = OBJ_SIZE * MAX_CONCURRENT_OBJS + 512;
		ptr = malloc(bytes_len);
		aligned_ptr = ptr;

		for (size_t i = 0; i < NUM_FLAG_WORDS; i++) {
			bv[i] = UINT64_MAX;
		}
		//aligned_ptr = std::align(512, OBJ_SIZE, aligned_ptr, bytes_len);
	}

	constexpr ~LargerRecyclingFixedSizeAllocator() {
		free(ptr);
	}

	void* allocate() {
		int word_idx = 0;
		int word_offset = 0;
		while(true) {
			word_offset = __builtin_ffsll(bv[word_idx]);
		
			if (word_offset == 0) {
				word_idx++;
//				if (word_idx >= NUM_FLAG_WORDS) {
//					std::printf("word_idx %lu words %lu\n", word_idx, NUM_FLAG_WORDS);
//				}
			} else {
				break;
			}
		}
		//while((word_offset = __builtin_ffsll(bv[word_idx])) == 0) {word_idx++;}
		
//		if (word_idx >= NUM_FLAG_WORDS) {
//			std::printf("word index is too high!!!\n");
//		}

		auto offset = word_offset - 1;
		bv[word_idx] &= ( ~( ((uint64_t)1) << offset));

		void* out_ptr = ((unsigned char*)aligned_ptr) + ((offset + word_idx * 64) * OBJ_SIZE);

//		std::printf("request out: offset %lu word_idx %lu output %p, initial %p\n", offset, word_idx, out_ptr, aligned_ptr);

		return out_ptr;// ((unsigned char*)aligned_ptr) + ((offset + word_idx*64)* OBJ_SIZE);


	}
	void deallocate(void* free_ptr) {
		auto offset_bytes = (unsigned char*) free_ptr - (unsigned char*) aligned_ptr;
		auto offset = offset_bytes/ OBJ_SIZE;
		auto word_idx = offset / 64;
		auto word_offset = offset % 64;

//		std::printf("freeing word %lu offset %lu\n", word_idx, word_offset);		
//		if (word_idx >= NUM_FLAG_WORDS) {
//			std::printf("tried to free %p off of %p, word_idx %ld\n", free_ptr, aligned_ptr, word_idx);
//		}
			
		bv[word_idx] |= ( ((uint64_t)1) << word_offset);
	}
};

//not threadsafe
template<typename Object>
struct RecyclingFixedSizeAllocator {

	constexpr static uint8_t MAX_CONCURRENT_OBJS = 64;
	constexpr static size_t OBJ_SIZE = 512;

	void* ptr;

	void* aligned_ptr;

	uint64_t bv;

	RecyclingFixedSizeAllocator();
//		: ptr(nullptr)
//		, bv(UINT64_MAX) {
//			size_t bytes_len = sizeof(Object) * MAX_CONCURRENT_OBJS;
//			ptr = malloc(bytes_len);
//		}

	constexpr ~RecyclingFixedSizeAllocator() {
		free(ptr);
	}

	void* allocate();
	void deallocate(void* free_ptr);
};


struct CoroutineThrottler;

struct NoOpTask {
	struct promise_type {
		inline static thread_local LargerRecyclingFixedSizeAllocator<promise_type> allocator = LargerRecyclingFixedSizeAllocator<promise_type>();


		NoOpTask get_return_object() {
			return {};
		}

		std::suspend_never initial_suspend() { return {}; }
		std::suspend_never final_suspend() noexcept { return {}; }

		void unhandled_exception() {}

		void return_void() {}

		static void* operator new(size_t sz) {
			
		//	if (sz > 512) {
			
//				std::printf("requesting %lu\n", sz);	
		//	}
			return allocator.allocate();
		}

		static void operator delete(void* ptr, size_t sz) {
//			if (sz > 512) {
//				std::printf("deallocated too large! %lu\n", sz);
//			}
			allocator.deallocate(ptr);
			//std::printf("done deallocate\n");
		}
	};
};
struct RootTask {
	struct promise_type {

		inline static thread_local RecyclingFixedSizeAllocator<promise_type> allocator = RecyclingFixedSizeAllocator<promise_type>();

		//CoroutineThrottler* owner = nullptr;

		RootTask get_return_object() {
			//return {}
			return {
				.h_ = std::coroutine_handle<promise_type>::from_promise(*this)
			};
		}

		std::suspend_always initial_suspend() { return {}; }
		std::suspend_never final_suspend() noexcept {return {};}// { owner -> on_task_done(); return {}; }

		void unhandled_exception() {};
		//TODO recycling allocator

		void return_void() {}

		static void* operator new(size_t sz) {
			//std::printf("requesting %lu\n", sz);	
			return allocator.allocate();
		}

		static void operator delete(void* ptr, size_t sz) {
			allocator.deallocate(ptr);
			//std::printf("done deallocate\n");
		}
	};

	//auto set_owner(CoroutineThrottler* throttler) {
	//	auto out = h_;
	//	h_.promise().owner = throttler;
	//	h_ = nullptr;
	//	return out;
	//}

	//~RootTask() { if (h_) { h_.destroy(); } }

	std::coroutine_handle<promise_type> h_;
	//operator std::coroutine_handle<promise_type>() const {return h_; }
};

struct Scheduler {
	using handle_t = std::coroutine_handle<>;
	
	const uint8_t QUEUE_SIZE;
	size_t start = 0;
	size_t end = 0;
	size_t sz = 0;

	std::vector<handle_t> handles;

	Scheduler(const uint8_t QUEUE_SIZE)
		: QUEUE_SIZE(QUEUE_SIZE)
		, start(0)
		, end(0)
		, sz(0) 
		{
			handles.resize(QUEUE_SIZE + 2);
		}
	//handle_t handles[QUEUE_SIZE];

	void push_back(handle_t handle) {
		//std::printf("push_back start %lu end %lu sz %lu\n", start, end, sz);

		handles[end] = handle;
		end = (end + 1) % handles.size();
		sz++;
	}

	bool full() const {
		return sz >= QUEUE_SIZE;
	}

	handle_t pop_front() {

		//std::printf("pop_front start %lu end %lu sz %lu\n", start, end, sz);
		auto out = handles[start];
		start = (start + 1) % handles.size();
		sz--;
		return out;
	}

	handle_t try_pop_front() { return sz != 0 ? pop_front() : handle_t{}; }
	void run() { 
		while (auto h = try_pop_front()) h.resume(); 
	}
};


struct CoroutineThrottler {

	const uint8_t QUEUE_SIZE;

	//constexpr static size_t QUEUE_SIZE = 5;
	//constexpr static size_t QUEUE_SIZE = 5;
	//constexpr static size_t SCHEDULER_SIZE = QUEUE_SIZE + 2;
	
	using scheduler_t = Scheduler;
	using handle_t = typename scheduler_t::handle_t;

	scheduler_t scheduler;

	CoroutineThrottler(uint8_t QUEUE_SIZE)
		: QUEUE_SIZE(QUEUE_SIZE)
		, scheduler(QUEUE_SIZE) {}

	void spawn(RootTask task) {
		//std::printf("spawning task, limit = %lu\n", limit);
		//this type of pattern doesn't really work when coroutines spawn more coroutines
	//	while (limit == 0) {
	//		scheduler.pop_front().resume();
	//	}
		//std::printf("finished clearing, limit = %lu\n", limit);

		//auto handle = task.set_owner(this);
		scheduler.push_back(task.h_);
		//--limit;
	}

	bool full() const {
		return scheduler.full();
	}

	/*bool empty() {
		return limit >= QUEUE_SIZE;
	}

	bool full() {
		return limit == 0;
	}

	void on_task_done() {
		++limit;
	}*/

	void join() {
		scheduler.run();
	}
};

template<typename PrefetchType, typename Scheduler>
struct PrefetchAwaiter {
	PrefetchType* value;
	Scheduler& scheduler;


	PrefetchAwaiter(PrefetchType* ptr, Scheduler& scheduler) 
		: value(ptr)
		, scheduler(scheduler)	{}

	bool await_ready() { return false; }

	auto await_suspend(std::coroutine_handle<> handle) {

//		_mm_prefetch(static_cast<const void*>(value), _MM_HINT_NTA);		
//		_mm_prefetch(static_cast<const void*>(value), _MM_HINT_T2);
		__builtin_prefetch(static_cast<const void*>(value), 1, 2);
		scheduler.push_back(handle);
		return scheduler.pop_front();
	}

	PrefetchType& await_resume() {
		return *value;
	}
};

template<typename PrefetchType, typename Scheduler>
struct WriteAwaiter {
	PrefetchType* value;
	Scheduler& scheduler;

	bool await_ready() {return false;}

	auto await_suspend(std::coroutine_handle<> handle) {
		__builtin_prefetch(static_cast<const void*>(value), 1, 2);
		//_mm_prefetch(static_cast<const void*>(value), _MM_HINT_NTA);
		scheduler.push_back(handle);
		return scheduler.pop_front();
	}

	PrefetchType& await_resume() {
		return *value;
	}
};

template<typename PrefetchType, typename Scheduler>
struct ReadAwaiter {
	PrefetchType* value;
	Scheduler& scheduler;

	inline static thread_local LargerRecyclingFixedSizeAllocator<ReadAwaiter> allocator = LargerRecyclingFixedSizeAllocator<ReadAwaiter>();

	bool await_ready() {return false;}

	auto await_suspend(std::coroutine_handle<> handle) {

		_mm_prefetch(static_cast<const void*>(value), _MM_HINT_NTA);
		//__builtin_prefetch(static_cast<const void*>(value), 1, 2);
		scheduler.push_back(handle);
		return scheduler.pop_front();
	}

	PrefetchType& await_resume() {
		return *value;
	}

	static void* operator new(size_t sz) {
		return allocator.allocate();
	}

	static void operator delete(void* ptr, size_t sz) {
		allocator.deallocate(ptr);
	}
};


} /* edce */
