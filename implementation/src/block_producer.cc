#include "block_producer.h"
#include "serial_transaction_processor.h"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <tbb/parallel_reduce.h>
#include <tbb/global_control.h>
#include <tbb/task_arena.h>

#include "xdr/block.h"

namespace edce {


//constexpr static int64_t BLOCK_LARGE_ENOUGH_THRESHOLD = 10000;

bool delete_tx_from_mempool(TransactionProcessingStatus status) {
	switch(status) {
		case SUCCESS:
		case SEQ_NUM_TOO_HIGH:
		case SEQ_NUM_TEMP_IN_USE:
		case NEW_ACCOUNT_TEMP_RESERVED:
			return false;
		case INSUFFICIENT_BALANCE:
		case CANCEL_OFFER_TARGET_NEXIST:
			return true;
		case SOURCE_ACCOUNT_NEXIST:
		case INVALID_OPERATION_TYPE:
		case SEQ_NUM_TOO_LOW:
		case STARTING_BALANCE_TOO_LOW:
		case NEW_ACCOUNT_ALREADY_EXISTS:
		case INVALID_TX_FORMAT:
		case INVALID_OFFER_CATEGORY:
		case INVALID_PRICE:
		case RECIPIENT_ACCOUNT_NEXIST:
		case INVALID_PRINT_MONEY_AMOUNT:
		case INVALID_AMOUNT:
			return true;
		default:
			throw std::runtime_error("forgot to add an error code to delete_tx_from_mempool");
	}
}

class BlockProductionReduce {
	EdceManagementStructures& management_structures;
	SerialTransactionProcessor<> tx_processor;
	Mempool& mempool;
	//std::mutex mtx;
	std::atomic<int64_t>& remaining_block_space;
	std::atomic<uint64_t>& total_block_size;


public:
	std::unordered_map<TransactionProcessingStatus, uint64_t> status_counts;
	BlockStateUpdateStatsWrapper stats;

	std::vector<SerialTransactionProcessor<>> accumulated_processors;
	
	void operator() (const tbb::blocked_range<std::size_t> r) {
		//std::lock_guard lock(mtx);

	//	std::atomic_thread_fence(std::memory_order_acquire);
		SerialAccountModificationLog serial_account_log(management_structures.account_modification_log);

		for (size_t i = r.begin(); i < r.end(); i++) {
			auto& chunk = mempool[i];
			std::vector<bool> bitmap;

			int64_t chunk_sz = chunk.size();

			bitmap.resize(chunk_sz, false);


			//reserve space for txs in output block
			int64_t remaining_space = remaining_block_space.fetch_sub(chunk_sz, std::memory_order_relaxed);
			if (remaining_space < chunk_sz) {
				remaining_block_space.fetch_add(chunk_sz - remaining_space, std::memory_order_relaxed);
				//reduce the number of txs that we look at, according to reservation
				//We'll guarantee that we don't exceed a block limit, but we might ignore a few valid txs.
				chunk_sz = remaining_space;
				//chunk_sz += remaining_space;
				//remaining_block_space.fetch_add(-remaining_space, std::memory_order_relaxed);
				if (chunk_sz <= 0) {
					return;
				}
			}


			int64_t elts_added_to_block = 0;

			for (int64_t j = 0; j < chunk_sz; j++) {
				auto status = tx_processor.process_transaction(chunk[j], stats, serial_account_log);
				status_counts[status] ++;
				if (status == TransactionProcessingStatus::SUCCESS) {
					bitmap[j] = true;
					elts_added_to_block++;
				} else if(delete_tx_from_mempool(status)) {
					bitmap[j] = true;
				}
			}
			chunk.set_confirmed_txs(std::move(bitmap));

			//if (remaining_space < BLOCK_LARGE_ENOUGH_THRESHOLD) return;

			auto post_check = remaining_block_space.fetch_add(chunk_sz - elts_added_to_block, std::memory_order_relaxed);
			total_block_size.fetch_add(elts_added_to_block, std::memory_order_release);
			if (post_check <= 0) {
				return;
			}
		}

	//	std::atomic_thread_fence(std::memory_order_release);

	}

	BlockProductionReduce(BlockProductionReduce& x, tbb::split)
		: management_structures(x.management_structures)
		, tx_processor(management_structures)
		, mempool(x.mempool)
		//, mtx()
		, remaining_block_space(x.remaining_block_space)
		, total_block_size(x.total_block_size)
		, accumulated_processors()
			{};

	void join(BlockProductionReduce& other) {
		//std::lock_guard lock(mtx);

		other.finish();
		
		//std::lock_guard lock2(other.mtx);

		for (size_t i = 0; i < other.accumulated_processors.size(); i++) {
			accumulated_processors.emplace_back(std::move(other.accumulated_processors.at(i)));
		}

		for (auto iter = other.status_counts.begin(); iter != other.status_counts.end(); iter++) {
			status_counts[iter->first] += iter->second;
		}

		stats += other.stats;

		other.accumulated_processors.clear();	
	}

