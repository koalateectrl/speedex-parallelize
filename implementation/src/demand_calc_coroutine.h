#pragma once

namespace edce {



template<typename Object>
struct DemandCalcRecyclingFixedSizeAllocator {

	constexpr static uint8_t MAX_CONCURRENT_OBJS = 64;
	constexpr static size_t OBJ_SIZE = 512;

	void* ptr;

	uint64_t bv;

	DemandCalcRecyclingFixedSizeAllocator()
		: ptr(nullptr)
		, bv(UINT64_MAX) {
			size_t bytes_len = OBJ_SIZE * MAX_CONCURRENT_OBJS;
			ptr = malloc(bytes_len);
			//std::printf("initializing recycler %p\n", ptr);

		}

	constexpr ~DemandCalcRecyclingFixedSizeAllocator() {
		//std::printf("running tls destructor on ptr %p\n", ptr);
		//throw std::runtime_error("what the fuck");
		free(ptr);
	}

	void* allocate() {
		auto idx = __builtin_ffsll(bv);

		if (idx == 0) {
			return nullptr;
		}

		auto offset = idx - 1;
		bv &= ( ~( ((uint64_t)1) << offset));

		//std::printf("Allocating %d\n", offset);

		auto* out = ((unsigned char*) ptr) + (offset * OBJ_SIZE);

		//std::printf("output %d is %p\n", offset, out);

		return out;
	}
	void deallocate(void* free_ptr) {
		auto offset_bytes = (unsigned char*) free_ptr - (unsigned char*) ptr;
		auto offset = offset_bytes/ OBJ_SIZE;
		bv |= ( ((uint64_t)1) << offset);

		//std::printf("Deallocating %lu\n", offset);

	}
};

struct GetMetadataTask {
	struct promise_type {

		inline static thread_local DemandCalcRecyclingFixedSizeAllocator<promise_type> allocator = DemandCalcRecyclingFixedSizeAllocator<promise_type>();

		GetMetadataTask get_return_object() {
			return {
				.h_ = std::coroutine_handle<promise_type>::from_promise(*this)
			};
		}

		std::suspend_always initial_suspend() { return {}; }
		std::suspend_never final_suspend() noexcept {return {};}

		void unhandled_exception() {}

		void return_void() {}

		static void* operator new(size_t sz) {

			//std::printf("REQUEST SIZE %lu\n", sz);
			
			return allocator.allocate();
		}

		static void operator delete(void* ptr, size_t sz) {
			allocator.deallocate(ptr);
		}
	};

	std::coroutine_handle<promise_type> h_;
};

struct DemandCalcScheduler {
	using handle_t = std::coroutine_handle<>;
	
	const uint8_t QUEUE_SIZE;

	size_t start = 0;
	size_t end = 0;
	size_t sz = 0;

	std::vector<handle_t> handles;

	DemandCalcScheduler(const uint8_t QUEUE_SIZE)
		: QUEUE_SIZE(QUEUE_SIZE)
		, start(0)
		, end(0)
		, sz(0) 
		{
			handles.resize(QUEUE_SIZE + 2); // +2 so we can push_back before pop_front, sometimes convenient
		}

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

struct DemandCalcThrottler {

	const uint8_t QUEUE_SIZE;

	using scheduler_t = DemandCalcScheduler;
	using handle_t = typename scheduler_t::handle_t;

	scheduler_t scheduler;

	DemandCalcThrottler(uint8_t QUEUE_SIZE)
		: QUEUE_SIZE(QUEUE_SIZE)
		, scheduler(QUEUE_SIZE) {}

	void spawn(GetMetadataTask task) {
		while (scheduler.full()) {
			scheduler.pop_front().resume();
		}

		scheduler.push_back(task.h_);
	}

	bool full() const {
		return scheduler.full();
	}

	void join() {
		scheduler.run();
	}
};

template<typename PrefetchType, typename Scheduler>
struct DemandCalcAwaiter {
	PrefetchType* value;
	Scheduler& scheduler;


	DemandCalcAwaiter(PrefetchType* ptr, Scheduler& scheduler) 
		: value(ptr)
		, scheduler(scheduler)	{}

	bool await_ready() { return false; }

	auto await_suspend(std::coroutine_handle<> handle) {

		_mm_prefetch(static_cast<const void*>(value), _MM_HINT_NTA);		
//		_mm_prefetch(static_cast<const void*>(value), _MM_HINT_T2);
	//	__builtin_prefetch(static_cast<const void*>(value), 0, 1);
		scheduler.push_back(handle);
		return scheduler.pop_front();
	}

	PrefetchType& await_resume() {
		return *value;
	}
};





} /* edce */
