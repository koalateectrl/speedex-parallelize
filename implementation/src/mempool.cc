#include "mempool.h"

#include <tbb/parallel_for.h>
namespace edce {

uint64_t MempoolChunk::remove_confirmed_txs() {
	uint64_t num_removed = 0;

	for (size_t i = 0; i < confirmed_txs_to_remove.size();) {
		if (confirmed_txs_to_remove[i]) {
			confirmed_txs_to_remove[i] = confirmed_txs_to_remove.back();
			txs[i] = txs.back();
			txs.pop_back();
			confirmed_txs_to_remove.pop_back();
			//txs.erase(txs.begin() + remove_idx);
			num_removed ++;
		} else {
			i++;
		}
	}
	return num_removed;
}


void Mempool::add_to_mempool_buffer(std::vector<SignedTransaction>&& chunk) {
	MempoolChunk to_add(std::move(chunk));
	std::lock_guard lock(buffer_mtx);
	buffered_mempool.push_back(std::move(to_add));
}

void Mempool::push_mempool_buffer_to_mempool() {
	std::lock_guard lock (buffer_mtx);
	std::lock_guard lock2 (mtx);

	for (size_t i = 0; i < buffered_mempool.size(); i++) {
		mempool_size.fetch_add(buffered_mempool[i].size(), std::memory_order_release);
		mempool.emplace_back(std::move(buffered_mempool[i]));
	}
	buffered_mempool.clear();
}

void Mempool::join_small_chunks() {
	std::lock_guard lock(mtx);

	//ensures that the average chunk size is at least TARGET/2
	for (size_t i = 0; i < mempool.size() - 1;) {
		if (mempool[i].size() + mempool[i+1].size() < TARGET_CHUNK_SIZE) {
			mempool[i].join(std::move(mempool[i+1]));
			mempool[i+1] = std::move(mempool.back());
			mempool.pop_back();
		} else {
			i++;
		}
	}
}

void Mempool::remove_confirmed_txs() {
	std::lock_guard lock(mtx);

	tbb::parallel_for(tbb::blocked_range<size_t>(0, mempool.size()),
		[this] (const auto& r) {

			int64_t deleted = 0;
			for (auto i = r.begin(); i < r.end(); i++) {
				deleted += mempool[i].remove_confirmed_txs();
			}
			mempool_size.fetch_sub(deleted, std::memory_order_release);

		});
}
void Mempool::clear_confirmed_txs_bitmaps() {
	std::lock_guard lock(mtx);

	tbb::parallel_for(tbb::blocked_range<size_t>(0, mempool.size()),
		[this] (const auto& r) {
			for (auto i = r.begin(); i < r.end(); i++) {
				mempool[i].clear_confirmed_txs_bitmap();
			}
		});
}


} /* edce */