	void finish() {
		//std::lock_guard lock(mtx);
		accumulated_processors.emplace_back(std::move(tx_processor));
		tx_processor.clear();
	}

	BlockProductionReduce(
		EdceManagementStructures& management_structures,
		Mempool& mempool,
		std::atomic<int64_t>& remaining_block_space,
		std::atomic<uint64_t>& total_block_size)
		: management_structures(management_structures)
		, tx_processor(management_structures)
		, mempool(mempool)
		//, mtx()
		, remaining_block_space(remaining_block_space)
		, total_block_size(total_block_size)
		, accumulated_processors()
		{}
};


//returns block size
uint64_t 
BlockProducer::build_block(
	Mempool& mempool,
	int64_t max_block_size,
	BlockCreationMeasurements& measurements,
	BlockStateUpdateStatsWrapper& state_update_stats) {

	if (management_structures.account_modification_log.size() != 0) {
		throw std::runtime_error("forgot to clear mod log");
	}

	auto lock = mempool.lock_mempool();

	std::atomic<int64_t> remaining_space = max_block_size;
	std::atomic<uint64_t> total_block_size = 0;


	auto producer = BlockProductionReduce(management_structures, mempool, remaining_space, total_block_size);

	tbb::blocked_range<size_t> range(0, mempool.num_chunks());

	BLOCK_INFO("starting produce block from mempool");

	auto timestamp = init_time_measurement();

	tbb::parallel_reduce(range, producer);

	BLOCK_INFO("done produce block from mempool: duration %lf", measure_time(timestamp));

	producer.finish();

	MEMPOOL_INFO_F(
		for (auto iter = producer.status_counts.begin(); iter != producer.status_counts.end(); iter++) {
			std::printf("block_producer.cc:   mempool stats: code %d count %lu\n", iter->first, iter->second);
		}
		std::printf("block_producer.cc: new_offers %u cancel_offer %u payment %u new_account %u\n", producer.stats.new_offer_count, producer.stats.cancel_offer_count, producer.stats.payment_count, producer.stats.new_account_count); 
	);

//	std::vector<SerialAccountModificationLog> serial_account_logs;

	//for (size_t i = 0; i < producer.accumulated_processors.size(); i++) {
	//	serial_account_logs.emplace_back(std::move(producer.accumulated_processors[i].extract_account_log()));
	//}

	//worker.do_merge(std::move(serial_account_logs));
	worker.do_merge();

/*	std::thread th([&management_structures, &serial_account_logs] () {
		if (serial_account_logs.size() > 0) {

			while(management_structures.account_modification_log.size() == 0 && serial_account_logs.size() > 0) {
				serial_account_logs.back().finish();
				serial_account_logs.pop_back();
			}

			if (serial_account_logs.size() > 0) {
				management_structures.account_modification_log.merge_in_log_batch(serial_account_logs);
			}
		}
	});*/

	size_t num_work_units = management_structures.work_unit_manager.get_num_work_units();

	/*tbb::parallel_for(
		tbb::blocked_range<std::size_t>(0, producer.accumulated_processors.size()),
		[&producer, num_work_units] (auto r) {

			auto idx = tbb::task_arena::current_thread_index();
			for (auto i = r.begin(); i < r.end(); i++) {
				producer.accumulated_processors.at(i).extract_manager_view().finish_merge(idx * 7);
			}
		});*/

	auto offer_merge_timestamp = init_time_measurement();
	tbb::parallel_for(
		tbb::blocked_range<std::size_t>(0, num_work_units),
		[&producer, num_work_units] (auto r) {
			for (auto i = r.begin(); i < r.end(); i++) {
				auto& processors = producer.accumulated_processors;
				size_t processors_sz = processors.size();
				for (size_t j = 0; j < processors_sz; j++) {
					processors[j].extract_manager_view().partial_finish(i);
				}
			}
		});
	for (auto& proc : producer.accumulated_processors) {
		proc.extract_manager_view().partial_finish_conclude();
	}

	measurements.offer_merge_time = measure_time(offer_merge_timestamp);
	BLOCK_INFO("merging in new offers took %lf", measurements.offer_merge_time);

	uint64_t block_size = total_block_size.load(std::memory_order_relaxed);// max_block_size - remaining_space.load(std::memory_order_release);

	BLOCK_INFO("produced block of size %lu", block_size);//max_block_size - remaining_space.load(std::memory_order_relaxed));


	state_update_stats += producer.stats;
	//th.join();
	worker.wait_for_merge_finish();
	return block_size;
}



};
