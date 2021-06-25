#pragma once

#include <mutex>
#include <vector>
#include <atomic>
#include <cstdint>

#include "xdr/transaction.h"
#include "async_worker.h"
#include "utils.h"

namespace edce {

//Individual chunks have no synchronization primitives.  Mempool manages synchronization.
struct MempoolChunk {

	std::vector<SignedTransaction> txs;
	std::vector<bool> confirmed_txs_to_remove;


	MempoolChunk(std::vector<SignedTransaction>&& txs_input) 
		: txs(std::move(txs_input))
		, confirmed_txs_to_remove()
		{}

	uint64_t remove_confirmed_txs(); //execute, return deleted count
	void clear_confirmed_txs_bitmap() {
		confirmed_txs_to_remove.clear();
	}
	void set_confirmed_txs(std::vector<bool>&& bitmap) { // also includes anything guaranteed to fail/invalid - anything to delete from mempool
		//TODO invalid txs can be guaranteed removed no matter what, I think.  Separate bitmap?
		confirmed_txs_to_remove = std::move(bitmap);
		if (confirmed_txs_to_remove.size() != txs.size()) {
			throw std::runtime_error("size mismatch: bitmap vs txs");
		}
	}

	size_t size() const {
		return txs.size();
	}

	const SignedTransaction& operator[](size_t idx) {
		return txs.at(idx);
	}

	void join(MempoolChunk&& other){
		//txs.splice_after(txs.before_begin, 
		//	std::move(other.txs));
		//sz += other.sz;
		txs.insert(txs.end(), 
			std::make_move_iterator(other.txs.begin()),
			std::make_move_iterator(other.txs.end()));
		confirmed_txs_to_remove.insert(confirmed_txs_to_remove.end(), other.confirmed_txs_to_remove.begin(), other.confirmed_txs_to_remove.end());
	}
};
	

class Mempool {

	std::vector<MempoolChunk> mempool;

	std::vector<MempoolChunk> buffered_mempool;

	std::atomic<uint64_t> mempool_size;

	mutable std::mutex mtx;
	std::mutex buffer_mtx;


public:

	const size_t TARGET_CHUNK_SIZE;

	std::atomic<uint64_t> latest_block_added_to_mempool = 0;

	Mempool(size_t target_chunk_size)
		: mempool()
		, buffered_mempool()
		, mempool_size(0)
		, mtx()
		, buffer_mtx()
       		, TARGET_CHUNK_SIZE(target_chunk_size) {}

	//constexpr static size_t TARGET_CHUNK_SIZE = 10000;

	void add_to_mempool_buffer(std::vector<SignedTransaction>&& chunk);
	void push_mempool_buffer_to_mempool();

	//threadsafe
	void join_small_chunks();

	uint64_t size() const {
		return mempool_size.load(std::memory_order_acquire);
	}

	std::lock_guard<std::mutex> lock_mempool() {
		mtx.lock();
		return {mtx, std::adopt_lock};
	}

	//threadsafe
	void remove_confirmed_txs(); // block was commited
	void clear_confirmed_txs_bitmaps(); //block production failed


	size_t num_chunks() const {
		return mempool.size();
	}

	//references will be invalidated unless mempool is locked
	MempoolChunk& operator[](size_t idx) {
		return mempool.at(idx);
	}
};

class MempoolWorker : public AsyncWorker {
	using AsyncWorker::mtx;
	using AsyncWorker::cv;

	Mempool& mempool;
	bool do_cleaning = false;
	float* output_measurement;

	bool exists_work_to_do() override final {
		return do_cleaning;
	}

	void run() {
		std::unique_lock lock(mtx);
		while(true) {
			if ((!done_flag) && (!exists_work_to_do())) {
			cv.wait(lock, [this] () { return done_flag || exists_work_to_do();});
			}
			if (done_flag) return;
			if (do_cleaning) {
				auto timestamp = init_time_measurement();
				mempool.remove_confirmed_txs();
				mempool.join_small_chunks();
				*output_measurement = measure_time(timestamp);

				do_cleaning = false;
			}
			cv.notify_all();
		}
	}

public:
	MempoolWorker(Mempool& mempool)
		:AsyncWorker()
		, mempool(mempool) {
			start_async_thread([this] () {run();});
		}

	~MempoolWorker() {
		wait_for_async_task();
		end_async_thread();
	}

	void do_mempool_cleaning(float* measurement_out) {
		wait_for_async_task();
		std::lock_guard lock(mtx);
		output_measurement = measurement_out;
		do_cleaning=true;
		cv.notify_all();
	}

	void wait_for_mempool_cleaning_done() {
		wait_for_async_task();
	}
};

}
