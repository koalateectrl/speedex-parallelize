#include "coroutine_throttler.h"
#include <stdexcept>
#include <memory>

namespace edce {

//RecyclingFixedSizeAllocator<RootTask::promise_type> RootTask::promise_type::allocator = RecyclingFixedSizeAllocator<RootTask::promise_type>();


template struct RecyclingFixedSizeAllocator<RootTask::promise_type>;

/*
std::suspend_never
RootTask::promise_type::final_suspend() {
	/*if (owner) {
		std::printf("calling done task!\n");
		owner -> on_task_done();
	} else {
		throw std::runtime_error("owner shouldn't be null!");
	}*/
//	return {};
//}

template<typename Object>
RecyclingFixedSizeAllocator<Object>::RecyclingFixedSizeAllocator()
	: ptr(nullptr)
	, bv(UINT64_MAX) {
		size_t bytes_len = OBJ_SIZE * MAX_CONCURRENT_OBJS + 512;
		ptr = malloc(bytes_len);
		aligned_ptr = ptr;
		//aligned_ptr = std::align(512, OBJ_SIZE, aligned_ptr, bytes_len);
	}



template<typename Object>
void* 
RecyclingFixedSizeAllocator<Object>::allocate() {

	auto idx = __builtin_ffsll(bv);
	//std::printf("calling allocate, index is %d, bv is %lx, obj size is %lu\n", idx, bv, OBJ_SIZE);

	if (idx == 0) {
		std::printf("nothing to allocate!\n");
		return nullptr;
	}

	auto offset = idx - 1;
	bv &= ( ~( ((uint64_t)1) << offset));

	return ((unsigned char*)aligned_ptr) + (offset * OBJ_SIZE);

}


template<typename Object>
void
RecyclingFixedSizeAllocator<Object>::deallocate(void* free_ptr) {
	
	auto offset_bytes = (unsigned char*) free_ptr - (unsigned char*) aligned_ptr;

	auto offset = offset_bytes/ OBJ_SIZE;

	bv |= ( ((uint64_t)1) << offset);
}



} /* edce */