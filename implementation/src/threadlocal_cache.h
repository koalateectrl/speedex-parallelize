#pragma once

#include <vector>
#include <optional>

#include "tbb/task_arena.h"
#include <atomic>

namespace edce {

class ThreadlocalIdentifier {
	inline static std::atomic<uint32_t> _tl_initializer = 0;


	inline static thread_local uint32_t tid = _tl_initializer.fetch_add(1, std::memory_order_relaxed); 

public:

	static uint32_t get() {
//		std::printf("getting tid %lu\n", tid);
		return tid;
	}
};


//using above tlid is safe with multiple arenas/whatever config, but still weirdly  slow in hashing application, somehow
//will break if shared by multiple arenas concurrently and we use the arena ids
template<typename ValueType, int CACHE_SIZE = 128>
class ThreadlocalCache {

	std::array<std::optional<ValueType>, CACHE_SIZE> objects;

public:

	ThreadlocalCache() : objects() {}

	template<typename... ctor_args>
	ValueType& get(ctor_args&... args) {
		auto idx = ThreadlocalIdentifier::get();
		//int idx = tbb::this_task_arena::current_thread_index();
		if (idx >= CACHE_SIZE) {
			throw std::runtime_error("invalid tlcache access!");
		}
		if (!objects[idx]) {
			objects[idx].emplace(args...);
		}
		return *objects[idx];
	}

	template<typename... ctor_args>
	ValueType& get_index(int idx, ctor_args&... args) {
		if (idx >= CACHE_SIZE || idx < 0) {
			throw std::runtime_error("invalid tlcache access!");
		}
		if (!objects[idx]) {
			objects[idx].emplace(args...);
		}
		return *objects[idx];
	}

	std::array<std::optional<ValueType>, CACHE_SIZE>& get_objects() {
		return objects;
	}

	void clear() {
		for (int i = 0; i < CACHE_SIZE; i++) {
			objects[i] = std::nullopt;
		}
	}

};

}
